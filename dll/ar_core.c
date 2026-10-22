// ck3_custom - generic core (reloadable with the game open in -debug_mode; docs/dll/arquitetura_dll.md).
// Loaded by the loader (winmm.dll) from ck3_custom_core.dll. Receives requests from ck3_custom_cmd.txt
// on the game's main thread, writes the replies to ck3_custom_dll.log and compiles the mods' plugins (TinyCC).
// Addresses (RVA, ck3.exe 1.19.0.6) in docs/dll/re_notas.md and docs/dll/re/notas_unidades.md.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <stdarg.h>
#include <ctype.h>
#include "ar_host.h"
#include "ck3_api.h"

// commands
#define RVA_VT_MOVE_UNIT   0x432bef0
#define RVA_VT_MOVE_UNIT2  0x432bf88
#define RVA_PATH_CTOR      0xc7ba80    // path list constructor (0x128 bytes; total cost at +0x128)
#define MOVE_UNIT_SIZE     0x168
// tables
#define RVA_UNIT_TABLE     0x570cbe8
#define RVA_ARMY_TABLE     0x570c6b0
#define RVA_CHAR_TABLE     0x570c0b0
#define RVA_GAME_STATE     0x570dfe8   // [[x]+0xa0]: provinces at +0x140, count at +0x14c
#define RVA_NOW            0x570edd8
#define RVA_PLAYER_CHAR_ID 0x4fe7ee0
// unit
#define UNIT_ID            0x10
#define UNIT_LOCATION      0x20
#define UNIT_PATH          0x38
#define UNIT_PATH_COUNT    0x44
#define UNIT_OWNER_CHAR    0x174
#define UNIT_ARMY          0x178
#define PROV_ID            0x10
// functions
#define RVA_FN_MOVING      0x2247890   // (unit, &out) -> pointer to the arrival date of the current step
#define RVA_FN_NEXT_PROV   0x2246d70   // (unit) -> next province of the path
#define RVA_FN_PF_CTOR     0x23c3310   // (ctx, unit)
#define RVA_FN_PF_FIND     0x23c33f0   // (ctx, start*, target*, 2, path*)
#define RVA_FN_TRAVEL_TIME 0x2247340   // (unit, &time, path*, start*)
#define RVA_FN_STEP_LEFT   0x2247600   // (unit, &out, 0)
#define RVA_FN_PAN_CAMERA  0xa1c880    // (province*) -> centers the camera (Province.PanCameraTo)
#define RVA_FN_ZOOM_CAMERA 0xa1ca20    // (province*) -> centers and zooms in (Province.ZoomCameraTo)
#define RVA_FN_RELATION    0x224a610   // (unit*) -> 0 mine, 1 allied, 2 enemy, 3 hostile, 4 neutral, 5 none
#define RVA_FN_PREDICT     0xc6e9c0    // (&ratio, army vector A, army vector B, k, k, 0, 0) combat prediction
#define RVA_PREDICT_K      0x57BFB48
#define RVA_PREDICT_LEVELS 0x4F42D40   // pointer to the thresholds; count at +0xC
#define RVA_RAID_TABLE      0x57BFC38   // raids in progress; checks [raid+8] == id
#define ARMY_RAID           0x1e8       // id of the raid in progress, in the army object (-1 = none)
#define RVA_FN_RAID_TOTAL   0x23a1040   // (&out, province*) -> total progress required
#define RVA_FN_RAID_PER_DAY 0x23a0cc0   // (&out, army*, 0) -> progress per day
#define RVA_FN_LOOT_CAP     0x2272a80   // (army*, &out) -> loot capacity
// county attrition (2026-10-08, auto_raid docs/dossie_exercitos.md 9.22): the movement step (0x1422712c0) calls the check and,
// if it says yes, the count; the same calculation as the skull on the path
#define RVA_FN_COUNTY_ATR   0x2271120   // (character*, from*, to*, mode = army is not a raiding one) -> does the step charge?
#define RVA_FN_COUNTY_LOSS  0x2275560   // (army*, NULL) -> casualties (5% x modifiers, min. 100, up to the size)
#define ARMY_IS_RAID        0x1d4
#define ARMY_LOOTER_STANCE  0x1d5       // "raid when stopped" mode (byte)
#define RVA_VT_TOGGLE_LOOT   0x432c750
#define RVA_VT_TOGGLE_LOOT2  0x432c720
#define ARMY_LOOT           0x1e0
#define RVA_FN_CAN_RAID     0x293db30   // (army*, province*, text or NULL) -> can it raid this location?
#define RVA_FN_PROV_LOOT    0x291dc90   // (&out, province*, NULL, NULL) -> loot available at the location
#define RVA_APP              0x570F738   // [x]+0x20: byte "paused by the player"
#define RVA_FN_EVENT_PAUSE   0x2707280   // ([[state]+0xa0]+0x2f4c0, 0) -> paused by event/message?
#define RVA_FN_OTHER_PAUSE   0x346b8f0   // ([app]) -> other pause
#define FIXED_ONE          100000

static const ar_host *H;
static uint8_t *B;
#define LOG H->log

// ---- safe read ----
static int rd(const void *addr, void *out, size_t n)
{
	SIZE_T got = 0;
	return ReadProcessMemory(GetCurrentProcess(), addr, out, n, &got) && got == n;
}
static uint8_t *rdp(const void *addr) { uint8_t *v = NULL; rd(addr, &v, 8); return v; }
static uint32_t rd32(const void *addr) { uint32_t v = 0; rd(addr, &v, 4); return v; }
static int64_t rd64(const void *addr) { int64_t v = 0; rd(addr, &v, 8); return v; }

// ---- tables ----
static uint8_t *table_get(uint32_t rva, uint32_t id, uint32_t id_off)
{
	uint8_t *tab = rdp(B + rva);
	if (!tab)
		return NULL;
	uint32_t idx = id & 0xFFFFFF;
	if (idx >= rd32(tab + 0x2c))
		return NULL;
	uint8_t *obj = rdp(rdp(tab + 0x20) + (size_t)idx * 16 + 8);
	if (!obj || rd32(obj + id_off) != id)
		return NULL;
	return obj;
}
static uint32_t table_count(uint32_t rva)
{
	uint8_t *tab = rdp(B + rva);
	return tab ? rd32(tab + 0x2c) : 0;
}
static uint8_t *table_at(uint32_t rva, uint32_t idx)
{
	uint8_t *tab = rdp(B + rva);
	return tab ? rdp(rdp(tab + 0x20) + (size_t)idx * 16 + 8) : NULL;
}
static uint8_t *unit_by_id(uint32_t id) { return table_get(RVA_UNIT_TABLE, id, UNIT_ID); }

// province table, re-read at the start of each effect and each request (only changes when another game is loaded): each
// safe read is a system call, and the loops over all provinces weigh on the tick
static uint8_t *pv_arr;
static uint32_t pv_n;
static void prov_cache(void)
{
	uint8_t *gs = rdp(rdp(B + RVA_GAME_STATE) + 0xa0);
	pv_arr = gs ? rdp(gs + 0x140) : NULL;
	pv_n = gs ? rd32(gs + 0x14c) : 0;
}

static uint8_t *province_by_id(uint32_t id)
{
	if (!pv_arr)
		prov_cache();
	if (!pv_arr || id < 1 || id >= pv_n)
		return NULL;
	return rdp(pv_arr + (size_t)id * 8);
}
static uint32_t prov_id(uint8_t *prov) { return prov ? rd32(prov + PROV_ID) : 0; }
// province text key (e.g. b_name): std::string at [definition+0x28]
static const char *prov_key(uint8_t *prov, char *buf, size_t n)
{
	uint8_t *def = prov ? rdp(prov + 8) : NULL;
	buf[0] = 0;
	if (!def)
		return buf;
	uint64_t size = (uint64_t)rd64(def + 0x38), cap = (uint64_t)rd64(def + 0x40);
	const uint8_t *src = cap >= 16 ? rdp(def + 0x28) : def + 0x28;
	if (size >= n)
		size = n - 1;
	if (src && rd(src, buf, size))
		buf[size] = 0;
	else
		buf[0] = 0;
	return buf;
}
static int prov_is_sea(uint8_t *prov);
static uint32_t player_char(void) { return rd32(B + RVA_PLAYER_CHAR_ID); }

// ---- recording (auto_raid docs/simulador.md, SIM1) ----
// `gravar on <nome>` writes to <binaries>\ck3\gravacao\<nome>.jsonl, one JSON line per event: effect
// called ("ef") and variables answered ("r"), game primitives with arguments and result ("eta", "etaf", "saq",
// "prev", "alvo", "levas"), script state per army ("exe") and of the player per tick ("jog").
static FILE *rec_file;
static CRITICAL_SECTION rec_lock;
static volatile LONG rec_lock_ok;
static long rec_lines;

static void rec(const char *fmt, ...)
{
	if (!rec_file)
		return;
	EnterCriticalSection(&rec_lock);
	if (rec_file) {
		va_list ap;
		va_start(ap, fmt);
		vfprintf(rec_file, fmt, ap);
		va_end(ap);
		fputc('\n', rec_file);
		rec_lines++;
	}
	LeaveCriticalSection(&rec_lock);
}

static int unit_relation(uint8_t *unit)
{
	return ((int (*)(void *))(B + RVA_FN_RELATION))(unit);
}
static const char *relation_name(int r)
{
	static const char *names[] = { "minha", "aliada", "inimiga", "hostil", "neutra", "nenhuma" };
	return r >= 0 && r <= 5 ? names[r] : "?";
}

// unit's current path: returns the count and fills up to max province ids
static int unit_path(uint8_t *unit, uint32_t *out, int max)
{
	int n = (int)rd32(unit + UNIT_PATH_COUNT);
	uint8_t *arr = rdp(unit + UNIT_PATH);
	if (n < 0 || n > 4096 || !arr)
		return 0;
	for (int i = 0; i < n && i < max; i++)
		out[i] = rd32(rdp(arr + (size_t)i * 8));
	return n;
}

// ---- log of the player's commands ----
static volatile LONG last_player_unit = -1;
static volatile LONG cmdlog;        // 1 = logs every command sent by the main thread; 2 = from all threads,
                                    // army classes only (2026-10-06: the AI's march and the click march do not come from the main thread)
static CRITICAL_SECTION cmdlog_lock;
static volatile LONG cmdlog_lock_ok;
static volatile LONG sending;

static const char *class_name(void *obj, char *buf, size_t n)
{
	// RTTI: vtable[-1] -> locator -> type descriptor (RVA at +0xC) -> name at +0x10
	uint8_t *vt = rdp(obj);
	uint8_t *col = rdp(vt - 8);
	uint32_t td = rd32(col + 0xC);
	buf[0] = 0;
	if (td && td < 0x6000000)
		rd(B + td + 0x10, buf, n - 1);
	buf[n - 1] = 0;
	return buf;
}

static void hexdump(const char *title, const uint8_t *p, int n)
{
	char line[160];
	for (int o = 0; o < n; o += 16) {
		int k = snprintf(line, sizeof line, "%s+0x%03X:", title, o);
		for (int i = 0; i < 16 && o + i < n; i++) {
			uint8_t b = 0;
			if (rd(p + o + i, &b, 1))
				k += snprintf(line + k, sizeof line - k, " %02X", b);
			else
				k += snprintf(line + k, sizeof line - k, " ??");
		}
		LOG("%s", line);
	}
}

// Without graphics the game unpauses by itself a few seconds after loading (no "Play" screen). ng_segura = 1 from
// loading until the first requested `pause 0`: the status (every 250 ms) pauses again and the commands are logged
static int ng_segura = -1, ng_cmds;

static void on_post(void *cmd, int flags, uint32_t thread)
{
	uint8_t *c = cmd;
	if (c && ng_segura == 1 && !sending && ng_cmds < 40) {
		char name[96];
		ng_cmds++;
		LOG("comando depois da carga (sem graficos): %s flags=0x%X thread=%u campos=%u %u", class_name(c, name, sizeof name),
		    flags, thread, rd32(c + 0x20), rd32(c + 0x24));
	}
	if (!c || thread != H->main_thread)
		return;   // only what comes from the main thread (the player and the DLL itself)
	if (rdp(c) == B + RVA_VT_MOVE_UNIT) {
		uint32_t *f = (uint32_t *)(c + 0x20);
		if (!sending && f[0] == 1)
			last_player_unit = (LONG)f[1];
		LOG("move_unit%s valid_for=%u unit=%u target=%u order_type=%u type=%u gathering=%u flags=0x%X",
		    sending ? " [DLL]" : "", f[0], f[1], f[2], f[3], f[4], f[5], flags);
		return;
	}
}

// ---- hook on the internal send function (exe+0x341d8e0), through which ALL commands pass ----
// The hook page is permanent (survives a core reload); its address is kept in the environment variable
// AR_HOOK2. The core only swaps the handler pointer.
#define RVA_POST_INNER 0x341d8e0   // (dispatcher, unique_ptr<command>*, flags)
static const uint8_t INNER_PROLOGUE[17] = {
	0x4C, 0x89, 0x4C, 0x24, 0x20,   // mov [rsp+0x20], r9
	0x48, 0x89, 0x54, 0x24, 0x10,   // mov [rsp+0x10], rdx
	0x53, 0x56, 0x57,               // push rbx; push rsi; push rdi
	0x48, 0x83, 0xEC, 0x20          // sub rsp, 0x20
};
typedef uint8_t (*inner_fn)(void *dispatcher, void **pcmd, int flags, void *r9);
static uint8_t *hook2_page;

static int cmd_de_exercito(const char *n)
{
	static const char *const K[] = { "Move", "Halt", "Raid", "Loot", "Unit", "Army", "Retreat", "Stop", "Cancel" };
	for (unsigned i = 0; i < sizeof K / sizeof *K; i++)
		if (strstr(n, K[i]))
			return 1;
	return 0;
}

static uint8_t inner_handler(void *dispatcher, void **pcmd, int flags, void *r9)
{
	if (cmdlog == 2 && pcmd && cmdlog_lock_ok) {
		uint8_t *c = rdp(pcmd);
		char name[96];
		if (c && cmd_de_exercito(class_name(c, name, sizeof name))) {
			EnterCriticalSection(&cmdlog_lock);
			LOG("comando %s vtable=exe+0x%llX flags=0x%X thread=%lu%s", name, (unsigned long long)(rdp(c) - B), flags,
			    GetCurrentThreadId(), GetCurrentThreadId() == H->main_thread ? " (principal)" : "");
			// march: the whole command (0x168) and the contents of the path list (+0x38: data, +0x40/+0x44: sizes)
			int move = rdp(c) == B + RVA_VT_MOVE_UNIT;
			hexdump("  cmd", c, move ? MOVE_UNIT_SIZE : 0x70);
			if (move) {
				uint8_t *lst = rdp(c + 0x38);
				uint32_t n1 = rd32(c + 0x40), n2 = rd32(c + 0x44), n = n1 < 64 ? n1 : n2 < 64 ? n2 : 0;
				if (lst && n && (uintptr_t)lst > 0x10000 && (uintptr_t)lst < 0x7FFFFFFFFFFFULL)
					hexdump("  caminho", lst, (int)n * 8 > 0x100 ? 0x100 : (int)n * 8);
			}
			LeaveCriticalSection(&cmdlog_lock);
		} else if (c) {
			// other classes: only the name (so a command with an unexpected name is not hidden)
			EnterCriticalSection(&cmdlog_lock);
			LOG("comando_outro %s flags=0x%X thread=%lu", name, flags, GetCurrentThreadId());
			LeaveCriticalSection(&cmdlog_lock);
		}
	} else if (cmdlog == 1 && GetCurrentThreadId() == H->main_thread && pcmd) {
		uint8_t *c = rdp(pcmd);
		if (c && rdp(c) != B + RVA_VT_MOVE_UNIT) {
			char name[96];
			LOG("comando %s vtable=exe+0x%llX flags=0x%X", class_name(c, name, sizeof name),
			    (unsigned long long)(rdp(c) - B), flags);
			hexdump("  cmd", c, 0x70);
			uint8_t *arr = rdp(c + 0x28);
			uint32_t cnt = rd32(c + 0x30);
			if (arr && cnt > 0 && cnt < 64 && (uintptr_t)arr > 0x10000 && (uintptr_t)arr < 0x7FFFFFFFFFFFULL)
				hexdump("  vetor+0x28", arr, cnt * 16 > 0x80 ? 0x80 : (int)cnt * 16);
		}
	}
	return ((inner_fn)(hook2_page + 14))(dispatcher, pcmd, flags, r9);
}

