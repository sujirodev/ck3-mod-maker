// ck3_custom - loader (fixed; docs/dll/arquitetura_dll.md).
// Loaded as winmm.dll from the binaries folder of CK3 1.19.0.6 (see exports.h). Forwards the functions to the system
// DLL of the same name, intercepts command posting, registers the script effects declared in the plugins of the active
// mods (plugin/plugin.txt) and hosts the core (ck3_custom_core.dll), which in -debug_mode can be swapped with the game
// open. No mod logic lives here. Addresses in docs/dll/re_notas.md.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "ar_host.h"

#define RVA_POST          0x973e00   // Post(dispatcher, command*, flags)
#define RVA_DISPATCHER    0x5762170
#define EXE_SIZE_OF_IMAGE_MIN 0x5b00000
#define WM_AR_COMMAND     (WM_APP + 0x5A1D)

static const uint8_t POST_PROLOGUE[15] = {
	0x48, 0x89, 0x5C, 0x24, 0x08,   // mov [rsp+8], rbx
	0x4C, 0x89, 0x4C, 0x24, 0x20,   // mov [rsp+0x20], r9
	0x57,                           // push rdi
	0x48, 0x83, 0xEC, 0x20          // sub rsp, 0x20
};

// ---- forwarding to the system DLL ----
// (version.dll does not work: Windows applies a compatibility shim to any ck3.exe and it loads the System32
// version.dll before the game's imports)
#include "exports.h"

#define DECL(name) void *real_##name; \
	__asm__(".text\n.globl " #name "\n" #name ":\n\tjmp *real_" #name "(%rip)\n");
EXPORTS(DECL)

static void load_real(void)
{
	char path[MAX_PATH];
	GetSystemDirectoryA(path, MAX_PATH);
	strcat(path, "\\" REAL_DLL);
	HMODULE real = LoadLibraryA(path);
#define RESOLVE(name) real_##name = (void *)GetProcAddress(real, #name);
	EXPORTS(RESOLVE)
}

// ---- log ----
static char dll_dir[MAX_PATH];
static char log_path[MAX_PATH + 32];
static char cmd_path[MAX_PATH + 32];
static char core_path[MAX_PATH + 32];
static CRITICAL_SECTION log_lock;

static void rlog(const char *fmt, ...)
{
	EnterCriticalSection(&log_lock);
	FILE *f = fopen(log_path, "a");
	if (f) {
		SYSTEMTIME t;
		GetLocalTime(&t);
		fprintf(f, "[%02d:%02d:%02d.%03d] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
		va_list ap;
		va_start(ap, fmt);
		vfprintf(f, fmt, ap);
		va_end(ap);
		fputc('\n', f);
		fclose(f);
	}
	LeaveCriticalSection(&log_lock);
}

// ---- core ----
static uint8_t *base;
static DWORD main_thread;
static const ar_core *volatile core;
static volatile LONG core_inflight;   // core calls in progress (to unload safely)

static const ar_core *core_enter(void)
{
	InterlockedIncrement(&core_inflight);
	const ar_core *c = core;
	if (!c)
		InterlockedDecrement(&core_inflight);
	return c;
}
static void core_leave(void) { InterlockedDecrement(&core_inflight); }

// ---- post interception ----
typedef uint8_t (*post_fn)(void *dispatcher, void *cmd, int flags, void *r9);
static post_fn post_original;

static uint8_t post_hook(void *dispatcher, void *cmd, int flags, void *r9)
{
	const ar_core *c = core_enter();
	if (c) {
		if (c->on_post)
			c->on_post(cmd, flags, GetCurrentThreadId());
		core_leave();
	}
	return post_original(dispatcher, cmd, flags, r9);
}

static uint8_t host_post(void *cmd, int flags)
{
	return ((post_fn)(base + RVA_POST))(base + RVA_DISPATCHER, cmd, flags, NULL);
}

// ---- protection against faults in the core (main thread) ----
// If the core causes an access violation while calling a game function, return to the safe point instead of
// crashing the game. Meant for development; the game state may be inconsistent after a fault.
static void *guard_buf[5];
static volatile DWORD guard_thread;
static volatile DWORD64 guard_fault_rip, guard_fault_addr;
static volatile DWORD guard_fault_code;
static uint8_t *text_lo, *text_hi;            // ck3.exe .text
static char guard_stack[160];                 // return addresses in the exe found on the faulting stack
static int looks_ret(const uint8_t *v);

static void guard_recover(void) { __builtin_longjmp(guard_buf, 1); }

// second safe point: script effects (may run outside the main thread; one at a time, fx_lock)
static void *fx_guard_buf[5];
static volatile DWORD fx_guard_thread;
static void fx_guard_recover(void) { __builtin_longjmp(fx_guard_buf, 1); }

static LONG CALLBACK guard_veh(EXCEPTION_POINTERS *e)
{
	DWORD code = e->ExceptionRecord->ExceptionCode;
	DWORD tid = GetCurrentThreadId();
	void (*recover)(void) = fx_guard_thread == tid ? fx_guard_recover : guard_thread == tid ? guard_recover : NULL;
	if (!recover)
		return EXCEPTION_CONTINUE_SEARCH;
	if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION &&
	    code != EXCEPTION_PRIV_INSTRUCTION && code != EXCEPTION_INT_DIVIDE_BY_ZERO &&
	    code != EXCEPTION_BREAKPOINT)
		return EXCEPTION_CONTINUE_SEARCH;
	guard_fault_code = code;
	guard_fault_rip = e->ContextRecord->Rip;
	guard_fault_addr = code == EXCEPTION_ACCESS_VIOLATION ? e->ExceptionRecord->ExceptionInformation[1] : 0;
	int len = 0, n = 0;
	const uint64_t *sp = (const uint64_t *)e->ContextRecord->Rsp;
	guard_stack[0] = 0;
	for (int k = 0; text_lo && k < 512 && n < 10; k++) {
		const uint8_t *v = (const uint8_t *)sp[k];
		if (v >= text_lo + 8 && v < text_hi && looks_ret(v)) {
			len += snprintf(guard_stack + len, sizeof guard_stack - len, " %llX", (unsigned long long)(v - base));
			n++;
		}
	}
	e->ContextRecord->Rsp = ((e->ContextRecord->Rsp - 0x200) & ~0xFULL) - 8;
	e->ContextRecord->Rip = (DWORD64)recover;
	return EXCEPTION_CONTINUE_EXECUTION;
}

static __attribute__((noinline)) void run_command_guarded(const ar_core *c, const char *volatile line)
{
	if (__builtin_setjmp(guard_buf) == 0) {
		guard_thread = GetCurrentThreadId();
		c->on_command(line);
		guard_thread = 0;
	} else {
		guard_thread = 0;
		rlog("FALHA CONTIDA no pedido '%s': codigo=0x%lX rip=exe+0x%llX endereco=0x%llX pilha(exe+):%s", line,
		     guard_fault_code, (unsigned long long)(guard_fault_rip - (DWORD64)base),
		     (unsigned long long)guard_fault_addr, guard_stack);
	}
}

// mod settings (defined further below)
static void cfg_reload(void);
static void cfg_watch(void);
static int debug_mode;
static int mod_dirs(char (*dirs)[MAX_PATH], int max);
static char *read_file(const char *path, size_t *len_out);

// ---- game window: requests run on the main thread ----
// Queue of request lines: they come in through the ck3_custom_cmd.txt file (worker thread) or from the core (run_main,
// any thread) and are executed on the main thread, by the game window's WndProc.
#define Q_MAX 64
static WNDPROC old_wndproc;
static HWND game_wnd;
static char q_lines[Q_MAX][512];
static int q_head, q_count;
static DWORD q_since;   // when the queue stopped being empty
static CRITICAL_SECTION q_lock;

static int q_push(const char *line)
{
	int ok = 0;
	EnterCriticalSection(&q_lock);
	if (q_count < Q_MAX) {
		if (!q_count)
			q_since = GetTickCount();
		char *slot = q_lines[(q_head + q_count++) % Q_MAX];
		strncpy(slot, line, sizeof q_lines[0] - 1);
		slot[sizeof q_lines[0] - 1] = 0;
		ok = 1;
	}
	LeaveCriticalSection(&q_lock);
	return ok;
}

static int q_pop(char *out)
{
	int ok = 0;
	EnterCriticalSection(&q_lock);
	if (q_count) {
		strcpy(out, q_lines[q_head]);
		q_head = (q_head + 1) % Q_MAX;
		q_count--;
		q_since = GetTickCount();
		ok = 1;
	}
	LeaveCriticalSection(&q_lock);
	return ok;
}

static void host_run_main(const char *line)
{
	if (!q_push(line)) {
		rlog("fila cheia, pedido descartado: %s", line);
		return;
	}
	HWND w = game_wnd;
	if (w)
		PostMessageW(w, WM_AR_COMMAND, 0, 0);   // no window yet: the worker thread finds it and posts
}

// runs the queued requests; main thread only (game window WndProc or the windowless pump)
static void q_drain(void)
{
	static char line[512];
	while (q_pop(line)) {
		if (strncmp(line, "status ", 7) != 0)   // status comes every second: kept out of the log
			rlog("> %s", line);
		if (!strcmp(line, "config recarregar")) {
			cfg_reload();
			continue;
		}
		const ar_core *c = core_enter();
		if (c) {
			if (c->on_command)
				run_command_guarded(c, line);
			core_leave();
		} else {
			rlog("pedido ignorado: nucleo nao carregado");
		}
	}
}

static LRESULT CALLBACK ar_wndproc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
	if (msg == WM_AR_COMMAND) {
		q_drain();
		return 0;
	}
	return CallWindowProcW(old_wndproc, w, msg, wp, lp);
}

