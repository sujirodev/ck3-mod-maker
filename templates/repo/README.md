# {{titulo}}

A mod for **Crusader Kings III** that {{resumo}}.

## Features

- (one bullet per player-visible feature; screenshots in `docs/images/`)

## Compatibility

- Game version: {{jogo}}.
- Languages: English and Brazilian Portuguese (Portuguese needs the community PT-BR translation, which adds `braz_por`).
- Overwritten vanilla files: none.

## Installation

Download the zip from the [Releases](../../releases) page and extract it into
`Documents/Paradox Interactive/Crusader Kings III/mod/`, then enable the mod in the launcher.

## Development

To test, run the game with `-debug_mode` and check
`Documents/Paradox Interactive/Crusader Kings III/logs/error.log`.

### Releasing a version

1. Bump `version` in `descriptor.mod`.
2. Add a `## [x.y.z] - YYYY-MM-DD` section to `CHANGELOG.md`, one `- ` line per change. It becomes the GitHub
   Release notes and the Nexus Mods changelog; the tag build fails without it.
3. Commit, tag and push: `git tag vx.y.z` and `git push origin main vx.y.z`.

`.github/workflows/release.yml` calls the shared pipeline from
[ck3-mod-maker](https://github.com/{{pipeline_repo}}): it builds the zip from the files listed in `ck3mod.toml`,
checks that the tag matches the `descriptor.mod` version, takes the notes from `CHANGELOG.md` and publishes the
Release. On regular pushes it only uploads the zip as a run artifact. To build locally, from a ck3-mod-maker
checkout: `python pipeline/package.py --mod <this folder>` (output in `dist/`).

### Nexus Mods

On tags the workflow also uploads the zip as a new version of the main file and replaces the page text with
`docs/nexus_summary.txt` and `docs/nexus_description.bbcode`. Skipped until these exist in the repository settings:

| Kind | Name | Value |
|---|---|---|
| Secret | `NEXUSMODS_API_KEY` | API key from https://www.nexusmods.com/settings/api-keys |
| Variable | `NEXUS_GAME_DOMAIN` | `crusaderkings3` |
| Variable | `NEXUS_GAME_MOD_ID` | number at the end of the mod page URL |
| Variable | `NEXUS_FILE_ID` | file ID of the main file (Files tab > Advanced) |

The mod page and its first file have to be created by hand on Nexus Mods.

## License

[MIT](LICENSE), covering the mod's own code and text. Crusader Kings III is a trademark of Paradox Interactive AB.
