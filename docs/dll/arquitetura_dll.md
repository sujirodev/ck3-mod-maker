Portuguese version: [arquitetura_dll.pt-BR.md](arquitetura_dll.pt-BR.md)

# DLL architecture: generic core + mod plugins in C source

Created on 2026-10-04 at the user's request. Goal: split the DLL into a generic part, to share with the community, and
what is specific to Auto Raid, which moves into the mod folder as a plugin in C source, compiled by the DLL with the
embedded TinyCC. What already works is kept; the change is of location, not of behavior.

Status: architecture confirmed by the user on 2026-10-04, including the plugin in C source (proof in section 8). A2 and
A1 done on 2026-10-05 (sections 10.1 and 10.2). Project name: `ck3_custom` (user's decision, 2026-10-05).
Nothing implemented beyond the proof.

## 1. Layers

```
C:\CK3\binaries\                         generic (community)
  winmm.dll        fixed loader: forwards to the system winmm, intercepts commands, contains faults,
                   finds the plugins of the enabled mods, reads the manifests, registers the effects, loads the core
  ck3_core.dll     generic core: game primitives, commands, development requests,
                   embedded libtcc (compiles and recompiles the plugins)
  ck3\include\     ck3_api.h + TinyCC headers
  ck3\lib\         libtcc1.a (TinyCC support routines)

mod\auto_raid\plugin\                    Auto Raid only
  plugin.txt       manifest
  auto_raid.c      Auto Raid logic in plain C (currently in ar_core.c); may have other .c and .h files
```

| Layer | Contents | Reloads with the game open? |
|---|---|---|
| Loader (`winmm.dll`) | what `ar_proxy.c` does today, plus the plugin search and manifest reading | No |
| Generic core (`ck3_core.dll`) | primitives and requests (section 3), plugin compiler | Yes, in `-debug_mode` |
| Mod plugin (`.c`) | mod effects and rules (section 4) | Yes, in `-debug_mode`: recompiles when the `.c` changes |

libtcc lives in the core, not in the loader, so that a fix in the compiler or in `ck3_api` also reloads with the game
open. The loader only needs to read text (manifests) before the game reads the scripts.

## 2. How the loader finds the plugins

Same reading the game does (checked on this machine on 2026-10-04):
1. `Documents\Paradox Interactive\Crusader Kings III\dlc_load.json`, field `enabled_mods` (e.g.:
   `"mod/auto_raid.mod"`). It is the file the launcher writes and the game reads; the game's `debug.log` lists the same
   decision (`Auto Raid|mod/auto_raid.mod|Enabled`).
2. Each `.mod` in the list: field `path=` (mod folder).
3. If `<path>/plugin/plugin.txt` exists, the mod has a plugin.

A mod disabled in the launcher is not included. The reading happens at startup, before the game reads the scripts,
because the effects need to be registered at that time.

Special cases:
- Mod in a `.zip` (`archive=` instead of `path=`, like the Paradox Mods ones): libtcc compiles from memory, so it is
  possible to read the `.c` from inside the zip. First version: folder only; zip ignored with a warning in the log.
- Steam Workshop mod: the `.mod` points `path=` to the Workshop folder; the same rule applies. Still to check whether
  the Workshop and Paradox Mods accept a mod with `.c` files.

## 3. Generic core (stays in binaries)

What already exists in `ar_core.c` and has nothing of Auto Raid:
- Reading game tables: units, armies, provinces, raids; province position and sea; unit relation.
- Primitives: days and path (`eta_days`, `eta_from`), path in progress (`unit_path`), combat prediction
  (`predict_armies`), can raid, province loot, raid days (`raid_days`), raid capacity.
- Variables: read and write scope variable (`get_var`, `set_scope_var`, `set_scope_var_scope`).
- Commands: move, route, stop, loot mode, raid mode, merge, split, disband, raise, pause, camera.
- Development requests: `info`, `where`, `eta`, `move`, `route`, `units`, `threats`, `peek`, `console`,
  `cmdlog`, `state`, `events`, `salvar`, `carregar`, `cmdraw`.
- New: plugin compiler (section 7).
- New: primitives that today come from the script triggers (supply, men-at-arms, army size, war,
  `can_raid_trigger`, county owner, top liege, independence, `max_military_strength`, realm provinces,
  capital). Needed for the decider in the plugin (docs/simulador.md, 7.1).
- New: generic recorder (docs/simulador.md, section 6): records each primitive call with arguments and result.
  Serves the simulator and any mod as debugging.
- New, if measurement calls for it: heavy loop computations offered to the plugin (e.g.: scanning provinces within a
  radius, sorting candidates), because the core is compiled with `gcc -O2` and the plugin with TinyCC (section 8).

Rule: no `ar_` or `auto_raid_` name inside the core. Requests change name only if the user decides (section 9).

## 4. Auto Raid plugin (goes to the mod folder)

What is in `ar_core.c` today and is specific to the mod:
- `auto_raid_*` effects and their `on_effect`: `estado`, `dias`, `alvo`, `ameaca`, `seguro`, `casa`, `consulta`,
  `fuga`, `mover`, `parar`, `modo_saque`, `modo_raid`, `dividir`, `dissolver`, `levantar`, `pausar`, `camera`,
  `chamar`, `ping`.
- Rules: `threat_scan` (threat group), `top_targets` (targets, A2, A3), `target_safe` (rule 3), `levy_days`
  (C12), `route_slack` and the choice of escape, the `casa`/`fuga` accumulator.
- Mod names: `ar_cfg_*`, `ar_excl_*`, response variables `ar_*`.
- Later (A4 / SIM2): the state machine that is in the script today.

The plugin does not read game memory. Everything goes through the `ck3_api` function table that the core provides
(section 5). That is what lets the simulator compile the same `.c` with another table.

## 5. Interface (`ck3\include\ck3_api.h`)

Evolution of the current `ar_host.h` (version 3):

```c
typedef struct ck3_api {
	uint32_t version;
	void (*log)(const char *fmt, ...);
	// primitives (game ids; -1 = no path / not found)
	int (*eta)(uint32_t unit, uint32_t prov, uint32_t *path, int *path_n, int path_max);
	int (*eta_from)(uint32_t unit, uint32_t from, uint32_t prov);
	int (*unit_path)(uint32_t unit, uint32_t *out, int max);
	double (*predict)(const uint32_t *ours, int n1, const uint32_t *theirs, int n2);
	int (*can_raid)(uint32_t army, uint32_t prov);
	double (*prov_loot)(uint32_t prov);
	int (*raid_days)(uint32_t army, uint32_t prov, double done);
	// ... reading units, provinces, characters, variables, commands
} ck3_api;

typedef struct ck3_plugin {
	uint32_t version;
	void (*on_effect)(const char *name, uint32_t scope_type, uint32_t scope_id, int64_t value, void *ctx,
	                  uint32_t thread);
	void (*on_command)(const char *line);   // plugin requests, with a prefix (e.g. "auto_raid ...")
	void (*on_stop)(void);                  // before the plugin is swapped or unloaded
} ck3_plugin;

// defined by the plugin
const ck3_plugin *ck3_plugin_start(const ck3_api *api);
```

- The arguments are ids (unit, province, army), not game pointers. The core converts. This way the simulator's table
  does not need to imitate game objects.
- The plugin is compiled with `-nostdlib`: it only sees `ck3_api` and what the core exposes by name (`memset`,
  `memcpy`, `memmove`; `snprintf` and math if needed). This avoids accidental use of the Windows API; it is not
  protection against malicious code (section 9).
- `on_post` (intercepting game commands) stays in the core only; the plugin does not receive it.
- API 2 (2026-10-07, docs/perfil_resposta.md): `game_files(pasta, out, cap)` lists the `.txt` files of a game folder
  as the game sees them with the active mods (base game, then the mods in `dlc_load.json` order; a file with the same
  name replaces; `replace_path` in the `.mod` discards the previous ones); `file_read`/`file_free` read one of them.
  `expr` now accepts text of any length (it used to be cut at ~290 characters) and keeps up to 1024 parsed expressions
  (it used to be 96, and besides that each call parsed the text again). An api 1 plugin does not load in core 2 and
  vice versa: swap together `ck3_custom_core.dll`, `ck3_custom\include\ck3_api.h` and the plugin. In the simulator
  (`sim3.py`) the three functions are null and the plugin checks before using them.

## 6. Manifest (`plugin/plugin.txt`)

Same format as today's `auto_raid_effects.txt`, plus the header:

```
nome auto_raid
api 1
fonte auto_raid.c
auto_raid_estado
auto_raid_pausar livre
var ar_alvo
```

- `fonte` (source): one or more `.c` files, relative to the `plugin` folder; the `.h` files in the same folder come in
  via `#include`.
- Effect line: effect name (army scope) or `nome livre` (`livre` = free: any scope).
- `var nome`: response variable (avoids the "used but never set" warning).
- The loader routes each effect to the plugin that declared it. Two plugins with the same effect name: the second is
  rejected with a warning in the log.
- `api` different from the core version: plugin rejected with a warning.

## 7. Compilation and reload

- At startup: the loader registers the effects of all manifests and loads the core; the core compiles each plugin in
  memory (libtcc) and calls `ck3_plugin_start`.
- Compilation error: log with file and line; with no previous plugin, its effects only record in the log that the
  plugin did not load (the script falls into E6, "DLL did not respond").
- With `-debug_mode` on the game's command line:
  - the core watches the timestamps of each plugin's `.c` and `.h` files; changed = recompiles; compiled = `on_stop` of
    the old one, swap, `ck3_plugin_start` of the new one; failed = the old one keeps running;
  - the loader watches `ck3_core.dll` as today (copies `_live_N`, waits for calls in progress, swaps); the new core
    recompiles all plugins.
- Without `-debug_mode`: compiles once at startup and does not watch.
- A new effect or variable in the manifest still requires reopening the game (registration before the scripts are read).
- Swap only between pulses: the core waits until no plugin call is in progress (same `core_inflight` counter as
  today).

## 8. Proof of the plugin in C source (2026-10-04): PASSED
Sources and step by step in `docs/re/prova_tcc/`. TinyCC 0.9.28rc from the official repository (commit `43c7708`).

| Item | Result |
|---|---|
| libtcc compiled with WinLibs GCC and embedded in a test host | OK (static `libtcc.o`; the proof core with libtcc came to 486 KB) |
| `.c` plugin compiled in memory, calling `ck3_api` and responding to an effect | OK |
| Compilation time | small plugin: 1 to 4 ms; the whole `ar_core.c` (2000 lines + `windows.h`): 143 ms via `tcc.exe` |
| Reload when the `.c` changes | OK: v1 -> v2 in 3 ms, the previous one is only discarded after the new one compiles |
| Compilation error | OK: log with file and line (`plugin_erro.c:21: error: ';' expected`), previous plugin keeps responding |
| GCC extensions used in `ar_core.c` | OK: `({ })`, `__typeof__`, `_Alignas`, `__attribute__((aligned))`; the whole `ar_core.c` compiles without errors |
| Performance of the generated code vs `gcc -O2` | 3.2 to 4.6 times slower (Dijkstra 120x120 x 40: 19 ms vs 61 ms) |
| Without the system library (`-nostdlib`) | OK: only what the host exposes (`memset`, `memcpy`, `memmove` + `ck3_api`); calling `ExitProcess` = rejected ("unresolved reference") |
| Inside the game, without reopening | OK: proof core swapped with the game open; request `tcc <arquivo.c>` compiled in 2.7 ms; `effect auto_raid_ping_livre` from the console reached the plugin, which called the game's real `eta` (37 days, same as the `eta` request) and wrote `ar_resp` to the scope; reload v1 -> v2 and compilation error repeated in the game with the same result |

Conclusions:
- The cost of compiling is negligible; it can recompile on every file change.
- What gets slower is the execution of the plugin code, not the loading: TinyCC compiles fast precisely because it
  barely optimizes. That is why heavy loop computation (Dijkstra, large scans) stays in the generic core, compiled
  with `gcc -O2`, and is offered to the plugin through `ck3_api`; the plugin keeps the rules. Today the expensive part
  of the pulse already runs inside the game (pathfinder, combat prediction), so the slowness weighs little. Measure the
  real pulse after A2 (target: average and peak equal to today's, ~7 ms and ~0.7 s, with 20% headroom).
- In the simulator the same `.c` is compiled with `gcc -O2`: TinyCC's slowness does not affect the runs.
- `-nostdlib` with a list of exposed symbols is not a sandbox: C with pointers can still reach anything.

## 9. Open points

1. CLOSED (2026-10-05): name `ck3_custom` (`ck3_custom_core.dll`, folder `ck3_custom\`). The C interface kept the
   name `ck3_api` (`ck3_api.h`).
2. CLOSED (2026-10-05): requests in `ck3_custom_cmd.txt`, responses in `ck3_custom_dll.log`; `arlib.py` adjusted.
3. Security: every active mod with a plugin runs code. Minimal proposal: the community README says so clearly, and
   the core records in the log each compiled plugin (name, path, hash of the sources).
4. Game version: the core only works on 1.19.0.6; each patch changes addresses. Plugins do not depend on that, only on
   `api`.
5. License and repository of the generic part. TinyCC is LGPL: with the core as open source there is no conflict.
6. Workshop and Paradox Mods rules for a mod with `.c` files.
7. (Resolved on 2026-10-07, docs/plano_sem_graficos.md: queue pump in `PeekMessageW`, `efeito` without console, pause
   held after loading.) Game without graphics (`-nographics`), tested on 2026-10-07 (docs/dossie_exercitos.md 9.6): the
   option exists and the loader, the core and the plugin start normally, but the process does not create any window
   and the request channel (`ck3_custom_cmd.txt` -> queue -> `PostMessage(WM_AR_COMMAND)` on the main window,
   `find_wnd` in `ar_proxy.c`) has no destination: every request ends with "no response in 8 s". The simulation also
   showed no sign of advancing (no pulse, memory dropped to 0.8 GB after "Load Save"). Idea for a second attempt:
   serve the queue from inside a call that the main thread already makes without graphics, and that is the proxy itself,
   `winmm.dll` (it exports `timeGetTime` and the like, called every frame); confirm the thread and frequency of the call
   without graphics, serve with the same `core_enter`/`run_command_guarded`, and only then see whether `pause 0` makes
   the days advance. Gains already put to use: request `velocidade <1..5>` in the core and `SEM_JANELA` mode of the
   watcher (`docs/testes/ideal.py`).

## 10. Stages (keeping what works)

| # | Stage | Check |
|---|---|---|
| A1 DONE (10.2) | Loader: plugin search (section 2), manifest reading, registration of each plugin's effects, core reload only in `-debug_mode`. With no plugin found, loads `auto_raid_core.dll` as today | game opens and the cycle runs the same |
| A2 DONE (10.1) | Split `ar_core.c` into `ck3_core.dll` (with libtcc, section 7) + `mod\auto_raid\plugin\auto_raid.c` (sections 3 and 4), without changing behavior; ids instead of pointers in the interface | same response in each effect, compared by the log and the recording (SIM1); pulse cost measured (section 8) |
| A3 | New primitives from the triggers in the generic core | each one checked against the script trigger in the same pulse |
| A4 | Script decisions to the plugin (docs/simulador.md, SIM2) | decides the same as the reference in 100% of pulses |
| A5 | Community and plugin README | installation from scratch following only the README |
| A6 | Mod configuration (section 11): proof C1 to C7, then implementation | section 11.6 |

### 10.1 A2 done (2026-10-05)
- Generic core: `dll/ar_core.c` (still with the name `auto_raid_core.dll`, which the loader looks for; the generic name
  comes with A1). Primitives, commands, variables, script expressions, recorder, Mods tab, development requests and
  the plugin compiler (embedded libtcc; the core came to ~510 KB). Implements `ck3_api` (`dll/ck3_api.h`, version 1)
  by ids.
- Auto Raid plugin: `plugin/plugin.txt` (name, api, sources), `plugin/auto_raid.c` (`auto_raid_*` effects, threat
  group, targets, rule 3, counter-raid, home, escape, `ar_cfg_*` configuration, mod recording lines, requests
  `levas`/`target`/`scan`, decider world) and `plugin/ar_decisor.c/.h` (state machine, SIM2). The core finds the
  plugin through the active mods (`dlc_load.json`), compiles on load (8 to 18 ms) and, in `-debug_mode`, when a
  `.c`/`.h` in the folder changes; compilation error = log with file and line, and the previous plugin keeps running
  (checked in the game). The effects remain registered by the loader from `auto_raid_effects.txt` until A1; the core
  forwards each effect to the first plugin that handles it.
- The core caches the province table at the start of each effect and request: each safe read is a system call
  (`ReadProcessMemory`), and without this the plugin's loops over all provinces cost ~30% more.
- Check (`docs/testes/a2_respostas.py`): on the fixed save `a2_base`, game paused, the same script of effects and
  requests with the old core and with core + plugin; the recording (responses, primitives, targets, levies,
  predictions, queries and the decider pulse) gave 2812 of 2812 identical lines (the script repeated with the same
  core also gives 0 differences). 1-year run of S1: SIM2 checker 371 of 371; average pulse 8.7 ms and peak 658 ms
  (before ~7 to 10 ms and ~700 ms). SIM2a checker with the decider in the new folder: 100% on the 10-year references.
- What was left for A1 is in 10.2.

### 10.2 A1 done (2026-10-05, with a game reopen)
- `ck3_custom` names in `C:\CK3\binaries`: `ck3_custom_core.dll` (copies `ck3_custom_core_live_N.dll`),
  `ck3_custom_cmd.txt`, `ck3_custom_dll.log` and the folder `ck3_custom\` (recording, map, `configuracoes_mods.txt`,
  TinyCC `include\` and `lib\`). The loader remains `winmm.dll`. The old files got the suffix `.antes_a1`
  (`winmm.dll`, `auto_raid_core.dll`, `auto_raid_effects.txt`).
- Loader: finds the active mods (`dlc_load.json`, the same reading as the configuration) and registers the effects and
  variables of each `plugin/plugin.txt`; a name repeated in another plugin is rejected with a warning. The
  `auto_raid_effects.txt` no longer exists. It also registers `ck3_custom_ping` (any scope), from the core itself:
  the `expr`/`ctxprova` proofs moved to it. The core is loaded at startup and is only reloaded on change in
  `-debug_mode`.
- Core: reads the effects of each manifest and delivers each effect to the plugin that declared it.
- In the same reopen the SIM1b monthly snapshot came in (`ar_retrato` in the script, effect `auto_raid_retrato`), only
  with recording on (variable `ar_gravando`, set by the plugin at each pulse).
- Checked after the reopen: 25 effects and 17 variables registered, plugin compiled in 8.8 ms, error.log without mod
  errors; reference `a2_respostas.py` 2812 of 2812 lines identical to the core from before A2; 1-year run of S1 with
  recording: 368 of 368 identical pulses in the checker, average pulse 8.8 ms, peak 652 ms, snapshot with ~406
  provinces per month; core swap with the game open (plugin recompiled along with it); `mudo` turned off the cycle on
  the second pulse without response (E6).

Order with the simulator: SIM1 (recorder) comes before A2, so that the recording of the current behavior serves as the
reference for the split.

## 11. Mod configuration (user request on 2026-10-04)

### 11.1 What is wanted
- The DLL adds a "Mods" tab to the game's settings window.
- The tab lists one button per mod that has configuration; the button shows that mod's options.
- Each mod declares its options in its own structure: which they are, of what type, default, limits and how they
  appear.
- A mod without configuration does not appear. No mod with configuration = the tab does not appear.
- For Auto Raid: global configuration (applies to all saves), with per-save override for the tests.

### 11.2 Path (decided)
A. Native tab (main): the DLL registers the options in the engine's settings registry, in a `mods` group; the page and the sections come from `game/settings_layout.txt` (page > category > order), which the mod replaces with one extra page (found in C2). The window builds the
tabs from `JominiSettingsWindow.AccessPages` (`game/gui/settings/setting_types.gui`) and already has the types
`jomini_setting_bool`, `_enum`, `_value_scrollbar`, `_string`, `_callback` and `jomini_settings_grouping`. The values
are stored in `pdx_settings.txt` in the user folder, written by the game itself (today with the pages "Tools", "game"
etc.). Works in the main menu and during the match, without replacing any `.gui` (no conflict with other mods).
Requires reverse engineering of the registry (proof 11.6).

B. Own window (fallback, if A does not work): a generic GUI shipped with the DLL, opened by a button in the pause
menu; the buttons call `scripted_gui` with DLL effects, which writes a configuration file. Only during the match, and
it replaces `frontend_ingame_menu.gui` (may conflict with mods that touch the same file).

### 11.3 Structure the mod declares (`plugin/config.txt`)
```
mod auto_raid
titulo ar_cfg_titulo                     # localization key of the mod button
grupo seguranca ar_cfg_grupo_seguranca
  real chance 0.80 0.50 0.99 0.01        # type key default minimum maximum step
  inteiro margem 5 0 30
  inteiro horizonte 25 5 60
  inteiro parado 15 5 40
grupo alvo ar_cfg_grupo_alvo
  inteiro raio 150 50 400 10
  real min_ouro_dia 0.5 0 5 0.05
  lista modo_seguro 0 nao sim            # values in order; the stored value is the index
grupo avisos ar_cfg_grupo_avisos
  liga pausa_fuga 1
  liga pausa_guerra 1
```
- Types: `liga <chave> <padrao>` (toggle); `inteiro <chave> <padrao> <min> <max> [passo] [formato]` (integer); `real <chave> <padrao> <min> <max> <passo> [formato]` (real). `formato` (format) = the game's value text key (`SETTING_VALUE_NONE` if missing; e.g.: `SETTING_VALUE_PERCENTAGE`, `SETTING_VALUE_SECONDS`). Done: liga, inteiro, real (C4). Planned: `lista` (enum), `texto` (string).
- Option name in the game: `<mod>_<chave>`, in the `mods` group of `pdx_settings.txt`. File: `plugin/config.txt`.
- Texts: `<mod>_cfg_<chave>` and `<mod>_cfg_<chave>_TOOLTIP` (game convention) in the mod's own localization; tab title `SETTINGS_PAGE_MODS` and section title the key from `titulo` (title) or `grupo` (group) (the game already loads it, because the
  mod is active). A key without text appears by its name.
- The full Auto Raid list comes from the current `ar_configurar` (23 parameters).

### 11.4 Effective value (Auto Raid)
Order: save override > global > `config.txt` default.
- Global: the tab value (in `pdx_settings.txt`).
- Save override: variable `ar_fixo_<chave>` on the player character, set via console or by the tests
  (`docs/testes/arlib.py`, `cfg()`, writes `ar_fixo_*` since 2026-10-05).
- While the decisions are in the script (before A4): at each pulse the core writes the effective value to
  `ar_cfg_<chave>` (in `auto_raid_chamar`), and the script keeps reading `root.var:ar_cfg_*` as today. `ar_configurar`
  keeps creating the `ar_cfg_*` with the defaults (the core only updates the ones that exist); since the override is
  `ar_fixo_*`, the script defaults do not become an override.
- After A4: the plugin reads directly through `ck3_api` (`cfg(mod, chave)`), with no variable in the save.

### 11.5 Interface
Additions to `ck3_api`: `double (*cfg)(const char *mod, const char *key)` and, in `ck3_plugin`,
`void (*on_cfg)(const char *key)` (notifies when the player changes a value). The loader reads the `config.txt` files
along with the manifests (section 2) and registers the tab; with no `config.txt` that has an option, it does not
register it.

### 11.6 Proof (C1 to C7 done; C4 without list and text; C7 at startup to be checked)
| # | Item | How it is checked |
|---|---|---|
| C1 | Find the settings registry in `ck3.exe` 1.19.0.6 | OK (2026-10-04): manager `exe+0x5710d58`, register `0x3b44cf0(gerenciador, &grupo, opcao, n)`, constructors per type, lock after `LoadSettings`; details in `docs/re_notas.md`, section C1. For C2: register right after the manager is created (`0x3b44b40`), with a hook in `winmm.dll` |
| C2 | Register at startup a "Mods" page with an on/off option | OK in memory (2026-10-04): hook in `winmm.dll` at `0x3b44b40` registered `mods/auto_raid_teste` before `pdx_settings.txt` was read, without the lock warning; with the mod's `settings_layout.txt` (vanilla + page `SETTINGS_PAGE_MODS`), `CJominiSettingsWindow` (`+0xd8`) built 4 pages, the fourth "Mods" with 1 option. Visual OK in the main menu: Mods tab with the "Auto Raid" section and "Opcao de teste do Auto Raid" in Portuguese. Findings: the checkbox appears unchecked with default 1 in the constructor (check in C3); with 4 tabs the window goes past the right edge on a 1707 px screen (check whether the original also gets cut) |
| C3 | Value written and read back in `pdx_settings.txt`, including after reopening the game; the game does not erase an option it does not know | PARTIAL (2026-10-04): the game writes on its own, on each change, a new section `"mods"={ "auto_raid_teste"={ version=0 enabled=yes } }`; reopened with the DLL, the value comes back (1 in the three value fields). FAILURE: opened WITHOUT the DLL, the game rewrites the file at startup and erases the `"mods"` section (fix in 11.7). Pending: the constructor default (1) does not apply when the option is not in the file (it appears as 0) |
| C4 | Value bar (integer and real), list and text | PARTIAL (2026-10-05): integer and real OK. `inteiro raio 150 50 400 10` and `real chance 0.8 0.5 0.99 0.01 SETTING_VALUE_PERCENTAGE` appeared as bars (150 and 80%); the user changed them to 400 and 99% and the game wrote `value=400` and `value=0.990000`; reopened, the values came back from the file; `inteiro margem 5 0 30` added with the game open came in through the reload with default 5. List and text are left for later (Auto Raid does not use them): the game's list is a derived class per option (`0x2049010`) with items in a fixed table in the code; text has constructor `0x3b4c290` |
| C5 | Button per mod that switches the displayed options | OK (2026-10-05, without reopening the game, in the core): the Mods tab shows a list of buttons (`CPdxCallbackSetting` `mods_botao_<mod>`, text = the mod's `titulo`; action = `std::function<void()>` of the game's type `_Func_impl_no_alloc<void(*)()>` pointing to a core function); the click (deferred to the main thread queue) replaces the Mods tab with that mod's sections plus a `mods_voltar` button, and selects the tab through the same path as a click on the tab (`0x32d9cf0`). A watcher (core thread) finds the window via the pointer saved by the loader and switches again when the loader's full tab comes back (window rebuilt, config reloaded). When the core is swapped, the buttons point to a game `ret` until the new core reconnects them. Checked by the user: list, Auto Raid, Back, and an option changed and saved |
| C6 | Read the value from the DLL during the match and be notified of the change | OK (2026-10-05, without reopening the game): the core reads the options of the `mods` group directly from the objects (request `cfg` lists; `cfg <nome> <valor>` changes through the interface path). At each pulse, in the player character's `auto_raid_chamar`, it writes `ar_cfg_<chave>` with the tab value, or `ar_fixo_<chave>` if the save has it; only on the 23 `ar_cfg_*` that `ar_configurar` creates. Checked on `auto_raid_teste`: tab raio 250 -> `ar_cfg_raio` 250; `ar_fixo_raio` 200 -> 200; without `ar_fixo` -> 250; tab 300 -> 300. The change applies on the next pulse (that is the "notification") |
| C7 | No `config.txt` with an option: tab not registered | OK with the game open (2026-10-05): `config.txt` replaced with one without options -> the core removed the Mods tab (3 tabs) and the loader, on reload, did not add its own; `config.txt` restored -> 4 tabs with the button list. The core now rereads the `config.txt` files on each swap and the watcher observes their timestamps. At startup (no mod with `config.txt`): according to the code, the loader leaves `cfg_install` before the hooks (registers neither option nor tab); check at the next game reopen that becomes necessary |

Risks: registering a page may require doing it before the window is created (hence in the loader, at startup); the
game may discard from `pdx_settings.txt` options it did not register in a startup without the DLL; each game patch
changes the addresses (like the rest of the core).

### 11.6b Menu built by the DLL, with reload (user request on 2026-10-04): proof C2b
Goal: the mod does not ship `settings_layout.txt`; the DLL generates the "Mods" page from the `config.txt` files and,
in `-debug_mode`, rebuilds the menu when a `config.txt` changes.

How the game builds the tabs (`CJominiSettingsWindow` method at `0x32d7700`, 2nd in the main vtable):
1. clears the tab vector (`+0xd8`, `0x7e9ea0`);
2. reads `settings_layout.txt` (`0x3b589f0` builds the path, `0x3b8e670` reads);
3. hands what was read to the window's reader (object at `+0xc8`, function 0x18 of the vtable), which creates pages
   and sections;
4. for each section of each page (`pagina+0x30` list, `+0x3c` count), binds the options (`0x32d5230`).

Plan:
- Startup: the DLL registers the options (as in C2) and appends a generated page to the layout text; the game's file
  stays intact.
- Reload (`-debug_mode`): `config.txt` changed = registers the new options, applies the values from its own copy
  (11.7, because registration after the lock does not receive the value from `pdx_settings.txt`) and redoes steps 1 to
  4 with the window closed.
- Texts: the mod's localization, reloaded by the game itself in `-debug_mode`.

| # | Item | Status |
|---|---|---|
| C2b.1 | How to append text to the layout: hook on the read (`0x3b8e670`) of `settings_layout.txt`, or the `+0xc8` reader called again with our text (if it appends instead of replacing) | OK (2026-10-04): the reader (`janela+0xc8`, vtable[3] `0x3b8b060`, per `page` block = token `0x2d1` at `0x32d6cc0`) appends pages to the vector `janela+0xd8` without clearing. Text from memory: `CMemoryFile` (vtable `0x4559758`; buffer `{dados, ?, alocador char 0x4fea420}` via `0x3bb5200`), `CLexer` via `0x3b58cf0(lexer, &arquivo, 0)` with the vtable swapped to `CTextLexer` (`0x4556c20`), reader `0x3b8e670(leitor, &lexer)`; then `0x32d5230(secao)` for each new section. Test requests `layout <janela> <arquivo>` and `layout_tirar <janela> <indice>` in the core |
| C2b.2 | Redo steps 1 to 4 with the game open and the window closed, without recreating the window; the interface shows the new tabs when opened | OK (2026-10-04, game open, main menu): removed the tab from the mod's `settings_layout.txt` and added one built by the DLL with the "Idioma" section; on opening the window, the 4th tab showed "Idioma" with the Auto Raid option, without failure. Missing: find the window without the address passed by hand; free the memory of the removed pages |
| C2b.3 | Register a new option after the lock and apply its value through the DLL | OK (2026-10-04): request `opcao mods auto_raid_teste2 AR_CFG_TESTE2 1` with the game open; the game warns `registered after setting lockdown, may get lost` (it only means it does not receive the value from the file); the value is applied by `0x3b46990(opcao, valor, 1)`, the same as the checkbox click (`0x32d6610`); the option appeared in the rebuilt tab and, when changed by the user, was saved to `pdx_settings.txt` together with the other one |
| C2b.4 | Test request `config recarregar` and, later, a `config.txt` watcher in `-debug_mode` | OK (2026-10-05): in the loader (`ar_proxy.c`, section "configuracoes dos mods"). Startup: finds the mods via `dlc_load.json`, reads `plugin/config.txt`, registers the options when the manager is created and, via a hook on the window build method (`0x32d7700`), appends the generated Mods page and saves the window (the game rebuilds the window on its own, e.g.: when reloading localization in `-debug_mode`, and the hook appends again). Reload: the watcher (every ~1 s, only in `-debug_mode`) saw `liga teste3 1` added to `config.txt`, registered the option with the game open, applied the default and rebuilt the tab; the user saw the three options with the right values. The mod no longer ships `settings_layout.txt` |

### 11.7 Fixes the proof called for
- Options erased when the game opens without the DLL (C3): DONE and checked on 2026-10-05 (game opened without the DLL erased the section; opened again with the DLL, the log showed `secao "mods" devolvida ao pdx_settings.txt a partir da copia` before the options were registered). the loader keeps its own copy of the mod sections
  (e.g.: `C:\CK3\binaries\ck3\configuracoes_mods.txt`), updated whenever the game rewrites `pdx_settings.txt`
  with them. At startup, before the game reads the file (in `DllMain`), if the section of a registered mod is missing
  from `pdx_settings.txt`, the loader restores it from the copy.
- Default that does not apply without an entry in the file (C3): DONE via the second path (the loader applies the `config.txt` default, with `0x3b46990`, to options without an entry in the file, right after the window is built; checked with `teste3`). First path, if ever needed: investigate how the game applies the default of its own options (the
  `prompt_for_beta` option also passes 1 in the constructor) or, after `LoadSettings`, write the `config.txt` default to the
  options without an entry through the same interface path.
- Synthetic clicks do not reach the game: Windows does not let a background process bring the game to the foreground
  (the clicks landed in the Claude app). In interface tests, the user clicks; reading is done through memory and the
  file.