static void hook2_install(void)
{
	char env[32];
	if (GetEnvironmentVariableA("AR_HOOK2", env, sizeof env)) {
		hook2_page = (uint8_t *)strtoull(env, NULL, 16);
	} else {
		uint8_t *target = B + RVA_POST_INNER;
		if (memcmp(target, INNER_PROLOGUE, sizeof INNER_PROLOGUE) != 0) {
			LOG("gancho interno: bytes nao batem, nao instalado");
			return;
		}
		uint8_t *pg = VirtualAlloc(NULL, 128, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
		if (!pg)
			return;
		static const uint8_t head[14] = { 0x48, 0x8B, 0x05, 41, 0, 0, 0,   // mov rax, [rip+41] (slot at +48)
		                                  0x48, 0x85, 0xC0,              // test rax, rax
		                                  0x74, 0x02,                    // jz +2
		                                  0xFF, 0xE0 };                  // jmp rax
		memcpy(pg, head, 14);
		memcpy(pg + 14, INNER_PROLOGUE, 17);
		pg[31] = 0xFF; pg[32] = 0x25; memset(pg + 33, 0, 4);
		*(uint8_t **)(pg + 37) = target + 17;
		*(void **)(pg + 48) = NULL;
		uint8_t patch[17];
		patch[0] = 0xFF; patch[1] = 0x25; memset(patch + 2, 0, 4);
		*(uint8_t **)(patch + 6) = pg;
		patch[14] = patch[15] = patch[16] = 0x90;
		DWORD old;
		if (!VirtualProtect(target, sizeof patch, PAGE_EXECUTE_READWRITE, &old))
			return;
		memcpy(target, patch, sizeof patch);
		VirtualProtect(target, sizeof patch, old, &old);
		FlushInstructionCache(GetCurrentProcess(), target, sizeof patch);
		snprintf(env, sizeof env, "%llX", (unsigned long long)pg);
		SetEnvironmentVariableA("AR_HOOK2", env);
		hook2_page = pg;
		LOG("gancho interno instalado em exe+0x%X", RVA_POST_INNER);
	}
	*(void *volatile *)(hook2_page + 48) = (void *)inner_handler;
}

static void hook2_remove(void)
{
	if (hook2_page) {
		*(void *volatile *)(hook2_page + 48) = NULL;
		Sleep(300);   // let whoever was already inside the handler finish
	}
}

// ---- sending move_unit ----
static void free_path(uint8_t *path)
{
	void *list = *(void **)path;
	if (list) {
		void **alloc = *(void ***)(path + 0x10);
		((void (*)(void *, void *, uint32_t))(*(void ***)alloc)[2])(alloc, list, 8);
		*(void **)path = NULL;
	}
}

// ia = 1: sends the way the AI sends (valid_for 2, flags 0x7; captured with `cmdlog all` on 2026-10-06: the AI's 131
// marches differed from the player's only in this). Test: the AI drops a raid in progress; the player does not.
static uint8_t send_move_como(uint32_t unit, uint32_t province, uint32_t order_type, int ia)
{
	_Alignas(16) uint8_t cmd[MOVE_UNIT_SIZE];
	memset(cmd, 0, sizeof cmd);
	*(uint8_t **)(cmd + 0x00) = B + RVA_VT_MOVE_UNIT;
	*(uint8_t **)(cmd + 0x18) = B + RVA_VT_MOVE_UNIT2;
	*(uint32_t *)(cmd + 0x20) = ia ? 2 : 1;   // valid_for: 1 player, 2 AI
	*(uint32_t *)(cmd + 0x24) = unit;
	*(uint32_t *)(cmd + 0x28) = province;
	*(uint32_t *)(cmd + 0x2C) = order_type;   // 0 = replace, 1 = queue
	*(uint32_t *)(cmd + 0x30) = 2;
	*(uint32_t *)(cmd + 0x34) = 1;
	((void (*)(void *))(B + RVA_PATH_CTOR))(cmd + 0x38);
	sending = 1;
	uint8_t r = H->post(cmd, ia ? 0x7 : 0xE);
	sending = 0;
	free_path(cmd + 0x38);
	return r;
}

static uint8_t send_move(uint32_t unit, uint32_t province, uint32_t order_type)
{
	return send_move_como(unit, province, order_type, 0);
}

// ---- other army commands (formats captured on 2026-10-04, see docs/dll/re/notas_comandos.md) ----
#define RVA_VT_TOGGLE_RAID   0x432C7E8
#define RVA_VT_TOGGLE_RAID2  0x432C628
#define RVA_VT_SPLIT_HALF    0x432D408
#define RVA_VT_SPLIT_HALF2   0x432D4A0
#define RVA_VT_MERGE         0x432D1E0
#define RVA_VT_MERGE2        0x432D1B0
#define RVA_VT_DISBAND       0x432D340
#define RVA_VT_DISBAND2      0x432D3D8
#define RVA_VT_PAUSE         0x432F300
#define RVA_VT_PAUSE2        0x432F2D0
#define RVA_ALLOC_U32        0x4FEADE0   // allocator used in the id vectors

static uint8_t send_cmd(uint32_t vt, uint32_t vt2, const uint32_t *fields, int nfields, int flags)
{
	_Alignas(16) uint8_t cmd[0x100];
	memset(cmd, 0, sizeof cmd);
	*(uint8_t **)(cmd + 0x00) = B + vt;
	*(uint8_t **)(cmd + 0x18) = B + vt2;
	memcpy(cmd + 0x20, fields, (size_t)nfields * 4);
	sending = 1;
	uint8_t r = H->post(cmd, flags);
	sending = 0;
	return r;
}

static void cmd_army_action(char **a, int n)
{
	// raid <army> | split <army> | disband <army> | pause <0|1>
	if (n < 2) {
		LOG("uso: raid|split|disband <id do exercito>   ou   pause <0|1>");
		return;
	}
	uint32_t id = (uint32_t)strtoul(a[1], NULL, 0);
	uint8_t r;
	if (!strcmp(a[0], "raid")) {
		uint32_t f[3] = { 1, player_char(), id };
		r = send_cmd(RVA_VT_TOGGLE_RAID, RVA_VT_TOGGLE_RAID2, f, 3, 0xE);
	} else if (!strcmp(a[0], "split")) {
		uint32_t f[3] = { 1, player_char(), id };
		r = send_cmd(RVA_VT_SPLIT_HALF, RVA_VT_SPLIT_HALF2, f, 3, 0xE);
	} else if (!strcmp(a[0], "disband")) {
		uint32_t f[2] = { 1, id };
		r = send_cmd(RVA_VT_DISBAND, RVA_VT_DISBAND2, f, 2, 0xE);
	} else {
		uint32_t f[2] = { 1, id };
		if (!id && ng_segura == 1) {
			ng_segura = 0;
			LOG("pause 0 pedido: o status deixa de segurar a pausa da carga");
		}
		r = send_cmd(RVA_VT_PAUSE, RVA_VT_PAUSE2, f, 2, 0x7);
	}
	LOG("%s %u: retorno=%u", a[0], id, r);
}

static uint32_t parse_unit(const char *s);

static void cmd_stance(char **a, int n)
{
	// stance <unit|last> <on|off|toggle> : the army's "raid when stopped" mode; only sends if it needs to change
	if (n < 3) {
		LOG("uso: stance <unidade|last> <on|off|toggle>");
		return;
	}
	uint32_t id = parse_unit(a[1]);
	uint8_t *u = unit_by_id(id);
	uint8_t *army = u ? table_get(RVA_ARMY_TABLE, rd32(u + UNIT_ARMY), 0x10) : NULL;
	if (!army) {
		LOG("stance: unidade %u nao encontrada", id);
		return;
	}
	uint8_t cur = 0;
	rd(army + ARMY_LOOTER_STANCE, &cur, 1);
	int want = !strcmp(a[2], "toggle") ? !cur : !strcmp(a[2], "on");
	if (want == cur) {
		LOG("stance: ja esta %s, nada enviado", cur ? "ligado" : "desligado");
		return;
	}
	uint32_t f[3] = { 1, player_char(), rd32(u + UNIT_ARMY) };
	uint8_t r = send_cmd(RVA_VT_TOGGLE_LOOT, RVA_VT_TOGGLE_LOOT2, f, 3, 0xE);
	LOG("stance: era %u, pedido %s, retorno=%u", cur, want ? "ligar" : "desligar", r);
}

#define RVA_VT_HALT   0x432be58
#define RVA_VT_HALT2  0x432be28

static void cmd_halt(char **a, int n)
{
	// halt <unit|last> [unit ...] : stop command (the same as the game's stop button)
	if (n < 2) {
		LOG("uso: halt <unidade|last> [unidade ...]");
		return;
	}
	static uint32_t ids[16];
	int cnt = 0;
	for (int i = 1; i < n && cnt < 16; i++)
		ids[cnt++] = parse_unit(a[i]);
	_Alignas(16) uint8_t cmd[0x100];
	memset(cmd, 0, sizeof cmd);
	*(uint8_t **)(cmd + 0x00) = B + RVA_VT_HALT;
	*(uint8_t **)(cmd + 0x18) = B + RVA_VT_HALT2;
	*(uint32_t *)(cmd + 0x20) = 1;   // valid_for = player (the AI uses 2)
	*(uint32_t **)(cmd + 0x28) = ids;
	*(int32_t *)(cmd + 0x30) = cnt;
	*(int32_t *)(cmd + 0x34) = cnt;
	*(uint8_t **)(cmd + 0x38) = B + RVA_ALLOC_U32;
	sending = 1;
	uint8_t r = H->post(cmd, 0xE);
	sending = 0;
	LOG("halt de %d unidade(s): retorno=%u", cnt, r);
}

static void cmd_merge(char **a, int n)
{
	// merge <target unit> <unit> [unit ...]
	if (n < 3) {
		LOG("uso: merge <unidade destino> <unidade> [unidade ...]");
		return;
	}
	static uint32_t ids[16];
	int cnt = 0;
	for (int i = 2; i < n && cnt < 16; i++)
		ids[cnt++] = (uint32_t)strtoul(a[i], NULL, 0);
	_Alignas(16) uint8_t cmd[0x100];
	memset(cmd, 0, sizeof cmd);
	*(uint8_t **)(cmd + 0x00) = B + RVA_VT_MERGE;
	*(uint8_t **)(cmd + 0x18) = B + RVA_VT_MERGE2;
	*(uint32_t *)(cmd + 0x20) = 1;
	*(uint32_t *)(cmd + 0x24) = (uint32_t)strtoul(a[1], NULL, 0);
	*(uint32_t **)(cmd + 0x28) = ids;
	*(int32_t *)(cmd + 0x30) = cnt;
	*(int32_t *)(cmd + 0x34) = cnt;
	*(uint8_t **)(cmd + 0x38) = B + RVA_ALLOC_U32;
	sending = 1;
	uint8_t r = H->post(cmd, 0xE);
	sending = 0;
	LOG("merge destino=%s com %d unidade(s): retorno=%u", a[1], cnt, r);
}

#define RVA_VT_RAISE         0x4122750
#define RVA_VT_RAISE2        0x4122720
#define RVA_ALLOC_RALLY      0x4FEBA40   // allocator of the rally point vector

static void cmd_raise(char **a, int n)
{
	// raise <rally point province> [type: 0 = normal, 1 = raid]
	if (n < 2) {
		LOG("uso: raise <provincia do ponto de reuniao> [tipo]");
		return;
	}
	static uint32_t rally[2];
	rally[0] = (uint32_t)strtoul(a[1], NULL, 0);   // province
	rally[1] = 0;                                  // color
	_Alignas(16) uint8_t cmd[0x100];
	memset(cmd, 0, sizeof cmd);
	*(uint8_t **)(cmd + 0x00) = B + RVA_VT_RAISE;
	*(uint8_t **)(cmd + 0x18) = B + RVA_VT_RAISE2;
	*(uint32_t *)(cmd + 0x20) = player_char();
	*(uint32_t **)(cmd + 0x28) = rally;
	*(int32_t *)(cmd + 0x30) = 1;
	*(int32_t *)(cmd + 0x34) = 1;
	*(uint8_t **)(cmd + 0x38) = B + RVA_ALLOC_RALLY;
	*(int32_t *)(cmd + 0x40) = -1;
	*(uint32_t *)(cmd + 0x48) = n > 2 ? (uint32_t)strtoul(a[2], NULL, 0) : 1;
	sending = 1;
	uint8_t r = H->post(cmd, 0xE);
	sending = 0;
	LOG("raise provincia=%u tipo=%u: retorno=%u", rally[0], *(uint32_t *)(cmd + 0x48), r);
}

// ---- days to a point (same calculation as the arrival estimate tooltip) ----
// Returns the days (rounded up), -1 if there is no path. Fills the path found if asked.
static int eta_days_raw(uint8_t *unit, uint8_t *target, int64_t *raw, uint32_t *path_out, int *path_n, int path_max,
                    int64_t *total_cost)
{
	_Alignas(16) uint8_t ctx[0x200];
	_Alignas(16) uint8_t path[0x140];
	int64_t t = 0, tmp = 0, tmp2 = 0;
	memset(ctx, 0, sizeof ctx);
	memset(path, 0, sizeof path);

	uint8_t *start = rdp(unit + UNIT_LOCATION);
	int moving = 0;
	int64_t *arrive = ((int64_t *(*)(void *, void *))(B + RVA_FN_MOVING))(unit, &tmp);
	if (arrive && *arrive > rd64(B + RVA_NOW)) {
		moving = 1;
		start = ((uint8_t *(*)(void *))(B + RVA_FN_NEXT_PROV))(unit);
	}
	if (!start || !target)
		return -1;

	((void (*)(void *))(B + RVA_PATH_CTOR))(path);
	((void (*)(void *, void *))(B + RVA_FN_PF_CTOR))(ctx, unit);
	((void (*)(void *, void *, void *, int, void *))(B + RVA_FN_PF_FIND))(ctx, start, target, 2, path);

	int n = *(int32_t *)(path + 0xC);
	if (path_n)
		*path_n = n;
	if (path_out) {
		uint8_t *data = *(uint8_t **)path;
		for (int i = 0; i < n && i < path_max; i++)
			path_out[i] = rd32(rdp(data + (size_t)i * 8));
	}
	if (total_cost)
		*total_cost = *(int64_t *)(path + 0x128);

	int days = -1;
	if (n > 0 || prov_id(start) == prov_id(target)) {
		((void (*)(void *, void *, void *, void *))(B + RVA_FN_TRAVEL_TIME))(unit, &t, path, start);
		if (moving) {
			int64_t *left = ((int64_t *(*)(void *, void *, int))(B + RVA_FN_STEP_LEFT))(unit, &tmp2, 0);
			if (left)
				t += *left;
		}
		days = (int)((t + FIXED_ONE - 1) / FIXED_ONE);
	}
	if (raw)
		*raw = t;
	free_path(path);
	return days;
}

static int eta_days(uint8_t *unit, uint8_t *target, int64_t *raw, uint32_t *path_out, int *path_n, int path_max,
                    int64_t *total_cost)
{
	int pn = 0, d = eta_days_raw(unit, target, raw, path_out, &pn, path_max, total_cost);
	if (path_n)
		*path_n = pn;
	if (rec_file) {
		char p[1024] = "";
		size_t k = 0;
		for (int i = 0; path_out && i < pn && i < path_max && k < sizeof p - 16; i++)
			k += (size_t)snprintf(p + k, sizeof p - k, "%s%u", i ? "," : "", path_out[i]);
		rec("{\"t\":\"eta\",\"u\":%u,\"a\":%u,\"r\":%d,\"n\":%d%s%s%s}", rd32(unit + UNIT_ID), prov_id(target), d, pn,
		    path_out ? ",\"p\":[" : "", p, path_out ? "]" : "");
	}
	return d;
}

// days the unit would take from `start` (any province) to `target`, at its speed; -1 without a path
static int eta_from_raw(uint8_t *unit, uint8_t *start, uint8_t *target, int64_t *raw)
{
	_Alignas(16) uint8_t ctx[0x200];
	_Alignas(16) uint8_t path[0x140];
	int64_t t = 0;
	if (!start || !target)
		return -1;
	if (start == target)
		return 0;
	memset(ctx, 0, sizeof ctx);
	memset(path, 0, sizeof path);
	((void (*)(void *))(B + RVA_PATH_CTOR))(path);
	((void (*)(void *, void *))(B + RVA_FN_PF_CTOR))(ctx, unit);
	((void (*)(void *, void *, void *, int, void *))(B + RVA_FN_PF_FIND))(ctx, start, target, 2, path);
	int days = -1;
	if (*(int32_t *)(path + 0xC) > 0) {
		((void (*)(void *, void *, void *, void *))(B + RVA_FN_TRAVEL_TIME))(unit, &t, path, start);
		days = (int)((t + FIXED_ONE - 1) / FIXED_ONE);
	}
	if (raw)
		*raw = t;
	free_path(path);
	return days;
}

// path from `start` to `target` at the unit's speed (provinces after `start`); returns the size
static int path_from(uint8_t *unit, uint8_t *start, uint8_t *target, uint32_t *out, int max)
{
	_Alignas(16) uint8_t ctx[0x200];
	_Alignas(16) uint8_t path[0x140];
	if (!start || !target || start == target)
		return 0;
	memset(ctx, 0, sizeof ctx);
	memset(path, 0, sizeof path);
	((void (*)(void *))(B + RVA_PATH_CTOR))(path);
	((void (*)(void *, void *))(B + RVA_FN_PF_CTOR))(ctx, unit);
	((void (*)(void *, void *, void *, int, void *))(B + RVA_FN_PF_FIND))(ctx, start, target, 2, path);
	int n = *(int32_t *)(path + 0xC);
	uint8_t *data = *(uint8_t **)path;
	for (int i = 0; i < n && i < max; i++)
		out[i] = rd32(rdp(data + (size_t)i * 8));
	free_path(path);
	return n < max ? n : max;
}

static int eta_from(uint8_t *unit, uint8_t *start, uint8_t *target)
{
	int d = eta_from_raw(unit, start, target, NULL);
	rec("{\"t\":\"etaf\",\"u\":%u,\"de\":%u,\"a\":%u,\"r\":%d}", rd32(unit + UNIT_ID), prov_id(start), prov_id(target), d);
	return d;
}


// ---- requests ----
static uint32_t parse_unit(const char *s)
{
	if (!s || strcmp(s, "last") == 0)
		return (uint32_t)last_player_unit;
	return (uint32_t)strtoul(s, NULL, 0);
}

static void describe_unit(uint32_t id, uint8_t *u)
{
	uint32_t path[64];
	int n = unit_path(u, path, 64);
	char buf[600];
	int k = 0;
	for (int i = 0; i < n && i < 64 && k < 560; i++)
		k += snprintf(buf + k, sizeof buf - k, " %u", path[i]);
	if (!n)
		strcpy(buf, " (parado)");
	LOG("unidade %u: provincia=%u dono=%u exercito=%u classe=%s caminho(%d):%s", id,
	    prov_id(rdp(u + UNIT_LOCATION)), rd32(u + UNIT_OWNER_CHAR), rd32(u + UNIT_ARMY),
	    relation_name(unit_relation(u)), n, buf);
}

static void cmd_where(char **a, int n)
{
	uint32_t id = parse_unit(n > 1 ? a[1] : "last");
	uint8_t *u = unit_by_id(id);
	if (!u) {
		LOG("where: unidade %u nao encontrada", id);
		return;
	}
	describe_unit(id, u);
}

static void cmd_eta(char **a, int n)
{
	if (n < 3) {
		LOG("uso: eta <unidade|last> <provincia>");
		return;
	}
	uint32_t id = parse_unit(a[1]), pid = (uint32_t)strtoul(a[2], NULL, 0);
	uint8_t *u = unit_by_id(id), *p = province_by_id(pid);
	if (!u || !p) {
		LOG("eta: unidade %u %s, provincia %u %s", id, u ? "ok" : "NAO encontrada", pid, p ? "ok" : "NAO encontrada");
		return;
	}
	uint32_t path[128];
	int pn = 0;
	int64_t raw = 0, cost = 0;
	int days = eta_days(u, p, &raw, path, &pn, 128, &cost);
	char buf[1200];
	int k = 0;
	buf[0] = 0;
	int sea = 0;
	for (int i = 0; i < pn && i < 128 && k < 1150; i++) {
		int s = prov_is_sea(province_by_id(path[i]));
		sea += s;
		k += snprintf(buf + k, sizeof buf - k, " %u%s", path[i], s ? "~" : "");
	}
	if (pn > 0 && k < 1150)
		snprintf(buf + k, sizeof buf - k, " | mar: %d de %d (~ = mar)", sea, pn);
	if (days < 0)
		LOG("eta: unidade %u -> provincia %u: SEM CAMINHO", id, pid);
	else
		LOG("eta: unidade %u (em %u) -> provincia %u: %d dias (bruto=%lld custo=%lld) caminho(%d):%s", id,
		    prov_id(rdp(u + UNIT_LOCATION)), pid, days, (long long)raw, (long long)cost, pn, buf);
}

static void cmd_move(char **a, int n, int first)
{
	// move <unit|last> <province> [queue] [ia]   (ia: sent as the AI, valid_for 2)
	if (n < first + 2) {
		LOG("uso: move <unidade|last> <provincia> [queue]");
		return;
	}
	uint32_t id = parse_unit(a[first]), pid = (uint32_t)strtoul(a[first + 1], NULL, 0);
	uint32_t order = 0;
	int ia = 0;
	for (int k = first + 2; k < n; k++) {
		order |= !strcmp(a[k], "queue");
		ia |= !strcmp(a[k], "ia");
	}
	uint8_t r = send_move_como(id, pid, order, ia);
	LOG("envio pela DLL: unit=%u target=%u order_type=%u%s retorno=%u", id, pid, order, ia ? " como IA" : "", r);
}

static void cmd_route(char **a, int n)
{
	// route <unit|last> <p1> <p2> ... : first province replaces, the rest go into the queue
	if (n < 3) {
		LOG("uso: route <unidade|last> <p1> <p2> ...");
		return;
	}
	uint32_t id = parse_unit(a[1]);
	for (int i = 2; i < n; i++) {
		uint32_t pid = (uint32_t)strtoul(a[i], NULL, 0);
		uint8_t r = send_move(id, pid, i > 2);
		LOG("rota: unit=%u ponto %d = %u retorno=%u", id, i - 1, pid, r);
	}
}

static void cmd_units(char **a, int n)
{
	// units [all|threat|count] : lists units (default: only ours; threat = enemy and hostile)
	int all = n > 1 && strcmp(a[1], "all") == 0;
	int threat = n > 1 && strcmp(a[1], "threat") == 0;
	int only_count = n > 1 && strcmp(a[1], "count") == 0;
	uint32_t by_class[6] = { 0 };
	uint32_t count = table_count(RVA_UNIT_TABLE), me = player_char(), shown = 0, total = 0;
	LOG("units: tabela com %u entradas, personagem do jogador=%u", count, me);
	for (uint32_t i = 0; i < count; i++) {
		uint8_t *u = table_at(RVA_UNIT_TABLE, i);
		if (!u)
			continue;
		uint32_t id = rd32(u + UNIT_ID);
		if ((id & 0xFFFFFF) != i)
			continue;
		total++;
		int rel = unit_relation(u);
		if (rel >= 0 && rel <= 5)
			by_class[rel]++;
		if (only_count)
			continue;
		if (threat ? (rel != 2 && rel != 3) : (!all && rd32(u + UNIT_OWNER_CHAR) != me))
			continue;
		if (shown++ < 80)
			describe_unit(id, u);
	}
	LOG("units: %u validas, %u listadas%s | minhas=%u aliadas=%u inimigas=%u hostis=%u neutras=%u outras=%u", total,
	    shown, shown > 80 ? " (corte em 80)" : "", by_class[0], by_class[1], by_class[2], by_class[3], by_class[4],
	    by_class[5]);
}

static void cmd_threats(char **a, int n)
{
	// threats <unit|last> [days] : foreign units whose path crosses our province or our route
	uint32_t id = parse_unit(n > 1 ? a[1] : "last");
	int horizon = n > 2 ? atoi(a[2]) : 15;
	int any_class = n > 3 && strcmp(a[3], "all") == 0;   // "all": considers any foreign unit
	uint8_t *me_u = unit_by_id(id);
	if (!me_u) {
		LOG("threats: unidade %u nao encontrada", id);
		return;
	}
	uint32_t mine[65];
	int mn = unit_path(me_u, mine + 1, 64);
	if (mn > 64)
		mn = 64;
	mine[0] = prov_id(rdp(me_u + UNIT_LOCATION));
	mn++;
	uint32_t me = player_char(), count = table_count(RVA_UNIT_TABLE);
	LARGE_INTEGER t0, t1, fq;
	QueryPerformanceFrequency(&fq);
	QueryPerformanceCounter(&t0);
	int checked = 0, crossing = 0, calcs = 0;
	for (uint32_t i = 0; i < count; i++) {
		uint8_t *u = table_at(RVA_UNIT_TABLE, i);
		if (!u)
			continue;
		uint32_t uid = rd32(u + UNIT_ID);
		if ((uid & 0xFFFFFF) != i || rd32(u + UNIT_OWNER_CHAR) == me)
			continue;
		int rel = unit_relation(u);
		if (!any_class && rel != 2 && rel != 3)
			continue;
		uint32_t path[256];
		int pn = unit_path(u, path, 256);
		if (pn <= 0)
			continue;
		if (pn > 256)
			pn = 256;
		checked++;
		// first point of its path that is in our province/route
		int hit = -1, where = 0;
		for (int k = 0; k < pn && hit < 0; k++)
			for (int m = 0; m < mn; m++)
				if (path[k] == mine[m]) {
					hit = k;
					where = m;
					break;
				}
		if (hit < 0)
			continue;
		crossing++;
		uint8_t *p = province_by_id(path[hit]);
		int64_t raw = 0;
		int days = p ? eta_days(u, p, &raw, NULL, NULL, 0, NULL) : -1;
		calcs++;
		LOG("ameaca: unidade %u (%s) dono=%u em %u destino=%u cruza em %u (%s) em %d dias%s", uid, relation_name(rel),
		    rd32(u + UNIT_OWNER_CHAR), prov_id(rdp(u + UNIT_LOCATION)), path[pn - 1], path[hit],
		    where == 0 ? "onde estamos" : "nossa rota", days, days >= 0 && days <= horizon ? " [DENTRO DO HORIZONTE]" : "");
	}
	QueryPerformanceCounter(&t1);
	LOG("threats: %d unidades inimigas/hostis em marcha, %d cruzam, %d calculos de caminho, %.2f ms", checked, crossing,
	    calcs, (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)fq.QuadPart);
}

static void cmd_peek(char **a, int n)
{
	// peek <address|exe+RVA|unit:<id>|prov:<id>> [bytes]
	if (n < 2) {
		LOG("uso: peek <endereco | exe+RVA | unit:<id> | prov:<id> | army:<id>> [bytes]");
		return;
	}
	const uint8_t *p;
	if (strncmp(a[1], "exe+", 4) == 0)
		p = B + strtoull(a[1] + 4, NULL, 16);
	else if (strncmp(a[1], "unit:", 5) == 0)
		p = unit_by_id(parse_unit(a[1] + 5));
	else if (strncmp(a[1], "prov:", 5) == 0)
		p = province_by_id((uint32_t)strtoul(a[1] + 5, NULL, 0));
	else if (strncmp(a[1], "army:", 5) == 0)
		p = table_get(RVA_ARMY_TABLE, (uint32_t)strtoul(a[1] + 5, NULL, 0), 0x10);
	else
		p = (const uint8_t *)strtoull(a[1], NULL, 16);
	int bytes = n > 2 ? (int)strtoul(a[2], NULL, 0) : 0x80;
	if (bytes > 0x1000)
		bytes = 0x1000;
	LOG("peek %s = %p", a[1], p);
	if (p)
		hexdump("  ", p, bytes);
}

// busca32 <gs|gs2|address> <value> [bytes=0x40000]: offsets (4-aligned) in the block that hold the 32-bit value;
// gs = [game_state], gs2 = [[game_state]+0xa0]. To find fields (e.g. the game date)
static void cmd_find32(char **a, int n)
{
	if (n < 3) {
		LOG("uso: busca32 <gs|gs2|endereco> <valor> [bytes]");
		return;
	}
	uint8_t *gs = rdp(B + RVA_GAME_STATE);
	const uint8_t *p = !strcmp(a[1], "gs") ? gs : !strcmp(a[1], "gs2") ? (gs ? rdp(gs + 0xa0) : NULL)
	                                                                   : (const uint8_t *)strtoull(a[1], NULL, 16);
	uint32_t v = (uint32_t)strtoul(a[2], NULL, 0);
	uint32_t bytes = n > 3 ? (uint32_t)strtoul(a[3], NULL, 0) : 0x40000;
	int achados = 0;
	for (uint32_t off = 0; p && off + 4 <= bytes; off += 4) {
		uint32_t x;
		if (!rd(p + off, &x, 4))
			break;
		if (x == v && achados++ < 32)
			LOG("busca32 %s+0x%X = %u", a[1], off, v);
	}
	LOG("busca32 %s (%p) valor %u: %d achado(s)", a[1], p, v, achados);
}

// procura <hex value | exe+RVA> [max] : addresses (8-aligned) in the process's own memory that hold the 64-bit
// value. Runs on a separate thread (reads tens of GB and would drag the main thread along)
static struct { uint64_t v; int max; } find_req;
static volatile LONG find_busy;

static DWORD WINAPI find_thread(void *unused)
{
	uint64_t v = find_req.v;
	int max = find_req.max, found = 0;
	MEMORY_BASIC_INFORMATION mi;
	uint8_t *p = NULL;
	uint64_t scanned = 0;
	while (found < max && VirtualQuery(p, &mi, sizeof mi) == sizeof mi) {
		uint8_t *next = (uint8_t *)mi.BaseAddress + mi.RegionSize;
		DWORD pr = mi.Protect & 0xff;
		// only the process's own memory: file mappings and video memory (write-combine) are slow to read
		if (mi.State == MEM_COMMIT && (mi.Type == MEM_PRIVATE || mi.Type == MEM_IMAGE) &&
		    !(mi.Protect & (PAGE_GUARD | PAGE_NOCACHE | PAGE_WRITECOMBINE)) &&
		    (pr == PAGE_READWRITE || pr == PAGE_READONLY || pr == PAGE_WRITECOPY || pr == PAGE_EXECUTE_READ ||
		     pr == PAGE_EXECUTE_READWRITE)) {
			const uint64_t *q = (const uint64_t *)mi.BaseAddress, *e = (const uint64_t *)next;
			scanned += mi.RegionSize;
			for (; q < e && found < max; q++)
				if (*q == v && (const void *)q != (const void *)&find_req.v) {
					uint8_t *at = (uint8_t *)q;
					if (at >= B && at < B + 0x6000000)
						LOG("procura: %p = exe+%llx", at, (unsigned long long)(at - B));
					else
						LOG("procura: %p", at);
					found++;
				}
		}
		if (next <= p)
			break;
		p = next;
	}
	LOG("procura %llx: %d achados, %.0f MB lidos", (unsigned long long)v, found, (double)scanned / 1048576.0);
	InterlockedExchange(&find_busy, 0);
	return 0;
}

static void cmd_find(char **a, int n)
{
	if (n < 2) {
		LOG("uso: procura <valor hex | exe+RVA> [max]");
		return;
	}
	if (InterlockedCompareExchange(&find_busy, 1, 0)) {
		LOG("procura: ja ha uma busca em andamento");
		return;
	}
	find_req.v = strncmp(a[1], "exe+", 4) == 0 ? (uint64_t)(B + strtoull(a[1] + 4, NULL, 16))
	                                            : strtoull(a[1], NULL, 16);
	find_req.max = n > 2 ? (int)strtoul(a[2], NULL, 0) : 32;
	HANDLE t = CreateThread(NULL, 0, find_thread, NULL, 0, NULL);
	if (t)
		CloseHandle(t);
	else
		InterlockedExchange(&find_busy, 0);
}

// layout <hex window> <file> : appends the file's pages (settings_layout.txt format) to the settings
// window, reading the text from memory (proof C2b, docs/dll/arquitetura_dll.md section 11.6b)
#define RVA_VT_MEMORY_FILE  0x4559758
#define RVA_ALLOC_CHAR      0x4fea420   // CPdxTypedNewDeleteAllocator<char>
#define RVA_FN_BUF_COPY     0x3bb5200   // (buffer, text, size)
#define RVA_FN_LEXER_STREAM 0x3b58cf0   // CLexer(lexer, &file, 0)
#define RVA_VT_TEXT_LEXER   0x4556c20
#define RVA_FN_LEXER_DTOR   0x3b58dc0
#define RVA_FN_READER       0x3b8e670   // (reader, &lexer)
#define RVA_FN_READER_DTOR  0x3b8e850
#define RVA_FN_BIND_CAT     0x32d5230   // (section)
#define WIN_PAGES           0xd8        // page vector: pointer, +0x08 capacity, +0x0c count
#define WIN_LAYOUT          0xc8        // sub-object that reads the layout (vtable[3])

static void cmd_layout(char **a, int n)
{
	if (n < 3) {
		LOG("uso: layout <janela hex> <arquivo>");
		return;
	}
	uint8_t *win = (uint8_t *)strtoull(a[1], NULL, 16);
	FILE *f = fopen(a[2], "rb");
	if (!f) {
		LOG("layout: nao abriu %s", a[2]);
		return;
	}
	static char text[16384];
	size_t len = fread(text, 1, sizeof text - 1, f);
	fclose(f);
	text[len] = 0;
	int before = (int)rd32(win + WIN_PAGES + 0xc);

	_Alignas(16) uint8_t buf[0x20] = {0}, mem[0x60] = {0}, lexer[0x100] = {0}, reader[0x400] = {0};
	*(void **)(buf + 0x10) = B + RVA_ALLOC_CHAR;
	((void (*)(void *, const char *, int))(B + RVA_FN_BUF_COPY))(buf, text, (int)len);
	*(void **)mem = B + RVA_VT_MEMORY_FILE;
	*(int32_t *)(mem + 0x08) = 1;
	*(int64_t *)(mem + 0x10) = 1;
	*(int64_t *)(mem + 0x30) = 0xf;              // empty std::string at +0x18
	*(void **)(mem + 0x38) = buf;
	*(int32_t *)(mem + 0x50) = -1;
	((void (*)(void *, void *, int))(B + RVA_FN_LEXER_STREAM))(lexer, mem, 0);
	*(void **)lexer = B + RVA_VT_TEXT_LEXER;
	((void (*)(void *, void *))(B + RVA_FN_READER))(reader, lexer);
	uint8_t *vis = win + WIN_LAYOUT;
	((void (*)(void *, void *))((*(void ***)vis)[3]))(vis, reader);
	((void (*)(void *))(B + RVA_FN_READER_DTOR))(reader);
	((void (*)(void *))(B + RVA_FN_LEXER_DTOR))(lexer);

	int after = (int)rd32(win + WIN_PAGES + 0xc);
	uint8_t **pages = (uint8_t **)rdp(win + WIN_PAGES);
	for (int i = before; i < after; i++) {
		uint8_t *pg = pages[i];
		uint8_t **cats = (uint8_t **)rdp(pg + 0x30);
		int nc = (int)rd32(pg + 0x3c);
		for (int k = 0; k < nc; k++)
			((void (*)(void *))(B + RVA_FN_BIND_CAT))(cats[k]);
		LOG("layout: pagina %d (%p) com %d secao(oes)", i, pg, nc);
	}
	LOG("layout: %d -> %d paginas (%u bytes de texto)", before, after, (unsigned)len);
}

// opcao <group> <name> <text_key> <0|1> : registers an on/off option with the game open (after the manager's
// lock) and applies the value through the same path as a click in the interface (proof C2b.3)
#define RVA_SETTINGS_MANAGER 0x5710d58
#define RVA_FN_SETTING_REG   0x3b44cf0   // (manager, &group, option, n)
#define RVA_FN_BOOL_SETTING  0x3b46730   // (obj 0xF8, &name, &text_key, default, 0)
#define RVA_FN_BOOL_SET      0x3b46990   // (option, value, 1): what the checkbox click calls
#define RVA_FN_NEW           0x3e25d74
static void cmd_option(char **a, int n)
{
	if (n < 5) {
		LOG("uso: opcao <grupo> <nome> <chave_texto> <0|1>");
		return;
	}
	struct { const char *p; int32_t len; uint8_t flag; } __attribute__((aligned(16))) g = { a[1], (int32_t)strlen(a[1]), 0 },
		nm = { a[2], (int32_t)strlen(a[2]), 0 }, k = { a[3], (int32_t)strlen(a[3]), 0 };
	static char names[16][3][64];   // the texts must outlive the call (the game copies them, but just in case)
	(void)names;
	void *mgr = rdp(B + RVA_SETTINGS_MANAGER);
	uint8_t *obj = mgr ? ((uint8_t *(*)(size_t))(B + RVA_FN_NEW))(0xF8) : NULL;
	if (!obj) {
		LOG("opcao: sem gerenciador");
		return;
	}
	int v = atoi(a[4]) != 0;
	((void (*)(void *, void *, void *, uint8_t, int))(B + RVA_FN_BOOL_SETTING))(obj, &nm, &k, (uint8_t)v, 0);
	InterlockedIncrement((LONG *)(obj + 0x10));
	((void (*)(void *, void *, void *, int))(B + RVA_FN_SETTING_REG))(mgr, &g, obj, 0);
	((void (*)(void *, uint8_t, int))(B + RVA_FN_BOOL_SET))(obj, (uint8_t)v, 1);
	LOG("opcao: %s/%s registrada (%p), valor %d aplicado; campos de valor = %d %d %d", a[1], a[2], obj, v,
	    obj[0xd0], obj[0xe0], obj[0xf0]);
}

// layout_tirar <hex window> <index> : removes the page from the page vector (the page's memory is not freed)
static void cmd_layout_remove(char **a, int n)
{
	if (n < 3) {
		LOG("uso: layout_tirar <janela hex> <indice>");
		return;
	}
	uint8_t *win = (uint8_t *)strtoull(a[1], NULL, 16);
	int idx = atoi(a[2]), count = (int)rd32(win + WIN_PAGES + 0xc);
	uint8_t **pages = (uint8_t **)rdp(win + WIN_PAGES);
	if (idx < 0 || idx >= count) {
		LOG("layout_tirar: indice %d fora de 0..%d", idx, count - 1);
		return;
	}
	memmove(pages + idx, pages + idx + 1, (size_t)(count - idx - 1) * sizeof *pages);
	*(int32_t *)(win + WIN_PAGES + 0xc) = count - 1;
	LOG("layout_tirar: pagina %d tirada, %d paginas", idx, count - 1);
}

static void cmd_pan(char **a, int n)
{
	// pan <province> : centers the game camera on the province
	uint32_t pid = n > 1 ? (uint32_t)strtoul(a[1], NULL, 0) : 0;
	uint8_t *p = province_by_id(pid);
	if (!p) {
		LOG("pan: provincia %u nao encontrada", pid);
		return;
	}
	char key[64];
	((void (*)(void *))(B + RVA_FN_PAN_CAMERA))(p);
	((void (*)(void *))(B + RVA_FN_ZOOM_CAMERA))(p);
	LOG("pan: camera na provincia %u (%s)", pid, prov_key(p, key, sizeof key));
}

// ---- battle (auto_raid docs/plano_re_batalha.md, R1/R2) ----
// combat: global table (id at +0x8); army -> combat id at +0x128 (CArmyToCombatSideLink, 0x19da920). Combat:
// sides at +0x20 and +0x368 (CCombatSide, 0x348 bytes; side 0 = attacker, whose armies are in the list +0x30/+0x3c),
// width +0x6c4, advantage base +0x6c8, rolls +0x6d0/+0x6d4, damage multiplier of the side with advantage +0x6d8,
// roll counter +0x6e4, forced winner +0x700, advantage (attacker - defender) +0x710. Side: commander +0x74,
// men fighting +0x98, total +0xa8.
#define RVA_COMBAT_TABLE   0x570c6d8
#define RVA_FN_SIDE_ADV    0x2307cd0   // (combat, &out, side, tooltip|NULL): roll + commander + the side's modifiers
#define RVA_FN_CMD_ADV     0x23076a0   // (combat, &out, character, side, x, tooltip|NULL)
#define RVA_FN_MOD_ADV     0x2307250   // (combat, &out, side block, side, x, tooltip|NULL)
#define RVA_FN_ADV_X       0x23070a0   // (combat, side) -> x
#define RVA_FN_CHAR_MODS   0x26172e0   // (character) -> object with the modifier block at +0x68
#define RVA_FN_MOD_VALUE   0x20ab970   // (block, &out, u16 index) -> fixed-point value (0 if absent)

static uint8_t *combat_of_army(uint32_t army)
{
	uint8_t *ex = table_get(RVA_ARMY_TABLE, army, 0x10);
	return ex ? table_get(RVA_COMBAT_TABLE, rd32(ex + 0x128), 0x8) : NULL;
}

#define RVA_FN_ROLL_BOUND  0x2308e10   // (character, terrain type, minimum?, tooltip|NULL) -> minimum/maximum roll

static int combat_fill(uint8_t *c, uint32_t army, ck3_combat *o)
{
	memset(o, 0, sizeof *o);
	o->id = rd32(c + 8);
	o->lado_do_exercito = 1;
	int na = (int)rd32(c + 0x3c);
	uint8_t *lst = rdp(c + 0x30);
	for (int i = 0; i < na && i < 256; i++)
		if (rd32(lst + 4 * i) == army)
			o->lado_do_exercito = 0;
	o->largura = (int)rd32(c + 0x6c4);
	o->contador = (int)rd32(c + 0x6e4);
	o->forcado = (int)rd32(c + 0x700);
	o->vantagem = (double)rd64(c + 0x710) / FIXED_ONE;
	o->base = (double)rd64(c + 0x6c8) / FIXED_ONE;
	o->mult = (double)rd64(c + 0x6d8) / FIXED_ONE;
	uint8_t *loc = rdp(c + 0x6b8), *terr = loc ? rdp(loc + 0x20) : NULL;
	terr = terr ? rdp(terr + 0xb8) : NULL;
	for (int s = 0; s < 2; s++) {
		uint8_t *sd = c + 0x20 + s * 0x348;
		int64_t adv = 0, cmd = 0, mods = 0;
		((int64_t *(*)(void *, int64_t *, int, void *))(B + RVA_FN_SIDE_ADV))(c, &adv, s, NULL);
		int x = ((int (*)(void *, int))(B + RVA_FN_ADV_X))(c, s);
		o->comandante[s] = rd32(sd + 0x74);
		uint8_t *ch = table_get(RVA_CHAR_TABLE, o->comandante[s], 0x18);
		if (ch) {
			((int64_t *(*)(void *, int64_t *, void *, int, int, void *))(B + RVA_FN_CMD_ADV))(c, &cmd, ch, s, x, NULL);
			if (terr) {
				// third argument 1 = minimum (COMMANDER_MIN_ROLL + min_combat_roll), 0 = maximum
				o->rmin[s] = ((int (*)(void *, void *, int, void *))(B + RVA_FN_ROLL_BOUND))(ch, terr, 1, NULL);
				o->rmax[s] = ((int (*)(void *, void *, int, void *))(B + RVA_FN_ROLL_BOUND))(ch, terr, 0, NULL);
			}
		}
		((int64_t *(*)(void *, int64_t *, void *, int, int, void *))(B + RVA_FN_MOD_ADV))(c, &mods, sd + 0x110, s, x, NULL);
		o->rolagem[s] = (int)rd32(c + 0x6d0 + 4 * s);
		o->lutando[s] = (double)rd64(sd + 0x98) / FIXED_ONE;
		o->total[s] = (double)rd64(sd + 0xa8) / FIXED_ONE;
		o->v[s] = (double)adv / FIXED_ONE;
		o->v_cmd[s] = (double)cmd / FIXED_ONE;
		o->v_mods[s] = (double)mods / FIXED_ONE;
	}
	return 1;
}

// (api 6) hypothetical combat (auto_raid docs/plano_re_batalha.md R7): minimal combat object with what the advantage functions read
// (province at +0x6b8, province flag at +0x6fd via the same function as the constructor, and per side: main participant
// +0x70 = army owner, commander +0x74 = army+0x120, army list +0x10/+0x1c) and the same game functions
// for V (without roll) and the roll range. The base (combat effects) is left to the caller
#define RVA_FN_COMBAT_EFFECTS 0x2304850 // (combat): starting combat effects in the base
#define RVA_FN_PROV_FLAG   0xbc24e0    // (province) -> byte stored at combat+0x6fd in the constructor

static int api_combat_hypo(uint32_t atac, uint32_t def, uint32_t prov, ck3_combat *o)
{
	uint8_t *pv = province_by_id(prov), *ex[2] = { table_get(RVA_ARMY_TABLE, atac, 0x10), table_get(RVA_ARMY_TABLE, def, 0x10) };
	if (!o || !pv || !ex[0] || !ex[1])
		return 0;
	uint8_t *c = calloc(1, 0x800);
	if (!c)
		return 0;
	static uint32_t lista[2];
	lista[0] = atac;
	lista[1] = def;
	*(uint8_t **)(c + 0x6b8) = pv;
	c[0x6fd] = ((uint8_t (*)(void *))(B + RVA_FN_PROV_FLAG))(pv);
	memset(o, 0, sizeof *o);
	o->provincia = prov;
	uint8_t *terr = rdp(pv + 0x20);
	terr = terr ? rdp(terr + 0xb8) : NULL;
	for (int k = 0; k < 2; k++) {
		uint8_t *sd = c + 0x20 + k * 0x348, *u = unit_by_id(rd32(ex[k] + 0x124));
		*(uint32_t *)(sd + 0x70) = u ? rd32(u + UNIT_OWNER_CHAR) : 0;
		*(uint32_t *)(sd + 0x74) = rd32(ex[k] + 0x120);   // army commander
		*(uint32_t **)(sd + 0x10) = &lista[k];
		*(int32_t *)(sd + 0x1c) = 1;
		*(int32_t *)(sd + 0x18) = 1;
	}
	// starting combat effects (supply, levy gathering, fort/faith of the location's owner, debt): the game function
	// (0x2304850) adds to the base (+0x6c8) and records each effect in the side's list (+0x78 of the side: pointer, capacity,
	// size; 16 bytes per effect); the list comes ready with spare room, so the game does not allocate memory in the fake combat. Terrain
	// and crossing are applied when the combat is created by movement (0x2209470) and are left out
	static uint8_t lista_ef[2][64 * 16];
	for (int k = 0; k < 2; k++) {
		uint8_t *sd = c + 0x20 + k * 0x348;
		*(uint8_t **)(sd + 0x78) = lista_ef[k];
		*(int32_t *)(sd + 0x80) = 64;
		*(int32_t *)(sd + 0x84) = 0;
	}
	((void (*)(void *))(B + RVA_FN_COMBAT_EFFECTS))(c);
	o->base = (double)rd64(c + 0x6c8) / FIXED_ONE;
	for (int k = 0; k < 2; k++) {
		uint8_t *sd = c + 0x20 + k * 0x348;
		int64_t adv = 0, cmd = 0, mods = 0;
		((int64_t *(*)(void *, int64_t *, int, void *))(B + RVA_FN_SIDE_ADV))(c, &adv, k, NULL);
		int x = ((int (*)(void *, int))(B + RVA_FN_ADV_X))(c, k);
		o->comandante[k] = rd32(sd + 0x74);
		uint8_t *ch = table_get(RVA_CHAR_TABLE, o->comandante[k], 0x18);
		if (ch) {
			((int64_t *(*)(void *, int64_t *, void *, int, int, void *))(B + RVA_FN_CMD_ADV))(c, &cmd, ch, k, x, NULL);
			if (terr) {
				o->rmin[k] = ((int (*)(void *, void *, int, void *))(B + RVA_FN_ROLL_BOUND))(ch, terr, 1, NULL);
				o->rmax[k] = ((int (*)(void *, void *, int, void *))(B + RVA_FN_ROLL_BOUND))(ch, terr, 0, NULL);
			}
		}
		((int64_t *(*)(void *, int64_t *, void *, int, int, void *))(B + RVA_FN_MOD_ADV))(c, &mods, sd + 0x110, k, x, NULL);
		o->v[k] = (double)adv / FIXED_ONE;
		o->v_cmd[k] = (double)cmd / FIXED_ONE;
		o->v_mods[k] = (double)mods / FIXED_ONE;
	}
	free(c);
	return 1;
}

static void cmd_hipo(char **a, int n)
{
	// hipo <attacking army> <defending army> <province>: advantage calculated without combat (api 6)
	if (n < 4) {
		LOG("uso: hipo <atacante> <defensor> <provincia>");
		return;
	}
	ck3_combat o;
	if (!api_combat_hypo((uint32_t)strtoul(a[1], NULL, 0), (uint32_t)strtoul(a[2], NULL, 0), (uint32_t)strtoul(a[3], NULL, 0), &o)) {
		LOG("hipo: exercito ou provincia invalido");
		return;
	}
	for (int k = 0; k < 2; k++)
		LOG("hipo lado %d: comandante %u, V sem rolagem %.3f (comandante %.3f, modificadores %.3f), rolagem [%d..%d]", k,
		    o.comandante[k], o.v[k], o.v_cmd[k], o.v_mods[k], o.rmin[k], o.rmax[k]);
	LOG("hipo: base dos efeitos de inicio %.3f (sem terreno e travessia)", o.base);
}

static int api_combat_info(uint32_t army, ck3_combat *out)
{
	uint8_t *c = combat_of_army(army);
	return c && out ? combat_fill(c, army, out) : 0;
}

static void cmd_combate(char **a, int n)
{
	// combate <army>: state of the combat the army is in
	if (n < 2) {
		LOG("uso: combate <exercito>");
		return;
	}
	uint8_t *c = combat_of_army((uint32_t)strtoul(a[1], NULL, 0));
	if (!c) {
		LOG("combate: exercito %s sem combate", a[1]);
		return;
	}
	LOG("combate %u (%p): vantagem %.3f base %.3f rolagens %d/%d mult %.3f largura %d contador %d forcado %d",
	    rd32(c + 8), c, (double)rd64(c + 0x710) / FIXED_ONE, (double)rd64(c + 0x6c8) / FIXED_ONE, (int)rd32(c + 0x6d0),
	    (int)rd32(c + 0x6d4), (double)rd64(c + 0x6d8) / FIXED_ONE, (int)rd32(c + 0x6c4), (int)rd32(c + 0x6e4),
	    (int)rd32(c + 0x700));
	for (int s = 0; s < 2; s++) {
		uint8_t *sd = c + 0x20 + s * 0x348;
		int64_t adv = 0, cmd = 0, mods = 0;
		((int64_t *(*)(void *, int64_t *, int, void *))(B + RVA_FN_SIDE_ADV))(c, &adv, s, NULL);
		int x = ((int (*)(void *, int))(B + RVA_FN_ADV_X))(c, s);
		uint8_t *ch = table_get(RVA_CHAR_TABLE, rd32(sd + 0x74), 0x18);
		if (ch)
			((int64_t *(*)(void *, int64_t *, void *, int, int, void *))(B + RVA_FN_CMD_ADV))(c, &cmd, ch, s, x, NULL);
		((int64_t *(*)(void *, int64_t *, void *, int, int, void *))(B + RVA_FN_MOD_ADV))(c, &mods, sd + 0x110, s, x, NULL);
		LOG("  lado %d: comandante %u, lutando %.1f de %.1f, vantagem do lado %.3f (comandante %.3f, modificadores %.3f, x %d)",
		    s, rd32(sd + 0x74), (double)rd64(sd + 0x98) / FIXED_ONE, (double)rd64(sd + 0xa8) / FIXED_ONE,
		    (double)adv / FIXED_ONE, (double)cmd / FIXED_ONE, (double)mods / FIXED_ONE, x);
	}
	ck3_combat o;
	combat_fill(c, (uint32_t)strtoul(a[1], NULL, 0), &o);
	LOG("  rolagem atacante %d [%d..%d], defensor %d [%d..%d]; exercito no lado %d", o.rolagem[0], o.rmin[0], o.rmax[0],
	    o.rolagem[1], o.rmin[1], o.rmax[1], o.lado_do_exercito);
}

static void cmd_modval(char **a, int n)
{
	// modval <character>: all modifiers of the character's block (index and value)
	if (n < 2) {
		LOG("uso: modval <personagem>");
		return;
	}
	uint8_t *ch = table_get(RVA_CHAR_TABLE, (uint32_t)strtoul(a[1], NULL, 0), 0x18);
	if (!ch) {
		LOG("modval: personagem %s nao achado", a[1]);
		return;
	}
	uint8_t *m = ((uint8_t *(*)(void *))(B + RVA_FN_CHAR_MODS))(ch);
	uint8_t *blk = m + 0x68;
	uint8_t *idx = rdp(blk);
	int cnt = (int)rd32(blk + 0xc);
	char linha[900] = "";
	size_t p = 0;
	int k = 0;
	for (int i = 0; i < cnt && i < 4000; i++) {
		uint16_t ix = 0;
		rd(idx + 2 * i, &ix, 2);
		int64_t v = 0;
		((int64_t *(*)(void *, int64_t *, uint16_t))(B + RVA_FN_MOD_VALUE))(blk, &v, ix);
		p += (size_t)snprintf(linha + p, sizeof linha - p, " %u=%.3f", ix, (double)v / FIXED_ONE);
		if (p > sizeof linha - 40) {
			LOG("modval %s:%s", a[1], linha);
			p = 0;
			linha[0] = 0;
		}
		k++;
	}
	LOG("modval %s:%s (%d modificadores)", a[1], linha, k);
}

static void cmd_predict(char **a, int n)
{
	// predict <our unit|last>[,unit...] <their unit>[,unit...] : the game's combat prediction
	// between two groups (unit ids separated by commas, no spaces)
	if (n < 3) {
		LOG("uso: predict <nossas,separadas,por,virgula> <deles,separadas,por,virgula>");
		return;
	}
	static uint32_t side[2][32];
	int cnt[2] = { 0, 0 };
	for (int s = 0; s < 2; s++) {
		char tmp[256];
		strncpy(tmp, a[1 + s], sizeof tmp - 1);
		tmp[sizeof tmp - 1] = 0;
		for (char *t = strtok(tmp, ","); t && cnt[s] < 32; t = strtok(NULL, ",")) {
			uint32_t id = parse_unit(t);
			uint8_t *u = unit_by_id(id);
			if (!u) {
				LOG("predict: unidade %u nao encontrada", id);
				return;
			}
			side[s][cnt[s]++] = rd32(u + UNIT_ARMY);
		}
	}
	struct { uint32_t *data; int32_t count, cap; uint8_t *alloc; } va = { side[0], cnt[0], cnt[0], B + RVA_ALLOC_U32 },
	                                                                vb = { side[1], cnt[1], cnt[1], B + RVA_ALLOC_U32 };
	int64_t ratio = 0;
	int64_t k = rd64(B + RVA_PREDICT_K);
	((void (*)(int64_t *, void *, void *, int64_t, int64_t, int64_t, int))(B + RVA_FN_PREDICT))(&ratio, &va, &vb, k, k,
	                                                                                             0, 0);
	int64_t *levels = (int64_t *)rdp(B + RVA_PREDICT_LEVELS);
	int nlev = (int)rd32(B + RVA_PREDICT_LEVELS + 0xC), level = 1;
	for (int i = 0; i < nlev && i < 16; i++)
		if (ratio >= rd64(levels + i))
			level = i + 2;
	LOG("predict: %d exercito(s) nosso(s) contra %d: chance=%.3f nivel=%d de %d", cnt[0], cnt[1],
	    (double)ratio / FIXED_ONE, level, nlev + 1);
}

static int prov_is_sea(uint8_t *prov)
{
	uint8_t *def = prov ? rdp(prov + 8) : NULL, b = 0;
	if (def)
		rd(def + 0x1d, &b, 1);
	return b != 0;
}

// raid days: total required by the province / the unit's progress per day (minus what is already done)
static int raid_days_raw(uint8_t *army, uint8_t *prov, int64_t done, int64_t *total_out, int64_t *perday_out);
static int raid_days(uint8_t *army, uint8_t *prov, int64_t done, int64_t *total_out, int64_t *perday_out)
{
	int64_t tot = 0, per = 0;
	int d = raid_days_raw(army, prov, done, &tot, &per);
	if (total_out)
		*total_out = tot;
	if (perday_out)
		*perday_out = per;
	rec("{\"t\":\"saq\",\"ex\":%u,\"a\":%u,\"feito\":%lld,\"total\":%lld,\"dia\":%lld,\"r\":%d}", rd32(army + 0x10),
	    prov_id(prov), (long long)done, (long long)tot, (long long)per, d);
	return d;
}

static int raid_days_raw(uint8_t *army, uint8_t *prov, int64_t done, int64_t *total_out, int64_t *perday_out)
{
	int64_t t1 = 0, t2 = 0;
	int64_t *total = ((int64_t *(*)(void *, void *))(B + RVA_FN_RAID_TOTAL))(&t1, prov);
	int64_t *perday = ((int64_t *(*)(void *, void *, int))(B + RVA_FN_RAID_PER_DAY))(&t2, army, 0);
	if (total_out)
		*total_out = total ? *total : 0;
	if (perday_out)
		*perday_out = perday ? *perday : 0;
	if (!total || !perday || *perday <= 0)
		return -1;
	int64_t left = *total - done;
	if (left < 0)
		left = 0;
	return (int)((left + *perday - 1) / *perday);
}

static void cmd_loot(char **a, int n)
{
	// loot <unit|last> [province] : loot carried, capacity, raid in progress and days to finish;
	// with a province, estimates the raid days at that location
	uint32_t id = parse_unit(n > 1 ? a[1] : "last");
	uint8_t *u = unit_by_id(id);
	if (!u) {
		LOG("loot: unidade %u nao encontrada", id);
		return;
	}
	uint8_t *army = table_get(RVA_ARMY_TABLE, rd32(u + UNIT_ARMY), 0x10);
	if (!army) {
		LOG("loot: exercito da unidade %u nao encontrado", id);
		return;
	}
	uint8_t is_raid = 0;
	rd(army + ARMY_IS_RAID, &is_raid, 1);
	int64_t cap = 0;
	((void (*)(void *, void *))(B + RVA_FN_LOOT_CAP))(army, &cap);
	int64_t loot = rd64(army + ARMY_LOOT);
	uint8_t stance = 0;
	rd(army + ARMY_LOOTER_STANCE, &stance, 1);
	LOG("loot: unidade %u exercito raid=%u saquear-ao-parar=%u saque=%.2f capacidade=%.2f (%s)", id, is_raid, stance,
	    (double)loot / FIXED_ONE, (double)cap / FIXED_ONE, is_raid && loot >= cap ? "LOTADO" : "cabe mais");
	uint32_t rid = rd32(army + ARMY_RAID);
	uint8_t *raid = rid != 0xFFFFFFFF ? table_get(RVA_RAID_TABLE, rid, 8) : NULL;
	if (raid) {
		uint8_t *prov = rdp(raid + 0x20);
		int64_t done = rd64(raid + 0x18), total = 0, perday = 0;
		int days = raid_days(army, prov, done, &total, &perday);
		LOG("loot: raid em andamento (alvo id=%u, objeto %p): progresso=%.1f de %.1f, %.1f por dia, faltam %d dias",
		    prov_id(prov), prov, (double)done / FIXED_ONE, (double)total / FIXED_ONE, (double)perday / FIXED_ONE, days);
	} else {
		LOG("loot: nenhum raid em andamento (id=%d)", (int)rid);
	}
}

static int unit_army_cap(uint8_t *u, uint8_t **army_out, double *free_cap)
{
	uint8_t *army = table_get(RVA_ARMY_TABLE, rd32(u + UNIT_ARMY), 0x10);
	if (!army)
		return 0;
	int64_t cap = 0;
	((void (*)(void *, void *))(B + RVA_FN_LOOT_CAP))(army, &cap);
	*army_out = army;
	*free_cap = (double)(cap - rd64(army + ARMY_LOOT)) / FIXED_ONE;
	return 1;
}

// pause state: 0 = running, bit 1 = paused by the player, bit 2 = paused by event/message, bit 4 = other
static int pause_state(void)
{
	int st = 0;
	uint8_t *app = rdp(B + RVA_APP), manual = 0;
	if (app && rd(app + 0x20, &manual, 1) && manual)
		st |= 1;
	uint8_t *gs = rdp(rdp(B + RVA_GAME_STATE) + 0xa0);
	if (gs && ((uint8_t (*)(void *, int))(B + RVA_FN_EVENT_PAUSE))(gs + 0x2f4c0, 0))
		st |= 2;
	if (app && ((uint8_t (*)(void *))(B + RVA_FN_OTHER_PAUSE))(app))
		st |= 4;
	return st;
}

static void cmd_state(void)
{
	int st = pause_state();
	uint8_t *gs = rdp(B + RVA_GAME_STATE);
	LOG("state: %s%s%s%s velocidade=%u", st == 0 ? "CORRENDO" : "PAUSADO", st & 1 ? " [pelo jogador]" : "",
	    st & 2 ? " [por evento/mensagem aguardando resposta]" : "", st & 4 ? " [outra pausa]" : "",
	    gs ? rd32(gs + 0x70) + 1 : 0);
}

// velocidade <1..5>: writes the field that `state` reads ([game_state]+0x70 = speed - 1). For windowless runs
// (-nographics), where the 5 key via PostMessage has nowhere to go (auto_raid docs/dossie_exercitos.md, 9.6)
static void cmd_speed(int v)
{
	uint8_t *gs = rdp(B + RVA_GAME_STATE);
	if (!gs || v < 1 || v > 5) {
		LOG("velocidade: %s", gs ? "uso: velocidade <1..5>" : "sem partida");
		return;
	}
	uint32_t antes = rd32(gs + 0x70) + 1;
	*(volatile uint32_t *)(gs + 0x70) = (uint32_t)(v - 1);
	LOG("velocidade: %u -> %u", antes, rd32(gs + 0x70) + 1);
}

#define GS_DATE_OFF 0x9c   // [game_state]+0x9c: game date in days since year 0, 365 per year (busca32, 2026-10-07)

// status <channel>: the loader asks every second; writes ck3_custom\status.json for the panel
// (auto_raid docs/testes/painel.py), without going through the log
static void cmd_status(const char *canal)
{
	char path[MAX_PATH + 64], tmp[MAX_PATH + 64];
	char inst[32] = "";
	GetEnvironmentVariableA("AR_INSTANCIA", inst, sizeof inst);
	snprintf(path, sizeof path, "%sck3_custom\\status%s%s.json", H->dir, inst[0] ? "_" : "", inst);
	snprintf(tmp, sizeof tmp, "%s.tmp", path);
	uint8_t *gs = rdp(B + RVA_GAME_STATE);
	FILE *f = fopen(tmp, "w");
	if (!f)
		return;
	// without graphics the game starts running after loading (with graphics it opens paused): pause on the transition
	// "loading -> loaded" (speed -1 during loading), like the game with a window. -1 = not seen yet (a core reload
	// with the game running does not pause)
	static int carregando_antes = -1;
	int carregando = !gs || rd32(gs + 0x70) > 4;
	if (carregando_antes == 1 && !carregando && strstr(GetCommandLineA(), "-nographics")) {
		uint32_t f[2] = { 1, 1 };
		uint8_t r = send_cmd(RVA_VT_PAUSE, RVA_VT_PAUSE2, f, 2, 0x7);
		LOG("status: partida carregada sem graficos, pausada como com janela (retorno=%u)", r);
		ng_segura = 1;
		ng_cmds = 0;
	}
	carregando_antes = carregando;
	int st = gs ? pause_state() : -1;
	if (ng_segura == 1 && !carregando && st == 0) {
		uint32_t f[2] = { 1, 1 };
		uint8_t r = send_cmd(RVA_VT_PAUSE, RVA_VT_PAUSE2, f, 2, 0x7);
		LOG("status: o jogo despausou sozinho depois da carga (dia %u); pausado de novo (retorno=%u)",
		    rd32(gs + GS_DATE_OFF), r);
		st = gs ? pause_state() : -1;
	}
	fprintf(f, "{\"t\":%lld,\"canal\":\"%s\",\"partida\":%d,\"pausa\":%d,\"velocidade\":%u", (long long)time(NULL),
	        canal, gs != NULL, st, gs ? rd32(gs + 0x70) + 1 : 0);
	if (gs) {
		static const int mes[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
		uint32_t dias = rd32(gs + GS_DATE_OFF), y = dias / 365, r = dias % 365, m = 0;
		while (m < 11 && r >= (uint32_t)mes[m])
			r -= mes[m++];
		fprintf(f, ",\"dia\":%u,\"data\":\"%u.%02u.%02u\"", dias, y, m + 1, r + 1);
	}
	fputs("}\n", f);
	fclose(f);
	MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING);
}

// ---- the player's pending events ----
#define EVENTS_OFF        0x2f4c0   // [[state]+0xa0] + this = event manager
#define EVENTS_ARRAY      0x1f18
#define EVENTS_COUNT      0x1f24
#define EVENT_OWNER       0x1b8
#define EVENT_ID          0x1bc
#define RVA_VT_EVENT_OPT  0x4335150
#define RVA_VT_EVENT_OPT2 0x4335120

// ---- load a save with the game open (the same path as --continuelastsave: "Hosting save") ----
#define RVA_FN_SAVE_EXISTS 0x3be2290   // (&text) -> non-null if the save file exists
#define RVA_FN_ALLOC       0x3e25d74   // the game's operator new
#define RVA_FN_SAVE_SOURCE 0x3b55990   // (object 0x68, &text, 1, 0): source of the save to load
#define RVA_GAME_APP       0x576cbe8   // CGameApplication*; vtable +0x78 = load save (app, source, options)

static void cmd_load(const char *line)
{
	// carregar [testar] <save path, may contain spaces>
	static char path[512];
	int test = !strncmp(line, "testar ", 7);
	if (test)
		line += 7;
	strncpy(path, line, sizeof path - 1);
	for (size_t k = strlen(path); k > 0 && (path[k - 1] == '\r' || path[k - 1] == '\n' || path[k - 1] == ' '); k--)
		path[k - 1] = 0;
	struct { const char *p; uint32_t len; uint8_t flag; } __attribute__((aligned(16))) text =
		{ path, (uint32_t)strlen(path), 0 };
	void *found = ((void *(*)(void *))(B + RVA_FN_SAVE_EXISTS))(&text);
	LOG("carregar '%s': arquivo %s", path, found ? "encontrado" : "NAO encontrado");
	if (!found || test)
		return;
	uint8_t *app = rdp(B + RVA_GAME_APP);
	if (!app) {
		LOG("carregar: aplicacao nao encontrada");
		return;
	}
	void *src = ((void *(*)(size_t))(B + RVA_FN_ALLOC))(0x68);
	((void (*)(void *, void *, int, int))(B + RVA_FN_SAVE_SOURCE))(src, &text, 1, 0);
	void (*load)(void *, void *, void *) = (void (*)(void *, void *, void *))rdp(rdp(app) + 0x78);
	load(app, src, NULL);
	LOG("carregar: pedido enviado");
}

// ---- save the game (CAutoSaveCommand: +0x20 std::string with the save name; empty = autosave) ----
#define RVA_VT_AUTOSAVE  0x40aabe8
#define RVA_VT_AUTOSAVE2 0x40aac80

static int mute_chamar;   // test: `mudo on` makes the plugins' effects not respond (the game without the DLL)

static void cmd_save(char **a, int n)
{
	const char *name = n > 1 ? a[1] : "";
	size_t len = strlen(name);
	if (len > 15) {
		LOG("salvar: nome com mais de 15 letras nao suportado");
		return;
	}
	_Alignas(16) uint8_t cmd[0x100];
	memset(cmd, 0, sizeof cmd);
	*(uint8_t **)(cmd + 0x00) = B + RVA_VT_AUTOSAVE;
	*(uint8_t **)(cmd + 0x18) = B + RVA_VT_AUTOSAVE2;
	memcpy(cmd + 0x20, name, len);       // short std::string: text inside the object
	*(uint64_t *)(cmd + 0x30) = len;
	*(uint64_t *)(cmd + 0x38) = 15;
	sending = 1;
	uint8_t r = H->post(cmd, 0xF);
	sending = 0;
	LOG("salvar '%s': retorno=%u", name, r);
}

// ---- succession screen (PAUSED_BY_SUCCESSION) ----
// CPlayerInfo at [[state]+0xa0]+0x1d548 (count at +0x1d554): +0xb0 player id, +0xd8 character,
// +0x260 succession waiting for the player to close the screen. CCloseSuccessionCommand: +0x20 = [+0xb0], +0x24 = [+0xd8].
// Flags 0xF: with the screen open the dispatcher is blocked (+0x29a) and only accepts commands with bit 0 (like pause)
#define RVA_VT_CLOSE_SUCC  0x43220e8
#define RVA_VT_CLOSE_SUCC2 0x43220b8

static int succession(int close)
{
	uint8_t *gs = rdp(rdp(B + RVA_GAME_STATE) + 0xa0);
	uint8_t *arr = gs ? rdp(gs + 0x1d548) : NULL;
	int cnt = gs ? (int)rd32(gs + 0x1d554) : 0, pend = 0;
	for (int i = 0; i < cnt && i < 64; i++) {
		uint8_t *pi = rdp(arr + (size_t)i * 8), flag = 0;
		if (!pi || !rd(pi + 0x260, &flag, 1) || !flag)
			continue;
		pend++;
		if (close) {
			uint32_t f[2] = { rd32(pi + 0xb0), rd32(pi + 0xd8) };
			uint8_t r = send_cmd(RVA_VT_CLOSE_SUCC, RVA_VT_CLOSE_SUCC2, f, 2, 0xF);
			LOG("  sucessao fechada: personagem=%u jogador=%u retorno=%u", f[0], f[1], r);
		}
	}
	return pend;
}

static void cmd_events(char **a, int n)
{
	// events [dump | answer [option]] : lists the player's pending events; answer replies to all of them
	uint8_t *gs = rdp(rdp(B + RVA_GAME_STATE) + 0xa0);
	if (!gs) {
		LOG("events: sem partida carregada");
		return;
	}
	uint8_t *mgr = gs + EVENTS_OFF;
	uint8_t *arr = rdp(mgr + EVENTS_ARRAY);
	int cnt = (int)rd32(mgr + EVENTS_COUNT);
	uint32_t me = player_char();
	int mine = 0;
	for (int i = 0; i < cnt && i < 4096; i++) {
		uint8_t *e = rdp(arr + (size_t)i * 8);
		if (!e || rd32(e + EVENT_OWNER) != me)
			continue;
		mine++;
		uint32_t eid = rd32(e + EVENT_ID);
		LOG("evento pendente #%d: id=%u objeto=%p definicao=%p", i, eid, e, rdp(e + 0x1b0));
		if (n > 1 && !strcmp(a[1], "dump"))
			hexdump("  ev", e, 0x1D0);
		if (n > 1 && !strcmp(a[1], "answer")) {
			// replies with the requested option (default 0, the first)
			uint32_t f[2] = { eid, n > 2 ? (uint32_t)strtoul(a[2], NULL, 0) : 0 };
			uint8_t r = send_cmd(RVA_VT_EVENT_OPT, RVA_VT_EVENT_OPT2, f, 2, 0xE);
			LOG("  respondido: evento=%u opcao=%u retorno=%u", f[0], f[1], r);
		}
	}
	int succ = succession(n > 1 && !strcmp(a[1], "answer"));
	LOG("events: %d no total, %d do jogador, %d tela(s) de sucessao", cnt, mine, succ);
}

static void cmd_answer(char **a, int n)
{
	// answer <event id> <option> : picks an event option (CSelectEventOptionCommand)
	if (n < 3) {
		LOG("uso: answer <id do evento> <indice da opcao>");
		return;
	}
	uint32_t f[2] = { (uint32_t)strtoul(a[1], NULL, 0), (uint32_t)strtoul(a[2], NULL, 0) };
	uint8_t r = send_cmd(RVA_VT_EVENT_OPT, RVA_VT_EVENT_OPT2, f, 2, 0xE);
	LOG("answer evento=%u opcao=%u: retorno=%u", f[0], f[1], r);
}

static void cmd_info(void)
{
	LOG("info: base=%p jogador(personagem)=%u unidades=%u exercitos=%u personagens=%u ultima unidade comandada=%ld "
	    "agora=%lld",
	    B, player_char(), table_count(RVA_UNIT_TABLE), table_count(RVA_ARMY_TABLE), table_count(RVA_CHAR_TABLE),
	    last_player_unit, (long long)rd64(B + RVA_NOW));
}

// ---- game console ----
#define RVA_CONSOLE         0x570f7b0   // global console pointer (CConsole)
#define RVA_FN_CONSOLE_EXEC 0x34b1970   // (text*): the function behind the interface's ExecuteConsoleCommand

// Script effect without the console (the game without graphics, -nographics, does not create the CConsole; docs/dll/plano_sem_graficos.md):
// 0xe3e8b0 is the `effect` console command (the same one the `run` command calls with the text of a file):
// (result*, reader*), result = { u8 ok; std::string message at +8 }. Runs on the player's character, immediately.
#define RVA_FN_EFFECT_CMD   0xe3e8b0

// The command writes the result to the console (0x34b47d0, console in rcx): without a console, rcx is null and it crashes. Permanent
// detour (the page survives a core reload; prologue already replaced = already installed): rcx null -> returns 0.
#define RVA_FN_CONSOLE_PRINT 0x34b47d0
static const uint8_t CONPRINT_PROLOGUE[15] = {
	0x48, 0x89, 0x5C, 0x24, 0x08,   // mov [rsp+8], rbx
	0x48, 0x89, 0x6C, 0x24, 0x10,   // mov [rsp+0x10], rbp
	0x48, 0x89, 0x74, 0x24, 0x18    // mov [rsp+0x18], rsi
};

static int conprint_guard(void)
{
	uint8_t *target = B + RVA_FN_CONSOLE_PRINT;
	if (memcmp(target, CONPRINT_PROLOGUE, sizeof CONPRINT_PROLOGUE) != 0)
		return target[0] == 0xFF && target[1] == 0x25;   // already detoured
	uint8_t *pg = VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
	if (!pg)
		return 0;
	static const uint8_t head[8] = { 0x48, 0x85, 0xC9,   // test rcx, rcx
	                                 0x75, 0x03,         // jnz +3
	                                 0x31, 0xC0,         // xor eax, eax
	                                 0xC3 };             // ret
	memcpy(pg, head, 8);
	memcpy(pg + 8, CONPRINT_PROLOGUE, 15);
	pg[23] = 0xFF; pg[24] = 0x25; memset(pg + 25, 0, 4);
	*(uint8_t **)(pg + 29) = target + 15;
	uint8_t patch[15];
	patch[0] = 0xFF; patch[1] = 0x25; memset(patch + 2, 0, 4);
	*(uint8_t **)(patch + 6) = pg;
	patch[14] = 0x90;
	DWORD old;
	if (!VirtualProtect(target, sizeof patch, PAGE_EXECUTE_READWRITE, &old))
		return 0;
	memcpy(target, patch, sizeof patch);
	VirtualProtect(target, sizeof patch, old, &old);
	FlushInstructionCache(GetCurrentProcess(), target, sizeof patch);
	LOG("console: escrita no console desviada (sem console, exe+0x%X)", RVA_FN_CONSOLE_PRINT);
	return 1;
}

static void effect_direct(const char *text)
{
	if (!rdp(B + RVA_CONSOLE) && !conprint_guard()) {
		LOG("console: efeito recusado, desvio da escrita do console nao instalado: %s", text);
		return;
	}
	uint8_t *gs = rdp(B + RVA_GAME_STATE);
	// during loading the speed is -1; an effect at that moment crashes the game (C0000005 at exe+0xe3ea3e, inside
	// this function: the player does not exist yet)
	if (!gs || rd32(gs + 0x70) > 4) {
		LOG("console: efeito recusado, partida nao carregada: %s", text);
		return;
	}
	size_t len = strlen(text);
	_Alignas(16) uint8_t buf[0x20] = {0}, mem[0x60] = {0}, lexer[0x100] = {0}, reader[0x400] = {0}, res[0x40] = {0};
	*(void **)(buf + 0x10) = B + RVA_ALLOC_CHAR;
	((void (*)(void *, const char *, int))(B + RVA_FN_BUF_COPY))(buf, text, (int)len);
	*(void **)mem = B + RVA_VT_MEMORY_FILE;
	*(int32_t *)(mem + 0x08) = 1;
	*(int64_t *)(mem + 0x10) = 1;
	*(int64_t *)(mem + 0x30) = 0xf;
	*(void **)(mem + 0x38) = buf;
	*(int32_t *)(mem + 0x50) = -1;
	((void (*)(void *, void *, int))(B + RVA_FN_LEXER_STREAM))(lexer, mem, 0);
	*(void **)lexer = B + RVA_VT_TEXT_LEXER;
	((void (*)(void *, void *))(B + RVA_FN_READER))(reader, lexer);
	LOG("console: enviado: effect %s (direto, sem console)", text);   // before: the panel joins what comes after
	((void (*)(void *, void *))(B + RVA_FN_EFFECT_CMD))(res, reader);
	((void (*)(void *))(B + RVA_FN_READER_DTOR))(reader);
	((void (*)(void *))(B + RVA_FN_LEXER_DTOR))(lexer);
	uint64_t mlen = *(uint64_t *)(res + 0x18), cap = *(uint64_t *)(res + 0x20);
	const char *msg = cap >= 0x10 ? *(const char **)(res + 8) : (const char *)(res + 8);
	LOG("console: direto: %s%s%.*s", res[0] ? "ok" : "FALHOU", mlen ? ": " : "", (int)(mlen < 200 ? mlen : 200), msg);
}

// console <text> : puts the text in the game's console queue, as if typed (e.g. console event x.1). Without a
// console (-nographics), `effect <text>` goes through the direct path
static void cmd_console(const char *text)
{
	while (*text == ' ' || *text == '	')
		text++;
	if (*text && !rdp(B + RVA_CONSOLE) && !strncmp(text, "effect ", 7)) {
		effect_direct(text + 7);
		return;
	}
	// `event <id>` without a console: trigger_event right away, outside the game loop, fails in the eventmanager (contained, 2026-10-07,
	// exe+0x356b6ca from 0x278d6b0) after the event runs. Running the event's effect directly (`effect ...`)
	if (!*text || !rdp(B + RVA_CONSOLE)) {
		LOG("console: %s", *text ? "console do jogo indisponivel" : "uso: console <comando>");
		return;
	}
	// text in the game's std::string format, read-only (the function copies what it needs)
	struct { const char *ptr; uint64_t pad; uint64_t size, cap; } s = { text, 0, strlen(text), strlen(text) | 0x10 };
	((void (*)(void *))(B + RVA_FN_CONSOLE_EXEC))(&s);
	LOG("console: enviado: %s", text);
}

static void cmd_cfg(char **a, int n);
static void cmd_record(char **a, int n);
static void cmd_map(char **a, int n);
static void cmd_table(char **a, int n);
static void cmd_expr(const char *texto);
static void cmd_ctxprova(const char *s);
static int plg_pedido(char **a, int n);
static void cmd_var(char **a, int n);
static void cmd_mods_sel(int k);

static void on_command(const char *line)
{
	prov_cache();
	if (!strncmp(line, "expr ", 5)) {
		cmd_expr(line + 5);
		return;
	}
	if (!strncmp(line, "ctxprova ", 9)) {
		cmd_ctxprova(line + 9);
		return;
	}
	if (!strncmp(line, "console ", 8)) {
		cmd_console(line + 8);
		return;
	}
	if (!strncmp(line, "efeito ", 7)) {   // direct script effect, with or without a console
		effect_direct(line + 7);
		return;
	}
	if (!strncmp(line, "carregar ", 9)) {
		cmd_load(line + 9);
		return;
	}
	char buf[512];
	char *a[40];
	int n = 0;
	strncpy(buf, line, sizeof buf - 1);
	buf[sizeof buf - 1] = 0;
	for (char *t = strtok(buf, " \t"); t && n < 40; t = strtok(NULL, " \t"))
		a[n++] = t;
	if (!n)
		return;
	if (!strcmp(a[0], "info")) cmd_info();
	else if (!strcmp(a[0], "where")) cmd_where(a, n);
	else if (!strcmp(a[0], "eta")) cmd_eta(a, n);
	else if (!strcmp(a[0], "move")) cmd_move(a, n, 1);
	else if (!strcmp(a[0], "route")) cmd_route(a, n);
	else if (!strcmp(a[0], "units")) cmd_units(a, n);
	else if (!strcmp(a[0], "threats")) cmd_threats(a, n);
	else if (!strcmp(a[0], "peek")) cmd_peek(a, n);
	else if (!strcmp(a[0], "procura")) cmd_find(a, n);
	else if (!strcmp(a[0], "busca32")) cmd_find32(a, n);
	else if (!strcmp(a[0], "layout")) cmd_layout(a, n);
	else if (!strcmp(a[0], "layout_tirar")) cmd_layout_remove(a, n);
	else if (!strcmp(a[0], "opcao")) cmd_option(a, n);
	else if (!strcmp(a[0], "cfg")) cmd_cfg(a, n);
	else if (!strcmp(a[0], "gravar")) cmd_record(a, n);
	else if (!strcmp(a[0], "var")) cmd_var(a, n);
	else if (!strcmp(a[0], "mapa")) cmd_map(a, n);
	else if (!strcmp(a[0], "tabela")) cmd_table(a, n);
	else if (!strcmp(a[0], "mods")) cmd_mods_sel(-1);
	else if (!strcmp(a[0], "mods_sel") && n > 1) cmd_mods_sel(atoi(a[1]));
	else if (!strcmp(a[0], "velocidade")) cmd_speed(n > 1 ? atoi(a[1]) : 0);
	else if (!strcmp(a[0], "pan")) cmd_pan(a, n);
	else if (!strcmp(a[0], "raid") || !strcmp(a[0], "split") || !strcmp(a[0], "disband") || !strcmp(a[0], "pause"))
		cmd_army_action(a, n);
	else if (!strcmp(a[0], "merge")) cmd_merge(a, n);
	else if (!strcmp(a[0], "halt")) cmd_halt(a, n);
	else if (!strcmp(a[0], "stance")) cmd_stance(a, n);
	else if (!strcmp(a[0], "raise")) cmd_raise(a, n);
	else if (!strcmp(a[0], "predict")) cmd_predict(a, n);
	else if (!strcmp(a[0], "combate")) cmd_combate(a, n);
	else if (!strcmp(a[0], "hipo")) cmd_hipo(a, n);
	else if (!strcmp(a[0], "modval")) cmd_modval(a, n);
	else if (!strcmp(a[0], "loot")) cmd_loot(a, n);
	else if (!strcmp(a[0], "state")) cmd_state();
	else if (!strcmp(a[0], "status")) cmd_status(n > 1 ? a[1] : "?");
	else if (!strcmp(a[0], "events")) cmd_events(a, n);
	else if (!strcmp(a[0], "answer")) cmd_answer(a, n);
	else if (!strcmp(a[0], "salvar")) cmd_save(a, n);
	else if (!strcmp(a[0], "mudo")) {
		mute_chamar = n > 1 && !strcmp(a[1], "on");
		LOG("mudo %s", mute_chamar ? "ligado: os efeitos dos plugins nao respondem" : "desligado");
	}
	else if (!strcmp(a[0], "cmdraw") && n > 3) {
		// cmdraw <vtable RVA> <vtable2 RVA> <flags> [field32 ...] : simple command with 32-bit fields starting at +0x20
		uint32_t f[16];
		int nf = 0;
		for (int k = 4; k < n && nf < 16; k++)
			f[nf++] = (uint32_t)strtoul(a[k], NULL, 0);
		uint8_t r = send_cmd((uint32_t)strtoul(a[1], NULL, 0), (uint32_t)strtoul(a[2], NULL, 0), f, nf,
		                     (int)strtoul(a[3], NULL, 0));
		LOG("cmdraw %s: retorno=%u", a[1], r);
	}
	else if (!strcmp(a[0], "rally")) {
		// list of rally point owners (game state +0x1d548) and the points of each one (+0x1d8)
		uint8_t *gs = rdp(rdp(B + RVA_GAME_STATE) + 0xa0);
		uint8_t *arr = gs ? rdp(gs + 0x1d548) : NULL;
		int cnt = gs ? (int)rd32(gs + 0x1d554) : 0;
		LOG("rally: %d donos", cnt);
		for (int i = 0; i < cnt && i < 64; i++) {
			uint8_t *o = rdp(arr + (size_t)i * 8);
			uint8_t *rp = o ? rdp(o + 0x1d8) : NULL;
			int rn = o ? (int)rd32(o + 0x1d8 + 0xc) : 0;
			char cls[128];
			char buf[512];
			int len = snprintf(buf, sizeof buf, "  #%d %s id=%u pontos=%d:", i, o ? class_name(o, cls, sizeof cls) : "?",
			                   o ? rd32(o + 0xb0) : 0, rn);
			for (int k = 0; k < rn && k < 20 && len < 480; k++) {
				uint8_t *pt = rdp(rp + (size_t)k * 8);
				len += snprintf(buf + len, sizeof buf - len, " %u", prov_id(rdp(pt + 8)));
			}
			LOG("%s", buf);
		}
	}
	else if (!strcmp(a[0], "etafrom") && n > 3) {
		uint8_t *u = unit_by_id(parse_unit(a[1]));
		uint32_t s = (uint32_t)strtoul(a[2], NULL, 0), t = (uint32_t)strtoul(a[3], NULL, 0);
		LOG("etafrom: unidade %s de %u ate %u: %d dias", a[1], s, t, u ? eta_from(u, province_by_id(s), province_by_id(t)) : -2);
	}
	else if (!strcmp(a[0], "cmdlog")) {
		if (!cmdlog_lock_ok) {
			InitializeCriticalSection(&cmdlog_lock);
			cmdlog_lock_ok = 1;
		}
		cmdlog = n > 1 && !strcmp(a[1], "on") ? 1 : n > 1 && !strcmp(a[1], "all") ? 2 : 0;
		LOG("cmdlog %s", cmdlog == 2 ? "ligado (todas as threads, comandos de exercito)" : cmdlog ? "ligado" : "desligado");
	} else if (n >= 2 && (!strcmp(a[0], "last") || (a[0][0] >= '0' && a[0][0] <= '9')))
		cmd_move(a, n, 0);   // old format: <unit|last> <province> [queue]
	else
		if (!plg_pedido(a, n))
			LOG("pedido desconhecido: %s", a[0]);
}

// ---- script effects (registered by the loader; docs/dll/re/notas_efeitos.md) ----
#define SCOPE_ARMY 0x1b
#define ARMY_UNIT  0x124

// script variables (the same functions as the set_variable effect)
#define RVA_FN_SCOPE_VARS   0x3329990   // (scope*) -> variables of the scope's object, or NULL
#define RVA_FN_VAR_POOL     0x3b970f0   // () -> variable name registry
#define RVA_FN_VAR_NAME     0x3b96da0   // (registry, &number, &text)
#define RVA_FN_VAR_SET      0x3346b30   // (variables+8, name number, &16-byte value, days; <= 0 = no expiry)
#define RVA_FN_VARS_CHANGED 0x33465f0   // (variables)

// reads a numeric script variable of the object (scope type + id). Structure seen in the write function:
// list at variables+8 (base at +8, count at +0x14), 0x20-byte entries: name number at +8,
// value type at +0x10 (1 = number), fixed-point value at +0x18.
// reads the scope's variable; *type_out = value type (1 = number; others: yes/no, scope...). Returns whether it exists
static int get_var_any(uint16_t scope_type, uint32_t scope_id, const char *name, int64_t *out, uint16_t *type_out);

static int get_var_any(uint16_t scope_type, uint32_t scope_id, const char *name, int64_t *out, uint16_t *type_out)
{
	struct { uint16_t type, zero; uint32_t pad; uint32_t id, pad2; } scope = { scope_type, 0, 0, scope_id, 0 };
	uint8_t *vars = ((uint8_t *(*)(void *))(B + RVA_FN_SCOPE_VARS))(&scope);
	if (!vars)
		return 0;
	struct { const char *p; uint32_t len; uint8_t flag; } __attribute__((aligned(16))) text =
		{ name, (uint32_t)strlen(name), 0 };
	int32_t id = -1;
	void *pool = ((void *(*)(void))(B + RVA_FN_VAR_POOL))();
	((void (*)(void *, int32_t *, void *))(B + RVA_FN_VAR_NAME))(pool, &id, &text);
	uint8_t *base = rdp(vars + 8 + 8);
	int n = (int)rd32(vars + 8 + 0x14);
	for (int i = 0; base && i < n && i < 4096; i++) {
		uint8_t *e = base + (size_t)i * 0x20;
		if ((int32_t)rd32(e + 8) == id) {
			uint16_t type = 0;
			rd(e + 0x10, &type, 2);
			*out = rd64(e + 0x18);
			*type_out = type;
			return 1;
		}
	}
	return 0;
}

#define SCOPE_CHARACTER 0x4

// the game's combat prediction between two groups of armies (army ids): win chance of side A, 0 to 1
static double predict_armies_raw(uint32_t *ours, int n1, uint32_t *theirs, int n2);
static double predict_armies(uint32_t *ours, int n1, uint32_t *theirs, int n2)
{
	double c = predict_armies_raw(ours, n1, theirs, n2);
	if (rec_file) {
		char a[512] = "", b[512] = "";
		size_t ka = 0, kb = 0;
		for (int i = 0; i < n1 && ka < sizeof a - 16; i++)
			ka += (size_t)snprintf(a + ka, sizeof a - ka, "%s%u", i ? "," : "", ours[i]);
		for (int i = 0; i < n2 && kb < sizeof b - 16; i++)
			kb += (size_t)snprintf(b + kb, sizeof b - kb, "%s%u", i ? "," : "", theirs[i]);
		rec("{\"t\":\"prev\",\"nos\":[%s],\"eles\":[%s],\"r\":%.6f}", a, b, c);
	}
	return c;
}

static double predict_armies_raw(uint32_t *ours, int n1, uint32_t *theirs, int n2)
{
	struct { uint32_t *data; int32_t count, cap; uint8_t *alloc; } va = { ours, n1, n1, B + RVA_ALLOC_U32 },
	                                                                vb = { theirs, n2, n2, B + RVA_ALLOC_U32 };
	int64_t ratio = 0, k = rd64(B + RVA_PREDICT_K);
	((void (*)(int64_t *, void *, void *, int64_t, int64_t, int64_t, int))(B + RVA_FN_PREDICT))(&ratio, &va, &vb, k, k,
	                                                                                             0, 0);
	return (double)ratio / FIXED_ONE;
}

#define SCOPE_PROVINCE 0x8

// ---- global configuration (Mods tab; docs/dll/arquitetura_dll.md, section 11.4) ----
// The Mods tab options are registered by the loader in the "mods" group of the settings manager. The core
// reads the value of each one directly from the option object (docs/dll/re_notas.md, sections C1, C2b and C4).
#define RVA_VT_BOOL_SETTING 0x4555700
#define RVA_VT_INT_SETTING  0x4238228
#define RVA_VT_REAL_SETTING 0x428cb28
#define SETTING_NAME        0x78    // std::string
#define SETTING_VALUE       0xd0    // data of the first value field (+0xc8), stored in the field itself

// the game's std::string (MSVC): text inside the object itself up to 15 characters, otherwise a pointer; size at +0x10
static const char *game_str(uint8_t *s, char *buf, size_t n)
{
	uint64_t len = (uint64_t)rd64(s + 0x10), cap = (uint64_t)rd64(s + 0x18);
	uint8_t *p = cap >= 16 ? rdp(s) : s;
	if (len >= n)
		len = n - 1;
	if (!p || !rd(p, buf, len))
		len = 0;
	buf[len] = 0;
	return buf;
}

// calls fn(name, value) for each option of the "mods" group; returns how many
static int mods_settings(void (*fn)(uint8_t *s, const char *name, double v, void *arg), void *arg)
{
	uint8_t *mgr = rdp(B + RVA_SETTINGS_MANAGER), *groups = mgr ? rdp(mgr + 0x18) : NULL;
	int ng = mgr ? (int)rd32(mgr + 0x24) : 0, n = 0;
	char buf[96];
	for (int i = 0; groups && i < ng; i++) {
		uint8_t *g = rdp(groups + (size_t)i * 8);
		if (!g || strcmp(game_str(g, buf, sizeof buf), "mods"))
			continue;
		uint8_t *list = rdp(g + 0x20);
		int count = (int)rd32(g + 0x2c);
		for (int k = 0; list && k < count; k++) {
			uint8_t *s = rdp(list + (size_t)k * 8);
			if (!s)
				continue;
			uint8_t *vt = rdp(s);
			double v;
			if (vt == B + RVA_VT_BOOL_SETTING) {
				uint8_t b = 0;
				rd(s + SETTING_VALUE, &b, 1);
				v = b != 0;
			} else if (vt == B + RVA_VT_INT_SETTING)
				v = (int32_t)rd32(s + SETTING_VALUE);
			else if (vt == B + RVA_VT_REAL_SETTING) {
				float f = 0;
				rd(s + SETTING_VALUE, &f, 4);
				v = f;
			} else
				continue;
			fn(s, game_str(s + SETTING_NAME, buf, sizeof buf), v, arg);
			n++;
		}
	}
	return n;
}

static void cfg_list_one(uint8_t *s, const char *name, double v, void *arg)
{
	LOG("cfg: %s = %g", name, v);
}

// cfg <name> <value>: changes the option through the same path as the interface (the game writes to pdx_settings.txt when it saves
// the settings)
#define RVA_FN_BOOL_SET 0x3b46990   // (option, value, 1)
#define RVA_FN_INT_SET  0xa89250    // (option, &value, 0)
#define RVA_FN_REAL_SET 0xa89430    // (option, &value, 0)
struct cfg_set { const char *name; double v; int done; };

static void cfg_set_one(uint8_t *s, const char *name, double v, void *arg)
{
	struct cfg_set *c = arg;
	if (c->done || strcmp(name, c->name))
		return;
	uint8_t *vt = rdp(s);
	if (vt == B + RVA_VT_BOOL_SETTING)
		((void (*)(void *, uint8_t, int))(B + RVA_FN_BOOL_SET))(s, (uint8_t)(c->v != 0), 0);
	else if (vt == B + RVA_VT_INT_SETTING) {
		int32_t i = (int32_t)llround(c->v);
		((void (*)(void *, void *, int))(B + RVA_FN_INT_SET))(s, &i, 0);
	} else {
		float f = (float)c->v;
		((void (*)(void *, void *, int))(B + RVA_FN_REAL_SET))(s, &f, 0);
	}
	c->done = 1;
}

static void cmd_cfg(char **a, int n)
{
	if (n >= 3) {
		struct cfg_set c = { a[1], atof(a[2]), 0 };
		mods_settings(cfg_set_one, &c);
		if (!c.done)
			LOG("cfg: opcao %s nao encontrada na aba Mods", a[1]);
	}
	LOG("cfg: %d opcao(oes) na aba Mods", mods_settings(cfg_list_one, NULL));
}

// ---- Mods tab with one button per mod (proof C5; docs/dll/arquitetura_dll.md, section 11.6) ----
// The loader builds the Mods tab with all options (section "configuracoes dos mods" of ar_proxy.c). The core replaces
// that tab with a list of buttons, one per mod (CPdxCallbackSetting); the click shows only that mod's options, with
// a "Voltar" (Back) button. A watcher (thread) notices when the loader's tab comes back (window rebuilt, config reloaded)
// and swaps it again. Addresses in docs/dll/re_notas.md, sections C2b and C5.
#define RVA_VT_SETTINGS_WIN   0x44c3348   // CJominiSettingsWindow
#define RVA_VT_CALLBACK_SET   0x4555d48   // CPdxCallbackSetting
#define RVA_FN_CALLBACK_SET   0x3b4bf70   // (obj 0x138, &name, &key, &std::function<void()>, 0)
#define RVA_VT_FUNC_PTR       0x408a450   // std::_Func_impl_no_alloc<void (*)(void), void>
#define RVA_FN_NOP            0x7e6490    // ret
#define RVA_FN_PAGE_CLICK     0x32d9cf0   // SettingsPage.OnClick(page): selects and redraws
#define WIN_SELECTED          0xd0        // selected page
#define PAGE_NAME             0x10        // std::string with the translated name
#define PAGE_WINDOW           0x08
#define MODS_MAX              8

typedef struct { const char *p; int32_t len; uint8_t flag; } __attribute__((aligned(16))) game_text;

static struct {
	char name[32], title[96], path[MAX_PATH + 32];
	FILETIME t;
	int nopt;
	char opt[64][96], cat[64][96];
	uint8_t *button;   // registered CPdxCallbackSetting
} mods[MODS_MAX];
static int nmods, mods_sel = -1;
static uint8_t *mods_page;          // Mods page built by the core
static uint8_t *mods_voltar;        // "Voltar" (Back) button
static volatile LONG mods_run, mods_pending;
static HANDLE mods_thread;

// configuration of the active mods (the same path as the loader: dlc_load.json -> .mod -> plugin/config.txt)
static char *mods_read(const char *path)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	char *b = n >= 0 ? malloc((size_t)n + 1) : NULL;
	size_t got = b ? fread(b, 1, (size_t)n, f) : 0;
	fclose(f);
	if (b)
		b[got] = 0;
	return b;
}

