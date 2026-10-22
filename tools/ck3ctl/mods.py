# Mods: each mod is a self-contained git repository (guardian_calc_assistant pattern), by default straight in
# Documents/.../mod/<name>, with its own state in dev/ (features.json, progress.md, decisions.md), CLAUDE.md and
# ck3mod.toml. ck3-mod-maker keeps no registry: it finds mods in the game's mod/ folder.
import json, os, re, shutil, subprocess, time
from . import config

NAME_OK = re.compile(r'^[a-z][a-z0-9_]{2,40}$')
PREFIX_OK = re.compile(r'^[a-z][a-z0-9_]{1,15}$')
INIT_FILES = ['ck3mod.toml', 'CLAUDE.md', 'dev']


def path(name):
    """Mod repo folder: mod/<name>, or the path= of mod/<name>.mod when the repo lives elsewhere."""
    d = os.path.join(config.MOD_DIR, name)
    if os.path.isdir(d):
        return d
    f = os.path.join(config.MOD_DIR, name + '.mod')
    if os.path.exists(f):
        m = re.search(r'^path="([^"]+)"', open(f, encoding='utf-8-sig').read(), re.M)
        if m and os.path.isdir(m.group(1)):
            return os.path.normpath(m.group(1))
    return d


def _render(text, ctx):
    return re.sub(r'\{\{(\w+)\}\}', lambda m: str(ctx.get(m.group(1), m.group(0))), text)


def _copy_tree(src, dst, ctx, only=None):
    """Copy the rendered template without overwriting. only: relative path prefixes to copy."""
    created = []
    for dp, _, fns in os.walk(src):
        for fn in fns:
            rel = _render(os.path.relpath(os.path.join(dp, fn), src), ctx).replace('\\', '/')
            if only and not any(rel == o or rel.startswith(o + '/') for o in only):
                continue
            out = os.path.join(dst, rel)
            if os.path.exists(out):
                continue
            os.makedirs(os.path.dirname(out), exist_ok=True)
            raw = open(os.path.join(dp, fn), 'rb').read()
            bom = raw.startswith(b'\xef\xbb\xbf')
            with open(out, 'w', encoding='utf-8-sig' if bom else 'utf-8', newline='\n') as f:
                f.write(_render(raw.decode('utf-8-sig'), ctx))
            created.append(rel)
    return created


def _ctx(name, title, prefix, summary, version='0.1.0'):
    return {'name': name, 'title': title, 'prefix': prefix, 'summary': summary or 'TODO: one-line summary',
            'version': version, 'game': config.CK3_VERSION.rsplit('.', 2)[0] + '.*', 'date': time.strftime('%Y-%m-%d'),
            'year': time.strftime('%Y'), 'author': config.AUTHOR, 'pipeline_repo': config.PIPELINE_REPO,
            'pipeline_ref': config.PIPELINE_REF}


def new(name, title, prefix=None, summary='', plugin=False, config_mods=False, repo=None):
    """Create the mod repo (templates/repo, + plugin/config if asked; both are the LAST RESORT), git init and the
    launcher file."""
    prefix = prefix or name
    if not NAME_OK.match(name):
        raise ValueError('mod name: lowercase letters, digits and _, 3 to 41 characters (folder and zip name)')
    if not PREFIX_OK.match(prefix):
        raise ValueError('prefix: lowercase letters, digits and _, 2 to 16 characters (e.g. tnt_ga); used in effects, '
                         'events and keys')
    dst = repo or os.path.join(config.MOD_DIR, name)
    if os.path.exists(dst):
        raise FileExistsError(dst)
    ctx = _ctx(name, title, prefix, summary)
    for part in ['repo'] + (['plugin'] if plugin else []) + (['config'] if config_mods else []):
        _copy_tree(os.path.join(config.TEMPLATES, part), dst, ctx)
    subprocess.run(['git', 'init', '-q', '-b', 'main'], cwd=dst, check=True)
    link(name, dst)
    return dst


def init(name, prefix, repo=None):
    """Add what an existing mod is missing from the standard (ck3mod.toml, CLAUDE.md, dev/), never overwriting.
    Returns the files created; the rest (README, workflow, CHANGELOG) is up to the mod's maintainer."""
    repo = repo or path(name)
    if not os.path.exists(os.path.join(repo, 'descriptor.mod')):
        raise FileNotFoundError('no descriptor.mod in %s' % repo)
    if not PREFIX_OK.match(prefix):
        raise ValueError('prefix: lowercase letters, digits and _, 2 to 16 characters')
    desc = open(os.path.join(repo, 'descriptor.mod'), encoding='utf-8-sig').read()
    title = (re.search(r'^name="([^"]*)"', desc, re.M) or [None, name])[1]
    return _copy_tree(os.path.join(config.TEMPLATES, 'repo'), repo, _ctx(name, title, prefix, ''), INIT_FILES)


