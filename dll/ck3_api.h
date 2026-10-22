// ck3_api: interface between the generic core (dll/ar_core.c) and the mod plugins written in C source
// (docs/dll/arquitetura_dll.md, section 5). The plugin does not read game memory: everything goes through this table, using
// game ids (unit, army, province, character) instead of pointers. In the game the core fills it in; in the simulator,
// the simulated world does.
// Script values are fixed point: 100000 = 1. Scope types: 0x4 character, 0x8 province, 0x1b army.
#ifndef CK3_API_H
#define CK3_API_H
#pragma once
#include <stdint.h>

#define CK3_API_VERSION 7   // 2 (2026-10-07): game_files, file_read, file_free; 3: file_write; 4: county_attrition; 5: combat_info; 6: combat_hypo; 7: debug_mode
#define CK3_NONE 0xFFFFFFFFu   // nonexistent id (army of a unit with no army, etc.)

// (api 5) state of a combat (combat_info)
typedef struct {
	uint32_t id, provincia;
	int lado_do_exercito, largura, contador, forcado;
	double vantagem, base, mult;
	uint32_t comandante[2];
	int rolagem[2], rmin[2], rmax[2];
	double lutando[2], total[2], v[2], v_cmd[2], v_mods[2];
} ck3_combat;

typedef struct ck3_api {
	uint32_t version;
	uint32_t main_thread;                                 // game main thread
	void (*log)(const char *fmt, ...);                    // line in the DLL log
	double (*clock_ms)(void);                             // clock in milliseconds (for measuring)
	int (*recording)(void);                               // recording on (request `gravar`)?
	void (*rec)(const char *fmt, ...);                    // JSON line in the recording (no-op while it is stopped)
	uint32_t (*player)(void);                             // player character
	// units (armies on the map)
	uint32_t (*unit_count)(void);                         // size of the unit table
	uint32_t (*unit_at)(uint32_t i);                      // id of the valid unit at index i, 0 = none
	int (*unit_exists)(uint32_t unit);
	uint32_t (*unit_owner)(uint32_t unit);                // owner character
	uint32_t (*unit_army)(uint32_t unit);                 // army of the unit, CK3_NONE without a valid army
	uint32_t (*unit_location)(uint32_t unit);             // current province
	int (*unit_path)(uint32_t unit, uint32_t *out, int max);   // path in progress; returns the total length
	int (*unit_relation)(uint32_t unit);                  // 0 mine, 1 allied, 2 enemy, 3 hostile, 4 neutral, 5 none
	// armies
	uint32_t (*army_unit)(uint32_t army);                 // unit of the army, 0 if it does not exist
	int64_t (*army_loot)(uint32_t army);                  // loot carried
	int64_t (*army_loot_cap)(uint32_t army);              // loot capacity
	int (*army_is_raid)(uint32_t army);                   // raiding army (raid mode)
	int (*army_raid)(uint32_t army, uint32_t *prov, int64_t *done);   // raid in progress: 1, plus location/progress
	// provinces
	uint32_t (*prov_count)(void);                         // valid ids: 1 .. prov_count()-1
	int (*prov_exists)(uint32_t prov);
	int (*prov_xy)(uint32_t prov, int *x, int *y);        // position on the map (pixels)
	int (*prov_is_sea)(uint32_t prov);
	// game primitives (written to the recording with arguments and result)
	int (*eta)(uint32_t unit, uint32_t prov, uint32_t *path, int *path_n, int path_max);   // days, -1 without a path
	int (*eta_from)(uint32_t unit, uint32_t from, uint32_t prov);   // days from `from` to `prov` at the unit's speed
	double (*predict)(const uint32_t *ours, int n1, const uint32_t *theirs, int n2);   // chance of A winning (armies)
	int (*can_raid)(uint32_t army, uint32_t prov);
	int64_t (*prov_loot)(uint32_t prov);                  // available loot
	int (*raid_days)(uint32_t army, uint32_t prov, int64_t done, int64_t *total, int64_t *per_day);
	// script variables on the object (scope type, id); days <= 0 = no expiry. var_get: 1 if it exists, type 1 = number
	int (*var_get)(uint16_t st, uint32_t id, const char *name, int64_t *v, uint16_t *type);
	int (*var_set)(uint16_t st, uint32_t id, const char *name, int64_t v, int days);
	int (*var_set_scope)(uint16_t st, uint32_t id, const char *name, uint16_t vst, uint32_t vid);
	int (*var_del)(uint16_t st, uint32_t id, const char *name);
	// script expression (a value or `{ if = { limit = { ... } add = 1 } }`) evaluated by the game in scope (st, id),
	// with the context `ctx` of the running effect (root and the rest come from it)
	int (*expr)(void *ctx, const char *text, uint16_t st, uint32_t id, int64_t *out);
	// Mods tab options (all of them): in-game name (<mod>_<key>) and value
	int (*cfg_each)(void (*fn)(const char *name, double v, void *arg), void *arg);
	// request line (auto_raid_cmd.txt format: move, halt, stance, raid, split, disband, raise, pause, pan...)
	// to the main thread queue; any thread
	void (*run_main)(const char *line);
	// .txt files of a game folder (e.g. "common/governments") as the game sees them with the active mods (base game
	// and mods in dlc_load.json order; same name replaces; replace_path discards the previous ones), by file name:
	// full paths separated by newlines in out. Returns how many, -1 if they did not fit
	int (*game_files)(const char *rel_dir, char *out, int cap);
	char *(*file_read)(const char *path);                 // contents with a trailing 0, NULL if it did not open; free with file_free
	void (*file_free)(char *p);
	// (api 3) writes the text to ck3_custom/<name> in the game folder, whole (temporary file + swap); with
	// AR_INSTANCIA=<x> the name gets _<x> before the extension. File name only, no folder. Returns 1 if written
	int (*file_write)(const char *name, const char *text, int len);
	// (api 4) county attrition casualties when moving from province `from` to the neighbor `to` (the skull on the
	// path): 0 = no charge, -1 = invalid. Game functions, with the current army size
	int (*county_attrition)(uint32_t army, uint32_t from, uint32_t to);
	// (api 4) path from `from` to `to` through the game pathfinder, at the unit's speed (provinces after `from`);
	// returns the length
	int (*path_from)(uint32_t unit, uint32_t from, uint32_t to, uint32_t *out, int max);
	// (api 5) state of the combat the army is in (auto_raid docs/plano_re_batalha.md 6.1): 1 if there is a combat. Side 0 =
	// attacker. vantagem = base + v[0] - v[1]; v = rolagem + v_cmd (commander, includes their modifiers) + v_mods
	// (the side's block); mult = damage multiplier of the side with the advantage; rolagem is rolled between rmin and
	// rmax every COMBAT_ROLL_DAYS
	int (*combat_info)(uint32_t army, ck3_combat *out);
	// (api 6) hypothetical combat between the attacking army and the defender in the province: v (without rolagem),
	// v_cmd, v_mods, comandante, rmin and rmax for each side from the game functions; the base (combat effects:
	// terrain, crossing, supply...): base = start-of-combat effects (supply, levy gathering, fort/faith, debt); terrain
	// and crossing are left to the caller
	int (*combat_hypo)(uint32_t atacante, uint32_t defensor, uint32_t provincia, ck3_combat *out);
	// (api 7) game started with -debug_mode? Without it (production) plugin log output is discarded and requests
	// through the file are not read
	int (*debug_mode)(void);
} ck3_api;

typedef struct ck3_plugin {
	uint32_t version;
	// mod script effect; returns 1 if handled
	int (*on_effect)(const char *name, uint32_t scope_type, uint32_t scope_id, int64_t value, void *ctx,
	                 uint32_t thread);
	// auto_raid_cmd.txt request the core does not know; returns 1 if handled
	int (*on_command)(char **argv, int argc);
	void (*on_stop)(void);   // before the plugin is replaced or unloaded
} ck3_plugin;

// defined by the plugin
const ck3_plugin *ck3_plugin_start(const ck3_api *api);

#endif
