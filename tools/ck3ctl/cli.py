# Linha de comando: python -m ck3ctl <comando> ... (rodar a partir de tools/, ou com tools/ no PYTHONPATH)
import argparse, json, sys
from . import client, doctor, dll, game, logs, mods, reference, validate


def _p(x):
    if isinstance(x, str):
        print(x.rstrip())
    else:
        print(json.dumps(x, ensure_ascii=False, indent=1, default=list))


def main(argv=None):
    ap = argparse.ArgumentParser(prog='ck3ctl', description='Ferramentas do ck3-mod-maker (jogo, DLL, mods)')
    ap.add_argument('--instancia', help='AR_INSTANCIA do jogo (varios jogos ao mesmo tempo)')
    sp = ap.add_subparsers(dest='cmd', required=True)

    sp.add_parser('doctor', help='checagem de inicio de sessao')
    sp.add_parser('state')
    sp.add_parser('info')
    a = sp.add_parser('pause'); a.add_argument('on', nargs='?', type=int, default=1)
    a = sp.add_parser('speed'); a.add_argument('v', nargs='?', type=int, default=5)
    a = sp.add_parser('req', help='pedido cru a DLL'); a.add_argument('linha'); a.add_argument('--wait', type=float, default=0.5)
    a = sp.add_parser('tail', help='fim do log da DLL'); a.add_argument('n', nargs='?', type=int, default=40)
    a = sp.add_parser('effect'); a.add_argument('texto')
    a = sp.add_parser('eval'); a.add_argument('valor'); a.add_argument('--scope')
    a = sp.add_parser('trigger'); a.add_argument('texto'); a.add_argument('--scope')
    a = sp.add_parser('var'); a.add_argument('tipo'); a.add_argument('id'); a.add_argument('nome')
    a.add_argument('--set', dest='valor'); a.add_argument('--dias', type=int); a.add_argument('--del', dest='apagar', action='store_true')
    a = sp.add_parser('console'); a.add_argument('texto')
    a = sp.add_parser('events'); a.add_argument('--answer', type=int)
    sp.add_parser('save')
    a = sp.add_parser('load'); a.add_argument('save')
    a = sp.add_parser('plugin'); a.add_argument('--recompile', action='store_true')
    a = sp.add_parser('faults'); a.add_argument('--since', type=int, default=0)

    g = sp.add_parser('game').add_subparsers(dest='sub', required=True)
    a = g.add_parser('start'); a.add_argument('save', nargs='?'); a.add_argument('--nographics', action='store_true')
    a.add_argument('--no-wait', action='store_true')
    g.add_parser('close'); g.add_parser('status')

    a = sp.add_parser('errors', help='error.log do jogo'); a.add_argument('--mod'); a.add_argument('--since', type=int, default=0)
    a.add_argument('--all', action='store_true', help='nao filtrar pelo mod')
    sp.add_parser('mark', help='posicao atual do error.log e do log da DLL')

    m = sp.add_parser('mod').add_subparsers(dest='sub', required=True)
    a = m.add_parser('new'); a.add_argument('nome'); a.add_argument('titulo')
    a.add_argument('--prefixo', help='prefixo curto de efeitos/eventos/chaves (padrao: nome)'); a.add_argument('--resumo', default='')
    a.add_argument('--plugin', action='store_true'); a.add_argument('--config', action='store_true')
    a = m.add_parser('link'); a.add_argument('nome')
    a = m.add_parser('adopt', help='registra um mod existente'); a.add_argument('nome'); a.add_argument('--repo'); a.add_argument('--prefixo')
    a = m.add_parser('package', help='zip pelo pipeline compartilhado'); a.add_argument('nome'); a.add_argument('--tag')
    a = m.add_parser('enable'); a.add_argument('nome')
    a = m.add_parser('disable'); a.add_argument('nome')
    m.add_parser('list')
    a = m.add_parser('check'); a.add_argument('nome')
    a = m.add_parser('status', help='funcionalidades e progresso'); a.add_argument('nome')

    a = sp.add_parser('check', help='confere arquivos'); a.add_argument('arquivos', nargs='+')

    r = sp.add_parser('ref').add_subparsers(dest='sub', required=True)
    a = r.add_parser('build'); a.add_argument('--refresh', action='store_true', help='roda script_docs no jogo antes')
    a = r.add_parser('find'); a.add_argument('termo'); a.add_argument('--kind'); a.add_argument('--full', action='store_true')
    a.add_argument('--limit', type=int, default=20)
    a = r.add_parser('grep'); a.add_argument('padrao'); a.add_argument('--dir', default='')
    a.add_argument('--limit', type=int, default=50); a.add_argument('-C', type=int, default=0)

    d = sp.add_parser('dll').add_subparsers(dest='sub', required=True)
    a = d.add_parser('build'); a.add_argument('parte', nargs='?', default='todas', choices=['carregador', 'nucleo', 'todas'])
    a = d.add_parser('install'); a.add_argument('parte', nargs='?', default='nucleo', choices=['carregador', 'nucleo', 'todas'])
    d.add_parser('status')

    o = ap.parse_args(argv)
    c = client.Ck3(o.instancia)

    if o.cmd == 'doctor':
        r = doctor.run(); _p(doctor.fmt(r)); return 1 if any(n == 'erro' for n, _, _ in r) else 0
    elif o.cmd == 'state': _p(c.state())
    elif o.cmd == 'info': _p(c.info())
    elif o.cmd == 'pause': _p({'ok': c.pause(bool(o.on))})
    elif o.cmd == 'speed': _p(c.speed(o.v))
    elif o.cmd == 'req': _p(c.req(o.linha, o.wait))
    elif o.cmd == 'tail': _p(c.tail(o.n))
    elif o.cmd == 'effect':
        ok, msg, _ = c.effect(o.texto); _p({'ok': ok, 'mensagem': msg})
    elif o.cmd == 'eval': _p(c.eval(o.valor, o.scope))
    elif o.cmd == 'trigger': _p(c.trigger(o.texto, o.scope))
    elif o.cmd == 'var': _p(c.var(o.tipo, o.id, o.nome, o.valor, o.dias, o.apagar))
    elif o.cmd == 'console': _p(c.console(o.texto))
    elif o.cmd == 'events': _p(c.events(o.answer is not None, o.answer or 0))
    elif o.cmd == 'save': _p(c.save())
    elif o.cmd == 'load': _p(c.load(o.save))
    elif o.cmd == 'plugin': _p(c.plugins(o.recompile))
    elif o.cmd == 'faults': _p(c.faults(o.since))
    elif o.cmd == 'game':
        if o.sub == 'start': _p(game.start(o.save, nographics=o.nographics, wait=not o.no_wait) or 'aberto')
        elif o.sub == 'close': game.close(); _p('fechado')
        else: _p({'aberto': game.running(), 'dll_responde': game.running() and c.alive(), 'versao': game.version()})
    elif o.cmd == 'errors':
        mp = None if o.all or not o.mod else mods.path(o.mod)
        _p(logs.fmt(logs.errors(mp, o.since)) or 'nenhum erro')
    elif o.cmd == 'mark': _p({'error_log': logs.mark(), 'dll_log': c.size()})
    elif o.cmd == 'mod':
        if o.sub == 'new': _p(mods.new(o.nome, o.titulo, o.prefixo, o.resumo, o.plugin, o.config))
        elif o.sub == 'adopt': _p(mods.adopt(o.nome, o.repo, o.prefixo))
        elif o.sub == 'package': _p(mods.package(o.nome, o.tag))
        elif o.sub == 'link': _p(mods.link(o.nome))
        elif o.sub == 'enable': _p(mods.enable(o.nome, True))
        elif o.sub == 'disable': _p(mods.enable(o.nome, False))
        elif o.sub == 'list': _p(mods.listing())
        elif o.sub == 'status': _p(mods.status(o.nome))
        else:
            res = validate.check_mod(mods.path(o.nome))
            falta = validate.missing_loc(mods.path(o.nome))
            _p({'arquivos': res, 'localizacao_faltando': falta} if res or falta else 'ok')
            return 1 if res else 0
    elif o.cmd == 'check':
        bad = {f: validate.check_file(f) for f in o.arquivos}
        bad = {f: e for f, e in bad.items() if e}
        _p(bad or 'ok')
        return 1 if bad else 0
    elif o.cmd == 'ref':
        if o.sub == 'build': _p(reference.build(c if o.refresh else None))
        elif o.sub == 'find':
            for e in reference.lookup(o.termo, o.kind, o.limit, o.full):
                print('## [%s] %s\n%s\n' % (e['kind'], e['name'], e['text']))
        else:
            for h in reference.grep_vanilla(o.padrao, o.dir, limit=o.limit, context=o.C):
                print('%s:%d: %s' % (h['file'], h['line'], h['text']))
    elif o.cmd == 'dll':
        if o.sub == 'build':
            ok, out = dll.build(o.parte); _p(out); return 0 if ok else 1
        elif o.sub == 'install': _p(dll.install(o.parte))
        else: _p(dll.status())
    return 0


if __name__ == '__main__':
    sys.exit(main())
