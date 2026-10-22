---
name: debug-mod
description: Debug a CK3 mod - error.log errors, effect that does nothing, event that never fires, raw keys on screen, FALHA CONTIDA. Use when something in a mod fails in game.
---

# Debug a mod

Never guess the cause: read the mod file and the log first.

## Where to look
| Symptom | Source |
|---|---|
| Script error (scope, unknown trigger, key) | `ck3_errors(mod=...)` (error.log grouped, with file and line) |
| Text shows as a raw key | `ck3_mod(action="check")`: BOM, `l_<language>:`, `_l_<language>.yml` name, key missing in one language |
| Effect "does nothing" | `ck3_effect` returns the game's message; then `ck3_eval`/`ck3_trigger` step by step on the same scope |
| Event never fires | the event's trigger with `ck3_trigger` on the right scope; `ck3_ref(<on_action>, kind="on_action")` shows the expected scope and whether the code fires it |
| C plugin | `ck3_plugin(recompile=True)`: error with file and line; `ck3_dll_log` for the plugin's log |
| FALHA CONTIDA (contained fault) | `ck3_dll_log`: address and stack; game state is suspect, reload the save before going on |
| Mod does not load | `Documents/.../logs/debug.log` (list of enabled mods) and `ck3_mod(action="list")` (linked, enabled) |

## Method
1. Reproduce with the smallest possible effect through `ck3_effect`.
2. Bisect: comment out half the block, wait for the hot reload (`-debug_mode`, 1-2 s), repeat.
3. Compare with the vanilla game: `ck3_vanilla` with the same effect/trigger to see the correct use and scope.
4. Fix, `ck3_mark`, repeat the test, `ck3_errors(since=mark)` clean.

Report: cause (file:line), fix, how it was checked. Update the affected feature in the mod repo's
`dev/features.json` (new evidence, or `blocked` with the reason) and `dev/progress.md`.