// `key="value"` at the start of a line of `t` (the simple search for `path="` used to find `replace_path="` first):
// returns the value in out, or 0. `de` = where to continue searching (another occurrence), may be NULL
static int desc_valor(const char *t, const char *chave, char *out, size_t cap, const char **de)
{
	size_t kn = strlen(chave);
	for (const char *l = de && *de ? *de : t; l && *l; l = strchr(l, '\n'), l = l ? l + 1 : NULL) {
		while (*l == ' ' || *l == '\t')
			l++;
		if (strncmp(l, chave, kn) || l[kn] != '=' || l[kn + 1] != '"')
			continue;
		const char *a = l + kn + 2, *b = strchr(a, '"');
		if (!b)
			return 0;
		snprintf(out, cap, "%.*s", (int)(b - a), a);
		if (de)
			*de = b;
		return 1;
	}
	return 0;
}

// active mods in dlc_load.json order (enabled_mods): folder (path= of the .mod) and the .mod text
static int mod_each(void (*fn)(const char *dir, const char *desc, void *arg), void *arg)
{
	char docs[MAX_PATH] = "", user[MAX_PATH + 64], p[MAX_PATH * 2], v[MAX_PATH], dir[MAX_PATH * 2];
	DWORD sz = sizeof docs;
	int n = 0;
	if (RegGetValueA(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\User Shell Folders",
	                 "Personal", RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, NULL, docs, &sz) != ERROR_SUCCESS)
		return 0;
	snprintf(user, sizeof user, "%s\\Paradox Interactive\\Crusader Kings III\\", docs);
	snprintf(p, sizeof p, "%sdlc_load.json", user);
	char *js = mods_read(p), *e = js ? strstr(js, "\"enabled_mods\"") : NULL;
	char *q = e ? strchr(e, '[') : NULL, *end = q ? strchr(q, ']') : NULL;
	while (q && end) {
		char *a = strchr(q, '"'), *b = a ? strchr(a + 1, '"') : NULL;
		if (!a || !b || b > end)
			break;
		q = b + 1;
		snprintf(p, sizeof p, "%s%.*s", user, (int)(b - a - 1), a + 1);
		char *md = mods_read(p);
		if (md && desc_valor(md, "path", v, sizeof v, NULL) && v[0]) {
			if (v[1] == ':')
				snprintf(dir, sizeof dir, "%s", v);
			else
				snprintf(dir, sizeof dir, "%s%s", user, v);
			fn(dir, md, arg);
			n++;
		}
		free(md);
	}
	free(js);
	return n;
}

