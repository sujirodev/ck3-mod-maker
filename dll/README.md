Portuguese version: [README.pt-BR.md](README.pt-BR.md)

# ck3_custom: generic mod plugin DLL

Source moved from the Auto Raid mod to ck3-mod-maker on 2026-10-21. Auto Raid is still the first mod that uses it
(`Documents/Paradox Interactive/Crusader Kings III/mod/auto_raid`).

Only valid for `ck3.exe` 1.19.0.6. On any other executable the loader stays inactive. Architecture in
docs/dll/arquitetura_dll.md.

## Parts
| File | Role | Swappable with the game open? |
|---|---|---|
| `winmm.dll` (source `ar_proxy.c`, `exports.h`, `proxy.def`) | Fixed loader: forwards to the system `winmm.dll`, intercepts command dispatch, reads requests, guards against crashes, registers the effects declared in the `plugin/plugin.txt` of active mods, builds the Mods tab and loads the core | No (close the game) |
| `ck3_custom_core.dll` (source `ar_core.c`) | Generic core: game primitives, commands, variables, script expressions, recorder, development requests and the plugin compiler (embedded libtcc) | Yes, in `-debug_mode`: just copy the new file to `C:\CK3\binaries`; the loader notices and reloads |
| `ar_host.h` | Loader / core interface | - |
| `ck3_api.h` | Core / mod plugin interface, by ids (docs/dll/arquitetura_dll.md, section 5) | - |
| `<mod>/plugin/` (e.g. `auto_raid/plugin/`: `plugin.txt`, `auto_raid.c`, `ar_decisor.c/.h`) | Mod plugin (Auto Raid's as an example) in C source: `auto_raid_*` effects, mod rules and decision maker; compiled by the core with TinyCC | Yes, in `-debug_mode`: recompiles when a `.c`/`.h` in the folder changes. New effect or variable in `plugin.txt`: reopen the game |
| `gen_exports.py` | Generates `exports.h` and `proxy.def` from the system DLL | - |

## Build (in the `dll` folder, with WinLibs GCC on the PATH)
Through the repo: `uv run ck3ctl dll build [loader|core|all]` (output in `dll/build/`) and
`uv run ck3ctl dll install [core|loader|all]`. By hand:
```
python gen_exports.py winmm
gcc -shared -O2 -Wall -Wno-format-truncation -static -o winmm.dll ar_proxy.c proxy.def
gcc -shared -O2 -Wall -Wno-format-truncation -Wno-restrict -static -I<tinycc> -o ck3_custom_core.dll ar_core.c <tinycc>/win32/libtcc.o
```
`<tinycc>`: TinyCC sources (`https://repo.or.cz/tinycc.git`, commit `43c7708`; on this machine at
`C:\Users\Matheus\src\tinycc`), with `win32/libtcc.o`, `win32/include` and `win32/lib/libtcc1.a` built as in
`dll/re/prova_tcc/README.md`.

Install into `C:\CK3\binaries`: `winmm.dll`, `ck3_custom_core.dll` and the `ck3_custom\` folder with the TinyCC support
files (`include\` = TinyCC's `win32/include` plus `ck3_api.h`; `lib\libtcc1.a`). Plugins live in each mod's folder
(`plugin/plugin.txt`) and are found through the active mods (`dlc_load.json`). Uninstall: delete the two files and the
`ck3_custom` folder.

## Production vs development (2026-10-09)
Without `-debug_mode` (production): the loader does not read `ck3_custom_cmd.txt` nor request `status`, the core
discards the plugins' log (api 7, `debug_mode()`) and the Auto Raid plugin does not run the debug panel (`ar_dbg_*`),
`painel_auto_raid.json` or the attrition check. With `-debug_mode` everything works as before. The release package
(auto_raid: `python docs/pacote.py`) leaves out the mod's test and debugging files (names with `teste`/`debug`, the
debug panel's `gui/hud_sidebars.gui`), docs, DLL sources and copies. The player turns it on and off in Settings >
Mods > Auto Raid > "Ligado" (`auto_raid_ligado`, off by default); there is no decision.

## Usage
- Request: write one line to `C:\CK3\binaries\ck3_custom_cmd.txt`. The loader reads it, deletes the file and runs it
  on the game's main thread. A request the core does not know goes to the plugins.
- Response: `C:\CK3\binaries\ck3_custom_dll.log`.
- Data: `C:\CK3\binaries\ck3_custom\` (`gravacao\`, `mapa\`, `configuracoes_mods.txt`).
- Panel: the Auto Raid plugin writes `ck3_custom\painel_auto_raid.json` on every pulse (API 3, `file_write`);
  auto_raid's `docs/testes/painel.py` shows the game (ruler, armies, decisions and results) without the interface.
- Game without graphics (`-nographics`): there is no window; the queue is served inside `PeekMessageW` on the main
  thread (hook in ck3.exe's import table, chosen by the heartbeat every 5 s). With `-nographics` or `AR_DIAG=1` the
  loader logs heartbeats (calls per second of the hooks and samples of where the main thread is);
  `AR_BOMBA=<function>` forces the hook. See docs/dll/plano_sem_graficos.md.
- If a request or effect causes an access violation, the loader contains the crash and logs "FALHA CONTIDA" with the
  address, instead of bringing the game down. After a contained crash the game state may be inconsistent: do not save.

## Requests
| Request | What it does |
|---|---|
| `info` | Base, player character, table sizes, last commanded unit |
| `where <unidade\|last>` | Current province, owner and current path of the unit |
| `eta <unidade\|last> <provincia>` | Days to the province (the game's tooltip calculation) and the path found |
| `move <unidade\|last> <provincia> [queue] [ia]` | March order (`queue` = goes into the queue; `ia` = sent as the AI, `valid_for` 2: accepted, but ignored on our army) |
| `route <unidade\|last> <p1> <p2> ...` | Route through waypoints: the first replaces, the rest go into the queue |
| `units [all]` | Lists the player's units (or all of them) |
| `threats <unidade\|last> [dias]` | Foreign units whose path crosses our province or our route, with days until the crossing |
| `peek <address \| exe+RVA \| unit:<id> \| prov:<id> \| army:<id>> [bytes]` | Shows memory (safe read) |
| `procura <hex value \| exe+RVA> [max]` | Addresses in the process's own memory that hold the 64-bit value (separate thread, ~80 s; one search at a time) |
| `layout <hex window> <file>` / `layout_tirar <hex window> <index>` | Proof C2b: adds / removes pages of the settings window |
| `config recarregar` | Rereads the mods' `plugin/config.txt`, registers new options and rebuilds the Mods tab (loader; in `-debug_mode` it is automatic when the file changes) |
| `cfg [name value]` | Lists the Mods tab options with their values; with name and value, changes the option through the interface path |
| `mods` / `mods_sel <n>` | Shows the Mods tab button list / the options of mod n (-1 = list) |
| `gravar on <nome>` / `gravar off` / `gravar` | Recording (auto_raid: docs/simulador.md, 6.1) to `ck3_custom\gravacao\<nome>.jsonl` |
| `mapa <unidade> <raio px> <nome> [vizinhanca px]` | Graph of the provinces around the unit from the game's pathfinder (auto_raid: docs/simulador.md, 6.2), to `ck3_custom\mapa\<nome>.jsonl`; use with the game paused |
| `tabela <unidade> <raio px> <nome> <de> <ate> [raio_dest px]` | Extraction bridge (auto_raid: docs/ia_reativa.md, 9.3): the game's exact ETA (pathfinder + travel time, with the unit's speed at the place where it is) from the origins with index [de, ate) in the radius to the provinces up to `raio_dest` px (default 400), to `ck3_custom\mapa\<nome>.bin`; in blocks of a few origins (the game stops while the block runs; `de` = 0 restarts the file) |
| `etafrom <unidade> <de> <ate>` | Days from `de` to `ate` with the unit's speed (speed probe) |
| `var <type> <id> <name> [set <value> [days] \| del]` | Reads (type, value, expiry) or changes a script variable on the scope object (type 4 character, 8 province, 0x1b army) |
| `expr <texto>` / `ctxprova <tipo> <id> <texto>` | Proofs A3 and SIM2b: the game parses the script expression; the core evaluates it on the next `ck3_custom_ping` (on the army / on the given scope, with that effect's context) |
| `console <texto>` | Puts the text in the game console's queue, as if typed (e.g. `console effect ...`) |
| `cmdlog on\|all\|off` | Logs every command sent by the main thread (class name and bytes); `all`: from all threads, army classes (Move, Halt, Raid, Loot, Unit, Army...) with the thread and the whole march (0x168 and the path), other classes by name only |
| `state` | Pause state (player, event/message, other) and speed |
| `events [dump \| answer [opcao]]` | Lists the player's pending events; `answer` answers all of them (default: first option) and closes the succession screen |
| `salvar` | Saves the game (writes `autosave.ck3`, rotating the previous autosaves) |
| `carregar [testar] save games/<nome>.ck3` | Reloads a save with the game open (~15 s); `testar` only checks whether the file exists |
| `velocidade <1..5>` | Writes the game speed (field read by `state`); for windowless runs (`-nographics`) |
| `status <canal>` | Writes `ck3_custom\status.json` (pause, speed, game date at `[game_state]+0x9c`); the loader requests it every second, outside the log (auto_raid's `docs/testes/painel.py` panel) |
| `efeito <texto>` | Runs a script effect on the player character through the function of the `effect` console command (`0xe3e8b0`), without going through the console. Without a console (`-nographics`), `console effect ...` and `console event <id>` (becomes `trigger_event`) go through this path |
| `busca32 <gs\|gs2\|address> <value> [bytes]` | Offsets in the block that hold the 32-bit value (to find fields) |
| `mudo on\|off` | Test: the core stops forwarding effects to the plugins (simulates the game without the DLL) |
| `plugin [recompilar]` | Plugins found and whether they are loaded; `recompilar` compiles again |
| `cmdraw <vtable> <vtable2> <flags> [campos]` | Sends a simple game command (32-bit fields starting at +0x20), for investigation |
| `levas`, `target`, `scan` | From the Auto Raid plugin |

`last` = last unit the player commanded by click since the core was loaded.

## Script effects
The loader registers the effects at game startup, before the scripts are read, from the `plugin/plugin.txt` of each
active mod, plus the core's own `ck3_custom_ping` (any scope). Manifest lines (full format in
docs/dll/arquitetura_dll.md, section 6):
- `nome <mod>`, `api <version>`, `fonte <file.c>`: for the core (mod name, API version, C source file).
- `<efeito>`: army-scope effect with a value (`auto_raid_mover = 7336`, `= var:ar_alvo`).
- `<efeito> livre`: the same, accepted in any scope.
- `var <nome>`: response variable written by the plugin (avoids the "used but never set" warning in error.log).
A name repeated in another plugin is refused with a warning in the log. Execution goes to the core, which hands it to
the plugin that declared the effect; a new effect or variable name requires reopening the game. Addresses in
docs/dll/re/notas_efeitos.md.

Auto Raid effects (`plugin/auto_raid.c`):
| Effect | Response |
|---|---|
| `auto_raid_ping`, `auto_raid_ping_livre`, `auto_raid_chamar = 1` | `ar_resp` = double the value; the player's `chamar` also copies the Mods tab into the `ar_cfg_*` and sets `ar_gravando` when recording is on |
| `auto_raid_chamar = 2` (player character) | runs the decision maker (`plugin/ar_decisor.c`), the cycle's state machine; warnings in `ar_av_<chave>` and `ar_pedido_levantar`, shown by the script's `ar_pos_decisor` |
| `auto_raid_estado` | `ar_saque`, `ar_capacidade`, `ar_provincia`, `ar_movendo`, `ar_destino` (0 = stopped), `ar_saqueando`, `ar_saque_dias` |
| `auto_raid_dias = <provincia>` | `ar_dias` (-1 = no path) |
| `auto_raid_alvo = <raio>` | `ar_alvo` (0 = none), `ar_alvo_prov`, `ar_alvo_levas`, `ar_alvo_ouro_dia`, `ar_alvo_ouro`, `ar_alvo_marcha`, `ar_alvo_saque_dias` |
| `auto_raid_ameaca` | `ar_ameaca` (group size), `ar_ameaca_dias`, `ar_chance`, `ar_perigo` (1 = group with chance below the limit) |
| `auto_raid_seguro = <provincia>` | `ar_seguro` (1 = safe until the end of the raid plus the margin), `ar_chance` |
| `auto_raid_casa`/`auto_raid_fuga = 0` ... `auto_raid_consulta = 1` in each province ... `= 1` | `ar_casa`, `ar_casa_dias` / `ar_fuga`, `ar_fuga_dias`, `ar_fuga_segura` |
| `auto_raid_retrato` (province) | `ret` line in the recording (monthly snapshot, SIM1b) |
| `auto_raid_mover`, `auto_raid_mover_fila`, `auto_raid_parar`, `auto_raid_modo_saque`, `auto_raid_modo_raid`, `auto_raid_dividir`, `auto_raid_dissolver`, `auto_raid_pausar`, `auto_raid_camera`, `auto_raid_levantar` (province) | command |
| `auto_raid_juntar`, `auto_raid_saque_fim` | reserved name |

Configuration read by the plugin from variables on the player character (`ar_cfg_*`, copied from the Mods tab). Test
only: `ar_cfg_teste_todos = 1` treats any foreign unit as hostile; `ar_ia_exp` on the player (reactive AI experiments)
makes the pulse record without running the decision maker; the request `ia_area [raio]` records on the next pulse the
faith, culture, inheritance, alliance and de jure title of each province (friendly area hypotheses). Recording
(auto_raid: docs/ia_reativa.md, 9): a `vel` line per pulse (speed and commander probes), `dn` in the `uni` lines and a
`recusa` line (live entries of the target refusal).

## Mod settings (Mods tab)
The loader finds the active mods (`dlc_load.json`) that have `plugin/config.txt`, registers the options in the `mods`
group of `pdx_settings.txt` and adds the "Mods" tab to the game's settings window, generated from those files. In
`-debug_mode`, a change to `config.txt` rebuilds the options and the tab with the game open. A copy of the `mods`
section is kept in `C:\CK3\binaries\ck3_custom\configuracoes_mods.txt` and restored to `pdx_settings.txt` if the game
deleted it (opened without the DLL). The tab shows one button per mod; clicking shows that mod's options (core). Format
and state: docs/dll/arquitetura_dll.md, section 11. Types: `liga` (toggle), `inteiro` (integer) and `real` (slider).