def link(name, src=None):
    """Launcher file mod/<name>.mod. Repo outside the game's mod/ folder: junction mod/<name> -> repo."""
    src = src or path(name)
    if not os.path.isdir(src):
        raise FileNotFoundError(src)
    dst = os.path.join(config.MOD_DIR, name)
    if os.path.normcase(os.path.abspath(src)) != os.path.normcase(dst):
        if os.path.lexists(dst):
            if os.path.normcase(os.path.realpath(dst)) != os.path.normcase(os.path.realpath(src)):
                raise FileExistsError('%s already exists and does not point to %s' % (dst, src))
        else:
            r = subprocess.run(['cmd', '/c', 'mklink', '/J', dst, os.path.normpath(src)], capture_output=True, text=True)
            if r.returncode:
                raise OSError(r.stderr or r.stdout)
    desc = open(os.path.join(src, 'descriptor.mod'), encoding='utf-8-sig').read()
    desc = re.sub(r'(?m)^path=.*\n?', '', desc).rstrip('\n') + '\npath="%s"\n' % dst.replace('\\', '/')
    with open(os.path.join(config.MOD_DIR, name + '.mod'), 'w', encoding='utf-8', newline='\n') as f:
        f.write(desc)
    return dst


def package(name, tag=None):
    """Distribution zip through the shared pipeline (pipeline/package.py), in <repo>/dist."""
    cmd = ['python', os.path.join(config.REPO, 'pipeline', 'package.py'), '--mod', path(name)]
    if tag:
        cmd += ['--tag', tag]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        raise RuntimeError((r.stderr or r.stdout).strip())
    return r.stdout.strip()


def _dlc():
    return json.load(open(config.DLC_LOAD, encoding='utf-8'))


def enabled():
    return _dlc().get('enabled_mods', [])


def enable(name, on=True):
    """Enable/disable the mod in dlc_load.json. Takes effect the next time the game starts."""
    j = _dlc()
    lst = j.setdefault('enabled_mods', [])
    key = 'mod/%s.mod' % name
    if on and key not in lst:
        lst.append(key)
    if not on and key in lst:
        lst.remove(key)
    bak = config.DLC_LOAD + '.before_ck3ctl'
    if not os.path.exists(bak):
        shutil.copy(config.DLC_LOAD, bak)
    with open(config.DLC_LOAD, 'w', encoding='utf-8') as f:
        json.dump(j, f, separators=(',', ':'))
    return lst


def features(name):
    f = os.path.join(path(name), 'dev', 'features.json')
    return json.load(open(f, encoding='utf-8-sig')).get('features', []) if os.path.exists(f) else None


def status(name):
    """dev/features.json summary + dev/progress.md: where a session on the mod starts."""
    repo = path(name)
    out = ['repo: %s' % repo]
    fs = features(name)
    if fs is None:
        out.append('(no dev/features.json: ck3ctl mod init %s --prefix <p>)' % name)
    for x in fs or []:
        out.append('%-4s %-12s %s%s' % (x.get('id'), x.get('status'), x.get('name'),
                                        ' <- ' + ', '.join(x['depends_on']) if x.get('depends_on') else ''))
    p = os.path.join(repo, 'dev', 'progress.md')
    out += ['', open(p, encoding='utf-8-sig').read() if os.path.exists(p) else '(no dev/progress.md)']
    return '\n'.join(out)


def listing():
    """Mods that follow the standard: folders in mod/ (or pointed to by a .mod file) with a ck3mod.toml."""
    on = set(enabled())
    names = set()
    if os.path.isdir(config.MOD_DIR):
        for e in os.listdir(config.MOD_DIR):
            names.add(e[:-4] if e.endswith('.mod') else e)
    out = []
    for n in sorted(names):
        repo = path(n)
        if not os.path.exists(os.path.join(repo, 'ck3mod.toml')):
            continue
        out.append({'name': n, 'repo': repo, 'linked': os.path.exists(os.path.join(config.MOD_DIR, n + '.mod')),
                    'enabled': 'mod/%s.mod' % n in on, 'git': os.path.isdir(os.path.join(repo, '.git')),
                    'plugin': os.path.exists(os.path.join(repo, 'plugin', 'plugin.txt'))})
    return out
