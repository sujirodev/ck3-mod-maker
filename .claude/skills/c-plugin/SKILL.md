---
name: c-plugin
description: LAST RESORT - C plugin for the ck3_custom DLL. Only when script provably cannot do it and the user approved, or to maintain an existing plugin (Auto Raid).
---

# C plugin

## First: is it really needed?
A C plugin makes the published mod require the DLL: it only runs on `ck3.exe` 1.19.0.6, breaks with every patch and
runs native code on the player's machine. Auto Raid only has a plugin because march orders, the pathfinder and combat
prediction do not exist in script.

Go ahead only if all of these are true:
1. Script was exhausted: effects, triggers, script_values, scripted_guis, game rules, on_actions, and the vanilla
   game (`ck3_vanilla`) shows no other way.
2. You can point at what is missing (the effect/trigger/information that does not exist), checked with `ck3_ref`.
3. The user saw that evidence and the cost of the dependency and approved the plugin.

If any fails, go back to script. Maintaining or fixing an existing plugin does not need this approval.

References: `dll/ck3_api.h` (everything a plugin can call), `docs/dll/arquitetura_dll.md` sections 5 to 7,
`templates/plugin/plugin/` (template), the Auto Raid plugin (`Documents/.../mod/auto_raid/plugin/auto_raid.c`) as a
large example.

## Rules
- The plugin only talks to the game through `ck3_api`, by ids (character 0x4, province 0x8, army 0x1b). No game
  pointers, no Windows API (it compiles with `-nostdlib`; the core exposes `memset`, `memcpy`, `memmove` and a few
  more).
- Script values are fixed point: 100000 = 1.
- New effect: declare it in `plugin/plugin.txt` (`<effect>` = army scope; `<effect> livre` = any scope) along with
  the answer variable (`var <name>`). A new name needs a game restart; the `.c` recompiles by itself in `-debug_mode`.
  The manifest keywords (`nome`, `api`, `fonte`, `var`, `livre`) are Portuguese because the DLL parses them.
- The mod prefix on everything. A name already taken by another plugin is refused.
- TinyCC code is 3 to 5 times slower than gcc -O2: a heavy loop (scanning every province, Dijkstra) calls for a new
  primitive in the core (`dll/ar_core.c` + `ck3_api.h`, bump `CK3_API_VERSION`), offered to every mod.
- `A->log` only writes in `-debug_mode` (api 7).

## Steps
1. Write the effect/request in the `.c` and in `plugin.txt`.
2. `uv run ck3ctl check <mod repo>/plugin/<file>.c` (tcc outside the game; the hook already does it on edit).
3. In game: `ck3_plugin(recompile=True)` -> "compilado e carregado" (compiled and loaded).
4. Test: `ck3_effect("<effect> = 5")` and read the answer with `ck3_eval("var:<var>")`; or `ck3_request("<request>")`.
5. New `ck3_api` function: implement it in the core, `ck3ctl dll build core`, `ck3ctl dll install core` (hot swap),
   check that the Auto Raid plugin still compiles (`ck3_plugin`).
