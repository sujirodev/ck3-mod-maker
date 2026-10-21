# Logs do jogo (Documents/.../logs): error.log em entradas, filtradas pelos arquivos de um mod.
import os, re
from . import config

ERROR_LOG = os.path.join(config.GAME_LOGS, 'error.log')
HEAD = re.compile(r'^\[(\d\d:\d\d:\d\d)\]\[(\w)\]\[([^\]]+)\]: (.*)')


def mark():
    """Posicao atual do error.log, para ler so o que vier depois (o jogo recria o arquivo ao abrir)."""
    return os.path.getsize(ERROR_LOG) if os.path.exists(ERROR_LOG) else 0


def entries(since=0):
    if not os.path.exists(ERROR_LOG):
        return []
    with open(ERROR_LOG, 'rb') as f:
        if os.path.getsize(ERROR_LOG) < since:
            since = 0
        f.seek(since)
        text = f.read().decode('utf-8', errors='replace')
    out, cur = [], None
    for l in text.splitlines():
        m = HEAD.match(l)
        if m:
            cur = {'hora': m.group(1), 'nivel': m.group(2), 'origem': m.group(3), 'msg': m.group(4), 'resto': []}
            out.append(cur)
        elif cur is not None and l.strip():
            cur['resto'].append(l.strip())
    for e in out:
        e['arquivos'] = re.findall(r'file: (\S+) line: (\d+)', ' '.join([e['msg']] + e['resto']))
    return out


def mod_files(mod_path):
    rel = set()
    for dp, _, fns in os.walk(mod_path):
        for fn in fns:
            rel.add(os.path.relpath(os.path.join(dp, fn), mod_path).replace('\\', '/'))
    return rel


def errors(mod_path=None, since=0, prefix=None, limit=100):
    """Erros do error.log. mod_path: so os que citam arquivo do mod ou o prefixo das chaves dele."""
    es = entries(since)
    if mod_path:
        files = mod_files(mod_path)
        name = prefix or os.path.basename(mod_path.rstrip('/\\'))
        es = [e for e in es if any(f in files for f, _ in e['arquivos'])
              or name in e['msg'] or any(name in r for r in e['resto'])]
    # agrupa repetidos (mesma mensagem e local)
    seen, out = {}, []
    for e in es:
        k = (e['msg'], tuple(e['arquivos'][:1]))
        if k in seen:
            seen[k]['vezes'] += 1
            continue
        e['vezes'] = 1
        seen[k] = e
        out.append(e)
    return out[:limit]


def fmt(es):
    lines = []
    for e in es:
        lines.append('[%s][%s] %s%s' % (e['hora'], e['origem'], e['msg'], ' (x%d)' % e['vezes'] if e['vezes'] > 1 else ''))
        lines += ['    ' + r for r in e['resto'][:4]]
    return '\n'.join(lines)
