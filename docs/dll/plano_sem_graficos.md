Portuguese version: [plano_sem_graficos.pt-BR.md](plano_sem_graficos.pt-BR.md)

# Plan: DLL working with the game without graphics (`-nographics`) and a monitoring panel

Origin: test of 2026-10-07 (docs/dossie_exercitos.md 9.6, docs/arquitetura_dll.md item 7). With `-nographics` the
loader, the core and the plugin start, but the process does not create a window. Every request goes through
`PostMessage(WM_AR_COMMAND)` to the game window (`find_wnd` and `worker_thread` in `dll/ar_proxy.c`), so without a
window nothing is served (discarded after 8 s). `debug.log` stopped in the middle of "Load Save" and memory dropped to
0.8 GB: it is not known whether the simulation runs without graphics.

Verified fact (objdump of ck3.exe 1.19.0.6): the game imports from our `winmm.dll` `timeGetTime`, `timeBeginPeriod`,
`timeEndPeriod`, `waveIn*` and `waveOut*`. Today those exports are a direct `jmp` to the system DLL
(`dll/exports.h`); to serve as a hook, the chosen one has to become a C function.

Rule: none of this is done with a run in progress (memory "do not edit the mod with a run in the game").

## Order
1. Panel (log reading only) - done in `docs/testes/painel.py`.
2. Phase 0 (diagnosis).
3. Status JSON in the core (feeds the panel without depending on the watcher).
4. Phases 1 to 3. Phase 4 optional.

## Phase 0: diagnosis
- Loader: replace the `timeGetTime` `jmp` with a wrapper that counts calls per thread; patch the `ck3.exe` IAT
  for `QueryPerformanceCounter` and `Sleep` (every game loop calls them).
- Worker thread: heartbeat every 5 s in the log with calls per second of each hook on the main thread, and a
  sample of where the main thread is (`SuspendThread` + `GetThreadContext`, `RIP = exe+0x...`).
- Run with `-nographics` and tell apart:
  - main thread in a loop calling a hook: go on to phase 1;
  - main thread stuck at a fixed point (render/SDL): the RIP says where; likely plan B;
  - process went back to the menu or aborted the load.

## Phase 1: request channel without a window
- Split the body of `ar_wndproc` into `q_drain()`; the WndProc keeps calling it (nothing changes with graphics).
- New pump inside the hook chosen in phase 0: only on `main_thread`, at most every ~50 ms, reentrancy lock, same
  `core_enter`/`run_command_guarded`.
- Risk: the WndProc runs at a safe point of the frame; `timeGetTime` may be called in the middle of an update.
  Mitigation: only drain when the return address is the call site of the main loop found in phase 0.
- The 8 s discard and `find_wnd` stay only for the window path. The log states the active channel (`janela` or `bomba`).

## Phase 2: does the simulation run?
- `state`, `velocidade 5`, `pause 0`, count days and pulses. If the days do not advance, the day loop is tied to the
  frame loop: stop and go to plan B.

## Phase 3: fidelity and gain
- `docs/testes/ng_eudes.py`, 60 days, compared by `dossie.py` against the 6 reference recordings. Measure ms per day
  against 153 to 184 ms with graphics.

## Phase 4 (optional): several instances
- `ck3_custom_cmd.txt` and `ck3_custom_dll.log` are unique in the `binaries` folder: names per pid and `arlib` with a
  target. Saves and autosaves are also shared.

## Plan B
Game with minimal graphics and a hidden window: the WndProc channel keeps working; relax the `IsWindowVisible` in
`find_wnd`. Smaller speed gain, but it frees the screen and is safer.

## Panel (`docs/testes/painel.py`)
- `rich` (installed; `textual` is not). Refreshes every ~1 s. Usage: `python docs/testes/painel.py [--out nome] [--uma-vez]`.
- Read only: the request file holds a single line; if the panel sent `state` alongside the watcher, one would step on
  the other. Reads the tail of `ck3_custom_dll.log` (the file is hundreds of MB: starts with the last 4 MB) and the
  run's `saida/<nome>.out`.
- Shows: process (pid, RAM, CPU, with or without a window), request channel (last served, discards), pause state
  and speed, date/gold/war and armies from the watcher's last sample, pulses (ms per day, days per minute, line
  chart), decision maker, contained crashes, last relevant lines of the log and of the run.
- Later: replace the watcher's sample with `ck3_custom_status.json` written by the core every day (temporary +
  rename) and show the phase 0 heartbeat.

## Results (2026-10-07, afternoon)

### Phase 0: diagnosis
Loader with hooks in ck3.exe's import table (`QueryPerformanceCounter`, `Sleep`, `SleepEx`,
`timeGetTime`, `PeekMessageW`, `GetMessageW`, `SwitchToThread`, `WaitForSingleObject(Ex)`), counting only the main
thread (`SwitchToThread` reaches millions per second on the other threads: a shared counter would be contended), samples
of the main thread every 250 ms (suspend, copy the stack, resume) and heartbeat every 5 s with `-nographics` or
`AR_DIAG=1`.

