# Static checks on a mod's files, before starting the game. Each function returns [(line, message)].
# The plugin.txt / config.txt keywords (nome, api, fonte, var, livre, liga, inteiro, real, mod, titulo, grupo) are
# Portuguese because the DLL parses them; they stay as they are.
import json, os, re, subprocess, tempfile
from . import config

LOC_LINE = re.compile(r'^\s*([\w.\-\']+):(\d+)?\s*"(.*)"\s*(#.*)?$')
CK3_API = 7
STATUS = ('todo', 'in_progress', 'done', 'blocked')


def _read(p):
    raw = open(p, 'rb').read()
    try:
        return raw.startswith(b'\xef\xbb\xbf'), raw.decode('utf-8-sig')
    except UnicodeDecodeError as e:
        return None, str(e)


def braces(text):
    """Open/close braces, outside comments and quoted text."""
    errs, stack = [], []
    for n, line in enumerate(text.splitlines(), 1):
        q = False
        for ch in line:
            if ch == '"':
                q = not q
            elif q:
                continue
            elif ch == '#':
                break
            elif ch == '{':
                stack.append(n)
            elif ch == '}':
                if not stack:
                    errs.append((n, '"}" without a matching "{"'))
                else:
                    stack.pop()
        if q:
            errs.append((n, 'unclosed quote on this line'))
    errs += [(n, '"{" opened here is never closed') for n in stack]
    return errs


def script(p):
    bom, text = _read(p)
    if bom is None:
        return [(0, 'not valid UTF-8: %s' % text)]
    errs = braces(text)
    if not bom and any(ord(c) > 127 for c in text):
        errs.append((0, 'non-ASCII text without a UTF-8 BOM: the game misreads it; save as UTF-8 with BOM'))
    return errs


def localization(p):
    bom, text = _read(p)
    if bom is None:
        return [(0, 'not valid UTF-8: %s' % text)]
    errs = [] if bom else [(1, 'localization needs UTF-8 with BOM (the game ignores the file without it)')]
    fn = os.path.basename(p)
    m = re.search(r'_l_(\w+)\.yml$', fn)
    if not m:
        return errs + [(0, 'file name must end in _l_<language>.yml')]
    lang = m.group(1)
    lines = text.splitlines()
    first = next(((i, l) for i, l in enumerate(lines, 1) if l.strip() and not l.strip().startswith('#')), None)
    if not first or first[1].strip() != 'l_%s:' % lang:
        errs.append((first[0] if first else 1, 'first line must be "l_%s:"' % lang))
    seen = {}
    for i, l in enumerate(lines, 1):
        s = l.strip()
        if not s or s.startswith('#') or (first and i == first[0]):
            continue
        mm = LOC_LINE.match(l)
        if not mm:
            errs.append((i, 'line does not match  key: "text"  (or key:0 "text")'))
            continue
        if not l.startswith(' '):
            errs.append((i, 'key not indented by one space'))
        k = mm.group(1)
        if k in seen:
            errs.append((i, 'duplicate key (first on line %d)' % seen[k]))
        seen[k] = i
    return errs


def descriptor(p):
    _, text = _read(p)
    errs = [(0, 'missing %s=' % k) for k in ('name', 'version', 'supported_version')
            if not re.search(r'(?m)^%s\s*=' % k, text)]
    return errs + braces(text)


def manifest(p):
    """plugin/plugin.txt (docs/dll/arquitetura_dll.md, section 6)."""
    _, text = _read(p)
    errs, name, api, sources = [], None, None, []
    d = os.path.dirname(p)
    for i, l in enumerate(text.splitlines(), 1):
        s = l.split('#', 1)[0].split()
        if not s:
            continue
        if s[0] == 'nome':
            name = s[1] if len(s) > 1 else None
        elif s[0] == 'api':
            api = int(s[1]) if len(s) > 1 and s[1].isdigit() else None
            if api is None or api > CK3_API:
                errs.append((i, 'invalid api (the core is api %d)' % CK3_API))
        elif s[0] == 'fonte':
            sources.append(s[1])
            if not os.path.exists(os.path.join(d, s[1])):
                errs.append((i, 'source %s does not exist in the plugin folder' % s[1]))
        elif s[0] == 'var':
            if len(s) != 2:
                errs.append((i, 'usage: var <name>'))
        elif len(s) > 2 or (len(s) == 2 and s[1] != 'livre'):
            errs.append((i, 'effect line: <effect> or <effect> livre'))
    if not name:
        errs.append((0, 'missing "nome <mod>"'))
    if api is None:
        errs.append((0, 'missing "api <version>"'))
    if not sources:
        errs.append((0, 'missing "fonte <file.c>"'))
    return errs


