# Command line: uv run ck3ctl <command> ... (or python -m ck3ctl with tools/ on PYTHONPATH)
import argparse, json, sys
from . import client, doctor, dll, game, logs, mods, reference, validate


def _p(x):
    if isinstance(x, str):
        print(x.rstrip())
    else:
        print(json.dumps(x, ensure_ascii=False, indent=1, default=list))


def main(argv=None):
    ap = argparse.ArgumentParser(prog='ck3ctl', description='ck3-mod-maker tools (game, DLL, mods)')
    ap.add_argument('--instance', help="the game's AR_INSTANCIA (several games at once)")
    sp = ap.add_subparsers(dest='cmd', required=True)

    sp.add_parser('doctor', help='session start check')
    sp.add_parser('state')
    sp.add_parser('info')
    a = sp.add_parser('pause'); a.add_argument('on', nargs='?', type=int, default=1)
    a = sp.add_parser('speed'); a.add_argument('v', nargs='?', type=int, default=5)
    a = sp.add_parser('req', help='raw request to the DLL'); a.add_argument('line'); a.add_argument('--wait', type=float, default=0.5)
    a = sp.add_parser('tail', help='end of the DLL log'); a.add_argument('n', nargs='?', type=int, default=40)
    a = sp.add_parser('effect'); a.add_argument('text')
    a = sp.add_parser('eval'); a.add_argument('value'); a.add_argument('--scope')
    a = sp.add_parser('trigger'); a.add_argument('text'); a.add_argument('--scope')
    a = sp.add_parser('var'); a.add_argument('kind'); a.add_argument('id'); a.add_argument('name')
    a.add_argument('--set', dest='value'); a.add_argument('--days', type=int); a.add_argument('--del', dest='delete', action='store_true')
    a = sp.add_parser('console'); a.add_argument('text')
    a = sp.add_parser('events'); a.add_argument('--answer', type=int)
    sp.add_parser('save')
    a = sp.add_parser('load'); a.add_argument('save')
    a = sp.add_parser('plugin'); a.add_argument('--recompile', action='store_true')
    a = sp.add_parser('faults'); a.add_argument('--since', type=int, default=0)

    g = sp.add_parser('game').add_subparsers(dest='sub', required=True)
    a = g.add_parser('start'); a.add_argument('save', nargs='?'); a.add_argument('--nographics', action='store_true')
    a.add_argument('--no-wait', action='store_true')
    g.add_parser('close'); g.add_parser('status')

    a = sp.add_parser('errors', help="the game's error.log"); a.add_argument('--mod'); a.add_argument('--since', type=int, default=0)
    a.add_argument('--all', action='store_true', help='do not filter by mod')
    sp.add_parser('mark', help='current position of error.log and of the DLL log')

    m = sp.add_parser('mod').add_subparsers(dest='sub', required=True)
    a = m.add_parser('new'); a.add_argument('name'); a.add_argument('title')
    a.add_argument('--prefix', help='short prefix for effects/events/keys (default: name)'); a.add_argument('--summary', default='')
    a.add_argument('--plugin', action='store_true', help='C plugin: last resort'); a.add_argument('--config', action='store_true', help='Mods tab: last resort')
    a = m.add_parser('link'); a.add_argument('name')
    a = m.add_parser('init', help='add ck3mod.toml, CLAUDE.md and dev/ to an existing mod'); a.add_argument('name')
    a.add_argument('--prefix', required=True); a.add_argument('--repo')
    a = m.add_parser('package', help='zip through the shared pipeline'); a.add_argument('name'); a.add_argument('--tag')
    a = m.add_parser('enable'); a.add_argument('name')
    a = m.add_parser('disable'); a.add_argument('name')
    m.add_parser('list')
    a = m.add_parser('check'); a.add_argument('name')
    a = m.add_parser('status', help="the mod's dev/features.json and dev/progress.md"); a.add_argument('name')

    a = sp.add_parser('check', help='check files'); a.add_argument('files', nargs='+')

    r = sp.add_parser('ref').add_subparsers(dest='sub', required=True)
    a = r.add_parser('build'); a.add_argument('--refresh', action='store_true', help='run script_docs in the game first')
    a = r.add_parser('find'); a.add_argument('term'); a.add_argument('--kind'); a.add_argument('--full', action='store_true')
    a.add_argument('--limit', type=int, default=20)
    a = r.add_parser('grep'); a.add_argument('pattern'); a.add_argument('--dir', default='')
    a.add_argument('--limit', type=int, default=50); a.add_argument('-C', type=int, default=0)

    d = sp.add_parser('dll').add_subparsers(dest='sub', required=True)
    a = d.add_parser('build'); a.add_argument('part', nargs='?', default='all', choices=dll.PARTS)
    a = d.add_parser('install'); a.add_argument('part', nargs='?', default='core', choices=dll.PARTS)
    d.add_parser('status')

    o = ap.parse_args(argv)
    c = client.Ck3(o.instance)

    if o.cmd == 'doctor':
        r = doctor.run(); _p(doctor.fmt(r)); return 1 if any(n == 'error' for n, _, _ in r) else 0
    elif o.cmd == 'state': _p(c.state())
    elif o.cmd == 'info': _p(c.info())
    elif o.cmd == 'pause': _p({'ok': c.pause(bool(o.on))})
    elif o.cmd == 'speed': _p(c.speed(o.v))
    elif o.cmd == 'req': _p(c.req(o.line, o.wait))
    elif o.cmd == 'tail': _p(c.tail(o.n))
    elif o.cmd == 'effect':
        ok, msg, _ = c.effect(o.text); _p({'ok': ok, 'message': msg})
    elif o.cmd == 'eval': _p(c.eval(o.value, o.scope))
    elif o.cmd == 'trigger': _p(c.trigger(o.text, o.scope))
    elif o.cmd == 'var': _p(c.var(o.kind, o.id, o.name, o.value, o.days, o.delete))
    elif o.cmd == 'console': _p(c.console(o.text))
    elif o.cmd == 'events': _p(c.events(o.answer is not None, o.answer or 0))
    elif o.cmd == 'save': _p(c.save())
    elif o.cmd == 'load': _p(c.load(o.save))
    elif o.cmd == 'plugin': _p(c.plugins(o.recompile))
    elif o.cmd == 'faults': _p(c.faults(o.since))
    elif o.cmd == 'game':
        if o.sub == 'start': _p(game.start(o.save, nographics=o.nographics, wait=not o.no_wait) or 'started')
        elif o.sub == 'close': game.close(); _p('closed')
        else: _p({'open': game.running(), 'dll_answers': game.running() and c.alive(), 'version': game.version()})
    elif o.cmd == 'errors':
        mp = None if o.all or not o.mod else mods.path(o.mod)
        _p(logs.fmt(logs.errors(mp, o.since)) or 'no errors')
    elif o.cmd == 'mark': _p({'error_log': logs.mark(), 'dll_log': c.size()})
    elif o.cmd == 'mod':
        if o.sub == 'new': _p(mods.new(o.name, o.title, o.prefix, o.summary, o.plugin, o.config))
        elif o.sub == 'init': _p(mods.init(o.name, o.prefix, o.repo) or 'nothing to add')
        elif o.sub == 'package': _p(mods.package(o.name, o.tag))
        elif o.sub == 'link': _p(mods.link(o.name))
        elif o.sub == 'enable': _p(mods.enable(o.name, True))
        elif o.sub == 'disable': _p(mods.enable(o.name, False))
        elif o.sub == 'list': _p(mods.listing())
        elif o.sub == 'status': _p(mods.status(o.name))
        else:
            res = validate.check_mod(mods.path(o.name))
            missing = validate.missing_loc(mods.path(o.name))
            _p({'files': res, 'missing_localization': missing} if res or missing else 'ok')
            return 1 if res else 0
    elif o.cmd == 'check':
        bad = {f: validate.check_file(f) for f in o.files}
        bad = {f: e for f, e in bad.items() if e}
        _p(bad or 'ok')
        return 1 if bad else 0
    elif o.cmd == 'ref':
        if o.sub == 'build': _p(reference.build(c if o.refresh else None))
        elif o.sub == 'find':
            for e in reference.lookup(o.term, o.kind, o.limit, o.full):
                print('## [%s] %s\n%s\n' % (e['kind'], e['name'], e['text']))
        else:
            for h in reference.grep_vanilla(o.pattern, o.dir, limit=o.limit, context=o.C):
                print('%s:%d: %s' % (h['file'], h['line'], h['text']))
    elif o.cmd == 'dll':
        if o.sub == 'build':
            ok, out = dll.build(o.part); _p(out); return 0 if ok else 1
        elif o.sub == 'install': _p(dll.install(o.part))
        else: _p(dll.status())
    return 0


if __name__ == '__main__':
    sys.exit(main())