| Situation | Main thread (calls/s) |
|---|---|
| With graphics, menu/load | `PeekMessageW` 60, `Sleep` 59 (frame limiter at exe+0x3E23F50), QPC ~950 |
| With graphics, game running | `PeekMessageW` 110 to 280, `SleepEx` 54 to 140, QPC 120 thousand to 800 thousand |
| Without graphics, load | `SwitchToThread` 0.5 to 2.5 million (busy wait at exe+0x3B67FE0), almost nothing else |
| Without graphics, game | `PeekMessageW` 150 to 260 thousand, `SleepEx` 75 to 110 thousand, QPC 450 to 670 thousand: loop without a limiter |

The previous test (dossie 9.6) only concluded that the channel was dead: loading without graphics works (memory 2.9 to
3.6 GB, `state` answers PAUSADO with the save loaded). The drop to 0.8 GB from that test did not happen again.

### Phase 1: channel without a window
`q_drain()` split out of the WndProc. The pump runs in the hook chosen at each heartbeat (the first, in the order
`PeekMessageW`, `GetMessageW`, `timeGetTime`, `SleepEx`, `Sleep`, QPC, with >= 10 calls/s on the main thread),
at most every 50 ms, with a reentrancy lock. Decision: `SwitchToThread` and the waits were left out of the list; on
the first attempt the pump picked `SwitchToThread` during the load (a point in the middle of the load) and stopped
serving when the load finished. With the new list the pump stays in `PeekMessageW`, the same point of the message loop
where the WndProc runs with graphics. All requests (`state`, `velocidade`, `pause`, `events`, `carregar`, `cfg`...)
respond.

### Console without graphics
The game console (`CConsole`, global 0x570f7b0) is not created with `-nographics`: `console effect ...` (watcher
sample, peace, ideal scenario, configuration) stopped at "console do jogo indisponivel". Solution in the core:
- `0xe3e8b0(resultado, leitor)` is the `effect` console command (the `run` command calls it with a text reader built
  like the core's `layout`). `efeito <texto>` builds the reader and calls it directly; without a console,
  `console effect ...` goes through it. Refuses while the game is loading (speed -1): that was the cause of the crashes
  at exe+0xe3ea3e (memory console-durante-carga-derruba), which happen in the command itself.
- The command writes the result to the console (`0x34b47d0`, console in rcx): permanent detour that returns if rcx is
  null, installed only when there is no console.
- `event <id>` has no safe path: `trigger_event` on the spot, outside the game loop, crashes in the event manager
  (exe+0x356b6ca, called from 0x278d6b0, eventmanager.cpp) after the event runs. `arlib.ligar()` and
  `desligar()` now run the effects of events auto_raid.2/.3 through `effect` (equivalent, in both modes).
- The loader's FALHA CONTIDA line now includes the stack (return addresses in the exe).

### Phase 2: the simulation runs
Yes. Without any request, the game went from 867.01.01 to 867.06.18 in ~1 min of load+run; event pauses show up and
`events answer` answers them. Opening the save: 69 s without graphics versus 139 s with graphics.

### Game date
`[game_state]+0x9c` = days since year 0 (365 per year), found with the new request `busca32` and checked against 35
days of advance. It goes into `status.json`.

### Game starts running without graphics
Without the "Play" screen, the game sends on its own, right after the load, `CAddPlayer`,
`CSetDoneReplayingCommandsCommand`, `CSetObserverCommand` and `CStartGameCommand`, and the days run (the first run
started at 867.01.27, the second at 868.07.01). Fix in the core: `status` (every 250 ms without graphics) pauses on the
loading -> loaded transition and holds the pause until the first requested `pause 0` (pauses again if the game unpauses
on its own). With that the run starts at 867.01.03, like the reference ones (867.01.02). The player is still human
(`is_ai = no` holds in `ligar`).

### Phase 3: fidelity and gain
Same save (`ar_ideal_eudes_pagao`), same script (`ng_eudes.py`, 60 days, `risco_combate` 0.25, dense recording),
same code; `gr_eudes` with graphics (`ng_eudes.py gr_eudes 60 graficos`), at the same time and with the machine equally
busy.

| | gr_eudes (graphics) | ng_eudes3 | ng_eudes4 |
|---|---|---|---|
| save opening | 147 s | 86 s | 83 s |
| ms per game day (median) | 222 | 117 | 126 |
| period / pulses | 867.01.02 to 03.21 / 79 | 867.01.03 to 03.18 / 75 | 867.01.03 to 03.23 / 80 |
| raid lines per pulse / raiding per pulse | 22.3 / 3.8 | 25.8 / 4.6 | 24.0 / 5.6 |
| AI armies / sorties | 49 / 25 | 52 / 29 | 56 / 28 |
| confirmed / truncated choices | 16 / 26 | 17 / 28 | 21 / 25 |

The old d0/d1a/d1b/d1c runs (with graphics, previous code) come in at 23.7 to 27.6 raid lines per pulse, 3.3
to 4.5 raiding per pulse and 49 to 58 units. Conclusion: without graphics the game behaves within the variation between
runs with graphics, opens the save in ~0.6 of the time and runs each day in ~0.55 of the time (1.8 times faster).

### Phase 4: several instances (code ready, test cancelled at the user's request)
- Loader: `AR_INSTANCIA=<nome>` -> `ck3_custom_dll_<nome>.log` and `ck3_custom_cmd_<nome>.txt`; copies of the core
  with the pid in the name (`ck3_custom_core_live_<pid>_<n>.dll`), cleanup by default (loaded ones are not deleted).
- Core: `ck3_custom\status_<nome>.json`.
- `arlib.usar_instancia(nome)`: own paths, `AR_INSTANCIA` passed to the game, `ck3_aberto`/`fechar_jogo` only on the
  process opened by this instance. `ng_eudes.py` uses the instance from the environment. `painel.py --instancia <nome> --pid <n>`.
- Without the variable, everything as before (verified in the runs above with the new loader).
- Pending: the test with two instances (open one, wait for it to load, open the other, so that `continue_game.json` is
  not contended; autosaves are shared).

## Pending items and decisions for the user
- `console event <id>` does not work without graphics (no safe path); use `effect` with the event's content.
- The pump may serve requests in `QueryPerformanceCounter` during the load (until the next heartbeat switches to
  `PeekMessageW`); during the load the watcher only sends `state`.
- The import hooks stay installed always (small cost: one thread comparison per call); the
  heartbeats and samples only with `-nographics` or `AR_DIAG=1`.

## Game panel (2026-10-07, user request: follow the game without the interface)
- The mod's plugin writes `ck3_custom/painel_auto_raid.json` on every daily pulse (function `painel_gravar` in
  plugin/auto_raid.c, after the decision maker): ruler (gold, prestige, piety, age, health, army strength, war,
  whether AI), the mod (on, combat risk) and each army (province, destination, state, soldiers, supply, loot and
  capacity, target with gold/day, march and raid days, candidates and unsafe ones, threat, chance, flight, home, combat).
