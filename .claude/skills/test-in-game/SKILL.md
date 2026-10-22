---
name: test-in-game
description: Test a mod in the running CK3 - save, effects, values, triggers, error.log; records evidence in dev/features.json. Use after any mod change.
---

# Test in game

Read the CLAUDE.md rules first ("Game and DLL rules").

## Prepare
1. `ck3_game(action="status")`. Closed: `ck3_game(action="start", save=<test save>)` (waits for the load, ~3 min;
   from the CLI use `run_in_background`). Open: if `plugin.txt` or `dlc_load.json` changed, restart; otherwise
   `ck3_save_load(action="load", save=...)`.
2. Test save: a dedicated save in `save games/` (e.g. `test_<mod>`). Never use the user's own save without asking;
   copy it before changing anything.
3. `ck3_state`: needs `loaded=true` and paused.
4. `ck3_events(answer=True)` if a screen is pending.

## Run
1. `ck3_mark` (stores the log positions).
2. Trigger the mod: `ck3_effect("trigger_event = <prefix>.0001")`, `ck3_effect("<prefix>_effect = yes")`, or unpause
   and let it run (`ck3_pause(False)`, `ck3_speed(5)`) when it depends on on_actions/time.
3. Check the result with `ck3_eval` (values: `gold`, `var:x`, `num_of_...`) and `ck3_trigger` (conditions), on the
   player or on `scope="character:<id>"`.
4. `ck3_errors(mod=<name>, since=<mark>)` and `ck3_dll_log`. An error from the mod means the test failed, even if the
   effect seemed to work.

## Record and report
1. The mod repo's `dev/features.json`: each tested feature becomes `done` with `evidence` = date, command and result
   (e.g. "2026-10-21 ck3ctl eval gold: 150 -> 250; error.log: nothing from mm"), or `blocked` with the reason. The
   hook rejects `done` without evidence or with a dependency that is not `done`.
2. Rewrite `dev/progress.md`: current state, next step, test save used.
3. To the user: a short table (tested, expected, got, new errors). If something failed, continue with the
   `debug-mod` skill.