static BOOL CALLBACK find_wnd(HWND w, LPARAM lp)
{
	DWORD pid;
	DWORD tid = GetWindowThreadProcessId(w, &pid);
	if (pid == GetCurrentProcessId() && tid == main_thread && IsWindowVisible(w) && !GetWindow(w, GW_OWNER)) {
		*(HWND *)lp = w;
		return FALSE;
	}
	return TRUE;
}

// ---- game without graphics (-nographics): diagnostics and queue pump (docs/dll/plano_sem_graficos.md) ----
// Without a window there is no WndProc to serve the queue. Functions the game loop calls are swapped in the ck3.exe
// import table; they count calls per thread and, on the main thread, the chosen one drains the queue.
enum { G_QPC, G_SLEEP, G_SLEEPEX, G_TGT, G_PEEK, G_GETMSG, G_SWITCH, G_WFSO, G_WFSOEX, G_N };
static const struct { const char *dll, *fn; } g_imp[G_N] = {
	{ "KERNEL32.dll", "QueryPerformanceCounter" }, { "KERNEL32.dll", "Sleep" }, { "KERNEL32.dll", "SleepEx" },
	{ "WINMM.dll", "timeGetTime" }, { "USER32.dll", "PeekMessageW" }, { "USER32.dll", "GetMessageW" },
	{ "KERNEL32.dll", "SwitchToThread" }, { "KERNEL32.dll", "WaitForSingleObject" },
	{ "KERNEL32.dll", "WaitForSingleObjectEx" } };
// pump preference order: the message loop site first (the same one where the WndProc runs)
static const int g_pref[G_N] = { G_PEEK, G_GETMSG, G_TGT, G_SLEEPEX, G_SLEEP, G_QPC, G_SWITCH, G_WFSO, G_WFSOEX };
#define G_PUMP_CAND 6   // SwitchToThread and the waits are not usable as a pump
#define G_SITES 16
static void *g_orig[G_N];
static volatile LONG64 g_main[G_N];
static struct { uint32_t rva, n; } g_site[G_N][G_SITES];   // call sites on the main thread
static int no_graphics, diag;
static volatile int pump_hook = -1;
static int pump_forced;
static DWORD pump_last;
static int pump_busy;
static volatile LONG64 pump_runs;

static void pump(void)
{
	if (!q_count || pump_busy || game_wnd)
		return;
	DWORD now = GetTickCount();
	if (now - pump_last < 50)
		return;
	pump_last = now;
	pump_busy = 1;
	pump_runs++;
	q_drain();
	pump_busy = 0;
}

static void g_hit(int i, void *ra)
{
	// only the main thread counts: SwitchToThread runs millions of times per second on the others (contended shared counter)
	if (GetCurrentThreadId() != main_thread)
		return;
	g_main[i]++;
	uint32_t rva = (uint32_t)((uint8_t *)ra - base);
	for (int k = 0; k < G_SITES; k++) {
		if (g_site[i][k].n && g_site[i][k].rva == rva) {
			g_site[i][k].n++;
			break;
		}
		if (!g_site[i][k].n) {
			g_site[i][k].rva = rva;
			g_site[i][k].n = 1;
			break;
		}
	}
	if (i == pump_hook)
		pump();
}

#define RA __builtin_return_address(0)
static BOOL WINAPI g_qpc(LARGE_INTEGER *c)
{ g_hit(G_QPC, RA); return ((BOOL (WINAPI *)(LARGE_INTEGER *))g_orig[G_QPC])(c); }
static void WINAPI g_sleep(DWORD ms)
{ g_hit(G_SLEEP, RA); ((void (WINAPI *)(DWORD))g_orig[G_SLEEP])(ms); }
static DWORD WINAPI g_sleepex(DWORD ms, BOOL a)
{ g_hit(G_SLEEPEX, RA); return ((DWORD (WINAPI *)(DWORD, BOOL))g_orig[G_SLEEPEX])(ms, a); }
static DWORD WINAPI g_tgt(void)
{ g_hit(G_TGT, RA); return ((DWORD (WINAPI *)(void))g_orig[G_TGT])(); }
static BOOL WINAPI g_peek(MSG *m, HWND w, UINT a, UINT b, UINT r)
{ g_hit(G_PEEK, RA); return ((BOOL (WINAPI *)(MSG *, HWND, UINT, UINT, UINT))g_orig[G_PEEK])(m, w, a, b, r); }
static BOOL WINAPI g_getmsg(MSG *m, HWND w, UINT a, UINT b)
{ g_hit(G_GETMSG, RA); return ((BOOL (WINAPI *)(MSG *, HWND, UINT, UINT))g_orig[G_GETMSG])(m, w, a, b); }
static BOOL WINAPI g_switch(void)
{ g_hit(G_SWITCH, RA); return ((BOOL (WINAPI *)(void))g_orig[G_SWITCH])(); }
static DWORD WINAPI g_wfso(HANDLE h, DWORD ms)
{ g_hit(G_WFSO, RA); return ((DWORD (WINAPI *)(HANDLE, DWORD))g_orig[G_WFSO])(h, ms); }
static DWORD WINAPI g_wfsoex(HANDLE h, DWORD ms, BOOL a)
{ g_hit(G_WFSOEX, RA); return ((DWORD (WINAPI *)(HANDLE, DWORD, BOOL))g_orig[G_WFSOEX])(h, ms, a); }
static void *const g_new[G_N] = { g_qpc, g_sleep, g_sleepex, g_tgt, g_peek, g_getmsg, g_switch, g_wfso, g_wfsoex };

static void g_install(void)
{
	IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
	IMAGE_DATA_DIRECTORY dd = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
	int n = 0;
	for (IMAGE_IMPORT_DESCRIPTOR *d = (void *)(base + dd.VirtualAddress); d->Name; d++) {
		const char *dll = (const char *)(base + d->Name);
		IMAGE_THUNK_DATA64 *names = (void *)(base + d->OriginalFirstThunk), *iat = (void *)(base + d->FirstThunk);
		for (; names->u1.AddressOfData; names++, iat++) {
			if (IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal))
				continue;
			const char *fn = (const char *)((IMAGE_IMPORT_BY_NAME *)(base + names->u1.AddressOfData))->Name;
			for (int i = 0; i < G_N; i++) {
				if (g_orig[i] || _stricmp(dll, g_imp[i].dll) || strcmp(fn, g_imp[i].fn))
					continue;
				DWORD old;
				if (!VirtualProtect(&iat->u1.Function, 8, PAGE_READWRITE, &old))
					break;
				g_orig[i] = (void *)iat->u1.Function;
				InterlockedExchangePointer((void **)&iat->u1.Function, g_new[i]);
				VirtualProtect(&iat->u1.Function, 8, old, &old);
				n++;
			}
		}
	}
	rlog("ganchos de importacao: %d de %d instalados%s", n, G_N, no_graphics ? " (-nographics)" : "");
}

// samples of where the main thread is (only with diagnostics on)
#define S_MAX 24
static HANDLE main_h;
static struct { uint32_t rva, n; char mod[24]; } s_tab[S_MAX];
static int s_total;
static uint64_t s_stack[2048];

static int looks_ret(const uint8_t *v)
{
	return v[-5] == 0xE8 || (v[-6] == 0xFF && (v[-5] & 0x38) == 0x10) || (v[-2] == 0xFF && (v[-1] & 0x38) == 0x10) ||
	       (v[-3] == 0xFF && (v[-2] & 0x38) == 0x10);
}

static void sample_main(void)
{
	if (!main_h)
		return;
	CONTEXT ctx;
	memset(&ctx, 0, sizeof ctx);
	ctx.ContextFlags = CONTEXT_CONTROL;
	size_t n = 0;
	if (SuspendThread(main_h) == (DWORD)-1)
		return;
	// nothing that takes a lock (heap, log) while the main thread is suspended
	if (GetThreadContext(main_h, &ctx)) {
		MEMORY_BASIC_INFORMATION mi;
		if (VirtualQuery((void *)ctx.Rsp, &mi, sizeof mi)) {
			size_t avail = (size_t)((uint8_t *)mi.BaseAddress + mi.RegionSize - (uint8_t *)ctx.Rsp) / 8;
			n = avail < 2048 ? avail : 2048;
			memcpy(s_stack, (void *)ctx.Rsp, n * 8);
		}
	}
	ResumeThread(main_h);
	if (!ctx.Rip)
		return;
	uint8_t *rip = (uint8_t *)ctx.Rip;
	uint32_t key = 0;
	char mod[24] = "exe";
	if (rip >= text_lo && rip < text_hi) {
		key = (uint32_t)(rip - base);
	} else {
		HMODULE m;
		char path[MAX_PATH];
		strcpy(mod, "?");
		if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		                       (LPCSTR)rip, &m) && GetModuleFileNameA(m, path, MAX_PATH)) {
			const char *s = strrchr(path, '\\');
			snprintf(mod, sizeof mod, "%s", s ? s + 1 : path);
		}
		for (size_t k = 0; k < n; k++) {
			uint8_t *v = (uint8_t *)s_stack[k];
			if (v >= text_lo + 8 && v < text_hi && looks_ret(v)) {
				key = (uint32_t)(v - base);
				break;
			}
		}
	}
	s_total++;
	for (int k = 0; k < S_MAX; k++) {
		if (s_tab[k].n && s_tab[k].rva == key && !strcmp(s_tab[k].mod, mod)) {
			s_tab[k].n++;
			return;
		}
		if (!s_tab[k].n) {
			s_tab[k].rva = key;
			s_tab[k].n = 1;
			strcpy(s_tab[k].mod, mod);
			return;
		}
	}
}

