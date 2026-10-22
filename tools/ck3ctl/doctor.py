# Session start check: environment, installed DLL vs source, reference index, game and mods. Each item is
# (level, item, detail) with level ok | warn | error.
import filecmp, json, os, shutil, time
from . import config, dll, game, mods
from .client import Ck3


def _same(a, b):
    return os.path.exists(a) and os.path.exists(b) and filecmp.cmp(a, b, shallow=False)


def run():
    r = []
    v = game.version()
    r.append(('ok' if v == config.CK3_VERSION else 'error', 'game version',
              '%s (the DLL only knows %s)' % (v, config.CK3_VERSION)))
    r.append(('ok' if shutil.which('gcc') else 'warn', 'gcc', shutil.which('gcc') or 'not in PATH: no DLL builds'))
    tcc = os.path.join(config.TINYCC, 'win32', 'tcc.exe')
    r.append(('ok' if os.path.exists(tcc) else 'warn', 'tcc',
              tcc if os.path.exists(tcc) else 'missing: no C plugin check outside the game'))

    rec = dll.record()
    for part, fn in dll.BINS.items():
        dst = os.path.join(config.BIN, fn)
        rg = rec.get(part)
        if not os.path.exists(dst):
            r.append(('error', 'DLL ' + part, 'not installed (ck3ctl dll build + dll install all, game closed)'))
        elif not rg:
            r.append(('warn', 'DLL ' + part, 'installed outside ck3ctl: no record to compare with the source '
                      '(dll build + dll install %s)' % part))
        elif dll.sha(dst) != rg['binary']:
            r.append(('warn', 'DLL ' + part, 'binary replaced outside ck3ctl since %s' % rg['date']))
        elif dll.source_sha(part) != rg['source']:
            r.append(('warn', 'DLL ' + part, 'source changed since the install of %s (dll build + dll install %s)'
                      % (rg['date'], part)))
        else:
            r.append(('ok', 'DLL ' + part, 'installed = current source (%s)' % rg['date']))
    h = os.path.join(config.BIN, 'ck3_custom', 'include', 'ck3_api.h')
    same_h = _same(h, os.path.join(config.DLL_SRC, 'ck3_api.h'))
    r.append(('ok' if same_h else 'warn', 'ck3_api.h', 'installed = source' if same_h else 'differs from source (dll install)'))

    idx = os.path.join(config.REFERENCE, 'index.json')
    if os.path.exists(idx):
        r.append(('ok', 'reference', 'index from %s' % json.load(open(idx, encoding='utf-8')).get('built', '?')))
    else:
        r.append(('warn', 'reference', 'no index (ck3ctl ref build)'))

    if game.running():
        c = Ck3()
        st = c.state() if c.alive() else None
        if not st:
            r.append(('error', 'game', 'open, but the DLL does not answer (no -debug_mode?)'))
        else:
            r.append(('ok', 'game', 'open, %s speed=%d%s' % (st['text'], st['speed'],
                                                            '' if st['loaded'] else ' (no save / loading)')))
            faults = c.faults(max(0, c.size() - 200000))
            if faults:
                r.append(('warn', 'contained fault', '%d at the end of the DLL log; last: %s' % (len(faults), faults[-1][:160])))
    else:
        r.append(('ok', 'game', 'closed'))

    for m in mods.listing():
        fs = mods.features(m['name'])
        summary = '' if fs is not None else 'no dev/features.json (mod init)'
        if fs:
            count = {}
            for x in fs:
                count[x.get('status')] = count.get(x.get('status'), 0) + 1
            summary = ', '.join('%s %d' % kv for kv in count.items())
        level = 'ok' if m['linked'] and m['enabled'] else 'warn'
        r.append((level, 'mod ' + m['name'], '%s%s%s%s' % (
            'linked' if m['linked'] else 'no launcher .mod (mod link)',
            ', enabled' if m['enabled'] else ', disabled (mod enable)',
            (', C PLUGIN' if m['plugin'] else '') + ('' if m['git'] else ', no git'), '; ' + summary if summary else '')))
    return r


def fmt(r):
    w = max(len(i) for _, i, _ in r)
    return '\n'.join('[%-5s] %-*s  %s' % (n, w, i, d) for n, i, d in r) + '\n(%s)' % time.strftime('%Y-%m-%d %H:%M')