struct mod_dirs_arg { char (*dirs)[MAX_PATH]; int n, max; };
static void mod_dirs_um(const char *dir, const char *desc, void *arg)
{
	struct mod_dirs_arg *m = arg;
	if (m->n < m->max)
		snprintf(m->dirs[m->n++], MAX_PATH, "%s", dir);
}

// folders of the active mods: dlc_load.json (enabled_mods) -> each .mod -> path=
static int mod_dirs(char (*dirs)[MAX_PATH], int max)
{
	struct mod_dirs_arg m = { dirs, 0, max };
	mod_each(mod_dirs_um, &m);
	return m.n;
}

// .txt files of a game folder (`rel`, e.g. "common/governments") as the game sees them with the active mods: the base
// game and each mod in dlc_load.json order; a file with the same name replaces the previous one; the mod's replace_path discards
// the previous ones of that folder. Same order as the game (by file name)
#define GF_MAX 512
static struct { char nome[MAX_PATH]; char cam[MAX_PATH * 2]; } gf[GF_MAX];
static int gf_n;
static const char *gf_rel;

static int gf_mesma_pasta(const char *a, const char *b)
{
	for (; *a && *b; a++, b++) {
		char x = *a == '\\' ? '/' : (char)tolower((unsigned char)*a), y = *b == '\\' ? '/' : (char)tolower((unsigned char)*b);
		if (x != y)
			return 0;
	}
	while (*a == '/' || *a == '\\')
		a++;
	while (*b == '/' || *b == '\\')
		b++;
	return !*a && !*b;
}