static void text_init(void)
{
	IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
	IMAGE_SECTION_HEADER *s = IMAGE_FIRST_SECTION(nt);
	for (int i = 0; i < nt->FileHeader.NumberOfSections; i++, s++)
		if (!memcmp(s->Name, ".text", 6)) {
			text_lo = base + s->VirtualAddress;
			text_hi = text_lo + s->Misc.VirtualSize;
		}
}

static void diag_init(void)
{
	main_h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, main_thread);
}

// heartbeat: calls per second of each hook (main thread / all), main thread samples and, every
// 6 heartbeats, the most frequent call sites on the main thread. Picks the windowless pump.
static void heartbeat(DWORD ms, int sites)
{
	static LONG64 m0[G_N], p0;
	char buf[1024];
	int len = 0;
	double rate[G_N];
	for (int i = 0; i < G_N; i++) {
		LONG64 m = g_main[i];
		rate[i] = (m - m0[i]) * 1000.0 / ms;
		if (m != m0[i])
			len += snprintf(buf + len, sizeof buf - len, " %s %.0f", g_imp[i].fn, rate[i]);
		m0[i] = m;
	}
	const char *canal = game_wnd ? "janela" : pump_hook >= 0 ? g_imp[pump_hook].fn : "nenhum";
	rlog("batimento: canal=%s fila=%d bomba=%lld/s | chamadas/s na thread principal:%s", canal, q_count,
	     (long long)((pump_runs - p0) * 1000 / ms), buf);
	p0 = pump_runs;
	if (s_total) {
		len = 0;
		for (int r = 0; r < 4; r++) {
			int best = -1;
			for (int k = 0; k < S_MAX; k++)
				if (s_tab[k].n && (best < 0 || s_tab[k].n > s_tab[best].n))
					best = k;
			if (best < 0)
				break;
			len += snprintf(buf + len, sizeof buf - len, " %s%s%sexe+0x%X %d%%", r ? "," : "",
			                strcmp(s_tab[best].mod, "exe") ? s_tab[best].mod : "",
			                strcmp(s_tab[best].mod, "exe") ? " via " : "", s_tab[best].rva,
			                s_tab[best].n * 100 / s_total);
			s_tab[best].n = 0;   // already listed (the table is cleared right below)
		}
		rlog("batimento: thread principal (%d amostras):%s", s_total, buf);
		memset(s_tab, 0, sizeof s_tab);
		s_total = 0;
	}
	if (sites)
		for (int i = 0; i < G_N; i++) {
			len = 0;
			for (int k = 0; k < G_SITES && g_site[i][k].n; k++)
				len += snprintf(buf + len, sizeof buf - len, " exe+0x%X=%u", g_site[i][k].rva, g_site[i][k].n);
			if (len)
				rlog("batimento: pontos de %s na principal:%s", g_imp[i].fn, buf);
		}
	// every heartbeat: the preferred site the main thread called >= 10 times per second. During loading the
	// main thread sits in SwitchToThread (busy wait, not usable: a site in the middle of loading); after it, in a
	// PeekMessageW loop (test of 2026-10-07)
	if (no_graphics && !game_wnd && !pump_forced)
		for (int k = 0; k < G_PUMP_CAND; k++)
			if (g_orig[g_pref[k]] && rate[g_pref[k]] >= 10) {
				if (pump_hook != g_pref[k]) {
					pump_hook = g_pref[k];
					rlog("bomba: fila atendida em %s (%.0f chamadas/s na thread principal)", g_imp[pump_hook].fn,
					     rate[pump_hook]);
				}
				break;
			}
}

// ---- core load and reload ----
static ar_host host;
static HMODULE core_mod;
static char core_live[MAX_PATH + 64];
static FILETIME core_time;
static int core_seq;

static void core_unload(void)
{
	if (!core_mod)
		return;
	core = NULL;
	for (int i = 0; i < 400 && core_inflight > 0; i++)
		Sleep(5);
	ar_stop_fn stop = (ar_stop_fn)GetProcAddress(core_mod, "ar_stop");
	if (stop)
		stop();
	FreeLibrary(core_mod);
	core_mod = NULL;
	DeleteFileA(core_live);
	rlog("nucleo descarregado");
}

static void core_load(void)
{
	// with the pid: another instance may have its own copy loaded
	snprintf(core_live, sizeof core_live, "%sck3_custom_core_live_%lu_%d.dll", dll_dir, GetCurrentProcessId(), ++core_seq);
	if (!CopyFileA(core_path, core_live, FALSE)) {
		rlog("nucleo: copia falhou (erro %lu)", GetLastError());
		return;
	}
	core_mod = LoadLibraryA(core_live);
	if (!core_mod) {
		rlog("nucleo: LoadLibrary falhou (erro %lu)", GetLastError());
		return;
	}
	ar_start_fn start = (ar_start_fn)GetProcAddress(core_mod, "ar_start");
	const ar_core *c = start ? start(&host) : NULL;
	if (!c || c->version != AR_API_VERSION) {
		rlog("nucleo: ar_start ausente ou versao diferente");
		FreeLibrary(core_mod);
		core_mod = NULL;
		return;
	}
	core = c;
	rlog("nucleo carregado (%s)", core_live);
}

static DWORD WINAPI worker_thread(LPVOID arg)
{
	core_load();
	WIN32_FILE_ATTRIBUTE_DATA f0;
	if (GetFileAttributesExA(core_path, GetFileExInfoStandard, &f0))
		core_time = f0.ftLastWriteTime;
	// cleans up old copies (loaded ones, from this or another instance, cannot be deleted)
	char pat[MAX_PATH + 64];
	snprintf(pat, sizeof pat, "%sck3_custom_core_live_*.dll", dll_dir);
	WIN32_FIND_DATAA fd;
	HANDLE fh = FindFirstFileA(pat, &fd);
	if (fh != INVALID_HANDLE_VALUE) {
		do {
			char old[MAX_PATH * 2];
			snprintf(old, sizeof old, "%s%s", dll_dir, fd.cFileName);
			DeleteFileA(old);
		} while (FindNextFileA(fh, &fd));
		FindClose(fh);
	}
	DWORD hb_last = GetTickCount();
	int hb_n = 0;
	for (int tick = 0;; tick++) {
		Sleep(250);
		if (tick % 4 == 0)
			cfg_watch();
		if (diag) {
			sample_main();
			DWORD now = GetTickCount();
			if (now - hb_last >= 5000) {
				heartbeat(now - hb_last, ++hb_n % 6 == 1);
				hb_last = now;
			}
		}
		// status every second (without graphics every 250 ms: pause on load), ck3_custom\status.json, only with a channel
		int ph = pump_hook;
		if (debug_mode && (no_graphics || tick % 4 == 2) && core && (!no_graphics || ph >= 0) && q_count < Q_MAX / 2) {
			char st[64];
			snprintf(st, sizeof st, "status %s", game_wnd || ph < 0 ? "janela" : g_imp[ph].fn);
			q_push(st);
		}

		// new or changed core? (only in -debug_mode)
		WIN32_FILE_ATTRIBUTE_DATA fa;
		if (debug_mode && GetFileAttributesExA(core_path, GetFileExInfoStandard, &fa) &&
		    CompareFileTime(&fa.ftLastWriteTime, &core_time) != 0) {
			Sleep(300);   // wait for the copy to finish
			WIN32_FILE_ATTRIBUTE_DATA fb;
			if (GetFileAttributesExA(core_path, GetFileExInfoStandard, &fb) &&
			    CompareFileTime(&fa.ftLastWriteTime, &fb.ftLastWriteTime) == 0 && !q_count) {
				core_time = fa.ftLastWriteTime;
				core_unload();
				core_load();
			}
		}

		// request through the file? (only in -debug_mode: in production nobody drives the game from outside)
		FILE *f = debug_mode ? fopen(cmd_path, "r") : NULL;
		if (f) {
			char line[512] = "";
			if (!fgets(line, sizeof line, f))
				line[0] = 0;
			fclose(f);
			DeleteFileA(cmd_path);
			line[strcspn(line, "\r\n")] = 0;
			if (line[0] && !q_push(line))
				rlog("fila cheia, pedido descartado: %s", line);
		}
		if (!q_count)
			continue;

		// stalled queue (game loading, closing or window recreated): discard
		if (GetTickCount() - q_since > 8000) {
			char line[512];
			while (q_pop(line))
				if (strncmp(line, "status ", 7) != 0)
					rlog("pedido sem resposta em 8 s, descartado: %s", line);
			continue;
		}
		if (pump_hook >= 0)
			continue;   // no window: the pump serves it on the main thread
		if (game_wnd && (!IsWindow(game_wnd) ||
		                 (WNDPROC)GetWindowLongPtrW(game_wnd, GWLP_WNDPROC) != ar_wndproc)) {
			rlog("janela do jogo mudou; procurando de novo");
			game_wnd = NULL;
		}
		if (!game_wnd) {
			HWND w = NULL;
			EnumWindows(find_wnd, (LPARAM)&w);
			if (!w)
				continue;   // still no window: retry until the 8 s deadline
			old_wndproc = (WNDPROC)SetWindowLongPtrW(w, GWLP_WNDPROC, (LONG_PTR)ar_wndproc);
			game_wnd = w;
			rlog("janela do jogo %p, WndProc substituido", game_wnd);
		}
		PostMessageW(game_wnd, WM_AR_COMMAND, 0, 0);
	}
	return 0;
}

