# Mods: cada mod e um repositorio git autossuficiente (padrao do guardian_calc_assistant), por padrao direto em
# Documents/.../mod/<nome>, com o proprio estado em dev/ (features.json, progress.md, decisions.md), CLAUDE.md e
# ck3mod.toml. O ck3-mod-maker nao guarda registro: acha os mods pela pasta mod/ do jogo.
import json, os, re, shutil, subprocess, time
from . import config

NOME_OK = re.compile(r'^[a-z][a-z0-9_]{2,40}$')
PREFIXO_OK = re.compile(r'^[a-z][a-z0-9_]{1,15}$')


def path(nome):
    """Pasta do repo do mod: mod/<nome>, ou o path= do mod/<nome>.mod quando o repo mora em outro lugar."""
    d = os.path.join(config.MOD_DIR, nome)
    if os.path.isdir(d):
        return d
    f = os.path.join(config.MOD_DIR, nome + '.mod')
    if os.path.exists(f):
        m = re.search(r'^path="([^"]+)"', open(f, encoding='utf-8-sig').read(), re.M)
        if m and os.path.isdir(m.group(1)):
            return os.path.normpath(m.group(1))
    return d


def _render(text, ctx):
    return re.sub(r'\{\{(\w+)\}\}', lambda m: str(ctx.get(m.group(1), m.group(0))), text)


def _copy_tree(src, dst, ctx, only=None):
    """Copia o modelo renderizado sem sobrescrever. only: prefixos de caminho relativo a copiar."""
    novos = []
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
            novos.append(rel)
    return novos


def _ctx(nome, titulo, prefixo, resumo, versao='0.1.0'):
    return {'nome': nome, 'titulo': titulo, 'prefixo': prefixo, 'resumo': resumo or 'TODO: one-line summary',
            'versao': versao, 'jogo': config.CK3_VERSION.rsplit('.', 2)[0] + '.*', 'data': time.strftime('%Y-%m-%d'),
            'ano': time.strftime('%Y'), 'autor': config.AUTHOR, 'pipeline_repo': config.PIPELINE_REPO,
            'pipeline_ref': config.PIPELINE_REF}


def new(nome, titulo, prefixo=None, resumo='', plugin=False, config_mods=False, repo=None):
    """Cria o repo do mod (templates/repo, + plugin/config se pedidos; os dois sao a ULTIMA OPCAO), git init e o
    arquivo do launcher."""
    prefixo = prefixo or nome
    if not NOME_OK.match(nome):
        raise ValueError('nome do mod: minusculas, numeros e _, 3 a 41 letras (pasta e nome do zip)')
    if not PREFIXO_OK.match(prefixo):
        raise ValueError('prefixo: minusculas, numeros e _, 2 a 16 letras (ex.: tnt_ga); vai em efeitos, eventos e chaves')
    dst = repo or os.path.join(config.MOD_DIR, nome)
    if os.path.exists(dst):
        raise FileExistsError(dst)
    ctx = _ctx(nome, titulo, prefixo, resumo)
    for parte in ['repo'] + (['plugin'] if plugin else []) + (['config'] if config_mods else []):
        _copy_tree(os.path.join(config.TEMPLATES, parte), dst, ctx)
    subprocess.run(['git', 'init', '-q', '-b', 'main'], cwd=dst, check=True)
    link(nome, dst)
    return dst


INIT_FILES = ['ck3mod.toml', 'CLAUDE.md', 'dev']


def init(nome, prefixo, repo=None):
    """Acrescenta a um mod existente o que falta do padrao (ck3mod.toml, CLAUDE.md, dev/), sem sobrescrever nada.
    Devolve os arquivos criados; o resto (README, workflow, CHANGELOG) fica a cargo de quem cuida do mod."""
    repo = repo or path(nome)
    if not os.path.exists(os.path.join(repo, 'descriptor.mod')):
        raise FileNotFoundError('sem descriptor.mod em %s' % repo)
    if not PREFIXO_OK.match(prefixo):
        raise ValueError('prefixo: minusculas, numeros e _, 2 a 16 letras')
    desc = open(os.path.join(repo, 'descriptor.mod'), encoding='utf-8-sig').read()
    titulo = (re.search(r'^name="([^"]*)"', desc, re.M) or [None, nome])[1]
    return _copy_tree(os.path.join(config.TEMPLATES, 'repo'), repo, _ctx(nome, titulo, prefixo, ''), INIT_FILES)