static void gf_pasta(const char *dir)
{
	char p[MAX_PATH * 2 + 8];
	snprintf(p, sizeof p, "%s\\%s\\*.txt", dir, gf_rel);
	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA(p, &fd);
	if (h == INVALID_HANDLE_VALUE)
		return;
	do {
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			continue;
		int k = 0;
		while (k < gf_n && _stricmp(gf[k].nome, fd.cFileName))
			k++;
		if (k == gf_n) {
			if (gf_n >= GF_MAX)
				continue;
			gf_n++;
		}
		snprintf(gf[k].nome, sizeof gf[k].nome, "%s", fd.cFileName);
		snprintf(gf[k].cam, sizeof gf[k].cam, "%s\\%s\\%s", dir, gf_rel, fd.cFileName);
	} while (FindNextFileA(h, &fd));
	FindClose(h);
}

static void gf_mod(const char *dir, const char *desc, void *arg)
{
	char v[MAX_PATH];
	const char *de = NULL;
	while (desc_valor(desc, "replace_path", v, sizeof v, &de))
		if (gf_mesma_pasta(v, gf_rel))
			gf_n = 0;
	gf_pasta(dir);
}

static int gf_cmp(const void *a, const void *b) { return _stricmp(((const char *)a), ((const char *)b)); }