// ---- custom script effects (docs/dll/re/notas_efeitos.md) ----
// Each effect declared in the plugin/plugin.txt of an active mod becomes an army-scope effect with a value
// (`nome = 123`), or an any-scope effect (`nome livre`). The object is a real game add_loot with its execute function
// swapped; execution is forwarded to the core (on_effect), which hands it to the plugin. The loader also registers
// `ck3_custom_ping` (any scope), belonging to the core itself (proofs and tests).
#define RVA_INIT_SLOT        0x40024f8   // initializer table: add_loot registration entry
#define RVA_INIT_ADD_LOOT    0x5c4300
#define RVA_FN_NAMES         0x3b587c0   // () -> name registry
#define RVA_FN_NAME_ID       0x3b58280   // (registry, &text) -> name number
#define RVA_EFFECT_DB        0x570f6f0
#define RVA_FN_EFFECT_DB     0x337e050
#define RVA_FN_EFFECT_REG    0x337e6b0   // (database, number, entry)
#define RVA_FN_ADD_LOOT_NEW  0x2e81070   // add_loot factory: (entry) -> object
#define RVA_VT_ADD_LOOT      0x443cad8
#define RVA_FN_ADD_LOOT_EXEC 0x2e80510
#define RVA_FN_NO_DESC       0x7e6590    // empty description (the same as the debug effects)
#define RVA_FN_ANY_SCOPE     0x7e9230    // scope type 0 (the same as the debug effects): accepts any scope
#define RVA_FN_VAR_POOL      0x3b970f0   // () -> variable name registry
#define RVA_FN_VAR_NAME      0x3b96da0   // (registry, &number, &text)
#define RVA_FN_VAR_KNOWN     0x3382560   // (database+0xd8, number): marks the variable as "written by some script"
#define FX_SLOT_SCOPE        5
#define RVA_FN_VALUE         0x9698b0    // (object+0x60, &out, context, [context+0x28], object+0x10)
#define FX_SLOTS             24
#define FX_SLOT_EXEC         22
#define FX_SLOT_DESC         23
#define FX_MAX               32

typedef struct fx_entry {
	void **vt;
	const char *name;
	int32_t id;
	int32_t pad;
} fx_entry;

static char fx_names[FX_MAX][48];
static fx_entry fx_entries[FX_MAX];
static int fx_count;
static void *fx_entry_vt[2];
static void *fx_vt[1 + FX_SLOTS];       // army scope; [0] = RTTI pointer, as in the original table
static void *fx_vt_any[1 + FX_SLOTS];   // any scope (line `nome livre` in the file)
static char fx_vars[64][48];            // response variable names (line `var nome`)
static int fx_var_count;
static CRITICAL_SECTION fx_lock;

static void *fx_entry_dtor(fx_entry *e, int flags) { return e; }   // our own memory: nothing to free

static uint8_t *fx_factory(fx_entry *e)
{
	uint8_t *obj = ((uint8_t *(*)(fx_entry *))(base + RVA_FN_ADD_LOOT_NEW))(e);
	*(void ***)obj = (e->pad ? fx_vt_any : fx_vt) + 1;
	return obj;
}

static __attribute__((noinline)) void fx_exec(uint8_t *obj, void **ctx)
{
	int32_t id = *(int32_t *)(obj + 8);
	const char *name = "?";
	for (int i = 0; i < fx_count; i++)
		if (fx_entries[i].id == id)
			name = fx_entries[i].name;
	EnterCriticalSection(&fx_lock);
	if (__builtin_setjmp(fx_guard_buf) == 0) {
		fx_guard_thread = GetCurrentThreadId();
		uint8_t *scope = ctx[0];
		uint32_t type = *(uint16_t *)scope, sid = *(uint32_t *)(scope + 8);
		int64_t value = 0;
		((void (*)(void *, int64_t *, void *, void *, void *))(base + RVA_FN_VALUE))(obj + 0x60, &value, ctx, ctx[5],
		                                                                           obj + 0x10);
		const ar_core *c = core_enter();
		if (c) {
			if (c->on_effect)
				c->on_effect(name, type, sid, value, ctx, GetCurrentThreadId());
			core_leave();
		} else {
			rlog("efeito %s chamado sem nucleo carregado", name);
		}
		fx_guard_thread = 0;
	} else {
		fx_guard_thread = 0;
		rlog("FALHA CONTIDA no efeito %s: codigo=0x%lX rip=exe+0x%llX endereco=0x%llX pilha(exe+):%s", name,
		     guard_fault_code, (unsigned long long)(guard_fault_rip - (DWORD64)base),
		     (unsigned long long)guard_fault_addr, guard_stack);
	}
	LeaveCriticalSection(&fx_lock);
}

static __attribute__((noinline)) void fx_register(void)
{
	if (__builtin_setjmp(fx_guard_buf) != 0) {
		fx_guard_thread = 0;
		rlog("FALHA CONTIDA no registro de efeitos: codigo=0x%lX rip=exe+0x%llX endereco=0x%llX", guard_fault_code,
		     (unsigned long long)(guard_fault_rip - (DWORD64)base), (unsigned long long)guard_fault_addr);
		return;
	}
	fx_guard_thread = GetCurrentThreadId();
	void *names = ((void *(*)(void))(base + RVA_FN_NAMES))();
	void *db = *(void **)(base + RVA_EFFECT_DB);
	if (!db) {
		((void (*)(void))(base + RVA_FN_EFFECT_DB))();
		db = *(void **)(base + RVA_EFFECT_DB);
	}
	rlog("registro de efeitos: thread=%lu nomes=%p banco=%p", GetCurrentThreadId(), names, db);
	for (int i = 0; db && i < fx_count; i++) {
		struct { const char *p; uint32_t len; uint8_t flag; } __attribute__((aligned(16))) text =
			{ fx_names[i], (uint32_t)strlen(fx_names[i]), 0 };
		fx_entry *e = &fx_entries[i];
		e->vt = fx_entry_vt;
		e->name = fx_names[i];
		e->id = ((int32_t (*)(void *, void *))(base + RVA_FN_NAME_ID))(names, &text);
		((void (*)(void *, int32_t, fx_entry *))(base + RVA_FN_EFFECT_REG))(db, e->id, e);
		rlog("efeito registrado: %s (numero %d, %s)", e->name, e->id, e->pad ? "qualquer escopo" : "exercito");
	}
	void *pool = ((void *(*)(void))(base + RVA_FN_VAR_POOL))();
	for (int i = 0; db && i < fx_var_count; i++) {
		struct { const char *p; uint32_t len; uint8_t flag; } __attribute__((aligned(16))) text =
			{ fx_vars[i], (uint32_t)strlen(fx_vars[i]), 0 };
		int32_t id = -1;
		((void (*)(void *, int32_t *, void *))(base + RVA_FN_VAR_NAME))(pool, &id, &text);
		((void (*)(void *, int32_t))(base + RVA_FN_VAR_KNOWN))((uint8_t *)db + 0xd8, id);
	}
	rlog("variaveis de resposta registradas: %d", fx_var_count);
	fx_guard_thread = 0;
}

// replaces the add_loot initializer: runs at game startup, before the scripts are read
static void fx_init_thunk(void)
{
	((void (*)(void))(base + RVA_INIT_ADD_LOOT))();
	fx_register();
}

static void fx_install(void)
{
	void **slot = (void **)(base + RVA_INIT_SLOT);
	void **vt = (void **)(base + RVA_VT_ADD_LOOT);
	if (*slot != base + RVA_INIT_ADD_LOOT || vt[FX_SLOT_EXEC] != base + RVA_FN_ADD_LOOT_EXEC) {
		rlog("efeitos: tabelas nao batem com a versao 1.19.0.6, nada registrado");
		return;
	}
	// manifests: `nome`, `api` and `fonte` belong to the core; `var nome` = response variable; any other line =
	// `efeito [livre]`. Name repeated in another plugin: refused with a warning
	strcpy(fx_names[fx_count], "ck3_custom_ping");
	fx_entries[fx_count++].pad = 1;
	static char dirs[16][MAX_PATH];
	int nd = mod_dirs(dirs, 16), nplug = 0;
	for (int d = 0; d < nd; d++) {
		char path[MAX_PATH + 32];
		snprintf(path, sizeof path, "%s\\plugin\\plugin.txt", dirs[d]);
		char *t = read_file(path, NULL);
		if (!t)
			continue;
		nplug++;
		int ne = 0, nv = 0;
		for (char *line = strtok(t, "\r\n"); line; line = strtok(NULL, "\r\n")) {
			char *c = strchr(line, '#');
			if (c)
				*c = 0;
			char a[64] = "", b[64] = "";
			if (sscanf(line, "%63s %63s", a, b) < 1 || !strcmp(a, "nome") || !strcmp(a, "api") || !strcmp(a, "fonte"))
				continue;
			if (!strcmp(a, "var")) {
				int dup = 0;
				for (int k = 0; k < fx_var_count && !dup; k++)
					dup = !strcmp(fx_vars[k], b);
				if (!dup && b[0] && strlen(b) < sizeof fx_vars[0] && fx_var_count < 64) {
					strcpy(fx_vars[fx_var_count++], b);
					nv++;
				}
				continue;
			}
			int dup = 0;
			for (int k = 0; k < fx_count && !dup; k++)
				dup = !strcmp(fx_names[k], a);
			if (dup)
				rlog("efeitos: %s ja registrado por outro plugin; recusado em %s", a, path);
			else if (strlen(a) < sizeof fx_names[0] && fx_count < FX_MAX) {
				fx_entries[fx_count].pad = !strcmp(b, "livre");
				strcpy(fx_names[fx_count++], a);
				ne++;
			}
		}
		free(t);
		rlog("efeitos: %s: %d efeito(s), %d variavel(is)", path, ne, nv);
	}
	if (!nplug)
		rlog("efeitos: nenhum mod ativo com plugin/plugin.txt");

	InitializeCriticalSection(&fx_lock);
	fx_entry_vt[0] = (void *)fx_entry_dtor;
	fx_entry_vt[1] = (void *)fx_factory;
	memcpy(fx_vt, vt - 1, sizeof fx_vt);
	fx_vt[1 + FX_SLOT_EXEC] = (void *)fx_exec;
	fx_vt[1 + FX_SLOT_DESC] = base + RVA_FN_NO_DESC;
	memcpy(fx_vt_any, fx_vt, sizeof fx_vt);
	fx_vt_any[1 + FX_SLOT_SCOPE] = base + RVA_FN_ANY_SCOPE;

	DWORD old;
	if (!VirtualProtect(slot, sizeof *slot, PAGE_READWRITE, &old)) {
		rlog("efeitos: VirtualProtect falhou, nada registrado");
		return;
	}
	*slot = (void *)fx_init_thunk;
	VirtualProtect(slot, sizeof *slot, old, &old);
	rlog("efeitos: %d nome(s) e %d variavel(is) a registrar na inicializacao do jogo", fx_count, fx_var_count);
}