def link(nome, src=None):
    """Arquivo mod/<nome>.mod do launcher. Repo fora da pasta mod/ do jogo: juncao mod/<nome> -> repo."""
    src = src or path(nome)
    if not os.path.isdir(src):
        raise FileNotFoundError(src)
    dst = os.path.join(config.MOD_DIR, nome)
    if os.path.normcase(os.path.abspath(src)) != os.path.normcase(dst):
        if os.path.lexists(dst):
            if os.path.normcase(os.path.realpath(dst)) != os.path.normcase(os.path.realpath(src)):
                raise FileExistsError('%s ja existe e nao aponta para %s' % (dst, src))
        else:
            r = subprocess.run(['cmd', '/c', 'mklink', '/J', dst, os.path.normpath(src)], capture_output=True, text=True)
            if r.returncode:
                raise OSError(r.stderr or r.stdout)
    desc = open(os.path.join(src, 'descriptor.mod'), encoding='utf-8-sig').read()
    desc = re.sub(r'(?m)^path=.*\n?', '', desc).rstrip('\n') + '\npath="%s"\n' % dst.replace('\\', '/')
    with open(os.path.join(config.MOD_DIR, nome + '.mod'), 'w', encoding='utf-8', newline='\n') as f:
        f.write(desc)
    return dst


def package(nome, tag=None):
    """Zip de distribuicao pelo pipeline compartilhado (pipeline/package.py), em <repo>/dist."""
    cmd = ['python', os.path.join(config.REPO, 'pipeline', 'package.py'), '--mod', path(nome)]
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


def enable(nome, on=True):
    """Liga/desliga o mod no dlc_load.json. Vale na proxima abertura do jogo."""
    j = _dlc()
    lst = j.setdefault('enabled_mods', [])
    key = 'mod/%s.mod' % nome
    if on and key not in lst:
        lst.append(key)
    if not on and key in lst:
        lst.remove(key)
    bak = config.DLC_LOAD + '.antes_ck3ctl'
    if not os.path.exists(bak):
        shutil.copy(config.DLC_LOAD, bak)
    with open(config.DLC_LOAD, 'w', encoding='utf-8') as f:
        json.dump(j, f, separators=(',', ':'))
    return lst


def features(nome):
    f = os.path.join(path(nome), 'dev', 'features.json')
    return json.load(open(f, encoding='utf-8-sig')).get('features', []) if os.path.exists(f) else None


def status(nome):
    """dev/features.json resumido + dev/progress.md: o ponto de partida de uma sessao no mod."""
    repo = path(nome)
    out = ['repo: %s' % repo]
    fs = features(nome)
    if fs is None:
        out.append('(sem dev/features.json: ck3ctl mod init %s --prefixo <p>)' % nome)
    for x in fs or []:
        out.append('%-4s %-12s %s%s' % (x.get('id'), x.get('status'), x.get('name'),
                                        ' <- ' + ', '.join(x['depends_on']) if x.get('depends_on') else ''))
    p = os.path.join(repo, 'dev', 'progress.md')
    out += ['', open(p, encoding='utf-8-sig').read() if os.path.exists(p) else '(sem dev/progress.md)']
    return '\n'.join(out)


def listing():
    """Mods que seguem o padrao: pastas em mod/ (ou apontadas por um .mod) com ck3mod.toml."""
    ativos = set(enabled())
    nomes = set()
    if os.path.isdir(config.MOD_DIR):
        for e in os.listdir(config.MOD_DIR):
            nomes.add(e[:-4] if e.endswith('.mod') else e)
    out = []
    for n in sorted(nomes):
        repo = path(n)
        if not os.path.exists(os.path.join(repo, 'ck3mod.toml')):
            continue
        out.append({'nome': n, 'repo': repo, 'ligado_ao_jogo': os.path.exists(os.path.join(config.MOD_DIR, n + '.mod')),
                    'ativo': 'mod/%s.mod' % n in ativos, 'git': os.path.isdir(os.path.join(repo, '.git')),
                    'plugin': os.path.exists(os.path.join(repo, 'plugin', 'plugin.txt'))})
    return out
