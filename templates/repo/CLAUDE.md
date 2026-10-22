# {{title}}

Crusader Kings III mod (`{{name}}`, prefix `{{prefix}}`). This file is for AI coding agents and contributors.

## Start here
1. `dev/progress.md`: where things stand and the next step.
2. `dev/features.json`: every feature with status, how to test it and the evidence that it works.
3. `dev/decisions.md`: lasting decisions and why.

## Rules
- Script only (`common/`, `events/`, `gui/`, `localization/`). Do not add a C plugin or anything that needs an
  external DLL unless the maintainer explicitly asks: it ties the mod to one exact game build.
- Never invent effects, triggers or scopes: check the vanilla files in `<CK3>/game/` and the game's `script_docs`
  output (`logs/effects.log`, `logs/triggers.log`).
- Everything the mod creates uses the `{{prefix}}` prefix (events, scripted effects/triggers, variables, keys).
- Prefer adding files over overwriting vanilla ones; list any overwritten vanilla file in the README.
- Localization: `localization/<language>/<name>_l_<language>.yml`, UTF-8 **with BOM**, first line `l_<language>:`,
  same keys in `english` and `braz_por`. Script files with non-ASCII text: UTF-8 with BOM.
- Docs, commits and changelog in English.

## Working on a feature
- One feature `in_progress` at a time in `dev/features.json`.
- `done` only with evidence from a real check: date, what was run, what came out (e.g. "2026-10-21 console
  `effect trigger_event = {{prefix}}.0001`: event shown; error.log: nothing from {{prefix}}"). "Looks right" is not
  evidence. No way to test yet: keep it `in_progress` or `blocked` with the reason.
- Test in game with `-debug_mode`; check `Documents/Paradox Interactive/Crusader Kings III/logs/error.log`.
- When you stop: update `dev/features.json`, rewrite `dev/progress.md`, add lasting decisions to `dev/decisions.md`.

## Release
Bump `version` in `descriptor.mod`, add the `CHANGELOG.md` section, tag `vX.Y.Z` and push (see README). The release
workflow comes from [ck3-mod-maker](https://github.com/{{pipeline_repo}}); only `ck3mod.toml` `include` goes into the
zip, so `dev/` and `docs/` never ship.

## Optional tooling
[ck3-mod-maker](https://github.com/{{pipeline_repo}}) has `ck3ctl` (CLI + MCP server) to validate these files and to
drive a running game (effects, values, triggers, error.log) through its debug DLL. The mod does not depend on it.
