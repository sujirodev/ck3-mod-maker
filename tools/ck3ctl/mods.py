# Mods: cada mod e um repositorio git proprio (padrao do guardian_calc_assistant), por padrao direto em
# Documents/.../mod/<nome>. O estado do harness (mod.json, funcionalidades.json, progresso.md, decisoes.md) fica
# fora dele, em ck3-mod-maker/mods/<nome>/, para o repo do mod so ter o que e do mod.
import json, os, re, shutil, subprocess, time
from . import config

NOME_OK = re.compile(r'^[a-z][a-z0-9_]{2,40}$')
PREFIXO_OK = re.compile(r'^[a-z][a-z0-9_]{1,15}$')


def estado(nome):
    return os.path.join(config.MODS, nome)


def info(nome):
    p = os.path.join(estado(nome), 'mod.json')
    return json.load(open(p, encoding='utf-8')) if os.path.exists(p) else {}


def path(nome):
    """Pasta do repositorio do mod."""
    return info(nome).get('repo') or os.path.join(config.MOD_DIR, nome)


def _render(text, ctx):
    return re.sub(r'\{\{(\w+)\}\}', lambda m: str(ctx.get(m.group(1), m.group(0))), text)


def _copy_tree(src, dst, ctx):
    for dp, _, fns in os.walk(src):
        for fn in fns:
            rel = _render(os.path.relpath(os.path.join(dp, fn), src), ctx)
            out = os.path.join(dst, rel)
            if os.path.exists(out):
                continue
            os.makedirs(os.path.dirname(out), exist_ok=True)
            raw = open(os.path.join(dp, fn), 'rb').read()
            bom = raw.startswith(b'\xef\xbb\xbf')
            with open(out, 'w', encoding='utf-8-sig' if bom else 'utf-8', newline='\n') as f:
                f.write(_render(raw.decode('utf-8-sig'), ctx))


def _ctx(nome, titulo, prefixo, resumo, versao='0.1.0'):
    return {'nome': nome, 'titulo': titulo, 'prefixo': prefixo, 'resumo': resumo or 'TODO: one-line summary',
            'versao': versao, 'jogo': config.CK3_VERSION.rsplit('.', 2)[0] + '.*', 'data': time.strftime('%Y-%m-%d'),
            'ano': time.strftime('%Y'), 'autor': config.AUTHOR, 'pipeline_repo': config.PIPELINE_REPO,
            'pipeline_ref': config.PIPELINE_REF}


def _write_info(nome, data):
    os.makedirs(estado(nome), exist_ok=True)
    with open(os.path.join(estado(nome), 'mod.json'), 'w', encoding='utf-8', newline='\n') as f:
        json.dump(data, f, ensure_ascii=False, indent=2)
        f.write('\n')


def new(nome, titulo, prefixo=None, resumo='', plugin=False, config_mods=False, repo=None):
    """Cria o repositorio do mod (templates/repo, + plugin/config se pedidos; os dois sao a ULTIMA OPCAO) com git
    init, o estado em mods/<nome>/ e o arquivo do launcher."""
    prefixo = prefixo or nome
    if not NOME_OK.match(nome):
        raise ValueError('nome do mod: minusculas, numeros e _, 3 a 41 letras (pasta e nome do zip)')
    if not PREFIXO_OK.match(prefixo):
        raise ValueError('prefixo: minusculas, numeros e _, 2 a 16 letras (ex.: tnt_ga); vai em efeitos, eventos e chaves')
    dst = repo or os.path.join(config.MOD_DIR, nome)
    if os.path.exists(dst) or os.path.exists(estado(nome)):
        raise FileExistsError(dst if os.path.exists(dst) else estado(nome))
    ctx = _ctx(nome, titulo, prefixo, resumo)
    for parte in ['repo'] + (['plugin'] if plugin else []) + (['config'] if config_mods else []):
        _copy_tree(os.path.join(config.TEMPLATES, parte), dst, ctx)
    _copy_tree(os.path.join(config.TEMPLATES, 'estado'), estado(nome), ctx)
    _write_info(nome, {'nome': nome, 'titulo': titulo, 'prefixo': prefixo, 'repo': dst.replace('\\', '/'),
                       'github': None, 'criado': ctx['data']})
    subprocess.run(['git', 'init', '-q', '-b', 'main'], cwd=dst, check=True)
    link(nome)
    return dst


def adopt(nome, repo=None, prefixo=None):
    """Registra no harness um mod que ja existe (ex.: guardian_calc_assistant): cria mods/<nome>/ sem tocar no repo."""
    repo = os.path.abspath(repo or os.path.join(config.MOD_DIR, nome))
    if not os.path.exists(os.path.join(repo, 'descriptor.mod')):
        raise FileNotFoundError('sem descriptor.mod em %s' % repo)
    if os.path.exists(os.path.join(estado(nome), 'mod.json')):
        raise FileExistsError(estado(nome))
    desc = open(os.path.join(repo, 'descriptor.mod'), encoding='utf-8-sig').read()
    titulo = (re.search(r'^name="([^"]*)"', desc, re.M) or [None, nome])[1]
    gh = None
    r = subprocess.run(['git', 'remote', 'get-url', 'origin'], cwd=repo, capture_output=True, text=True)
    m = re.search(r'github\.com[:/](.+?)(?:\.git)?$', r.stdout.strip())
    if m:
        gh = m.group(1)
    ctx = _ctx(nome, titulo, prefixo or nome, '')
    _copy_tree(os.path.join(config.TEMPLATES, 'estado'), estado(nome), ctx)
    _write_info(nome, {'nome': nome, 'titulo': titulo, 'prefixo': prefixo, 'repo': repo.replace('\\', '/'),
                       'github': gh, 'adotado': ctx['data']})
    return estado(nome)


def link(nome):
    """Arquivo mod/<nome>.mod do launcher. Repo fora da pasta mod/ do jogo: juncao mod/<nome> -> repo."""
    src = path(nome)
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


def status(nome):
    """funcionalidades.json resumido + progresso.md: o ponto de partida de uma sessao no mod."""
    d = estado(nome)
    out = ['repo: %s' % path(nome)]
    f = os.path.join(d, 'funcionalidades.json')
    if os.path.exists(f):
        for x in json.load(open(f, encoding='utf-8-sig')).get('funcionalidades', []):
            out.append('%-4s %-12s %s%s' % (x.get('id'), x.get('status'), x.get('nome'),
                                            ' <- ' + ', '.join(x['depende_de']) if x.get('depende_de') else ''))
    else:
        out.append('(sem funcionalidades.json)')
    p = os.path.join(d, 'progresso.md')
    out += ['', open(p, encoding='utf-8-sig').read() if os.path.exists(p) else '(sem progresso.md)']
    return '\n'.join(out)


def listing():
    ativos = set(enabled())
    out = []
    for n in sorted(os.listdir(config.MODS)) if os.path.isdir(config.MODS) else []:
        if not os.path.exists(os.path.join(estado(n), 'mod.json')):
            continue
        repo = path(n)
        out.append({'nome': n, 'repo': repo, 'github': info(n).get('github'),
                    'ligado_ao_jogo': os.path.exists(os.path.join(config.MOD_DIR, n + '.mod')),
                    'ativo': 'mod/%s.mod' % n in ativos,
                    'plugin': os.path.exists(os.path.join(repo, 'plugin', 'plugin.txt'))})
    return out