// ---- mod settings (docs/dll/arquitetura_dll.md, sections 11 and 11.6b; docs/dll/re_notas.md, section C1) ----
// Each active mod (dlc_load.json) with plugin/config.txt declares options. The loader:
// - registers the options in the "mods" group right after the manager is created (before the game reads pdx_settings.txt);
// - after the settings window builds its tabs (the game's settings_layout.txt), appends the "Mods" page,
//   generated from the config.txt files, read from memory;
// - in -debug_mode, rebuilds the options and the page when a config.txt changes (or on the "config recarregar" request);
// - keeps a copy of the "mods" section of pdx_settings.txt and restores it if the game deleted it (opened without the DLL).
// Types: liga, inteiro and real (proof C4); lista and texto not yet.
#define RVA_FN_SETTINGS_CREATE 0x3b44b40   // creates the manager and stores the global
#define RVA_SETTINGS_MANAGER   0x5710d58
#define RVA_FN_SETTING_REG     0x3b44cf0   // (manager, &group, option, n)
#define RVA_FN_BOOL_SETTING    0x3b46730   // (obj 0xF8, &name, &text_key, default, 0)
#define RVA_FN_BOOL_SET        0x3b46990   // (option, value, 1): the same as clicking the checkbox
#define RVA_FN_NEW             0x3e25d74   // the game's operator new
#define BOOL_SETTING_SIZE      0xF8
#define RVA_FN_WIN_BUILD       0x32d7700   // CJominiSettingsWindow: clears the tabs and builds them from settings_layout.txt
#define RVA_VT_MEMORY_FILE     0x4559758
#define RVA_ALLOC_CHAR         0x4fea420   // CPdxTypedNewDeleteAllocator<char>
#define RVA_FN_BUF_COPY        0x3bb5200   // (buffer, text, size)
#define RVA_FN_LEXER_STREAM    0x3b58cf0   // CLexer(lexer, &file, 0)
#define RVA_VT_TEXT_LEXER      0x4556c20
#define RVA_FN_LEXER_DTOR      0x3b58dc0
#define RVA_FN_READER          0x3b8e670   // (reader, &lexer)
#define RVA_FN_READER_DTOR     0x3b8e850
#define RVA_FN_BIND_CAT        0x32d5230   // (section): binds the section's options
#define WIN_PAGES              0xd8        // page vector: pointer, +0x08 capacity, +0x0c count
#define WIN_LAYOUT             0xc8        // sub-object that reads the layout (vtable[3]); appends pages

static const uint8_t SETTINGS_PROLOGUE[9] = {
	0x40, 0x53,                                // push rbx
	0x48, 0x81, 0xEC, 0x90, 0x00, 0x00, 0x00   // sub rsp, 0x90
};
static const uint8_t WIN_BUILD_PROLOGUE[10] = {
	0x48, 0x89, 0x5C, 0x24, 0x10,   // mov [rsp+0x10], rbx
	0x48, 0x89, 0x74, 0x24, 0x18    // mov [rsp+0x18], rsi
};

typedef struct { const char *p; int32_t len; uint8_t flag; } __attribute__((aligned(16))) game_text;

#define CFG_MODS 16
#define CFG_OPTS 128
typedef struct {
	char name[32], title[96], path[MAX_PATH];
	FILETIME t;
} cfg_mod;
enum { OPT_BOOL, OPT_INT, OPT_REAL };
typedef struct {
	char name[96], key[112], cat[96], fmt[64];
	int mod, type, live, late;
	double def, min, max, step;
	uint8_t *obj;
} cfg_opt;
static cfg_mod cfg_mods[CFG_MODS];
static int cfg_nmods;
static cfg_opt cfg_opts[CFG_OPTS];
static int cfg_nopts;
static char user_dir[MAX_PATH];        // ...\Documents\Paradox Interactive\Crusader Kings III\ (with trailing slash)
static char settings_path[MAX_PATH + 32], backup_path[MAX_PATH + 64];
static uint8_t *cfg_win;               // settings window (stored by the build hook)
static uint8_t *cfg_pages[8];          // pages we appended to the window
static int cfg_npages, cfg_defaults_done;

static char *read_file(const char *path, size_t *len_out)
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
	if (!b)
		return NULL;
	b[got] = 0;
	if (len_out)
		*len_out = got;
	return b;
}

static int file_time(const char *path, FILETIME *t)
{
	WIN32_FILE_ATTRIBUTE_DATA fa;
	if (!GetFileAttributesExA(path, GetFileExInfoStandard, &fa))
		return 0;
	*t = fa.ftLastWriteTime;
	return 1;
}

static cfg_opt *cfg_add(int mi, const char *name, const char *key, const char *cat, int type, double def)
{
	cfg_opt *o = NULL;
	for (int i = 0; i < cfg_nopts && !o; i++)
		if (!strcmp(cfg_opts[i].name, name))
			o = &cfg_opts[i];
	if (!o) {
		if (cfg_nopts >= CFG_OPTS)
			return NULL;
		o = &cfg_opts[cfg_nopts++];
		memset(o, 0, sizeof *o);
		snprintf(o->name, sizeof o->name, "%s", name);
	}
	snprintf(o->key, sizeof o->key, "%s", key);
	snprintf(o->cat, sizeof o->cat, "%s", cat);
	if (o->obj && o->type != type)
		rlog("config: %s mudou de tipo; vale o tipo antigo ate reabrir o jogo", name);
	else
		o->type = type;
	o->mod = mi;
	o->def = def;
	o->live = 1;
	return o;
}

// plugin/config.txt (docs/dll/arquitetura_dll.md, 11.3): mod, titulo, grupo, liga, inteiro, real
static void cfg_parse(int mi)
{
	cfg_mod *m = &cfg_mods[mi];
	file_time(m->path, &m->t);
	char *t = read_file(m->path, NULL);
	if (!t) {
		rlog("config: nao abriu %s", m->path);
		return;
	}
	char cat[96] = "";
	int n = 0;
	for (char *line = strtok(t, "\r\n"); line; line = strtok(NULL, "\r\n")) {
		char *c = strchr(line, '#');
		if (c)
			*c = 0;
		char w[8][96] = { "", "", "", "", "", "", "", "" };
		int k = sscanf(line, "%95s %95s %95s %95s %95s %95s %95s %95s", w[0], w[1], w[2], w[3], w[4], w[5], w[6],
		               w[7]);
		if (k <= 0)
			continue;
		if (!strcmp(w[0], "mod") && k >= 2)
			snprintf(m->name, sizeof m->name, "%s", w[1]);
		else if (!strcmp(w[0], "titulo") && k >= 2)
			snprintf(m->title, sizeof m->title, "%s", w[1]);
		else if (!strcmp(w[0], "grupo") && k >= 3)
			snprintf(cat, sizeof cat, "%s", w[2]);
		else if ((!strcmp(w[0], "liga") && k >= 2) || (!strcmp(w[0], "inteiro") && k >= 5) ||
		         (!strcmp(w[0], "real") && k >= 6)) {
			// liga <key> <default> | inteiro <key> <default> <min> <max> [step] [format]
			// | real <key> <default> <min> <max> <step> [format]
			int type = w[0][0] == 'l' ? OPT_BOOL : w[0][0] == 'i' ? OPT_INT : OPT_REAL;
			char name[96], key[112];
			snprintf(name, sizeof name, "%s_%s", m->name, w[1]);
			snprintf(key, sizeof key, "%s_cfg_%s", m->name, w[1]);
			double def = k >= 3 ? atof(w[2]) : 0;
			cfg_opt *o = cfg_add(mi, name, key, cat[0] ? cat : m->title, type, type == OPT_BOOL ? def != 0 : def);
			if (o && type != OPT_BOOL) {
				o->min = atof(w[3]);
				o->max = atof(w[4]);
				o->step = k >= 6 ? atof(w[5]) : 1;
				const char *fmt = k >= 7 ? w[6] : "";
				if (type == OPT_INT && k == 6 && !isdigit((unsigned char)w[5][0]) && w[5][0] != '.') {
					o->step = 1;   // inteiro without step, with format
					fmt = w[5];
				}
				snprintf(o->fmt, sizeof o->fmt, "%s", fmt[0] ? fmt : "SETTING_VALUE_NONE");
				if (o->step <= 0 || o->max < o->min) {
					rlog("config %s: limites invalidos em %s (min %g max %g passo %g)", m->name, name, o->min, o->max,
					     o->step);
					o->live = 0;
				}
			}
			if (o && o->live)
				n++;
		} else
			rlog("config %s: linha ignorada (tipo ainda nao suportado): %s", m->name, line);
	}
	free(t);
	rlog("config %s: %d opcao(oes) em %s", m->name, n, m->path);
}

