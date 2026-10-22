# ck3-mod-maker

Portuguese version: [README.pt-BR.md](README.pt-BR.md)

> **Study project.** Built to learn Crusader Kings III modding, reverse engineering and AI-agent-driven development
> (Claude Code). Not affiliated with or endorsed by Paradox Interactive. The DLL only works on one exact game build
> (1.19.0.6), reads and patches the game process memory, and is meant for local single-player use in `-debug_mode`.
> Use at your own risk; nothing here is a supported product.

A repository for making CK3 mods with Claude Code in an agent-driven way: Claude writes the mod, starts the game,
runs effects, reads values and logs and fixes things on its own, through the `ck3_custom` DLL.

Mods are written in game script. The DLL is a development tool (testing and debugging); a mod that depends on it
(C plugin or Mods tab) is the last resort, only when script cannot do the job, as in Auto Raid.

## Parts
- **`ck3_custom` DLL** (`dll/`): loader `winmm.dll` + core `ck3_custom_core.dll`. Request channel into the running
  game, new script effects written in C (plugins compiled with TinyCC inside the game), a Mods tab in the settings.
  Reverse-engineering notes in `docs/dll/`. Binaries are not published: build from source.
- **`ck3ctl`** (`tools/ck3ctl/`): library, CLI and the `ck3` MCP server.
- **Shared release pipeline** (`pipeline/`, `.github/workflows/mod-release.yml`): each mod has a few-line
  `release.yml` that calls this workflow at a fixed tag; reproducible zip, notes from `CHANGELOG.md`, GitHub Release
  and optional Nexus Mods upload.
- **Mod repository template** (`templates/repo`): one mod = one git repo, following the
  [Guardian Calculation Assistant](https://github.com/sujirodev/ck3-guardian-calc-assistant).
- **Harness** (`.claude/`): skills (`new-mod`, `test-in-game`, `debug-mod`, `c-plugin`, `release-mod`),
  subagents and a validation hook. Each mod's state (features with evidence, progress, decisions) lives in the mod's
  own repo, in `dev/`, next to a `CLAUDE.md`: whoever clones the mod can contribute without this repo.

## Mods built with ck3-mod-maker
| Mod | What it does |
|---|---|
| [Guardian Calculation Assistant](https://github.com/sujirodev/ck3-guardian-calc-assistant) | Scores every guardian candidate with the real chance of education progress. This repo's mod template and release pipeline came from it |

## Using the pipeline in a mod
The mod's `.github/workflows/release.yml`:
```yaml
on: { push: { branches: [main], tags: ["v*"] }, pull_request: {}, workflow_dispatch: {} }
permissions: { contents: write }
jobs:
  release:
    uses: sujirodev/ck3-mod-maker/.github/workflows/mod-release.yml@v1
    with:
      pipeline_ref: v1
```
and `ck3mod.toml` at the root:
```toml
[mod]
dir = "my_mod"
title = "My Mod"
```
Details (Nexus Mods, `include`) in the header of `.github/workflows/mod-release.yml` and in
`templates/repo/README.md`.

## Machine setup
```
uv sync
uv run ck3ctl dll build          # GCC (WinLibs) and TinyCC in ~/src/tinycc (dll/README.md)
uv run ck3ctl dll install all    # with the game closed
uv run ck3ctl ref build          # script_docs index (run `script_docs` in the game console once first)
uv run ck3ctl doctor
```
Paths in `tools/ck3ctl/config.py` (variables `CK3_GAME`, `CK3_DOCS`, `CK3_TINYCC`, `CK3_AUTHOR`).

## Quick use (CLI)
```
uv run ck3ctl mod new my_mod "My Mod" --prefix mm --summary "does something"
uv run ck3ctl mod check my_mod
uv run ck3ctl mod enable my_mod
uv run ck3ctl game start test_my_mod
uv run ck3ctl effect "trigger_event = mm.0001"
uv run ck3ctl eval gold
uv run ck3ctl errors --mod my_mod
uv run ck3ctl mod package my_mod
```
In Claude Code the same operations show up as `mcp__ck3__*` tools.

## License
[MIT](LICENSE) for the code and text in this repository. The DLL core embeds TinyCC (LGPL) when built; TinyCC is not
distributed here. Crusader Kings III is a trademark of Paradox Interactive AB; no game files are distributed here.
