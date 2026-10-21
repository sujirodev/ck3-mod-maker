# Conferencias estaticas dos arquivos de um mod, antes de abrir o jogo. Cada funcao devolve [(linha, mensagem)].
import json, os, re, subprocess, tempfile
from . import config

LOC_LINE = re.compile(r'^\s*([\w.\-\']+):(\d+)?\s*"(.*)"\s*(#.*)?$')
CK3_API = 7


def _read(p):
    raw = open(p, 'rb').read()
    try:
        return raw.startswith(b'\xef\xbb\xbf'), raw.decode('utf-8-sig')
    except UnicodeDecodeError as e:
        return None, str(e)


def braces(text):
    """Chaves abertas/fechadas, fora de comentario e de texto entre aspas."""
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
                    errs.append((n, '"}" sem "{" correspondente'))
                else:
                    stack.pop()
        if q:
            errs.append((n, 'aspas sem fechar na linha'))
    errs += [(n, '"{" aberta aqui nunca fecha') for n in stack]
    return errs


def script(p):
    bom, text = _read(p)
    if bom is None:
        return [(0, 'nao e UTF-8 valido: %s' % text)]
    errs = braces(text)
    if not bom and any(ord(c) > 127 for c in text):
        errs.append((0, 'tem acentos/nao-ASCII sem BOM UTF-8: o jogo le errado; grave como UTF-8 com BOM'))
    return errs


def localization(p):
    bom, text = _read(p)
    if bom is None:
        return [(0, 'nao e UTF-8 valido: %s' % text)]
    errs = [] if bom else [(1, 'localizacao exige UTF-8 com BOM (o jogo ignora o arquivo sem BOM)')]
    fn = os.path.basename(p)
    m = re.search(r'_l_(\w+)\.yml$', fn)
    if not m:
        return errs + [(0, 'nome do arquivo deve terminar em _l_<idioma>.yml')]
    lang = m.group(1)
    lines = text.splitlines()
    first = next(((i, l) for i, l in enumerate(lines, 1) if l.strip() and not l.strip().startswith('#')), None)
    if not first or first[1].strip() != 'l_%s:' % lang:
        errs.append((first[0] if first else 1, 'primeira linha deve ser "l_%s:"' % lang))
    seen = {}
    for i, l in enumerate(lines, 1):
        s = l.strip()
        if not s or s.startswith('#') or (first and i == first[0]):
            continue
        mm = LOC_LINE.match(l)
        if not mm:
            errs.append((i, 'linha fora do formato  chave: "texto"  (ou chave:0 "texto")'))
            continue
        if not l.startswith(' '):
            errs.append((i, 'chave sem recuo de um espaco'))
        k = mm.group(1)
        if k in seen:
            errs.append((i, 'chave repetida (primeira na linha %d)' % seen[k]))
        seen[k] = i
    return errs


def descriptor(p):
    _, text = _read(p)
    errs = [(0, 'falta %s=' % k) for k in ('name', 'version', 'supported_version') if not re.search(r'(?m)^%s\s*=' % k, text)]
    return errs + braces(text)


def manifest(p):
    """plugin/plugin.txt (docs/dll/arquitetura_dll.md, secao 6)."""
    _, text = _read(p)
    errs, nome, api, fontes = [], None, None, []
    d = os.path.dirname(p)
    for i, l in enumerate(text.splitlines(), 1):
        s = l.split('#', 1)[0].split()
        if not s:
            continue
        if s[0] == 'nome':
            nome = s[1] if len(s) > 1 else None
        elif s[0] == 'api':
            api = int(s[1]) if len(s) > 1 and s[1].isdigit() else None
            if api is None or api > CK3_API:
                errs.append((i, 'api invalida (o nucleo e da %d)' % CK3_API))
        elif s[0] == 'fonte':
            fontes.append(s[1])
            if not os.path.exists(os.path.join(d, s[1])):
                errs.append((i, 'fonte %s nao existe na pasta plugin' % s[1]))
        elif s[0] == 'var':
            if len(s) != 2:
                errs.append((i, 'uso: var <nome>'))
        elif len(s) > 2 or (len(s) == 2 and s[1] != 'livre'):
            errs.append((i, 'linha de efeito: <efeito> ou <efeito> livre'))
    if not nome:
        errs.append((0, 'falta "nome <mod>"'))
    if api is None:
        errs.append((0, 'falta "api <versao>"'))
    if not fontes:
        errs.append((0, 'falta "fonte <arquivo.c>"'))
    return errs


