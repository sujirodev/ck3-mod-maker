# ck3-mod-maker

Agent-driven repository for making Crusader Kings III mods. Claude writes the mod, starts the game, tests and debugs
it on its own through the `ck3_custom` DLL, which gives access to the running game (effects, values, events, saves,
logs).

Public repository, built for study: never write secrets, API keys, personal data or game content here (that is why
`reference/` and the binaries stay out of git).

Language: always answer the user in Brazilian Portuguese. Everything written to this repository and to mod
repositories (code, comments, docs, commit messages) is in English; documents meant for people keep a Portuguese
translation next to them as `<name>.pt-BR.md` (update both when you change one).

## Main rule: mods in script; a C plugin is the last resort
The DLL has two roles, and only one of them is free:
- **Development tool (always):** Claude uses the DLL to test and debug any mod (effects, values, events, saves,
  logs). Players never need it because of this.
- **Mod dependency (last resort):** a C plugin (`plugin/plugin.txt` + `.c`) or the Mods tab (`plugin/config.txt`)
  makes the published mod require the DLL, which only works on one exact game build (1.19.0.6), breaks with every
  patch and runs native code on the player's machine. Auto Raid only uses a plugin because there was no other way
  (real march orders, the pathfinder and combat prediction do not exist in script).

Before proposing a C plugin or the Mods tab:
1. Exhaust game script: effects, triggers, script_values, scripted_guis, game rules (`common/game_rules`) for player
   options, decisions, events, on_actions. Look in the vanilla game (`ck3_vanilla`) for how Paradox solves something
   similar.
2. If it still cannot be done, show the user what script cannot reach (with evidence: the missing effect/trigger) and
   the cost of the dependency, and only go ahead with the plugin after the user approves.
3. Player options go into a game rule, not the Mods tab, unless explicitly asked.

## Map
| Folder | What it is |
|---|---|
| `dll/` | Source of the `ck3_custom` DLL (loader `winmm.dll` + core `ck3_custom_core.dll` + `ck3_api.h` for plugins). Requests and effects in `dll/README.md`; architecture in `docs/dll/arquitetura_dll.md`; addresses and reverse engineering in `docs/dll/re*` |
| `tools/ck3ctl/` | Python library, CLI (`uv run ck3ctl ...`) and the `ck3` MCP server (`mcp__ck3__*` tools) |
| `pipeline/` + `.github/workflows/mod-release.yml` | Shared release pipeline (zip, changelog, GitHub Release, Nexus), called by each mod's `release.yml` at a fixed tag (`v1`) |
| `templates/` | `repo` (mod repository, guardian_calc_assistant pattern, with `dev/` and `CLAUDE.md`), `plugin` and `config` (last resort) |
| `reference/` | Generated (not in git): the game's `script_docs` indexed (`ck3ctl ref build`) |
| `.claude/skills/`, `.claude/agents/` | Workflows (new-mod, test-in-game, debug-mod, c-plugin, release-mod) and specialists |

The DLL itself speaks Portuguese and that stays: its request names (`carregar`, `salvar`, `velocidade`, `efeito`...),
log lines (`state: PAUSADO`, `FALHA CONTIDA`...) and the manifest/config keywords (`nome`, `fonte`, `livre`, `liga`,
`inteiro`...) are parsed by ck3ctl, by Auto Raid and by the DLL. Comments in the C sources are English; leave those
string literals as they are.

## Mods are separate repositories
Each mod is its own git repo, by default straight in `Documents\Paradox Interactive\Crusader Kings III\mod\<name>`
(the game reads it from there), following the guardian_calc_assistant pattern: game content at the root; `README.md`,
`CHANGELOG.md`, `CONTRIBUTING.md`, `LICENSE` and `docs/`; `ck3mod.toml` (what goes into the zip); a few-line
`.github/workflows/release.yml` that calls the shared pipeline. The repo is self-contained: it carries its own state in
`dev/` (`features.json`, `progress.md`, `decisions.md`) and a `CLAUDE.md` with its rules, so anyone (person or agent)
can contribute with the mod repo alone. ck3-mod-maker keeps no registry: it finds mods in `mod/` by their
`ck3mod.toml`.
- Create: `ck3ctl mod new <name> "<Title>" --prefix <short> --summary "<one line>"`. A short prefix on everything the
  mod creates (e.g. `tnt_ga`). Existing mod: `ck3ctl mod init <name> --prefix <p>` (adds `ck3mod.toml`, `CLAUDE.md`
  and `dev/` without overwriting anything).