// active mod folders: dlc_load.json (enabled_mods) -> each .mod -> path=. A mod without path= (e.g. .zip with
// archive=) is left out
static int mod_dirs(char (*dirs)[MAX_PATH], int max)
{
	char p[MAX_PATH * 2];
	snprintf(p, sizeof p, "%sdlc_load.json", user_dir);
	char *js = read_file(p, NULL);
	if (!js) {
		rlog("mods: sem %s", p);
		return 0;
	}
	int n = 0;
	char *e = strstr(js, "\"enabled_mods\"");
	char *q = e ? strchr(e, '[') : NULL, *end = q ? strchr(q, ']') : NULL;
	while (q && end && n < max) {
		char *a = strchr(q, '"');
		if (!a || a > end)
			break;
		char *b = strchr(a + 1, '"');
		if (!b || b > end)
			break;
		q = b + 1;
		char desc[MAX_PATH * 2];
		snprintf(desc, sizeof desc, "%s%.*s", user_dir, (int)(b - a - 1), a + 1);
		char *md = read_file(desc, NULL);
		char *pp = md ? strstr(md, "path=\"") : NULL;
		if (!pp) {
			free(md);
			continue;
		}
		pp += 6;
		char *pe = strchr(pp, '"');
		dirs[n][0] = 0;
		if (pe && pp[0] && pp[1] == ':')
			snprintf(dirs[n], MAX_PATH, "%.*s", (int)(pe - pp), pp);
		else if (pe)
			snprintf(dirs[n], MAX_PATH, "%s%.*s", user_dir, (int)(pe - pp), pp);
		free(md);
		if (dirs[n][0])
			n++;
	}
	free(js);
	return n;
}

// active mods that have plugin/config.txt
static void cfg_discover(void)
{
	static char dirs[16][MAX_PATH];
	int nd = mod_dirs(dirs, 16);
	for (int d = 0; d < nd && cfg_nmods < CFG_MODS; d++) {
		const char *dir = dirs[d];
		cfg_mod *m = &cfg_mods[cfg_nmods];
		memset(m, 0, sizeof *m);
		snprintf(m->path, sizeof m->path, "%s/plugin/config.txt", dir);
		if (GetFileAttributesA(m->path) == INVALID_FILE_ATTRIBUTES)
			continue;
		const char *base_name = strrchr(dir, '/');
		const char *bs = strrchr(dir, '\\');
		if (bs > base_name)
			base_name = bs;
		snprintf(m->name, sizeof m->name, "%s", base_name ? base_name + 1 : dir);
		snprintf(m->title, sizeof m->title, "%s", m->name);
		cfg_parse(cfg_nmods++);
	}
}

// value saved in pdx_settings.txt (`enabled=yes/no` or `value=<number>`); 0 if the option is not in the file
static int cfg_saved(const char *text, const char *name, double *out)
{
	char pat[128];
	snprintf(pat, sizeof pat, "\"%s\"={", name);
	const char *p = text ? strstr(text, pat) : NULL;
	if (!p)
		return 0;
	const char *close = strchr(p, '}'), *en = strstr(p, "enabled="), *va = strstr(p, "value=");
	if (en && (!close || en < close)) {
		*out = strncmp(en + 8, "yes", 3) == 0;
		return 1;
	}
	if (va && (!close || va < close)) {
		*out = atof(va + 6);
		return 1;
	}
	return 0;
}

// value slider (CPdxValueSetting<int> / <float>): the game builds it inline in code, with no constructor; fields taken
// from debug_saves (0x403ef0) and tooltip_delay (0x4045e0). docs/dll/re_notas.md, section C4
#define VALUE_SETTING_SIZE   0x128
#define RVA_VT_INT_SETTING   0x4238228   // and +8: 0x4238218
#define RVA_VT_INT_SETTING2  0x4238218
#define RVA_VT_REAL_SETTING  0x428cb28
#define RVA_VT_REAL_SETTING2 0x428cb18
#define RVA_ALLOC_CALLBACK   0x4fea408   // allocator of the change callback list (+0x28)
#define RVA_FN_STR_ASSIGN    0x7e9530    // std::string::assign(text, size)
#define RVA_FN_ANY_EMPTY     0x9ff600    // empty type of the value fields
#define RVA_FN_TYPE_INT      0x7ea910    // type number (+0xbc)
#define RVA_FN_TYPE_REAL     0x7ea8a0
#define RVA_FN_INT_SET       0xa89250    // (option, &value, 0)
#define RVA_FN_REAL_SET      0xa89430    // (option, &value, 0)

static void game_string(uint8_t *s, const char *text)
{
	memset(s, 0, 0x20);
	*(uint64_t *)(s + 0x18) = 0xf;   // empty std::string (SSO)
	((void (*)(void *, const char *, size_t))(base + RVA_FN_STR_ASSIGN))(s, text, strlen(text));
}

static uint8_t *cfg_new_value_setting(cfg_opt *o)
{
	uint8_t *obj = ((uint8_t *(*)(size_t))(base + RVA_FN_NEW))(VALUE_SETTING_SIZE);
	if (!obj)
		return NULL;
	memset(obj, 0, VALUE_SETTING_SIZE);
	int real = o->type == OPT_REAL;
	*(void **)obj = base + (real ? RVA_VT_REAL_SETTING : RVA_VT_INT_SETTING);
	*(void **)(obj + 8) = base + (real ? RVA_VT_REAL_SETTING2 : RVA_VT_INT_SETTING2);
	*(void **)(obj + 0x28) = base + RVA_ALLOC_CALLBACK;
	game_string(obj + 0x78, o->name);
	game_string(obj + 0x98, o->key);
	*(int32_t *)(obj + 0xbc) = ((int32_t (*)(void))(base + (real ? RVA_FN_TYPE_REAL : RVA_FN_TYPE_INT)))();
	*(uint16_t *)(obj + 0xc0) = 0x101;
	for (int s = 0; s < 3; s++)
		*(void **)(obj + 0xc8 + s * 0x10) = ((void *(*)(void))(base + RVA_FN_ANY_EMPTY))();
	if (real) {
		*(float *)(obj + 0xf8) = (float)o->step;
		*(float *)(obj + 0xfc) = (float)o->min;
		*(float *)(obj + 0x100) = (float)o->max;
	} else {
		*(int32_t *)(obj + 0xf8) = (int32_t)o->step;
		*(int32_t *)(obj + 0xfc) = (int32_t)o->min;
		*(int32_t *)(obj + 0x100) = (int32_t)o->max;
	}
	game_string(obj + 0x108, o->fmt);
	return obj;
}

static void cfg_set_value(cfg_opt *o, double v)
{
	if (o->type == OPT_BOOL)
		((void (*)(void *, uint8_t, int))(base + RVA_FN_BOOL_SET))(o->obj, (uint8_t)(v != 0), 1);
	else if (o->type == OPT_INT) {
		int32_t i = (int32_t)(v < 0 ? v - 0.5 : v + 0.5);
		((void (*)(void *, void *, int))(base + RVA_FN_INT_SET))(o->obj, &i, 0);
	} else {
		float f = (float)v;
		((void (*)(void *, void *, int))(base + RVA_FN_REAL_SET))(o->obj, &f, 0);
	}
}

// registers the options that do not have a game object yet; returns how many
static int cfg_register_pending(int late)
{
	void *mgr = *(void **)(base + RVA_SETTINGS_MANAGER);
	if (!mgr)
		return 0;
	static const char grp[] = "mods";
	game_text g = { grp, 4, 0 };
	int n = 0;
	for (int i = 0; i < cfg_nopts; i++) {
		cfg_opt *o = &cfg_opts[i];
		if (!o->live || o->obj)
			continue;
		uint8_t *obj;
		if (o->type == OPT_BOOL) {
			game_text nm = { o->name, (int32_t)strlen(o->name), 0 }, k = { o->key, (int32_t)strlen(o->key), 0 };
			obj = ((uint8_t *(*)(size_t))(base + RVA_FN_NEW))(BOOL_SETTING_SIZE);
			if (obj)
				((void (*)(void *, void *, void *, uint8_t, int))(base + RVA_FN_BOOL_SETTING))(obj, &nm, &k,
				                                                                              (uint8_t)(o->def != 0), 0);
		} else
			obj = cfg_new_value_setting(o);
		if (!obj)
			break;
		o->obj = obj;
		if (o->type != OPT_BOOL)
			cfg_set_value(o, o->def);   // as the game does right after building the option
		InterlockedIncrement((LONG *)(obj + 0x10));   // our own reference, like the game's option globals
		((void (*)(void *, void *, void *, int))(base + RVA_FN_SETTING_REG))(mgr, &g, obj, 0);
		o->late = late;
		n++;
		rlog("config: opcao mods/%s registrada (%s, %s)", o->name,
		     o->type == OPT_BOOL ? "liga" : o->type == OPT_INT ? "inteiro" : "real",
		     late ? "com o jogo aberto" : "na abertura");
	}
	return n;
}

// applies the value through the game's path: an option without an entry in pdx_settings.txt gets the config.txt default;
// an option registered with the game open (after the file was read) gets the saved value
static void cfg_apply_values(int only_late)
{
	char *text = read_file(settings_path, NULL);
	for (int i = 0; i < cfg_nopts; i++) {
		cfg_opt *o = &cfg_opts[i];
		if (!o->obj || (only_late && !o->late))
			continue;
		double saved = 0;
		int found = cfg_saved(text, o->name, &saved);
		if (found && !o->late)
			continue;   // the game already read it from the file
		double v = found ? saved : o->def;
		cfg_set_value(o, v);
		o->late = 0;
		rlog("config: %s = %g (%s)", o->name, v, found ? "gravado" : "padrao");
	}
	free(text);
}