def mods_config(p):
    """plugin/config.txt (Mods tab; docs/dll/arquitetura_dll.md, section 11.3)."""
    _, text = _read(p)
    errs = []
    n_args = {'liga': (2, 2), 'inteiro': (4, 6), 'real': (5, 6), 'mod': (1, 1), 'titulo': (1, 1), 'grupo': (2, 2)}
    for i, l in enumerate(text.splitlines(), 1):
        s = l.split('#', 1)[0].split()
        if not s:
            continue
        if s[0] not in n_args:
            errs.append((i, 'unknown type "%s" (liga, inteiro, real, mod, titulo, grupo)' % s[0]))
            continue
        lo, hi = n_args[s[0]]
        if not lo <= len(s) - 1 <= hi:
            errs.append((i, '%s takes %d to %d arguments' % (s[0], lo, hi)))
    return errs


def features(p):
    """The mod's dev/features.json: unique ids, existing dependencies, one in_progress, done only with evidence."""
    try:
        fs = json.load(open(p, encoding='utf-8-sig')).get('features')
    except ValueError as e:
        return [(0, 'invalid JSON: %s' % e)]
    if not isinstance(fs, list):
        return [(0, 'missing the "features" list')]
    errs, by_id = [], {}
    for f in fs:
        fid = f.get('id')
        if not fid or fid in by_id:
            errs.append((0, 'missing or duplicate id: %r' % fid))
        by_id[fid] = f
    for f in fs:
        fid, st = f.get('id'), f.get('status')
        if st not in STATUS:
            errs.append((0, '%s: status "%s" (use %s)' % (fid, st, ', '.join(STATUS))))
        for d in f.get('depends_on', []):
            if d not in by_id:
                errs.append((0, '%s: depends_on "%s", which does not exist' % (fid, d)))
            elif st == 'done' and by_id[d].get('status') != 'done':
                errs.append((0, '%s: done, but dependency %s is not done' % (fid, d)))
        if not f.get('test'):
            errs.append((0, '%s: missing "test" (how to check the feature)' % fid))
        if st == 'done' and not str(f.get('evidence', '')).strip():
            errs.append((0, '%s: done without evidence (date, what was run and the result)' % fid))
    active = [f.get('id') for f in fs if f.get('status') == 'in_progress']
    if len(active) > 1:
        errs.append((0, 'more than one feature in_progress: %s (one at a time)' % ', '.join(active)))
    return errs


def plugin_c(p):
    """Compile the .c with tcc.exe (syntax and types only; symbols are linked by the core inside the game)."""
    tcc = os.path.join(config.TINYCC, 'win32', 'tcc.exe')
    inc = os.path.join(config.BIN, 'ck3_custom', 'include')
    if not os.path.exists(tcc):
        return []
    out = os.path.join(tempfile.gettempdir(), 'ck3ctl_check.o')
    r = subprocess.run([tcc, '-nostdlib', '-I', inc, '-I', config.DLL_SRC, '-I', os.path.dirname(p), '-c', p, '-o', out],
                       capture_output=True, text=True)
    errs = []
    for l in (r.stdout + r.stderr).splitlines():
        m = re.match(r'.*?:(\d+): (error|warning): (.*)', l)
        if m:
            errs.append((int(m.group(1)), '%s: %s' % (m.group(2), m.group(3))))
        elif l.strip():
            errs.append((0, l.strip()))
    return errs


def check_file(p):
    p = os.path.abspath(p)
    norm = p.replace('\\', '/')
    fn = os.path.basename(p)
    if fn == 'features.json' and '/dev/' in norm:
        return features(p)
    if fn == 'descriptor.mod' or fn.endswith('.mod'):
        return descriptor(p)
    if '/plugin/' in norm and fn == 'plugin.txt':
        return manifest(p)
    if '/plugin/' in norm and fn == 'config.txt':
        return mods_config(p)
    if '/plugin/' in norm and fn.endswith('.c'):
        return plugin_c(p)
    if fn.endswith('.yml') and '/localization/' in norm:
        return localization(p)
    if fn.endswith(('.txt', '.gui')):
        return script(p)
    return []


def check_mod(mod_path):
    res = {}
    for dp, dns, fns in os.walk(mod_path):
        dns[:] = [d for d in dns if not d.startswith('.') and d != 'dist']   # .git, .github, zips
        for fn in fns:
            p = os.path.join(dp, fn)
            e = check_file(p)
            if e:
                res[os.path.relpath(p, mod_path).replace('\\', '/')] = e
    return res


def loc_keys(mod_path):
    """Localization keys defined by the mod, per language."""
    keys = {}
    for dp, _, fns in os.walk(os.path.join(mod_path, 'localization')):
        for fn in fns:
            m = re.search(r'_l_(\w+)\.yml$', fn)
            if not m:
                continue
            _, text = _read(os.path.join(dp, fn))
            keys.setdefault(m.group(1), set()).update(mm.group(1) for mm in map(LOC_LINE.match, text.splitlines()) if mm)
    return keys


def missing_loc(mod_path):
    """Keys present in one language and missing in another."""
    keys = loc_keys(mod_path)
    every = set().union(*keys.values()) if keys else set()
    return {lang: sorted(every - ks) for lang, ks in keys.items() if every - ks}
