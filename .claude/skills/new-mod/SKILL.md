---
name: new-mod
description: Create a new CK3 mod from an idea - self-contained git repo, script, features, localization, release ready. Use for "make a mod that...".
---

# New mod

1. Get the idea in one sentence: what changes in the game, for whom (player/AI), when it happens (event, decision,
   on_action, interaction, GUI). Ask only what would change the design.
2. Names: `name` = folder and zip (lowercase with `_`); a short `prefix` (2 to 16 characters, e.g. `tnt_ga`) on
   everything the mod creates (effects, events, variables, keys); a readable title and a one-line summary.
3. Research before designing: `mcp__ck3__ck3_vanilla` to find a similar mechanic in the vanilla game (decisions in
   `common/decisions`, events in `events`, interactions in `common/character_interactions`) and `ck3_ref` for every
   effect/trigger/on_action you plan to use.
4. Create: `mcp__ck3__ck3_mod(action="new", name, title, prefix, summary)`. It creates the mod's git repo in
   `Documents/.../mod/<name>` (guardian_calc_assistant pattern: release through the shared pipeline, state in `dev/`,
   a `CLAUDE.md` with the rules) and the launcher `.mod`. Script only: no `plugin` and no `config_mods`, which make
   the published mod require the DLL (CLAUDE.md, "Main rule").
   - Player options: a game rule (`common/game_rules`), not the Mods tab.
   - A C plugin only after exhausting script and with the user's approval; then the `c-plugin` skill.
5. Fill `dev/features.json` with the idea's features (each with `test` = how to check it, and `depends_on`),
   replacing the f02 example. Show the list to the user before implementing: it is the scope.
6. Write the mod one feature at a time (delegate to `ck3-script`, `ck3-localization`, `ck3-gui` when it is large).
   Localization in `english` and `braz_por`, same keys.
7. `ck3_mod(action="check")` until clean.
8. Enable: `ck3_mod(action="enable")` (applies the next time the game starts).
9. Test with the `test-in-game` skill (it records the evidence for each feature).
10. In the mod repo: `README.md` (features, compatibility) and `CHANGELOG.md`; small commits. State in
    `dev/progress.md` and `dev/decisions.md`; new mod-specific rules in its `CLAUDE.md`. Create the GitHub repo and
    push only when the user asks.
