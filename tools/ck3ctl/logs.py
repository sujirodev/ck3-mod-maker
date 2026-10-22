# Game logs (Documents/.../logs): error.log split into entries, optionally filtered by a mod's files.
import os, re
from . import config

ERROR_LOG = os.path.join(config.GAME_LOGS, 'error.log')
HEAD = re.compile(r'^\[(\d\d:\d\d:\d\d)\]\[(\w)\]\[([^\]]+)\]: (.*)')


def mark():
    """Current error.log position, to read only what comes after (the game recreates the file on start)."""
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
            cur = {'time': m.group(1), 'level': m.group(2), 'source': m.group(3), 'msg': m.group(4), 'rest': []}
            out.append(cur)
        elif cur is not None and l.strip():
            cur['rest'].append(l.strip())
    for e in out:
        e['files'] = re.findall(r'file: (\S+) line: (\d+)', ' '.join([e['msg']] + e['rest']))
    return out


def mod_files(mod_path):
    rel = set()
    for dp, _, fns in os.walk(mod_path):
        for fn in fns:
            rel.add(os.path.relpath(os.path.join(dp, fn), mod_path).replace('\\', '/'))
    return rel


def errors(mod_path=None, since=0, prefix=None, limit=100):
    """error.log errors. mod_path: only those that cite a file of the mod or its key prefix."""
    es = entries(since)
    if mod_path:
        files = mod_files(mod_path)
        name = prefix or os.path.basename(mod_path.rstrip('/\\'))
        es = [e for e in es if any(f in files for f, _ in e['files'])
              or name in e['msg'] or any(name in r for r in e['rest'])]
    # group repeats (same message and location)
    seen, out = {}, []
    for e in es:
        k = (e['msg'], tuple(e['files'][:1]))
        if k in seen:
            seen[k]['count'] += 1
            continue
        e['count'] = 1
        seen[k] = e
        out.append(e)
    return out[:limit]


def fmt(es):
    lines = []
    for e in es:
        lines.append('[%s][%s] %s%s' % (e['time'], e['source'], e['msg'], ' (x%d)' % e['count'] if e['count'] > 1 else ''))
        lines += ['    ' + r for r in e['rest'][:4]]
    return '\n'.join(lines)
