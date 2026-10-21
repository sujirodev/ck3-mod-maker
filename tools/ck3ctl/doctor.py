# Checagem de inicio de sessao: ambiente, DLL instalada x fonte, referencia, jogo e mods. Cada item: (nivel, item,
# detalhe) com nivel ok | aviso | erro.
import filecmp, json, os, shutil, time
from . import config, dll, game, mods
from .client import Ck3


def _same(a, b):
    return os.path.exists(a) and os.path.exists(b) and filecmp.cmp(a, b, shallow=False)


def run():
    r = []
    v = game.version()
    r.append(('ok' if v == config.CK3_VERSION else 'erro', 'versao do jogo',
              '%s (a DLL so conhece a %s)' % (v, config.CK3_VERSION)))
    r.append(('ok' if shutil.which('gcc') else 'aviso', 'gcc', shutil.which('gcc') or 'fora do PATH: sem build da DLL'))
    tcc = os.path.join(config.TINYCC, 'win32', 'tcc.exe')
    r.append(('ok' if os.path.exists(tcc) else 'aviso', 'tcc', tcc if os.path.exists(tcc) else 'sem checagem de plugin C fora do jogo'))

    reg = dll.registro()
    for parte, fn in dll.BINS.items():
        dst = os.path.join(config.BIN, fn)
        rg = reg.get(parte)
        if not os.path.exists(dst):
            r.append(('erro', 'DLL ' + parte, 'nao instalada (ck3ctl dll build + dll install todas, jogo fechado)'))
        elif not rg:
            r.append(('aviso', 'DLL ' + parte, 'instalada fora do ck3ctl: sem registro para comparar com a fonte '
                      '(dll build + dll install %s)' % parte))
        elif dll.sha(dst) != rg['binario']:
            r.append(('aviso', 'DLL ' + parte, 'binario trocado fora do ck3ctl desde %s' % rg['data']))
        elif dll.fonte_sha(parte) != rg['fonte']:
            r.append(('aviso', 'DLL ' + parte, 'fonte mudou desde a instalacao de %s (dll build + dll install %s)'
                      % (rg['data'], parte)))
        else:
            r.append(('ok', 'DLL ' + parte, 'instalada = fonte atual (%s)' % rg['data']))
    h = os.path.join(config.BIN, 'ck3_custom', 'include', 'ck3_api.h')
    r.append(('ok' if _same(h, os.path.join(config.DLL_SRC, 'ck3_api.h')) else 'aviso', 'ck3_api.h',
              'instalado = fonte' if _same(h, os.path.join(config.DLL_SRC, 'ck3_api.h')) else 'difere da fonte (dll install)'))

    idx = os.path.join(config.REFERENCE, 'index.json')
    if os.path.exists(idx):
        g = json.load(open(idx, encoding='utf-8')).get('gerado', '?')
        r.append(('ok', 'referencia', 'indice de %s' % g))
    else:
        r.append(('aviso', 'referencia', 'sem indice (ck3ctl ref build)'))

    if game.running():
        c = Ck3()
        st = c.state() if c.alive() else None
        if not st:
            r.append(('erro', 'jogo', 'aberto, mas a DLL nao responde (sem -debug_mode?)'))
        else:
            r.append(('ok', 'jogo', 'aberto, %s velocidade=%d%s' % (st['texto'], st['velocidade'],
                                                                   '' if st['carregado'] else ' (sem partida/carregando)')))
            falhas = c.faults(max(0, c.size() - 200000))
            if falhas:
                r.append(('aviso', 'FALHA CONTIDA', '%d no fim do log; ultima: %s' % (len(falhas), falhas[-1][:160])))
    else:
        r.append(('ok', 'jogo', 'fechado'))

    for m in mods.listing():
        fs = mods.features(m['nome'])
        resumo = '' if fs is not None else 'sem dev/features.json (mod init)'
        if fs:
            cont = {}
            for x in fs:
                cont[x.get('status')] = cont.get(x.get('status'), 0) + 1
            resumo = ', '.join('%s %d' % kv for kv in cont.items())
        nivel = 'ok' if m['ligado_ao_jogo'] and m['ativo'] else 'aviso'
        r.append((nivel, 'mod ' + m['nome'], '%s%s%s%s' % (
            'ligado' if m['ligado_ao_jogo'] else 'sem .mod no launcher (mod link)',
            ', ativo' if m['ativo'] else ', inativo (mod enable)',
            (', PLUGIN C' if m['plugin'] else '') + ('' if m['git'] else ', sem git'), '; ' + resumo if resumo else '')))
    return r


def fmt(r):
    w = max(len(i) for _, i, _ in r)
    return '\n'.join('[%-5s] %-*s  %s' % (n, w, i, d) for n, i, d in r) + '\n(%s)' % time.strftime('%Y-%m-%d %H:%M')
