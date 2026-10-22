# Contributing

Bug reports, translations and pull requests are welcome.

- **Bugs:** open a [bug report](../../issues/new?template=bug_report.yml) with your game version, mod version and mod list.

## Translations

1. Copy `localization/english/{{prefix}}_l_english.yml` to `localization/<folder>/{{prefix}}_l_<folder>.yml`
   (`french`, `german`, `spanish`, `russian`, `polish`, `simp_chinese`, `korean`, `japanese`).
2. Change the first line from `l_english:` to `l_<folder>:`.
3. Translate only the text between the quotes. Keep the keys and everything inside `[...]` exactly as they are.
   Codes like `#P`, `#N` and `#!` are colours; keep them around the same words.
4. Save the file as **UTF-8 with BOM**. CK3 ignores localization files without the BOM.
5. Open a pull request.

Use the terms the game itself uses in your language, so the mod reads like part of the game.

## Code

Start with `dev/progress.md` (current state) and `dev/features.json` (features, how to test each one, evidence);
`CLAUDE.md` has the working rules. Test with `-debug_mode` and check `logs/error.log` before opening a pull request,
and record what you checked as the feature's `evidence`.
