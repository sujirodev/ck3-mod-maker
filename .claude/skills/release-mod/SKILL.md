---
name: release-mod
description: Release a CK3 mod version - checks features, CHANGELOG, version, zip through the shared pipeline, tag and release. Use when publishing.
---

# Release a mod

The release runs on GitHub through the shared pipeline (ck3-mod-maker's `.github/workflows/mod-release.yml`), called
by the mod's `release.yml`. The `vX.Y.Z` tag publishes the GitHub Release and, if configured, the Nexus Mods version.

1. `ck3_mod(action="check")` clean and every feature in `dev/features.json` `done` with evidence from the final
   version (`test-in-game` skill). Anything pending or blocked: show the user first.
2. `descriptor.mod`: bump `version`; check `supported_version`.
3. `CHANGELOG.md`: a `## [x.y.z] - YYYY-MM-DD` section, one `- ` line per change. Without it the tag build fails.
4. Check the zip locally: `ck3_mod(action="package", name)` and list its contents. Game content only (the
   `ck3mod.toml` `include`); no `docs/`, `dev/`, `tools/` or `test`/`debug` files.
5. Mod with a C plugin or the Mods tab: the page must say it requires the `ck3_custom` DLL and the exact game build,
   and the mod must fail visibly and safely without the DLL (e.g. Auto Raid's `ar_aviso_sem_dll` warning).
6. Page text: `README.md`, `docs/nexus_summary.txt` (up to 350 characters) and `docs/nexus_description.bbcode`.
7. First release of the mod: add a row to the "Mods built with ck3-mod-maker" table in ck3-mod-maker's `README.md`
   and `README.pt-BR.md` (name linking to the repo and what it does in one sentence), and check that the mod's README
   has the "Built with ck3-mod-maker" line.
8. Show the user the version, changelog and zip listing. Commit, `git tag vX.Y.Z` and `git push origin main vX.Y.Z`
   only after the user's ok (external action). Then check the workflow run (`gh run list` / `gh run view`).