// "Mods" page: one section per mod (or per config.txt group), only with the options present in config.txt
static int cfg_layout(char *out, size_t cap)
{
	size_t n = (size_t)snprintf(out, cap, "page = {\n\tname = \"SETTINGS_PAGE_MODS\"\n");
	char done[CFG_OPTS] = { 0 };
	int any = 0;
	for (int i = 0; i < cfg_nopts && n < cap; i++) {
		cfg_opt *o = &cfg_opts[i];
		if (done[i] || !o->live || !o->obj)
			continue;
		n += (size_t)snprintf(out + n, cap - n,
		                      "\tcategory = {\n\t\tcategory = \"mods\"\n\t\tname = \"%s\"\n\t\torder = {\n", o->cat);
		for (int j = i; j < cfg_nopts && n < cap; j++) {
			cfg_opt *p = &cfg_opts[j];
			if (done[j] || !p->live || !p->obj || p->mod != o->mod || strcmp(p->cat, o->cat))
				continue;
			n += (size_t)snprintf(out + n, cap - n, "\t\t\t\"%s\"\n", p->name);
			done[j] = 1;
		}
		n += (size_t)snprintf(out + n, cap - n, "\t\t}\n\t}\n");
		any = 1;
	}
	if (n < cap)
		n += (size_t)snprintf(out + n, cap - n, "}\n");
	return any && n < cap ? (int)n : 0;
}

// removes the pages we appended and appends the generated page (text read from memory, CMemoryFile)
static void cfg_inject(uint8_t *win)
{
	int count = *(int32_t *)(win + WIN_PAGES + 0xc);
	uint8_t **pages = *(uint8_t ***)(win + WIN_PAGES);
	for (int k = 0; k < cfg_npages; k++)
		for (int i = 0; i < count; i++)
			if (pages[i] == cfg_pages[k]) {
				memmove(pages + i, pages + i + 1, (size_t)(count - i - 1) * sizeof *pages);
				count--;
				break;
			}
	*(int32_t *)(win + WIN_PAGES + 0xc) = count;   // the memory of the removed pages is not freed
	cfg_npages = 0;

	static char text[32768];
	int len = cfg_layout(text, sizeof text);
	if (!len) {
		rlog("config: nenhuma opcao; aba Mods nao acrescentada");
		return;
	}
	_Alignas(16) uint8_t buf[0x20] = { 0 }, mem[0x60] = { 0 }, lexer[0x100] = { 0 }, reader[0x400] = { 0 };
	*(void **)(buf + 0x10) = base + RVA_ALLOC_CHAR;
	((void (*)(void *, const char *, int))(base + RVA_FN_BUF_COPY))(buf, text, len);
	*(void **)mem = base + RVA_VT_MEMORY_FILE;
	*(int32_t *)(mem + 0x08) = 1;
	*(int64_t *)(mem + 0x10) = 1;
	*(int64_t *)(mem + 0x30) = 0xf;   // empty std::string at +0x18
	*(void **)(mem + 0x38) = buf;
	*(int32_t *)(mem + 0x50) = -1;
	((void (*)(void *, void *, int))(base + RVA_FN_LEXER_STREAM))(lexer, mem, 0);
	*(void **)lexer = base + RVA_VT_TEXT_LEXER;
	((void (*)(void *, void *))(base + RVA_FN_READER))(reader, lexer);
	uint8_t *vis = win + WIN_LAYOUT;
	((void (*)(void *, void *))((*(void ***)vis)[3]))(vis, reader);
	((void (*)(void *))(base + RVA_FN_READER_DTOR))(reader);
	((void (*)(void *))(base + RVA_FN_LEXER_DTOR))(lexer);

	int after = *(int32_t *)(win + WIN_PAGES + 0xc);
	pages = *(uint8_t ***)(win + WIN_PAGES);
	for (int i = count; i < after; i++) {
		uint8_t *pg = pages[i];
		uint8_t **cats = *(uint8_t ***)(pg + 0x30);
		int nc = *(int32_t *)(pg + 0x3c);
		for (int k = 0; k < nc; k++)
			((void (*)(void *))(base + RVA_FN_BIND_CAT))(cats[k]);
		if (cfg_npages < 8)
			cfg_pages[cfg_npages++] = pg;
	}
	rlog("config: aba Mods acrescentada (%d -> %d paginas)", count, after);
}

static uint64_t (*settings_create_orig)(void);
static uint64_t (*win_build_orig)(void *);

static uint64_t settings_create_hook(void)
{
	uint64_t r = settings_create_orig();
	if (__builtin_setjmp(fx_guard_buf) == 0) {
		fx_guard_thread = GetCurrentThreadId();
		rlog("configuracoes: gerenciador=%p, %d opcao(oes) registrada(s)", *(void **)(base + RVA_SETTINGS_MANAGER),
		     cfg_register_pending(0));
		fx_guard_thread = 0;
	} else {
		fx_guard_thread = 0;
		rlog("FALHA CONTIDA no registro de configuracoes: codigo=0x%lX rip=exe+0x%llX endereco=0x%llX",
		     guard_fault_code, (unsigned long long)(guard_fault_rip - (DWORD64)base),
		     (unsigned long long)guard_fault_addr);
	}
	return r;
}

static uint64_t win_build_hook(void *win)
{
	uint64_t r = win_build_orig(win);
	if (__builtin_setjmp(fx_guard_buf) == 0) {
		fx_guard_thread = GetCurrentThreadId();
		cfg_win = win;
		cfg_npages = 0;   // the build cleared the tabs
		if (!cfg_defaults_done) {
			cfg_defaults_done = 1;
			rlog("configuracoes: janela %p montada (trava do gerenciador = %d)", win,
			     *(uint8_t *)(*(uint8_t **)(base + RVA_SETTINGS_MANAGER) + 0x79));
			cfg_apply_values(0);
		}
		cfg_inject(win);
		fx_guard_thread = 0;
	} else {
		fx_guard_thread = 0;
		rlog("FALHA CONTIDA na pagina Mods: codigo=0x%lX rip=exe+0x%llX endereco=0x%llX", guard_fault_code,
		     (unsigned long long)(guard_fault_rip - (DWORD64)base), (unsigned long long)guard_fault_addr);
	}
	return r;
}

// "config recarregar" request (main thread): rereads the config.txt files, registers new options and rebuilds the page
static __attribute__((noinline)) void cfg_reload(void)
{
	if (__builtin_setjmp(guard_buf) != 0) {
		guard_thread = 0;
		rlog("FALHA CONTIDA ao recarregar a configuracao: codigo=0x%lX rip=exe+0x%llX endereco=0x%llX",
		     guard_fault_code, (unsigned long long)(guard_fault_rip - (DWORD64)base),
		     (unsigned long long)guard_fault_addr);
		return;
	}
	guard_thread = GetCurrentThreadId();
	for (int i = 0; i < cfg_nopts; i++)
		cfg_opts[i].live = 0;
	for (int i = 0; i < cfg_nmods; i++)
		cfg_parse(i);
	int n = cfg_register_pending(1);
	cfg_apply_values(1);
	if (cfg_win)
		cfg_inject(cfg_win);
	else
		rlog("config: janela de configuracoes ainda nao montada");
	rlog("config recarregada: %d opcao(oes) nova(s)", n);
	guard_thread = 0;
}

// copy of the "mods" section of pdx_settings.txt (the game opened without the DLL deletes the section)
static void cfg_backup(void)
{
	char *t = read_file(settings_path, NULL);
	char *a = t ? strstr(t, "\n\"mods\"={") : NULL;
	char *b = a ? strstr(a + 1, "\n}") : NULL;
	if (b) {
		FILE *f = fopen(backup_path, "wb");
		if (f) {
			fwrite(a + 1, 1, (size_t)(b + 2 - (a + 1)), f);
			fputc('\n', f);
			fclose(f);
		}
	}
	free(t);
}

static void cfg_restore(void)
{
	char *t = read_file(settings_path, NULL);
	if (t && !strstr(t, "\n\"mods\"={")) {
		char *bk = read_file(backup_path, NULL);
		FILE *f = bk ? fopen(settings_path, "ab") : NULL;
		if (f) {
			fputc('\n', f);
			fputs(bk, f);
			fclose(f);
			rlog("config: secao \"mods\" devolvida ao pdx_settings.txt a partir da copia");
		}
		free(bk);
	}
	free(t);
}

// watcher (worker thread, every ~1 s): copy of pdx_settings.txt and changed config.txt (only in -debug_mode)
static FILETIME settings_time;
static void cfg_watch(void)
{
	FILETIME t;
	if (!settings_path[0])
		return;
	if (file_time(settings_path, &t) && CompareFileTime(&t, &settings_time) != 0) {
		settings_time = t;
		cfg_backup();
	}
	if (!debug_mode)
		return;
	for (int i = 0; i < cfg_nmods; i++)
		if (file_time(cfg_mods[i].path, &t) && CompareFileTime(&t, &cfg_mods[i].t) != 0) {
			cfg_mods[i].t = t;
			rlog("config: %s mudou; recarregando", cfg_mods[i].path);
			q_push("config recarregar");
			break;
		}
}