static int api_game_files(const char *rel, char *out, int cap)
{
	char base[MAX_PATH * 2];
	gf_n = 0;
	gf_rel = rel;
	snprintf(base, sizeof base, "%s..\\game", H->dir);
	gf_pasta(base);
	mod_each(gf_mod, NULL);
	qsort(gf, (size_t)gf_n, sizeof gf[0], gf_cmp);   // name is the first field
	int k = 0;
	if (cap > 0)
		out[0] = 0;
	for (int i = 0; i < gf_n; i++) {
		int w = snprintf(out + k, (size_t)(cap - k), "%s\n", gf[i].cam);
		if (w < 0 || k + w >= cap)
			return -1;
		k += w;
	}
	return gf_n;
}
static char *api_file_read(const char *path) { return mods_read(path); }
static void api_file_free(char *p) { free(p); }

static int api_file_write(const char *name, const char *text, int len)
{
	if (!name || !*name || strpbrk(name, "/\\:") || strstr(name, "..") || !text || len < 0)
		return 0;
	char inst[32] = "", nome[128], path[MAX_PATH + 160], tmp[MAX_PATH + 170];
	GetEnvironmentVariableA("AR_INSTANCIA", inst, sizeof inst);
	const char *ext = strrchr(name, '.');
	int base = ext ? (int)(ext - name) : (int)strlen(name);
	snprintf(nome, sizeof nome, "%.*s%s%s%s", base, name, inst[0] ? "_" : "", inst, ext ? ext : "");
	snprintf(path, sizeof path, "%sck3_custom\\%s", H->dir, nome);
	snprintf(tmp, sizeof tmp, "%s.tmp", path);
	FILE *f = fopen(tmp, "wb");
	if (!f)
		return 0;
	int ok = fwrite(text, 1, (size_t)len, f) == (size_t)len;
	ok &= fclose(f) == 0;
	return ok && MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING);
}

static void mods_load(void)
{
	static char dirs[16][MAX_PATH];
	char p[MAX_PATH * 2];
	int nd = mod_dirs(dirs, 16);
	nmods = 0;
	for (int d = 0; d < nd && nmods < MODS_MAX; d++) {
		const char *dir = dirs[d];
		snprintf(p, sizeof p, "%s/plugin/config.txt", dir);
		char *t = mods_read(p);
		if (!t)
			continue;
		WIN32_FILE_ATTRIBUTE_DATA fa;
		const char *bn = strrchr(dir, '/'), *bs = strrchr(dir, '\\');
		if (bs > bn)
			bn = bs;
		int mi = nmods;
		uint8_t *button = mods[mi].button;   // registered before: still valid
		memset(&mods[mi], 0, sizeof mods[mi]);
		mods[mi].button = button;
		snprintf(mods[mi].path, sizeof mods[mi].path, "%s", p);
		if (GetFileAttributesExA(p, GetFileExInfoStandard, &fa))
			mods[mi].t = fa.ftLastWriteTime;
		snprintf(mods[mi].name, sizeof mods[mi].name, "%s", bn ? bn + 1 : dir);
		snprintf(mods[mi].title, sizeof mods[mi].title, "%s", mods[mi].name);
		char cat[96] = "";
		for (char *line = strtok(t, "\r\n"); line; line = strtok(NULL, "\r\n")) {
			char *c = strchr(line, '#');
			if (c)
				*c = 0;
			char w0[96] = "", w1[96] = "", w2[96] = "";
			int k = sscanf(line, "%95s %95s %95s", w0, w1, w2);
			if (k >= 2 && !strcmp(w0, "mod"))
				snprintf(mods[mi].name, sizeof mods[mi].name, "%s", w1);
			else if (k >= 2 && !strcmp(w0, "titulo"))
				snprintf(mods[mi].title, sizeof mods[mi].title, "%s", w1);
			else if (k >= 3 && !strcmp(w0, "grupo"))
				snprintf(cat, sizeof cat, "%s", w2);
			else if (k >= 2 && (!strcmp(w0, "liga") || !strcmp(w0, "inteiro") || !strcmp(w0, "real")) &&
			         mods[mi].nopt < 64) {
				int o = mods[mi].nopt++;
				snprintf(mods[mi].opt[o], sizeof mods[mi].opt[o], "%s_%s", mods[mi].name, w1);
				snprintf(mods[mi].cat[o], sizeof mods[mi].cat[o], "%s", cat[0] ? cat : mods[mi].title);
			}
		}
		free(t);
		nmods++;
	}
}

// settings window: the loader keeps the pointer (cfg_win) in its data area; search there for a pointer
// to an object with the CJominiSettingsWindow vtable
static uint8_t *mods_window(void)
{
	HMODULE m = NULL;
	if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
	                        (LPCSTR)H->log, &m))
		return NULL;
	uint8_t *mb = (uint8_t *)m;
	IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(mb + ((IMAGE_DOS_HEADER *)mb)->e_lfanew);
	IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
	for (int i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++) {
		if (!(sec->Characteristics & IMAGE_SCN_MEM_WRITE))
			continue;
		uint8_t **p = (uint8_t **)(mb + sec->VirtualAddress), **e = (uint8_t **)(mb + sec->VirtualAddress + sec->Misc.VirtualSize);
		for (; p + 1 <= e; p++)
			if (*p && rdp(*p) == B + RVA_VT_SETTINGS_WIN)
				return *p;
	}
	return NULL;
}

static int is_mods_page(uint8_t *pg)
{
	char buf[32];
	return pg && (pg == mods_page || !strcmp(game_str(pg + PAGE_NAME, buf, sizeof buf), "Mods"));
}

// button: std::function<void()> with a function pointer (the same type as the game's options, e.g. reset_reactive_advice)
static void mods_click(int k)
{
	char line[32];
	snprintf(line, sizeof line, "mods_sel %d", k);
	H->run_main(line);   // outside the interface click
}
static void mods_cb_back(void) { mods_click(-1); }
#define MODS_CB(n) static void mods_cb##n(void) { mods_click(n); }
MODS_CB(0) MODS_CB(1) MODS_CB(2) MODS_CB(3) MODS_CB(4) MODS_CB(5) MODS_CB(6) MODS_CB(7)
static void (*const mods_cbs[MODS_MAX])(void) = { mods_cb0, mods_cb1, mods_cb2, mods_cb3,
                                                  mods_cb4, mods_cb5, mods_cb6, mods_cb7 };

// the function called by the button is at option+0xf8 (std::function; impl in its own buffer, function pointer at +8)
static void button_set_fn(uint8_t *obj, void (*fn)(void))
{
	uint8_t *impl = rdp(obj + 0xf8 + 0x38);
	if (impl && rdp(impl) == B + RVA_VT_FUNC_PTR)
		*(void **)(impl + 8) = (void *)fn;
}

// finds an option of the "mods" group by name (registered before, e.g. by a previous core)
static uint8_t *mods_find_setting(const char *name)
{
	uint8_t *mgr = rdp(B + RVA_SETTINGS_MANAGER), *groups = mgr ? rdp(mgr + 0x18) : NULL;
	int ng = mgr ? (int)rd32(mgr + 0x24) : 0;
	char buf[96];
	for (int i = 0; groups && i < ng; i++) {
		uint8_t *g = rdp(groups + (size_t)i * 8);
		if (!g || strcmp(game_str(g, buf, sizeof buf), "mods"))
			continue;
		uint8_t *list = rdp(g + 0x20);
		for (int k = 0; list && k < (int)rd32(g + 0x2c); k++) {
			uint8_t *s = rdp(list + (size_t)k * 8);
			if (s && !strcmp(game_str(s + SETTING_NAME, buf, sizeof buf), name))
				return s;
		}
	}
	return NULL;
}

static uint8_t *mods_button(const char *name, const char *key, void (*fn)(void))
{
	uint8_t *s = mods_find_setting(name);
	if (s) {
		button_set_fn(s, fn);   // core reloaded: the old function is gone from memory
		return s;
	}
	void *mgr = rdp(B + RVA_SETTINGS_MANAGER);
	uint8_t *obj = mgr ? ((uint8_t *(*)(size_t))(B + RVA_FN_NEW))(0x138) : NULL;
	if (!obj)
		return NULL;
	_Alignas(16) uint8_t func[0x40] = { 0 };
	*(void **)func = B + RVA_VT_FUNC_PTR;
	*(void **)(func + 8) = (void *)fn;
	*(void **)(func + 0x38) = func;
	game_text nm = { name, (int32_t)strlen(name), 0 }, k = { key, (int32_t)strlen(key), 0 }, g = { "mods", 4, 0 };
	((void (*)(void *, void *, void *, void *, int))(B + RVA_FN_CALLBACK_SET))(obj, &nm, &k, func, 0);
	InterlockedIncrement((LONG *)(obj + 0x10));
	((void (*)(void *, void *, void *, int))(B + RVA_FN_SETTING_REG))(mgr, &g, obj, 0);
	LOG("mods: botao %s registrado", name);
	return obj;
}

static int mods_layout(char *out, size_t cap)
{
	size_t n = (size_t)snprintf(out, cap, "page = {\n\tname = \"SETTINGS_PAGE_MODS\"\n");
	if (mods_sel < 0 || mods_sel >= nmods) {
		n += (size_t)snprintf(out + n, cap - n,
		                      "\tcategory = {\n\t\tcategory = \"mods\"\n\t\tname = \"SETTINGS_PAGE_MODS\"\n\t\torder = {\n");
		for (int i = 0; i < nmods && n < cap; i++)
			n += (size_t)snprintf(out + n, cap - n, "\t\t\t\"mods_botao_%s\"\n", mods[i].name);
		n += (size_t)snprintf(out + n, cap - n, "\t\t}\n\t}\n");
	} else {
		typeof(mods[0]) *m = &mods[mods_sel];
		n += (size_t)snprintf(out + n, cap - n,
		                      "\tcategory = {\n\t\tcategory = \"mods\"\n\t\tname = \"%s\"\n\t\torder = {\n\t\t\t\"mods_voltar\"\n\t\t}\n\t}\n",
		                      m->title);
		char done[64] = { 0 };
		for (int i = 0; i < m->nopt && n < cap; i++) {
			if (done[i])
				continue;
			n += (size_t)snprintf(out + n, cap - n,
			                      "\tcategory = {\n\t\tcategory = \"mods\"\n\t\tname = \"%s\"\n\t\torder = {\n", m->cat[i]);
			for (int j = i; j < m->nopt && n < cap; j++)
				if (!done[j] && !strcmp(m->cat[j], m->cat[i]) && mods_find_setting(m->opt[j])) {
					n += (size_t)snprintf(out + n, cap - n, "\t\t\t\"%s\"\n", m->opt[j]);
					done[j] = 1;
				}
			n += (size_t)snprintf(out + n, cap - n, "\t\t}\n\t}\n");
		}
	}
	if (n < cap)
		n += (size_t)snprintf(out + n, cap - n, "}\n");
	return n < cap ? (int)n : 0;
}

// replaces the window's Mods tab(s) with the one built by the core (main thread)
static void mods_show(int sel, int select)
{
	uint8_t *win = mods_window();
	if (!win) {
		LOG("mods: janela de configuracoes nao encontrada");
		return;
	}
	mods_load();   // on every swap: config.txt may have changed
	int total = 0;
	for (int i = 0; i < nmods; i++)
		total += mods[i].nopt;
	for (int i = 0; i < nmods; i++) {
		char nm[64];
		snprintf(nm, sizeof nm, "mods_botao_%s", mods[i].name);
		mods[i].button = mods_button(nm, mods[i].title, mods_cbs[i]);
	}
	mods_voltar = mods_button("mods_voltar", "mods_voltar", mods_cb_back);
	mods_sel = sel;

	int count = (int)rd32(win + WIN_PAGES + 0xc), was_selected = 0;
	uint8_t **pages = (uint8_t **)rdp(win + WIN_PAGES), *selected = rdp(win + WIN_SELECTED);
	for (int i = 0; i < count;)
		if (is_mods_page(pages[i])) {
			was_selected |= pages[i] == selected;
			memmove(pages + i, pages + i + 1, (size_t)(count - i - 1) * sizeof *pages);
			count--;
		} else
			i++;
	*(int32_t *)(win + WIN_PAGES + 0xc) = count;   // the memory of the removed pages is not freed
	if (!total) {
		// no mod with options (C7): no Mods tab
		if (was_selected && count > 0)
			((uint8_t (*)(void *))(B + RVA_FN_PAGE_CLICK))(pages[0]);
		mods_page = NULL;
		LOG("mods: nenhuma opcao nos config.txt; aba Mods tirada, %d pagina(s)", count);
		return;
	}
	if (mods_sel >= nmods)
		mods_sel = -1;

	static char text[32768];
	int len = mods_layout(text, sizeof text);
	_Alignas(16) uint8_t buf[0x20] = { 0 }, mem[0x60] = { 0 }, lexer[0x100] = { 0 }, reader[0x400] = { 0 };
	*(void **)(buf + 0x10) = B + RVA_ALLOC_CHAR;
	((void (*)(void *, const char *, int))(B + RVA_FN_BUF_COPY))(buf, text, len);
	*(void **)mem = B + RVA_VT_MEMORY_FILE;
	*(int32_t *)(mem + 0x08) = 1;
	*(int64_t *)(mem + 0x10) = 1;
	*(int64_t *)(mem + 0x30) = 0xf;
	*(void **)(mem + 0x38) = buf;
	*(int32_t *)(mem + 0x50) = -1;
	((void (*)(void *, void *, int))(B + RVA_FN_LEXER_STREAM))(lexer, mem, 0);
	*(void **)lexer = B + RVA_VT_TEXT_LEXER;
	((void (*)(void *, void *))(B + RVA_FN_READER))(reader, lexer);
	uint8_t *vis = win + WIN_LAYOUT;
	((void (*)(void *, void *))((*(void ***)vis)[3]))(vis, reader);
	((void (*)(void *))(B + RVA_FN_READER_DTOR))(reader);
	((void (*)(void *))(B + RVA_FN_LEXER_DTOR))(lexer);

	int after = (int)rd32(win + WIN_PAGES + 0xc);
	pages = (uint8_t **)rdp(win + WIN_PAGES);
	mods_page = after > count ? pages[after - 1] : NULL;
	for (int i = count; i < after; i++) {
		uint8_t **cats = (uint8_t **)rdp(pages[i] + 0x30);
		int nc = (int)rd32(pages[i] + 0x3c);
		for (int k = 0; k < nc; k++)
			((void (*)(void *))(B + RVA_FN_BIND_CAT))(cats[k]);
	}
	if (mods_page && (was_selected || select))
		((uint8_t (*)(void *))(B + RVA_FN_PAGE_CLICK))(mods_page);
	LOG("mods: aba Mods trocada (%s), %d pagina(s)", sel < 0 ? "lista de mods" : mods[sel].name, after);
}

// watcher: the loader's tab comes back when the window is rebuilt or the configuration reloaded
static DWORD WINAPI mods_watch(void *unused)
{
	while (mods_run) {
		Sleep(300);
		uint8_t *win = mods_window();
		if (!win || mods_pending)
			continue;
		int count = (int)rd32(win + WIN_PAGES + 0xc), foreign = 0;
		for (int i = 0; i < nmods; i++) {
			WIN32_FILE_ATTRIBUTE_DATA fa;
			if (GetFileAttributesExA(mods[i].path, GetFileExInfoStandard, &fa) &&
			    CompareFileTime(&fa.ftLastWriteTime, &mods[i].t) != 0)
				foreign = 1;   // config.txt changed: rebuild (the loader's tab also comes, right after)
		}
		uint8_t **pages = (uint8_t **)rdp(win + WIN_PAGES);
		for (int i = 0; pages && i < count && i < 64; i++) {
			uint8_t *pg = rdp(pages + i);
			if (pg && pg != mods_page && is_mods_page(pg))
				foreign = 1;
		}
		if (foreign) {
			InterlockedExchange(&mods_pending, 1);
			H->run_main("mods_sel -2");   // -2 = keep the current selection
		}
	}
	return 0;
}

static void cmd_mods_sel(int k)
{
	InterlockedExchange(&mods_pending, 0);
	if (k == -2)
		mods_show(mods_sel, 0);
	else
		mods_show(k, 1);
}

static void mods_start(void)
{
	mods_run = 1;
	mods_thread = CreateThread(NULL, 0, mods_watch, NULL, 0, NULL);
}

static void mods_stop(void)
{
	mods_run = 0;
	if (mods_thread) {
		WaitForSingleObject(mods_thread, 2000);
		CloseHandle(mods_thread);
	}
	// the button functions leave memory with the core: they point to a game ret until the next core
	for (int i = 0; i < nmods; i++)
		if (mods[i].button)
			button_set_fn(mods[i].button, (void (*)(void))(B + RVA_FN_NOP));
	if (mods_voltar)
		button_set_fn(mods_voltar, (void (*)(void))(B + RVA_FN_NOP));
}


static void cmd_record(char **a, int n)
{
	if (!InterlockedExchange(&rec_lock_ok, 1))
		InitializeCriticalSection(&rec_lock);
	if (n >= 3 && !strcmp(a[1], "on")) {
		char dir[MAX_PATH + 32], path[MAX_PATH + 96];
		snprintf(dir, sizeof dir, "%sck3_custom", H->dir);
		CreateDirectoryA(dir, NULL);
		snprintf(dir, sizeof dir, "%sck3_custom\\gravacao", H->dir);
		CreateDirectoryA(dir, NULL);
		snprintf(path, sizeof path, "%s\\%s.jsonl", dir, a[2]);
		FILE *f = fopen(path, "ab");
		EnterCriticalSection(&rec_lock);
		if (rec_file)
			fclose(rec_file);
		rec_file = f;
		rec_lines = 0;
		LeaveCriticalSection(&rec_lock);
		rec("{\"t\":\"inicio\",\"jogador\":%u}", player_char());
		LOG("gravar: %s em %s", f ? "gravando" : "NAO abriu", path);
	} else if (n >= 2 && !strcmp(a[1], "off")) {
		EnterCriticalSection(&rec_lock);
		if (rec_file)
			fclose(rec_file);
		rec_file = NULL;
		LeaveCriticalSection(&rec_lock);
		LOG("gravar: parado (%ld linhas)", rec_lines);
	} else
		LOG("gravar: %s, %ld linhas", rec_file ? "gravando" : "parado", rec_lines);
	if (rec_file)
		fflush(rec_file);
}

// mapa <unit|last> <radius px> <name>: graph of the provinces around the unit, via the game's own pathfinder
// (auto_raid docs/simulador.md, SIM1b), in <binaries>\ck3\mapa\<nome>.jsonl. Nodes ("no"): position, sea, the unit's travel time there,
// available loot, total loot required and progress per day of its army. Edges ("aresta"): consecutive pairs
// of the paths found, with the exact time from one end to the other (fixed point, 100000 = 1 day). Runs on the main thread
// and freezes the game for a few seconds: use with the game paused.
static uint32_t map_edge[32768][2];
static int map_ne;

