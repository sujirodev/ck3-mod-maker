# MCP server: the same ck3ctl operations as tools for Claude (registered in .mcp.json).
from mcp.server.mcpserver import MCPServer
from . import client, doctor, dll, game, logs, mods, reference, validate

mcp = MCPServer('ck3', instructions=(
    'Controls Crusader Kings III 1.19.0.6 running with -debug_mode and the ck3_custom DLL. Before any effect, check '
    'ck3_state: with "loaded" false (a save is loading) an effect crashes the game. Requests up to 500 characters; '
    'a longer effect goes into a scripted_effect in the mod. Read CLAUDE.md and dll/README.md.'))
C = client.Ck3()


@mcp.tool()
def ck3_doctor() -> str:
    """Session start: game version, installed DLL vs source, reference index, game/DLL answering, mods and each
    mod's feature summary (dev/features.json). Run before working."""
    return doctor.fmt(doctor.run())


@mcp.tool()
def ck3_state() -> dict:
    """Pause, speed and whether a save is loaded (speed 0 = no save or loading)."""
    return C.state()


@mcp.tool()
def ck3_game(action: str, save: str = '', nographics: bool = False) -> str:
    """action: 'status' | 'start' (starts in -debug_mode; with a save, continues from it and waits for the load) |
    'close'."""
    if action == 'start':
        return str(game.start(save or None, nographics=nographics))
    if action == 'close':
        game.close()
        return 'closed'
    return str({'open': game.running(), 'dll_answers': game.running() and C.alive(), 'version': game.version()})


@mcp.tool()
def ck3_pause(pause: bool = True) -> bool:
    """Pause or unpause (checked through the state)."""
    return C.pause(pause)


@mcp.tool()
def ck3_speed(speed: int = 5) -> str:
    """Game speed 1..5."""
    return C.speed(speed)


@mcp.tool()
def ck3_effect(text: str) -> dict:
    """Run a script effect on the player character (root). E.g. 'add_gold = 100',
    'character:123 = { add_trait = brave }'. Returns ok and the game's message."""
    ok, msg, _ = C.effect(text)
    return {'ok': ok, 'message': msg}


@mcp.tool()
def ck3_eval(value: str, scope: str = '') -> float:
    """Script value on the player or on a scope (e.g. value='gold', scope='character:123'; value='var:my_var')."""
    return C.eval(value, scope or None)


@mcp.tool()
def ck3_trigger(text: str, scope: str = '') -> bool:
    """Test a trigger on the player or on a scope. E.g. 'is_at_war = yes', 'has_trait = brave'."""
    return C.trigger(text, scope or None)


@mcp.tool()
def ck3_request(line: str, wait: float = 0.5) -> str:
    """Raw request to the DLL (list in dll/README.md: info, units, where, var, events, peek, cmdlog, cfg, mods...)."""
    return C.req(line, wait)


@mcp.tool()
def ck3_dll_log(lines: int = 40) -> str:
    """End of the DLL log (ck3_custom_dll.log): answers, plugin logs, FALHA CONTIDA (contained faults)."""
    return C.tail(lines)


@mcp.tool()
def ck3_events(answer: bool = False, option: int = 0) -> str:
    """The player's pending events; answer=True answers all of them (option 0 = first) and closes succession."""
    return C.events(answer, option)


@mcp.tool()
def ck3_save_load(action: str, save: str = '') -> str:
    """action 'save' (writes autosave.ck3) or 'load' (reloads save games/<save>.ck3 with the game open, ~15 s)."""
    return C.save() if action == 'save' else str(C.load(save))


@mcp.tool()
def ck3_plugin(recompile: bool = False) -> str:
    """C plugins found in the enabled mods and whether they loaded; recompile=True compiles again (errors with line)."""
    return C.plugins(recompile)


@mcp.tool()
def ck3_errors(mod: str = '', since: int = 0, all: bool = False) -> str:
    """The game's error.log, grouped. mod: only errors citing that mod's files. since: position from ck3_mark."""
    return logs.fmt(logs.errors(None if all or not mod else mods.path(mod), since)) or 'no errors'


@mcp.tool()
def ck3_mark() -> dict:
    """Current positions of error.log and of the DLL log, to read only what comes after an action."""
    return {'error_log': logs.mark(), 'dll_log': C.size()}


@mcp.tool()
def ck3_mod(action: str, name: str = '', title: str = '', prefix: str = '', summary: str = '', repo: str = '',
            plugin: bool = False, config_mods: bool = False) -> str:
    """action: 'list' | 'new' (self-contained mod git repo in Documents/.../mod/<name>, guardian_calc_assistant
    pattern, with dev/ and CLAUDE.md; short prefix e.g. 'tnt_ga'; one-line summary) | 'init' (adds ck3mod.toml,
    CLAUDE.md and dev/ to an existing mod, never overwriting; prefix required) | 'package' (zip through the shared
    pipeline) | 'link' (launcher .mod file) | 'enable' | 'disable' (dlc_load.json, applies on restart) | 'check'
    (validation) | 'status' (the mod's dev/features.json + dev/progress.md: read when starting).
    plugin (C plugin) and config_mods (Mods tab) are the LAST RESORT: they make the mod require the DLL on the
    player's machine. Leave them False unless script provably cannot do it and the user approved (CLAUDE.md, "Main
    rule")."""
    if action == 'new':
        return mods.new(name, title or name, prefix or None, summary, plugin, config_mods)
    if action == 'init':
        return str(mods.init(name, prefix, repo or None) or 'nothing to add')
    if action == 'package':
        return mods.package(name)
    if action == 'link':
        return mods.link(name)
    if action in ('enable', 'disable'):
        return str(mods.enable(name, action == 'enable'))
    if action == 'status':
        return mods.status(name)
    if action == 'check':
        return str({'files': validate.check_mod(mods.path(name)), 'missing_localization': validate.missing_loc(mods.path(name))})
    return str(mods.listing())


@mcp.tool()
def ck3_ref(term: str, kind: str = '', full_text: bool = False, limit: int = 15) -> str:
    """script_docs reference: kind effect | trigger | on_action | event_target | event_scope | modifier | data_type.
    term is a regex on the name (full_text also searches the description)."""
    es = reference.lookup(term, kind or None, limit, full_text)
    return '\n\n'.join('## [%s] %s\n%s' % (e['kind'], e['name'], e['text']) for e in es) or 'nothing'


@mcp.tool()
def ck3_vanilla(pattern: str, folder: str = '', limit: int = 40, context: int = 0) -> str:
    """Regex over the vanilla game files (C:/CK3/game). folder e.g. 'common/scripted_effects', 'events', 'gui'."""
    return '\n'.join('%s:%d: %s' % (h['file'], h['line'], h['text'])
                     for h in reference.grep_vanilla(pattern, folder, limit=limit, context=context)) or 'nothing'


@mcp.tool()
def ck3_dll(action: str = 'status', part: str = 'core') -> str:
    """action 'status' | 'build' | 'install'. part: 'core' (swappable with the game open) | 'loader' (game
    closed) | 'all'."""
    if action == 'build':
        return dll.build(part)[1]
    if action == 'install':
        return str(dll.install(part))
    return str(dll.status())


def main():
    mcp.run()


if __name__ == '__main__':
    main()