- Events (the last 40, with the game date): decision maker warnings (the same texts the game shows), target chosen and
  why, raid start, return home (raid finished, after fleeing, gave up on the target), loot delivery, flight
  (to where, threat in how many days, chance of victory), entering and leaving combat (soldiers before and after), new
  army and vanished army (destroyed in combat or disbanded). Return and flight come out on the following pulse, because
  the script fills in home and flight after the decision maker.
- The TinyCC that compiles the plugin in the game does not link file functions (fopen, fclose, rename, vsnprintf): the
  API gained `file_write(nome, texto, tamanho)` (ck3_api.h, api 3), which the core writes into ck3_custom/ with a
  temporary file and swap and with the AR_INSTANCIA suffix. The plugin builds the JSON in a buffer with snprintf.
- `docs/testes/painel.py` (rich): header (date, pause, speed, days per minute, channel, process), ruler, Auto
  Raid, army table with the province name and what each one is doing, colored events and a DLL strip
  (contained crashes, unanswered requests). Province names: `common/landed_titles` + the game's braz_por localization
  (cache in saida/nomes_provincias.json); warning texts: the mod's localization.
- Own window (user request: the CLI looked ugly): `docs/testes/painel_qt.py` (PySide6, installed on
  2026-10-07), dark theme: header (date, running/paused, no graphics, days/min, ms/day, process), cards for the
  ruler (gold, prestige, piety, age, health, war, strength), for Auto Raid and for the raid scoreboard (attempted,
  started, finished without and with combat, interrupted, deliveries and gold delivered, empty returns, flights, combats,
  destroyed), gold chart, army table (supply and loot bars), bar of the time in each state (game days
  and real seconds) and the list of decisions and results. Open: `pythonw painel_qt.py` in docs/testes. The scoreboard
  and the times count from when the window opened (they come from the plugin's events).
- Exact scoreboard (2026-10-07, user request: "precisos igual os que voces estao vendo" - as precise as the ones you
  are seeing): the plugin counts every pulse, since the save was loaded (resets when the date goes back or jumps), with
  the definitions from dossie.py (sortie, raid with combat / interrupted / without combat, delivery with gold, lost
  loot, destroyed) and the time in each state (game days and real seconds); writes `inicio`, `real_s`, `placar` and
  `tempo` to painel_auto_raid.json, and the window only displays them.
  Checked in `ng_placar` (3 years): 8 sorties, 8 empty returns, 3 raids with combat, 3 combats, 0 gold, 0 lost,
  identical to dossie.py for the same recording.
