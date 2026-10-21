# Referencia do jogo: documentacao gerada pelo `script_docs` (efeitos, triggers, on_actions, event targets,
# modificadores, data types) indexada em reference/index.json, e busca nos arquivos do jogo base.
import json, os, re, shutil, time
from . import config

DOCS = {'effect': 'effects.log', 'trigger': 'triggers.log', 'on_action': 'on_actions.log',
        'event_target': 'event_targets.log', 'event_scope': 'event_scopes.log', 'modifier': 'modifiers.log'}
INDEX = os.path.join(config.REFERENCE, 'index.json')
SCRIPT_DIRS = ('common', 'events', 'gui', 'localization', 'history', 'map_data', 'gfx')


def _entries(kind, text):
    if kind == 'modifier':
        for blk in re.split(r'\n\s*\n', text):
            m = re.match(r'Tag: (\S+)\s*\nUse areas: (.*)', blk.strip())
            if m:
                yield {'kind': kind, 'name': m.group(1), 'text': 'Use areas: ' + m.group(2)}
        return
    for blk in text.split('--------------------')[1:]:
        blk = blk.strip()
        if not blk:
            continue
        first = blk.splitlines()[0]
        name = re.split(r' - |:$', first, 1)[0].strip().rstrip(':')
        if name:
            yield {'kind': kind, 'name': name, 'text': blk}


def _data_types():
    d = os.path.join(config.GAME_LOGS, 'data_types')
    if not os.path.isdir(d):
        return
    for fn in sorted(os.listdir(d)):
        for blk in open(os.path.join(d, fn), encoding='utf-8', errors='replace').read().split('-----------------------'):
            blk = blk.strip()
            if blk:
                yield {'kind': 'data_type', 'name': blk.splitlines()[0].strip(), 'text': blk,
                       'file': fn.removeprefix('data_types_').removesuffix('.txt')}


def build(refresh_with=None):
    """Monta o indice a partir de logs/ do jogo. refresh_with: um Ck3 para rodar `script_docs` antes (jogo aberto)."""
    if refresh_with is not None:
        refresh_with.console('script_docs')
        time.sleep(8)
    os.makedirs(os.path.join(config.REFERENCE, 'script_docs'), exist_ok=True)
    out = []
    for kind, fn in DOCS.items():
        p = os.path.join(config.GAME_LOGS, fn)
        if not os.path.exists(p):
            continue
        shutil.copy(p, os.path.join(config.REFERENCE, 'script_docs', fn))
        out += list(_entries(kind, open(p, encoding='utf-8', errors='replace').read()))
    out += list(_data_types())
    with open(INDEX, 'w', encoding='utf-8') as f:
        json.dump({'gerado': time.strftime('%Y-%m-%d %H:%M'), 'entradas': out}, f, ensure_ascii=False)
    counts = {}
    for e in out:
        counts[e['kind']] = counts.get(e['kind'], 0) + 1
    return counts


def _load():
    if not os.path.exists(INDEX):
        build()
    return json.load(open(INDEX, encoding='utf-8'))['entradas']


def lookup(term, kind=None, limit=20, full=False):
    """Busca por nome (exato primeiro, depois contem, depois no texto). term pode ser regex."""
    rx = re.compile(term, re.I)
    exact, name, body = [], [], []
    for e in _load():
        if kind and e['kind'] != kind:
            continue
        if e['name'].lower() == term.lower():
            exact.append(e)
        elif rx.search(e['name']):
            name.append(e)
        elif full and rx.search(e['text']):
            body.append(e)
    return (exact + name + body)[:limit]


def grep_vanilla(pattern, subdir='', glob_ext=('.txt', '.gui', '.yml'), limit=50, context=0):
    """Procura no jogo base (C:\\CK3\\game). subdir ex.: 'common/scripted_effects', 'events'."""
    rx = re.compile(pattern)
    root = os.path.join(config.VANILLA, subdir)
    hits = []
    for dp, _, fns in os.walk(root):
        for fn in fns:
            if not fn.endswith(glob_ext):
                continue
            p = os.path.join(dp, fn)
            try:
                lines = open(p, encoding='utf-8-sig', errors='replace').read().splitlines()
            except OSError:
                continue
            for i, l in enumerate(lines):
                if rx.search(l):
                    rel = os.path.relpath(p, config.VANILLA).replace('\\', '/')
                    snip = '\n'.join(lines[max(0, i - context):i + context + 1]) if context else l.strip()
                    hits.append({'file': rel, 'line': i + 1, 'text': snip})
                    if len(hits) >= limit:
                        return hits
    return hits