def mods_config(p):
    """plugin/config.txt (aba Mods; docs/dll/arquitetura_dll.md, secao 11.3)."""
    _, text = _read(p)
    errs = []
    n_args = {'liga': (2, 2), 'inteiro': (4, 6), 'real': (5, 6), 'mod': (1, 1), 'titulo': (1, 1), 'grupo': (2, 2)}
    for i, l in enumerate(text.splitlines(), 1):
        s = l.split('#', 1)[0].split()
        if not s:
            continue
        if s[0] not in n_args:
            errs.append((i, 'tipo desconhecido "%s" (liga, inteiro, real, mod, titulo, grupo)' % s[0]))
            continue
        lo, hi = n_args[s[0]]
        if not lo <= len(s) - 1 <= hi:
            errs.append((i, '%s espera de %d a %d argumentos' % (s[0], lo, hi)))
    return errs


STATUS = ('pendente', 'em_andamento', 'feito', 'bloqueado')


def features(p):
    """funcionalidades.json do mod: ids unicos, dependencias existentes, uma em andamento, feito so com evidencia."""
    try:
        fs = json.load(open(p, encoding='utf-8-sig')).get('funcionalidades')
    except ValueError as e:
        return [(0, 'JSON invalido: %s' % e)]
    if not isinstance(fs, list):
        return [(0, 'falta a lista "funcionalidades"')]
    errs, por_id = [], {}
    for f in fs:
        fid = f.get('id')
        if not fid or fid in por_id:
            errs.append((0, 'id ausente ou repetido: %r' % fid))
        por_id[fid] = f
    for f in fs:
        fid, st = f.get('id'), f.get('status')
        if st not in STATUS:
            errs.append((0, '%s: status "%s" (use %s)' % (fid, st, ', '.join(STATUS))))
        for d in f.get('depende_de', []):
            if d not in por_id:
                errs.append((0, '%s: depende de "%s", que nao existe' % (fid, d)))
            elif st == 'feito' and por_id[d].get('status') != 'feito':
                errs.append((0, '%s: feito, mas a dependencia %s nao esta feita' % (fid, d)))
        if not f.get('teste'):
            errs.append((0, '%s: falta "teste" (comando que confere a funcionalidade)' % fid))
        if st == 'feito' and not str(f.get('evidencia', '')).strip():
            errs.append((0, '%s: feito sem evidencia (data, comando rodado e resultado)' % fid))
    ativas = [f.get('id') for f in fs if f.get('status') == 'em_andamento']
    if len(ativas) > 1:
        errs.append((0, 'mais de uma funcionalidade em andamento: %s (uma por vez)' % ', '.join(ativas)))
    return errs


def plugin_c(p):
    """Compila o .c com o tcc.exe (so sintaxe e tipos; os simbolos sao ligados pelo nucleo no jogo)."""
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
    if fn == 'funcionalidades.json':
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
    for dp, _, fns in os.walk(mod_path):
        for fn in fns:
            p = os.path.join(dp, fn)
            e = check_file(p)
            if e:
                res[os.path.relpath(p, mod_path).replace('\\', '/')] = e
    return res


def loc_keys(mod_path):
    """Chaves de localizacao definidas no mod, por idioma."""
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
    """Chaves presentes num idioma e ausentes em outro."""
    keys = loc_keys(mod_path)
    todas = set().union(*keys.values()) if keys else set()
    return {lang: sorted(todas - ks) for lang, ks in keys.items() if todas - ks}