static void map_add_path(uint32_t from, const uint32_t *pp, int pn)
{
	uint32_t prev = from;
	for (int k = 0; k < pn; k++) {
		uint32_t a2 = prev, b2 = pp[k];
		prev = pp[k];
		if (a2 == b2)
			continue;
		int seen = 0;
		for (int e = 0; e < map_ne && !seen; e++)
			seen = map_edge[e][0] == a2 && map_edge[e][1] == b2;
		if (!seen && map_ne < 32768) {
			map_edge[map_ne][0] = a2;
			map_edge[map_ne][1] = b2;
			map_ne++;
		}
	}
}

static void cmd_map(char **a, int n)
{
	if (n < 4) {
		LOG("uso: mapa <unidade|last> <raio px> <nome> [vizinhanca px: cada provincia vira origem]");
		return;
	}
	uint8_t *u = unit_by_id(parse_unit(a[1])), *army = NULL;
	double free_cap = 0;
	int radius = atoi(a[2]);
	if (!u || !unit_army_cap(u, &army, &free_cap)) {
		LOG("mapa: unidade %s nao encontrada", a[1]);
		return;
	}
	char path[MAX_PATH + 96];
	snprintf(path, sizeof path, "%sck3_custom", H->dir);
	CreateDirectoryA(path, NULL);
	snprintf(path, sizeof path, "%sck3_custom\\mapa", H->dir);
	CreateDirectoryA(path, NULL);
	snprintf(path, sizeof path, "%sck3_custom\\mapa\\%s.jsonl", H->dir, a[3]);
	FILE *f = fopen(path, "wb");
	if (!f) {
		LOG("mapa: nao abriu %s", path);
		return;
	}
	LARGE_INTEGER t0, t1, fq;
	QueryPerformanceFrequency(&fq);
	QueryPerformanceCounter(&t0);
	uint8_t *start = rdp(u + UNIT_LOCATION), *here = rdp(start + 8);
	int hx = (int)rd32(here + 0x10), hy = (int)rd32(here + 0x14);
	fprintf(f, "{\"t\":\"inicio\",\"u\":%u,\"ex\":%u,\"de\":%u,\"raio\":%d}\n", rd32(u + UNIT_ID), rd32(army + 0x10),
	        prov_id(start), radius);
	uint8_t *gs = rdp(rdp(B + RVA_GAME_STATE) + 0xa0), *arr = gs ? rdp(gs + 0x140) : NULL;
	uint32_t count = gs ? rd32(gs + 0x14c) : 0;
	static uint32_t nid[4096];
	static int nxy[4096][2];
	int nodes = 0, neigh = n > 4 ? atoi(a[4]) : 0;
	map_ne = 0;
	for (uint32_t i = 1; arr && i < count; i++) {
		uint8_t *p = rdp(arr + (size_t)i * 8), *def = p ? rdp(p + 8) : NULL;
		if (!def)
			continue;
		int x = (int)rd32(def + 0x10), y = (int)rd32(def + 0x14), dx = x - hx, dy = y - hy;
		if ((int64_t)dx * dx + (int64_t)dy * dy > (int64_t)radius * radius)
			continue;
		uint32_t pp[256];
		int pn = 0;
		int64_t raw = 0, loot = 0, tot = 0, per = 0;
		int d = p == start ? 0 : eta_days_raw(u, p, &raw, pp, &pn, 256, NULL);
		int sea = prov_is_sea(p), can = 0;
		if (!sea) {
			can = ((uint8_t (*)(void *, void *, void *))(B + RVA_FN_CAN_RAID))(army, p, NULL) != 0;
			((void (*)(void *, void *, void *, void *))(B + RVA_FN_PROV_LOOT))(&loot, p, NULL, NULL);
			raid_days_raw(army, p, 0, &tot, &per);
		}
		fprintf(f, "{\"t\":\"no\",\"id\":%u,\"x\":%d,\"y\":%d,\"mar\":%d,\"d\":%d,\"tempo\":%lld,\"pode\":%d,"
		        "\"saque\":%lld,\"total\":%lld,\"dia\":%lld}\n", i, x, y, sea, d, (long long)raw, can, (long long)loot,
		        (long long)tot, (long long)per);
		if (nodes < 4096) {
			nid[nodes] = i;
			nxy[nodes][0] = x;
			nxy[nodes][1] = y;
		}
		nodes++;
		map_add_path(prov_id(start), pp, pn < 256 ? pn : 256);
	}
	// multiple origins: path from each province to its neighbors within `neigh` pixels (brings in the lateral links)
	int pairs = 0;
	for (int i = 0; neigh > 0 && i < nodes && i < 4096; i++)
		for (int j = 0; j < nodes && j < 4096; j++) {
			int dx = nxy[j][0] - nxy[i][0], dy = nxy[j][1] - nxy[i][1];
			if (i == j || (int64_t)dx * dx + (int64_t)dy * dy > (int64_t)neigh * neigh)
				continue;
			uint32_t pp[256];
			int pn = path_from(u, province_by_id(nid[i]), province_by_id(nid[j]), pp, 256);
			map_add_path(nid[i], pp, pn);
			pairs++;
		}
	for (int e = 0; e < map_ne; e++) {
		int64_t raw = 0;
		int d = eta_from_raw(u, province_by_id(map_edge[e][0]), province_by_id(map_edge[e][1]), &raw);
		fprintf(f, "{\"t\":\"aresta\",\"de\":%u,\"para\":%u,\"d\":%d,\"tempo\":%lld}\n", map_edge[e][0], map_edge[e][1], d,
		        (long long)raw);
	}
	if (neigh > 0)
		LOG("mapa: %d pares de origem/destino a ate %d pixels", pairs, neigh);
	fclose(f);
	QueryPerformanceCounter(&t1);
	LOG("mapa: %d provincias, %d arestas em %s (%.1f s)", nodes, map_ne, path,
	    (double)(t1.QuadPart - t0.QuadPart) / (double)fq.QuadPart);
}

// ---- ETA table (auto_raid docs/ia_reativa.md, extraction bridge): the game's exact time (pathfinder + travel time,
// with the unit's speed at the place where it is) from each province in the radius to the provinces within `raio_dest` px
// of it. In blocks: `tabela <unit> <radius> <name> <from> <to> [raio_dest]` computes the origins with index [from, to) (in
// the order of the ids in the radius) and appends to the file; with from = 0 it restarts the file. Binary: per origin, uint32 id,
// uint32 m, m x (uint32 destination, int32 thousandths of a day; -1 without a path). The game stays frozen while the block runs:
// send few origins per request. Run with the unit inside and outside the realm (the friendly territory bonus depends on it).
static void cmd_table(char **a, int n)
{
	if (n < 6) {
		LOG("uso: tabela <unidade|last> <raio px> <nome> <de> <ate> [raio_dest px, padrao 400]");
		return;
	}
	uint8_t *u = unit_by_id(parse_unit(a[1]));
	int radius = atoi(a[2]), i0 = atoi(a[4]), i1 = atoi(a[5]), rd = n > 6 ? atoi(a[6]) : 400;
	if (!u) {
		LOG("tabela: unidade %s nao encontrada", a[1]);
		return;
	}
	char path[MAX_PATH + 96];
	snprintf(path, sizeof path, "%sck3_custom\\mapa", H->dir);
	CreateDirectoryA(path, NULL);
	snprintf(path, sizeof path, "%sck3_custom\\mapa\\%s.bin", H->dir, a[3]);
	FILE *f = fopen(path, i0 == 0 ? "wb" : "ab");
	if (!f) {
		LOG("tabela: nao abriu %s", path);
		return;
	}
	LARGE_INTEGER t0, t1, fq;
	QueryPerformanceFrequency(&fq);
	QueryPerformanceCounter(&t0);
	uint8_t *here = rdp(rdp(u + UNIT_LOCATION) + 8);
	int hx = (int)rd32(here + 0x10), hy = (int)rd32(here + 0x14);
	uint8_t *gs = rdp(rdp(B + RVA_GAME_STATE) + 0xa0), *arr = gs ? rdp(gs + 0x140) : NULL;
	uint32_t count = gs ? rd32(gs + 0x14c) : 0;
	static uint32_t ids[8192];
	static uint8_t *ps[8192];
	static int xy[8192][2];
	static uint32_t row[2 * 8192];
	uint32_t nn = 0;
	for (uint32_t i = 1; arr && i < count && nn < 8192; i++) {
		uint8_t *p = rdp(arr + (size_t)i * 8), *def = p ? rdp(p + 8) : NULL;
		if (!def)
			continue;
		int x = (int)rd32(def + 0x10), y = (int)rd32(def + 0x14), dx = x - hx, dy = y - hy;
		if ((int64_t)dx * dx + (int64_t)dy * dy > (int64_t)(radius + rd) * (radius + rd))
			continue;
		ids[nn] = i;
		ps[nn] = p;
		xy[nn][0] = x;
		xy[nn][1] = y;
		nn++;
	}
	int origens = 0, pares = 0;
	for (uint32_t i = 0, k = 0; i < nn; i++) {
		int dx = xy[i][0] - hx, dy = xy[i][1] - hy;
		if ((int64_t)dx * dx + (int64_t)dy * dy > (int64_t)radius * radius)
			continue;   // origin outside the radius (the destinations go up to radius + raio_dest)
		if ((int)k++ < i0 || (int)k > i1)
			continue;
		uint32_t m = 0;
		for (uint32_t j = 0; j < nn; j++) {
			int ex = xy[j][0] - xy[i][0], ey = xy[j][1] - xy[i][1];
			if (i == j || (int64_t)ex * ex + (int64_t)ey * ey > (int64_t)rd * rd)
				continue;
			int64_t raw = 0;
			int d = eta_from_raw(u, ps[i], ps[j], &raw);
			row[2 * m] = ids[j];
			row[2 * m + 1] = (uint32_t)(d < 0 ? -1 : (int32_t)(raw / (FIXED_ONE / 1000)));
			m++;
		}
		fwrite(&ids[i], 4, 1, f);
		fwrite(&m, 4, 1, f);
		fwrite(row, 8, m, f);
		origens++;
		pares += (int)m;
	}
	fclose(f);
	QueryPerformanceCounter(&t1);
	LOG("tabela: %s origens [%d, %d): %d origens, %d pares, %.2f s", a[3], i0, i1, origens, pares,
	    (double)(t1.QuadPart - t0.QuadPart) / (double)fq.QuadPart);
}

// ---- script expressions evaluated by the game itself (A3; docs/dll/re_notas.md, section A3) ----
// `expr <text>`: the game reads the text as the value of an effect (an add_loot created by the game's factory, army
// scope) and stores it; on each auto_raid_ping_livre on an army, the core evaluates the expression with the script context
// of that moment and logs it next to the value the script passed. Proof: C can ask the game for any
// script trigger or value (army_supply, is_at_war, ar_em_casa...) without finding fields in memory.
#define RVA_FN_ADD_LOOT_NEW 0x2e81070   // add_loot factory: (entry; name number at +0x10) -> 0x168 object
#define RVA_FN_VALUE        0x9698b0    // (object+0x60, &out, context, [context+0x28], object+0x10)
#define SCOPE_ARMY_TYPE     0x1b
#define RVA_FN_READ_STMT    0x3b90120   // reader: reads `key =` and stops at the value (the reader loop calls it before each value)

static uint8_t *expr_obj;
static char expr_texto[256];

static void cmd_expr(const char *texto)
{
	static struct { void *vt; const char *name; int32_t id, pad; } entrada = { NULL, "ck3_expr", 0, 0 };
	uint8_t *obj = ((uint8_t *(*)(void *))(B + RVA_FN_ADD_LOOT_NEW))(&entrada);
	if (!obj) {
		LOG("expr: fabrica do efeito falhou");
		return;
	}
	char linha[300];
	int len = snprintf(linha, sizeof linha, "valor = %s", texto);
	_Alignas(16) uint8_t buf[0x20] = { 0 }, mem[0x60] = { 0 }, lexer[0x100] = { 0 }, reader[0x400] = { 0 };
	*(void **)(buf + 0x10) = B + RVA_ALLOC_CHAR;
	((void (*)(void *, const char *, int))(B + RVA_FN_BUF_COPY))(buf, linha, len);
	*(void **)mem = B + RVA_VT_MEMORY_FILE;
	*(int32_t *)(mem + 0x08) = 1;
	*(int64_t *)(mem + 0x10) = 1;
	*(int64_t *)(mem + 0x30) = 0xf;
	*(void **)(mem + 0x38) = buf;
	*(int32_t *)(mem + 0x50) = -1;
	((void (*)(void *, void *, int))(B + RVA_FN_LEXER_STREAM))(lexer, mem, 0);
	*(void **)lexer = B + RVA_VT_TEXT_LEXER;
	((void (*)(void *, void *))(B + RVA_FN_READER))(reader, lexer);
	((void (*)(void *))(B + RVA_FN_READ_STMT))(reader);   // `value =`
	// effect method 1: reads the value (passes it on to the value object at +0x60), with the input scope type
	((void (*)(void *, void *, uint16_t))((*(void ***)obj)[1]))(obj, reader, SCOPE_ARMY_TYPE);
	((void (*)(void *))(B + RVA_FN_READER_DTOR))(reader);
	((void (*)(void *))(B + RVA_FN_LEXER_DTOR))(lexer);
	expr_obj = obj;
	snprintf(expr_texto, sizeof expr_texto, "%s", texto);
	LOG("expr: '%s' lido (objeto %p); avalia no proximo ck3_custom_ping em exercito", texto, obj);
}

// expression read for a scope type, stored by its text (A3, SIM2b)
// (2026-10-07: text with no size limit and 1024 entries; before, 240 characters and 96, and a longer text or one beyond the
// 96 was read again on every call)
#define EXPR_MAX 1024
static struct { char *texto; uint16_t st; uint8_t *obj; } expr_cache[EXPR_MAX];
static int expr_n;

static uint8_t *expr_ler(const char *texto, uint16_t st)
{
	for (int i = 0; i < expr_n; i++)
		if (expr_cache[i].st == st && !strcmp(expr_cache[i].texto, texto))
			return expr_cache[i].obj;
	static struct { void *vt; const char *name; int32_t id, pad; } entrada = { NULL, "ck3_expr", 0, 0 };
	uint8_t *obj = ((uint8_t *(*)(void *))(B + RVA_FN_ADD_LOOT_NEW))(&entrada);
	if (!obj)
		return NULL;
	size_t cap = strlen(texto) + 16;
	char *linha = malloc(cap);
	if (!linha)
		return NULL;
	int len = snprintf(linha, cap, "valor = %s", texto);
	_Alignas(16) uint8_t buf[0x20] = { 0 }, mem[0x60] = { 0 }, lexer[0x100] = { 0 }, reader[0x400] = { 0 };
	*(void **)(buf + 0x10) = B + RVA_ALLOC_CHAR;
	((void (*)(void *, const char *, int))(B + RVA_FN_BUF_COPY))(buf, linha, len);
	free(linha);
	*(void **)mem = B + RVA_VT_MEMORY_FILE;
	*(int32_t *)(mem + 0x08) = 1;
	*(int64_t *)(mem + 0x10) = 1;
	*(int64_t *)(mem + 0x30) = 0xf;
	*(void **)(mem + 0x38) = buf;
	*(int32_t *)(mem + 0x50) = -1;
	((void (*)(void *, void *, int))(B + RVA_FN_LEXER_STREAM))(lexer, mem, 0);
	*(void **)lexer = B + RVA_VT_TEXT_LEXER;
	((void (*)(void *, void *))(B + RVA_FN_READER))(reader, lexer);
	((void (*)(void *))(B + RVA_FN_READ_STMT))(reader);
	((void (*)(void *, void *, uint16_t))((*(void ***)obj)[1]))(obj, reader, st);
	((void (*)(void *))(B + RVA_FN_READER_DTOR))(reader);
	((void (*)(void *))(B + RVA_FN_LEXER_DTOR))(lexer);
	if (expr_n < EXPR_MAX && (expr_cache[expr_n].texto = _strdup(texto))) {
		expr_cache[expr_n].st = st;
		expr_cache[expr_n].obj = obj;
		expr_n++;
	}
	return obj;
}

// evaluates `texto` in scope (st, id) with the context of a running effect: copies the context and swaps the current scope
// ([context]); root and the rest stay those of the effect
#define CTX_QW 16
static int expr_valor(void **ctx, const char *texto, uint16_t st, uint32_t id, int64_t *out)
{
	uint8_t *obj = expr_ler(texto, st);
	if (!obj || !ctx)
		return 0;
	struct { uint16_t type, zero; uint32_t pad; uint32_t id, pad2; } sc = { st, 0, 0, id, 0 };
	void *c2[CTX_QW];
	memcpy(c2, ctx, sizeof c2);
	c2[0] = &sc;
	*out = 0;
	((void (*)(void *, int64_t *, void *, void *, void *))(B + RVA_FN_VALUE))(obj + 0x60, out, c2, c2[5], obj + 0x10);
	return 1;
}

// proof SIM2b: `ctxprova <type> <id> <text>` evaluates on the next auto_raid_ping_livre (any scope) and shows the
// context
static struct { int on; uint16_t st; uint32_t id; char texto[240]; } ctxprova;

static void ctxprova_rodar(void **ctx, uint32_t scope_type, uint32_t scope_id)
{
	if (!ctxprova.on)
		return;
	ctxprova.on = 0;
	for (int i = 0; i < CTX_QW; i++) {
		uint8_t *p = (uint8_t *)ctx[i];
		LOG("ctxprova: ctx[%d] = %p (u16 %u, u32+8 %u)", i, p, p ? (uint16_t)rd32(p) : 0, p ? rd32(p + 8) : 0);
	}
	int64_t v = 0;
	int ok = expr_valor(ctx, ctxprova.texto, ctxprova.st, ctxprova.id, &v);
	LOG("ctxprova: efeito em 0x%x/%u; '%s' em 0x%x/%u = %.3f (%s)", scope_type, scope_id, ctxprova.texto, ctxprova.st,
	    ctxprova.id, (double)v / FIXED_ONE, ok ? "ok" : "falhou");
	uint8_t *ar = ctxprova.st == SCOPE_ARMY_TYPE ? table_get(RVA_ARMY_TABLE, ctxprova.id, 0x10) : NULL;
	if (ar)
		LOG("ctxprova: memoria do exercito: tamanho %u, suprimento %.3f", rd32(ar + 0x24),
		    (double)rd64(ar + 0x180) / 100000.0);
}

static void cmd_ctxprova(const char *s)
{
	char *e;
	ctxprova.st = (uint16_t)strtoul(s, &e, 0);
	ctxprova.id = (uint32_t)strtoul(e, &e, 0);
	while (*e == ' ')
		e++;
	snprintf(ctxprova.texto, sizeof ctxprova.texto, "%s", e);
	ctxprova.on = 1;
	LOG("ctxprova: '%s' em 0x%x/%u no proximo ck3_custom_ping", ctxprova.texto, ctxprova.st, ctxprova.id);
}

static void expr_avaliar(void **ctx, uint32_t scope_type, uint32_t scope_id, int64_t value)
{
	ctxprova_rodar(ctx, scope_type, scope_id);
	if (!expr_obj || scope_type != SCOPE_ARMY_TYPE)
		return;
	int64_t v = 0;
	((void (*)(void *, int64_t *, void *, void *, void *))(B + RVA_FN_VALUE))(expr_obj + 0x60, &v, ctx, ctx[5],
	                                                                        expr_obj + 0x10);
	LOG("expr: exercito %u: '%s' = %.3f (script passou %.3f)", scope_id, expr_texto, (double)v / FIXED_ONE,
	    (double)value / FIXED_ONE);
}

// ---- script variables by (scope type, id) ----
typedef struct { uint16_t type, zero; uint32_t pad; uint32_t id, pad2; } scope_ref;

static int32_t var_num(const char *name)
{
	struct { const char *p; uint32_t len; uint8_t flag; } __attribute__((aligned(16))) text =
		{ name, (uint32_t)strlen(name), 0 };
	int32_t id = -1;
	void *pool = ((void *(*)(void))(B + RVA_FN_VAR_POOL))();
	((void (*)(void *, int32_t *, void *))(B + RVA_FN_VAR_NAME))(pool, &id, &text);
	return id;
}

// set_variable = { name value days } on the object (scope type, id); days <= 0 = no expiry
static int var_set(uint16_t st, uint32_t id, const char *name, int64_t fixed, int dias)
{
	scope_ref sc = { st, 0, 0, id, 0 };
	uint8_t *vars = ((uint8_t *(*)(void *))(B + RVA_FN_SCOPE_VARS))(&sc);
	if (!vars)
		return 0;
	struct { uint16_t type, zero; uint32_t pad; int64_t value; } val = { 1, 0, 0, fixed };
	((void (*)(void *, int32_t, void *, int32_t))(B + RVA_FN_VAR_SET))(vars + 8, var_num(name), &val, dias);
	((void (*)(void *))(B + RVA_FN_VARS_CHANGED))(vars);
	((void (*)(void *))(B + RVA_FN_VARS_CHANGED))(vars);
	return 1;
}

// remove_variable (CJominiRemoveVariableEffect, function 22 at 0x3393c00)
#define RVA_FN_VAR_REMOVE 0x3345af0   // (variables, name number)
static int var_del(uint16_t st, uint32_t id, const char *name)
{
	scope_ref sc = { st, 0, 0, id, 0 };
	uint8_t *vars = ((uint8_t *(*)(void *))(B + RVA_FN_SCOPE_VARS))(&sc);
	if (!vars)
		return 0;
	((void (*)(void *, int32_t))(B + RVA_FN_VAR_REMOVE))(vars, var_num(name));
	((void (*)(void *))(B + RVA_FN_VARS_CHANGED))(vars);
	return 1;
}

// `var <type> <id> <name> [set <value> [days] | del]`: reads (type, value and raw expiry) or changes a variable
static void cmd_var(char **a, int n)
{
	if (n < 4) {
		LOG("uso: var <tipo> <id> <nome> [set <valor> [dias] | del]");
		return;
	}
	uint16_t st = (uint16_t)strtoul(a[1], NULL, 0);
	uint32_t id = (uint32_t)strtoul(a[2], NULL, 0);
	if (n >= 6 && !strcmp(a[4], "set"))
		var_set(st, id, a[3], (int64_t)(atof(a[5]) * FIXED_ONE), n > 6 ? atoi(a[6]) : 0);
	else if (n >= 5 && !strcmp(a[4], "del"))
		var_del(st, id, a[3]);
	scope_ref sc = { st, 0, 0, id, 0 };
	uint8_t *vars = ((uint8_t *(*)(void *))(B + RVA_FN_SCOPE_VARS))(&sc);
	int32_t num = var_num(a[3]);
	uint8_t *base = vars ? rdp(vars + 8 + 8) : NULL;
	int cnt = vars ? (int)rd32(vars + 8 + 0x14) : 0;
	for (int i = 0; base && i < cnt && i < 4096; i++) {
		uint8_t *e = base + (size_t)i * 0x20;
		if ((int32_t)rd32(e + 8) == num) {
			LOG("var %s em 0x%x/%u: tipo %u valor %.3f prazo(+0xc) %d", a[3], st, id, (uint16_t)rd32(e + 0x10),
			    (double)rd64(e + 0x18) / FIXED_ONE, (int32_t)rd32(e + 0xc));
			return;
		}
	}
	LOG("var %s em 0x%x/%u: nao existe", a[3], st, id);
}