// executable memory within 2 GB of the game, for the hook's near jump (E9)
static uint8_t *alloc_near(uint8_t *target)
{
	for (uintptr_t a = ((uintptr_t)target & ~(uintptr_t)0xFFFF) - 0x10000; a > (uintptr_t)target - 0x70000000;
	     a -= 0x10000) {
		uint8_t *p = VirtualAlloc((void *)a, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
		if (p)
			return p;
	}
	return NULL;
}

// hook at the start of a game function: the first `len` bytes (whole instructions, no relative addressing)
// go to the trampoline, which *orig then points to
static int hook_install(uint32_t rva, const uint8_t *prologue, int len, void *hook, void **orig, const char *what)
{
	uint8_t *target = base + rva;
	if (memcmp(target, prologue, len) != 0) {
		rlog("%s: bytes em 0x%X nao batem com a versao 1.19.0.6, desvio nao instalado", what, rva);
		return 0;
	}
	uint8_t *page = alloc_near(base);
	if (!page) {
		rlog("%s: sem memoria perto do jogo, desvio nao instalado", what);
		return 0;
	}
	// +0: far jump to the hook; +32: trampoline (original prologue + jump back)
	page[0] = 0xFF; page[1] = 0x25; memset(page + 2, 0, 4);
	*(void **)(page + 6) = hook;
	uint8_t *tramp = page + 32;
	memcpy(tramp, prologue, len);
	uint8_t *j = tramp + len;
	j[0] = 0xFF; j[1] = 0x25; memset(j + 2, 0, 4);
	*(uint8_t **)(j + 6) = target + len;
	*orig = tramp;
	uint8_t patch[16];
	patch[0] = 0xE9;
	*(int32_t *)(patch + 1) = (int32_t)(page - (target + 5));
	memset(patch + 5, 0x90, (size_t)len - 5);
	DWORD old;
	if (!VirtualProtect(target, len, PAGE_EXECUTE_READWRITE, &old)) {
		rlog("%s: VirtualProtect falhou, desvio nao instalado", what);
		return 0;
	}
	memcpy(target, patch, len);
	VirtualProtect(target, len, old, &old);
	FlushInstructionCache(GetCurrentProcess(), target, len);
	rlog("%s: desvio instalado em 0x%X", what, rva);
	return 1;
}

static void cfg_install(void)
{
	char docs[MAX_PATH] = "";
	DWORD sz = sizeof docs;
	if (RegGetValueA(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\User Shell Folders",
	                 "Personal", RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, NULL, docs, &sz) != ERROR_SUCCESS) {
		rlog("config: pasta Documentos nao encontrada; configuracoes dos mods desligadas");
		return;
	}
	snprintf(user_dir, sizeof user_dir, "%s\\Paradox Interactive\\Crusader Kings III\\", docs);
	snprintf(settings_path, sizeof settings_path, "%spdx_settings.txt", user_dir);
	char dir[MAX_PATH + 32];
	snprintf(dir, sizeof dir, "%sck3_custom", dll_dir);
	CreateDirectoryA(dir, NULL);
	snprintf(backup_path, sizeof backup_path, "%sck3_custom\\configuracoes_mods.txt", dll_dir);
	debug_mode = strstr(GetCommandLineA(), "-debug_mode") != NULL;
	cfg_discover();
	rlog("config: %d mod(s) com config.txt, %d opcao(oes); recarga %s", cfg_nmods, cfg_nopts,
	     debug_mode ? "ligada (-debug_mode)" : "desligada");
	if (!cfg_nopts)
		return;
	cfg_restore();
	file_time(settings_path, &settings_time);
	hook_install(RVA_FN_SETTINGS_CREATE, SETTINGS_PROLOGUE, sizeof SETTINGS_PROLOGUE, (void *)settings_create_hook,
	             (void **)&settings_create_orig, "configuracoes (gerenciador)");
	hook_install(RVA_FN_WIN_BUILD, WIN_BUILD_PROLOGUE, sizeof WIN_BUILD_PROLOGUE, (void *)win_build_hook,
	             (void **)&win_build_orig, "configuracoes (janela)");
}

// Game without graphics: runs the commands in ck3_custom\sem_graficos_abrir.txt (one per line, `folder|command`; # comments),
// for example a mod's panel (Auto Raid's auto_raid docs/testes/painel_qt.py). The loader does not know what it is: it just opens it.
static void run_on_nographics(void)
{
	char path[MAX_PATH + 64];
	snprintf(path, sizeof path, "%sck3_custom\\sem_graficos_abrir.txt", dll_dir);
	FILE *f = fopen(path, "r");
	if (!f)
		return;
	char line[1024];
	while (fgets(line, sizeof line, f)) {
		line[strcspn(line, "\r\n")] = 0;
		char *bar = strchr(line, '|');
		if (!line[0] || line[0] == '#' || !bar)
			continue;
		*bar = 0;
		STARTUPINFOA si = { sizeof si };
		PROCESS_INFORMATION pi;
		if (CreateProcessA(NULL, bar + 1, NULL, NULL, FALSE, DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP, NULL, line, &si,
		                   &pi)) {
			CloseHandle(pi.hThread);
			CloseHandle(pi.hProcess);
			rlog("sem graficos: aberto '%s' em %s", bar + 1, line);
		} else {
			rlog("sem graficos: nao abriu '%s' em %s (erro %lu)", bar + 1, line, GetLastError());
		}
	}
	fclose(f);
}

static void install(void)
{
	char exe[MAX_PATH];
	GetModuleFileNameA(NULL, exe, MAX_PATH);
	base = (uint8_t *)GetModuleHandleA(NULL);
	main_thread = GetCurrentThreadId();
	rlog("carregador em %s base=%p thread=%lu", exe, base, main_thread);

	const char *name = strrchr(exe, '\\');
	if (!name || _stricmp(name + 1, "ck3.exe") != 0) {
		rlog("nao e ck3.exe: inativo");
		return;
	}
	IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
	if (nt->OptionalHeader.SizeOfImage < EXE_SIZE_OF_IMAGE_MIN) {
		rlog("imagem pequena demais (0x%lX): nao e o ck3.exe 1.19.0.6, inativo",
		     nt->OptionalHeader.SizeOfImage);
		return;
	}
	uint8_t *target = base + RVA_POST;
	if (memcmp(target, POST_PROLOGUE, sizeof POST_PROLOGUE) != 0) {
		rlog("bytes em 0x%X nao batem com a versao 1.19.0.6: inativo", RVA_POST);
		return;
	}

	// trampoline: original prologue + jump back
	uint8_t *tramp = VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
	if (!tramp) {
		rlog("VirtualAlloc falhou: inativo");
		return;
	}
	memcpy(tramp, POST_PROLOGUE, sizeof POST_PROLOGUE);
	uint8_t *p = tramp + sizeof POST_PROLOGUE;
	p[0] = 0xFF; p[1] = 0x25; memset(p + 2, 0, 4);   // jmp [rip+0]
	*(uint8_t **)(p + 6) = target + sizeof POST_PROLOGUE;
	post_original = (post_fn)tramp;

	uint8_t patch[15];
	patch[0] = 0xFF; patch[1] = 0x25; memset(patch + 2, 0, 4);
	*(void **)(patch + 6) = (void *)post_hook;
	patch[14] = 0x90;

	DWORD old;
	if (!VirtualProtect(target, sizeof patch, PAGE_EXECUTE_READWRITE, &old)) {
		rlog("VirtualProtect falhou: inativo");
		return;
	}
	memcpy(target, patch, sizeof patch);
	VirtualProtect(target, sizeof patch, old, &old);
	FlushInstructionCache(GetCurrentProcess(), target, sizeof patch);
	rlog("interceptacao instalada em 0x%X", RVA_POST);

	host.version = AR_API_VERSION;
	host.base = base;
	host.main_thread = main_thread;
	host.dir = dll_dir;
	host.log = rlog;
	host.post = host_post;
	host.run_main = host_run_main;
	AddVectoredExceptionHandler(1, guard_veh);
	char docs[MAX_PATH] = "";
	DWORD sz = sizeof docs;
	if (RegGetValueA(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\User Shell Folders",
	                 "Personal", RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, NULL, docs, &sz) == ERROR_SUCCESS)
		snprintf(user_dir, sizeof user_dir, "%s\\Paradox Interactive\\Crusader Kings III\\", docs);
	debug_mode = strstr(GetCommandLineA(), "-debug_mode") != NULL;
	no_graphics = strstr(GetCommandLineA(), "-nographics") != NULL;
	char ev[32] = "";
	diag = no_graphics || (GetEnvironmentVariableA("AR_DIAG", ev, sizeof ev) && ev[0] == '1');
	text_init();
	g_install();
	if (diag)
		diag_init();
	if (GetEnvironmentVariableA("AR_BOMBA", ev, sizeof ev))   // forces the pump hook (function name)
		for (int i = 0; i < G_N; i++)
			if (!strcmp(ev, g_imp[i].fn) && g_orig[i]) {
				pump_hook = i;
				pump_forced = 1;
				rlog("bomba: fila atendida em %s (AR_BOMBA)", ev);
			}
	fx_install();
	cfg_install();
	DeleteFileA(cmd_path);
	CloseHandle(CreateThread(NULL, 0, worker_thread, NULL, 0, NULL));
	if (no_graphics)
		run_on_nographics();
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
	if (reason == DLL_PROCESS_ATTACH) {
		DisableThreadLibraryCalls(inst);
		InitializeCriticalSection(&log_lock);
		InitializeCriticalSection(&q_lock);
		GetModuleFileNameA(inst, dll_dir, MAX_PATH);
		char *slash = strrchr(dll_dir, '\\');
		if (slash)
			slash[1] = 0;
		// AR_INSTANCIA=<name>: several games at the same time, each with its own log and its own request file
		// (docs/dll/plano_sem_graficos.md, phase 4)
		char inst_name[32] = "", suf[40] = "";
		if (GetEnvironmentVariableA("AR_INSTANCIA", inst_name, sizeof inst_name) && inst_name[0])
			snprintf(suf, sizeof suf, "_%s", inst_name);
		snprintf(log_path, sizeof log_path, "%sck3_custom_dll%s.log", dll_dir, suf);
		snprintf(cmd_path, sizeof cmd_path, "%sck3_custom_cmd%s.txt", dll_dir, suf);
		snprintf(core_path, sizeof core_path, "%sck3_custom_core.dll", dll_dir);
		load_real();
		install();
	}
	return TRUE;
}