- Push, tag and release of a mod repo are external actions: only when the user asks.
- A `pipeline/` change that alters behavior: new tag (`v2`), and mods migrate one by one by changing the ref in their
  `release.yml`; never move a tag that is already in use.
- First release of a mod: add it to the "Mods built with ck3-mod-maker" table in `README.md` and
  `README.pt-BR.md`; the mod's README carries the "Built with ck3-mod-maker" line.

Machine paths in `tools/ck3ctl/config.py`: game in `C:\CK3` (only build 1.19.0.6, the one the DLL knows), user data
in `Documents\Paradox Interactive\Crusader Kings III`, TinyCC in `~\src\tinycc`.

## How to work
1. Script question (effect, trigger, scope, on_action): `mcp__ck3__ck3_ref` and `ck3_vanilla` before writing. Never
   invent an effect or trigger; the vanilla game in `C:\CK3\game` is the source of truth.
2. After editing a mod file: the hook checks braces, BOM and localization; fix what it reports.
3. Before starting the game: `ck3ctl mod check <name>`.
4. In game: `ck3_mark` -> action -> `ck3_errors(mod=...)` and `ck3_dll_log`. Values and conditions: `ck3_eval` /
   `ck3_trigger`.
5. Choosing the approach: see "Main rule" above. A C plugin only with the user's approval.

## Session (state lives in files, not in the conversation)
Each mod repo has `dev/features.json` (features with status, how to test and evidence) and `dev/progress.md` (current
state and next step, rewritten, never a growing history). The hook validates `features.json`.
- Start: `ck3_doctor`; to work on a mod, `ck3_mod(action="status", name)`.
- During: one feature `in_progress` at a time. `done` only with evidence from a check that was run (date, command,
  result); "seems to work" does not count. No way to test yet: keep it `in_progress` or `blocked` with the reason.
- End (or before stopping on a blocker): update `dev/features.json`, rewrite `dev/progress.md`, lasting decisions
  in `dev/decisions.md`.

## Game and DLL rules (learned on Auto Raid; breaking them costs a game crash)
- Effects or console only with a save loaded and paused: during a load (`state` with speed 0) an effect crashes the
  game (C0000005 at `exe+0xe3ea3e`). `ck3ctl` already checks before `effect`.
- Requests up to ~500 characters (the loader cuts at 511). Longer effect: create a `scripted_effect` in the mod and
  call it.
- Switch saves without restarting: `ck3_save_load(load, name)` (~15 s). Restart the process (~3 min) only when
  required: new loader, new effect/variable in `plugin.txt`, a change to `dlc_load.json`. Batch those needs into a
  single restart.
- With `-debug_mode` the game hot-reloads script, localization and C plugins. Do not edit the mod while the game is
  running a test round (it has crashed the game): pause, edit, reload the save.
- `reload gui` in the console reads a new `.gui` without restarting.
- Synthetic clicks do not reach the game (Windows does not bring it to the front). UI tests: ask the user to click
  and check through the DLL/logs.
- Long game rounds and test batteries: `run_in_background`, never in the foreground.
- Screens that pause the game (events, succession): `ck3_events(answer=True)`; write down what was answered.
- Without graphics (`-nographics`): `console event <id>` does not work; use `effect trigger_event = <id>` or the
  event's content. Default: with graphics.
- Test saves: copy before changing; never touch the user's own saves without asking.
- After "FALHA CONTIDA" (contained fault) in the DLL log the game state may be inconsistent: do not save over
  anything.

## DLL
- `uv run ck3ctl dll build [core|loader|all]` and `dll install`. The core can be swapped with the game open in
  `-debug_mode`; the loader only with the game closed.
- `ck3_api` changed (new function): bump `CK3_API_VERSION`, document it in `dll/ck3_api.h` and `dll/README.md`
  (and `dll/README.pt-BR.md`), and update `CK3_API` in `tools/ck3ctl/validate.py`. A plugin with a different api
  does not load.
- No mod names (`auto_raid_`, `ar_` etc.) inside the core: what belongs to a mod goes into that mod's plugin.
- Auto Raid (`Documents\...\mod\auto_raid`) is the first mod using the DLL; DLL changes must not break it.