// ---- mod plugins in C source (A2; docs/dll/arquitetura_dll.md, sections 4 to 7) ----
// Each active mod with `plugin/plugin.txt` has a plugin: the `.c` files listed in `fonte` are compiled in memory by
// TinyCC (embedded libtcc), without the system library (-nostdlib): the plugin only sees ck3_api and the
// text and memory functions allowed below. In -debug_mode the core checks the dates of the folder's .c/.h and recompiles; the old
// plugin keeps running if the compilation fails. Script effects stay registered by the loader; each effect goes
// to the first plugin that handles it.
#include "libtcc.h"
#include "ck3_api.h"

#define PLG_MAX 4
static struct {
	char name[64], dir[MAX_PATH], moddir[MAX_PATH];
	char src[8][64];
	int nsrc;
	char efeitos[32][48];   // effects declared in plugin.txt: the core hands each one to its plugin
	int nefeitos;
	FILETIME fontes, gatilhos;   // compiled .c/.h; the mod's triggers when the expressions were read
	TCCState *s;
	const ck3_plugin *pl;
} plg[PLG_MAX];
static int nplg, debug_mode;
static SRWLOCK plg_lock = SRWLOCK_INIT;
static volatile LONG plg_compilando;
static ULONGLONG plg_visto;

// ---- ck3_api: the game seen through ids ----
static void api_log(const char *fmt, ...)
{
	if (!debug_mode)   // release: the plugins' log only exists in -debug_mode
		return;
	char b[2048];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(b, sizeof b, fmt, ap);
	va_end(ap);
	LOG("%s", b);
}
static double api_clock(void)
{
	LARGE_INTEGER t, f;
	QueryPerformanceFrequency(&f);
	QueryPerformanceCounter(&t);
	return (double)t.QuadPart * 1000.0 / (double)f.QuadPart;
}
static int api_recording(void) { return rec_file != NULL; }
static int api_debug_mode(void) { return debug_mode; }
static void api_rec(const char *fmt, ...)
{
	if (!rec_file)
		return;
	char b[8192];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(b, sizeof b, fmt, ap);
	va_end(ap);
	rec("%s", b);
}
static uint32_t api_player(void) { return player_char(); }
static uint32_t api_unit_count(void) { return table_count(RVA_UNIT_TABLE); }
static uint32_t api_unit_at(uint32_t i)
{
	uint8_t *u = table_at(RVA_UNIT_TABLE, i);
	uint32_t id = u ? rd32(u + UNIT_ID) : 0;
	return u && (id & 0xFFFFFF) == i ? id : 0;
}
static int api_unit_exists(uint32_t unit) { return unit_by_id(unit) != NULL; }
static uint32_t api_unit_owner(uint32_t unit)
{
	uint8_t *u = unit_by_id(unit);
	return u ? rd32(u + UNIT_OWNER_CHAR) : 0;
}
static uint32_t api_unit_army(uint32_t unit)
{
	uint8_t *u = unit_by_id(unit);
	uint32_t army = u ? rd32(u + UNIT_ARMY) : CK3_NONE;
	return army != CK3_NONE && table_get(RVA_ARMY_TABLE, army, 0x10) ? army : CK3_NONE;
}
static uint32_t api_unit_location(uint32_t unit)
{
	uint8_t *u = unit_by_id(unit);
	return u ? prov_id(rdp(u + UNIT_LOCATION)) : 0;
}
static int api_unit_path(uint32_t unit, uint32_t *out, int max)
{
	uint8_t *u = unit_by_id(unit);
	return u ? unit_path(u, out, out ? max : 0) : 0;
}
static int api_unit_relation(uint32_t unit)
{
	uint8_t *u = unit_by_id(unit);
	return u ? unit_relation(u) : 5;
}
static uint8_t *army_obj(uint32_t army) { return table_get(RVA_ARMY_TABLE, army, 0x10); }
static uint32_t api_army_unit(uint32_t army)
{
	uint8_t *a = army_obj(army);
	return a ? rd32(a + ARMY_UNIT) : 0;
}
// (api 4) county attrition casualties when moving from `from` to `to` (neighboring provinces on the path); 0 = no charge,
// -1 = invalid army, owner or province
static int api_county_attrition(uint32_t army, uint32_t from, uint32_t to)
{
	uint8_t *a = army_obj(army), *u = a ? unit_by_id(rd32(a + ARMY_UNIT)) : NULL;
	uint8_t *ch = u ? table_get(RVA_CHAR_TABLE, rd32(u + UNIT_OWNER_CHAR), 0x18) : NULL;
	uint8_t *pf = province_by_id(from), *pt = province_by_id(to);
	if (!a || !ch || !pf || !pt)
		return -1;
	uint8_t raid = 0;
	rd(a + ARMY_IS_RAID, &raid, 1);
	if (!((char (*)(void *, void *, void *, int))(B + RVA_FN_COUNTY_ATR))(ch, pf, pt, raid == 0))
		return 0;
	return ((int (*)(void *, void *))(B + RVA_FN_COUNTY_LOSS))(a, NULL);
}
// (api 4) path from `from` to `to` at the unit's speed (provinces after `from`); returns the size
static int api_path_from(uint32_t unit, uint32_t from, uint32_t to, uint32_t *out, int max)
{
	uint8_t *u = unit_by_id(unit);
	return u ? path_from(u, province_by_id(from), province_by_id(to), out, max) : 0;
}
static int64_t api_army_loot(uint32_t army)
{
	uint8_t *a = army_obj(army);
	return a ? rd64(a + ARMY_LOOT) : 0;
}
static int64_t api_army_loot_cap(uint32_t army)
{
	uint8_t *a = army_obj(army);
	int64_t cap = 0;
	if (a)
		((void (*)(void *, void *))(B + RVA_FN_LOOT_CAP))(a, &cap);
	return cap;
}
static int api_army_is_raid(uint32_t army)
{
	uint8_t *a = army_obj(army), r = 0;
	if (a)
		rd(a + ARMY_IS_RAID, &r, 1);
	return r != 0;
}
static int api_army_raid(uint32_t army, uint32_t *prov, int64_t *done)
{
	uint8_t *a = army_obj(army);
	uint32_t rid = a ? rd32(a + ARMY_RAID) : 0xFFFFFFFF;
	uint8_t *raid = rid != 0xFFFFFFFF ? table_get(RVA_RAID_TABLE, rid, 8) : NULL;
	if (!raid)
		return 0;
	*prov = prov_id(rdp(raid + 0x20));
	*done = rd64(raid + 0x18);
	return 1;
}
static uint32_t api_prov_count(void)
{
	if (!pv_arr)
		prov_cache();
	return pv_n;
}
static int api_prov_exists(uint32_t prov) { return province_by_id(prov) != NULL; }
static int api_prov_xy(uint32_t prov, int *x, int *y)
{
	uint8_t *p = province_by_id(prov), *def = p ? rdp(p + 8) : NULL;
	if (!def)
		return 0;
	int32_t xy[2] = { 0, 0 };
	rd(def + 0x10, xy, 8);
	*x = xy[0];
	*y = xy[1];
	return 1;
}
static int api_prov_is_sea(uint32_t prov) { return prov_is_sea(province_by_id(prov)); }
static int api_eta(uint32_t unit, uint32_t prov, uint32_t *path, int *path_n, int path_max)
{
	uint8_t *u = unit_by_id(unit);
	if (path_n)
		*path_n = 0;
	return u ? eta_days(u, province_by_id(prov), NULL, path, path_n, path_max, NULL) : -1;
}
static int api_eta_from(uint32_t unit, uint32_t from, uint32_t prov)
{
	uint8_t *u = unit_by_id(unit);
	return u ? eta_from(u, province_by_id(from), province_by_id(prov)) : -1;
}
static double api_predict(const uint32_t *ours, int n1, const uint32_t *theirs, int n2)
{
	return predict_armies((uint32_t *)ours, n1, (uint32_t *)theirs, n2);
}
static int api_can_raid(uint32_t army, uint32_t prov)
{
	uint8_t *a = army_obj(army), *p = province_by_id(prov);
	return a && p && ((uint8_t (*)(void *, void *, void *))(B + RVA_FN_CAN_RAID))(a, p, NULL) != 0;
}
static int64_t api_prov_loot(uint32_t prov)
{
	uint8_t *p = province_by_id(prov);
	int64_t loot = 0;
	if (p)
		((void (*)(void *, void *, void *, void *))(B + RVA_FN_PROV_LOOT))(&loot, p, NULL, NULL);
	return loot;
}
static int api_raid_days(uint32_t army, uint32_t prov, int64_t done, int64_t *total, int64_t *per_day)
{
	uint8_t *a = army_obj(army), *p = province_by_id(prov);
	return a && p ? raid_days(a, p, done, total, per_day) : -1;
}
static int api_var_get(uint16_t st, uint32_t id, const char *name, int64_t *v, uint16_t *type)
{
	return get_var_any(st, id, name, v, type);
}
static int api_var_set_scope(uint16_t st, uint32_t id, const char *name, uint16_t vst, uint32_t vid)
{
	scope_ref sc = { st, 0, 0, id, 0 };
	uint8_t *vars = ((uint8_t *(*)(void *))(B + RVA_FN_SCOPE_VARS))(&sc);
	if (!vars)
		return 0;
	struct { uint16_t type, zero; uint32_t pad; uint32_t id, pad2; } val = { vst, 0, 0, vid, 0 };
	((void (*)(void *, int32_t, void *, int32_t))(B + RVA_FN_VAR_SET))(vars + 8, var_num(name), &val, 0);
	((void (*)(void *))(B + RVA_FN_VARS_CHANGED))(vars);
	((void (*)(void *))(B + RVA_FN_VARS_CHANGED))(vars);
	return 1;
}
static int api_expr(void *ctx, const char *text, uint16_t st, uint32_t id, int64_t *out)
{
	return expr_valor((void **)ctx, text, st, id, out);
}
struct cfg_each { void (*fn)(const char *, double, void *); void *arg; };
static void cfg_each_one(uint8_t *s, const char *name, double v, void *arg)
{
	struct cfg_each *c = arg;
	c->fn(name, v, c->arg);
}
static int api_cfg_each(void (*fn)(const char *name, double v, void *arg), void *arg)
{
	struct cfg_each c = { fn, arg };
	return mods_settings(cfg_each_one, &c);
}
static void api_run_main(const char *line) { H->run_main(line); }

static ck3_api API = {
	CK3_API_VERSION, 0, api_log, api_clock, api_recording, api_rec, api_player,
	api_unit_count, api_unit_at, api_unit_exists, api_unit_owner, api_unit_army, api_unit_location, api_unit_path,
	api_unit_relation,
	api_army_unit, api_army_loot, api_army_loot_cap, api_army_is_raid, api_army_raid,
	api_prov_count, api_prov_exists, api_prov_xy, api_prov_is_sea,
	api_eta, api_eta_from, api_predict, api_can_raid, api_prov_loot, api_raid_days,
	api_var_get, var_set, api_var_set_scope, var_del,
	api_expr, api_cfg_each, api_run_main, api_game_files, api_file_read, api_file_free, api_file_write,
	api_county_attrition, api_path_from, api_combat_info, api_combat_hypo, api_debug_mode };

// ---- find, compile and swap ----
static FILETIME mais_novo(const char *dir, const char *const *pads, int npads)
{
	FILETIME t = { 0, 0 };
	for (int k = 0; k < npads; k++) {
		char p[MAX_PATH + 16];
		snprintf(p, sizeof p, "%s\\%s", dir, pads[k]);
		WIN32_FIND_DATAA fd;
		HANDLE h = FindFirstFileA(p, &fd);
		if (h == INVALID_HANDLE_VALUE)
			continue;
		do
			if (CompareFileTime(&fd.ftLastWriteTime, &t) > 0)
				t = fd.ftLastWriteTime;
		while (FindNextFileA(h, &fd));
		FindClose(h);
	}
	return t;
}

static FILETIME plg_fontes(int i)
{
	static const char *const P[] = { "*.c", "*.h" };
	return mais_novo(plg[i].dir, P, 2);
}

static FILETIME plg_gatilhos(int i)
{
	static const char *const P[] = { "*.txt" };
	char d[MAX_PATH + 32];
	snprintf(d, sizeof d, "%s\\common\\scripted_triggers", plg[i].moddir);
	return mais_novo(d, P, 1);
}

static void plg_achar(void)
{
	static char dirs[16][MAX_PATH];
	int nd = mod_dirs(dirs, 16);
	nplg = 0;
	for (int d = 0; d < nd && nplg < PLG_MAX; d++) {
		char p[MAX_PATH + 32];
		snprintf(p, sizeof p, "%s\\plugin\\plugin.txt", dirs[d]);
		char *t = mods_read(p);
		if (!t)
			continue;
		int i = nplg++;
		memset(&plg[i], 0, sizeof plg[i]);
		snprintf(plg[i].moddir, sizeof plg[i].moddir, "%s", dirs[d]);
		snprintf(plg[i].dir, sizeof plg[i].dir, "%s\\plugin", dirs[d]);
		snprintf(plg[i].name, sizeof plg[i].name, "%s", dirs[d]);
		for (char *line = strtok(t, "\r\n"); line; line = strtok(NULL, "\r\n")) {
			char w0[64] = "", w1[64] = "";
			int nw = sscanf(line, "%63s %63s", w0, w1);
			if (nw < 1 || (nw < 2 && (!strcmp(w0, "nome") || !strcmp(w0, "api") || !strcmp(w0, "fonte") ||
			                          !strcmp(w0, "var"))))
				continue;
			if (!strcmp(w0, "nome"))
				snprintf(plg[i].name, sizeof plg[i].name, "%s", w1);
			else if (!strcmp(w0, "fonte") && plg[i].nsrc < 8)
				snprintf(plg[i].src[plg[i].nsrc++], sizeof plg[i].src[0], "%s", w1);
			else if (!strcmp(w0, "api") && atoi(w1) != CK3_API_VERSION)
				LOG("plugin %s: api %s, o nucleo e da %d", plg[i].name, w1, CK3_API_VERSION);
			else if (strcmp(w0, "var") && strcmp(w0, "nome") && strcmp(w0, "api") && strcmp(w0, "fonte") && w0[0] != '#' &&
			         plg[i].nefeitos < 32)
				snprintf(plg[i].efeitos[plg[i].nefeitos++], sizeof plg[i].efeitos[0], "%s", w0);
		}
		free(t);
		LOG("plugin %s: %s (%d fonte(s), %d efeito(s))", plg[i].name, plg[i].dir, plg[i].nsrc, plg[i].nefeitos);
	}
}

static void tcc_erro(void *o, const char *msg) { LOG("plugin: %s", msg); }

static int plg_compilar(int i)
{
	double t0 = api_clock();
	FILETIME ft = plg_fontes(i);
	TCCState *s = tcc_new();
	char p[MAX_PATH + 64];
	snprintf(p, sizeof p, "%sck3_custom", H->dir);
	tcc_set_lib_path(s, p);
	tcc_set_error_func(s, NULL, tcc_erro);
	snprintf(p, sizeof p, "%sck3_custom/include", H->dir);
	tcc_add_sysinclude_path(s, p);
	tcc_add_include_path(s, plg[i].dir);
	tcc_set_options(s, "-nostdlib");
	tcc_set_output_type(s, TCC_OUTPUT_MEMORY);
	static const struct { const char *n; const void *f; } SIM[] = {
		{ "memset", memset }, { "memcpy", memcpy }, { "memmove", memmove }, { "memcmp", memcmp },
		{ "strlen", strlen }, { "strcmp", strcmp }, { "strncmp", strncmp }, { "strcpy", strcpy },
		{ "strncpy", strncpy }, { "strchr", strchr }, { "strrchr", strrchr }, { "strstr", strstr },
		{ "snprintf", snprintf }, { "vsnprintf", vsnprintf }, { "atoi", atoi }, { "atof", atof },
		{ "strtol", strtol }, { "strtoul", strtoul } };
	for (unsigned k = 0; k < sizeof SIM / sizeof *SIM; k++)
		tcc_add_symbol(s, SIM[k].n, SIM[k].f);
	int ok = 1;
	for (int k = 0; k < plg[i].nsrc && ok; k++) {
		snprintf(p, sizeof p, "%s\\%s", plg[i].dir, plg[i].src[k]);
		ok = tcc_add_file(s, p) >= 0;
	}
	snprintf(p, sizeof p, "%sck3_custom/lib/libtcc1.a", H->dir);
	const ck3_plugin *(*start)(const ck3_api *) = NULL;
	const ck3_plugin *pl = NULL;
	if (ok && tcc_add_file(s, p) >= 0 && tcc_relocate(s) >= 0 && (start = tcc_get_symbol(s, "ck3_plugin_start")))
		pl = start(&API);
	plg[i].fontes = ft;   // does not try again until the source changes
	if (!pl) {
		tcc_delete(s);
		LOG("plugin %s: NAO compilou; %s", plg[i].name, plg[i].pl ? "o anterior continua" : "sem plugin");
		return 0;
	}
	AcquireSRWLockExclusive(&plg_lock);
	TCCState *old = plg[i].s;
	if (plg[i].pl && plg[i].pl->on_stop)
		plg[i].pl->on_stop();
	plg[i].s = s;
	plg[i].pl = pl;
	ReleaseSRWLockExclusive(&plg_lock);
	if (old)
		tcc_delete(old);
	LOG("plugin %s: compilado e carregado em %.1f ms (%s)", plg[i].name, api_clock() - t0, plg[i].dir);
	return 1;
}

// every ~1 s (in -debug_mode): source changed = recompile; the mod's triggers changed = the stored expressions are read
// again (the game re-reads the file; wait 5 s)
static void plg_conferir(void)
{
	ULONGLONG agora = GetTickCount64();
	if (!debug_mode || agora - plg_visto < 1000 || InterlockedExchange(&plg_compilando, 1))
		return;
	plg_visto = agora;
	for (int i = 0; i < nplg; i++) {
		FILETIME f = plg_fontes(i);
		if (CompareFileTime(&f, &plg[i].fontes) > 0)
			plg_compilar(i);
		FILETIME g = plg_gatilhos(i);
		if (CompareFileTime(&g, &plg[i].gatilhos)) {
			FILETIME now;
			GetSystemTimeAsFileTime(&now);
			ULARGE_INTEGER a = { .LowPart = now.dwLowDateTime, .HighPart = now.dwHighDateTime },
			               b = { .LowPart = g.dwLowDateTime, .HighPart = g.dwHighDateTime };
			if (a.QuadPart - b.QuadPart >= 50000000ULL) {
				if (plg[i].gatilhos.dwLowDateTime || plg[i].gatilhos.dwHighDateTime) {
					LOG("plugin %s: gatilhos do mod mudaram; expressoes lidas de novo", plg[i].name);
					expr_n = 0;
				}
				plg[i].gatilhos = g;
			}
		}
	}
	InterlockedExchange(&plg_compilando, 0);
}

static int plg_pedido(char **a, int n)
{
	if (!strcmp(a[0], "plugin")) {
		if (n > 1 && !strcmp(a[1], "recompilar"))
			for (int i = 0; i < nplg; i++)
				plg_compilar(i);
		for (int i = 0; i < nplg; i++)
			LOG("plugin %s: %s, %d fonte(s), %s", plg[i].name, plg[i].dir, plg[i].nsrc, plg[i].pl ? "carregado" : "SEM PLUGIN");
		if (!nplg)
			LOG("plugin: nenhum mod ativo com plugin/plugin.txt");
		return 1;
	}
	int ok = 0;
	AcquireSRWLockShared(&plg_lock);
	for (int i = 0; i < nplg && !ok; i++)
		if (plg[i].pl && plg[i].pl->on_command)
			ok = plg[i].pl->on_command(a, n);
	ReleaseSRWLockShared(&plg_lock);
	return ok;
}

static void on_effect(const char *name, uint32_t scope_type, uint32_t scope_id, int64_t value, void *ctx,
                      uint32_t thread)
{
	prov_cache();
	if (!strcmp(name, "ck3_custom_ping")) {
		// the core's own effect, registered by the loader (any scope): proofs A3 and SIM2b (requests expr and
		// ctxprova) and a check that the core responds
		expr_avaliar(ctx, scope_type, scope_id, value);
		LOG("efeito ck3_custom_ping: escopo tipo=0x%x id=%u valor=%.3f thread=%u (%s)", scope_type, scope_id,
		    (double)value / FIXED_ONE, thread, thread == H->main_thread ? "principal" : "OUTRA");
		return;
	}
	if (mute_chamar) {
		LOG("efeito %s: sem resposta (pedido mudo, simula o jogo sem a DLL)", name);
		return;
	}
	plg_conferir();
	int ok = 0, dono = -1;
	for (int i = 0; i < nplg && dono < 0; i++)
		for (int k = 0; k < plg[i].nefeitos && dono < 0; k++)
			if (!strcmp(plg[i].efeitos[k], name))
				dono = i;
	AcquireSRWLockShared(&plg_lock);
	if (dono >= 0 && plg[dono].pl)
		ok = plg[dono].pl->on_effect(name, scope_type, scope_id, value, ctx, thread);
	for (int i = 0; dono < 0 && i < nplg && !ok; i++)
		if (plg[i].pl)
			ok = plg[i].pl->on_effect(name, scope_type, scope_id, value, ctx, thread);
	ReleaseSRWLockShared(&plg_lock);
	if (!ok)
		LOG("efeito %s: nenhum plugin tratou (escopo 0x%x/%u, valor %.3f)", name, scope_type, scope_id,
		    (double)value / FIXED_ONE);
}

static const ar_core CORE = { AR_API_VERSION, on_post, on_command, on_effect };

__declspec(dllexport) const ar_core *ar_start(const ar_host *host)
{
	if (!host || host->version != AR_API_VERSION)
		return NULL;
	H = host;
	B = host->base;
	hook2_install();
	mods_start();
	debug_mode = strstr(GetCommandLineA(), "-debug_mode") != NULL;
	API.main_thread = H->main_thread;
	plg_achar();
	for (int i = 0; i < nplg; i++)
		plg_compilar(i);
	return &CORE;
}

__declspec(dllexport) void ar_stop(void)
{
	hook2_remove();
	mods_stop();
	for (int i = 0; i < nplg; i++) {
		if (plg[i].pl && plg[i].pl->on_stop)
			plg[i].pl->on_stop();
		if (plg[i].s)
			tcc_delete(plg[i].s);
		plg[i].s = NULL;
		plg[i].pl = NULL;
	}
	if (rec_file) {
		fclose(rec_file);   // the next core starts stopped; `gravar on` again continues in the same file
		rec_file = NULL;
	}
	// the memory search runs on its own thread: its code must not leave memory before it finishes
	for (int i = 0; i < 1200 && find_busy; i++)
		Sleep(100);
}
