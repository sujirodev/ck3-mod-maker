# Compilar e instalar a DLL ck3_custom (dll/LEIAME.md).
import hashlib, json, os, shutil, subprocess, time
from . import config, game

# o build do gcc nao e reproduzivel byte a byte: a instalacao registra o hash da fonte e do binario instalado
FONTES = {'carregador': ['ar_proxy.c', 'exports.h', 'proxy.def', 'ar_host.h'],
          'nucleo': ['ar_core.c', 'ck3_api.h', 'ar_host.h']}
BINS = {'carregador': 'winmm.dll', 'nucleo': 'ck3_custom_core.dll'}
REGISTRO = os.path.join(config.BIN, 'ck3_custom', 'instalado.json')


def sha(*paths):
    h = hashlib.sha256()
    for p in paths:
        h.update(open(p, 'rb').read())
    return h.hexdigest()[:16]


def fonte_sha(parte):
    return sha(*[os.path.join(config.DLL_SRC, f) for f in FONTES[parte]])


def registro():
    return json.load(open(REGISTRO, encoding='utf-8')) if os.path.exists(REGISTRO) else {}


def build(parte='todas'):
    """parte: 'carregador' (winmm.dll), 'nucleo' (ck3_custom_core.dll) ou 'todas'. Devolve (ok, saida)."""
    os.makedirs(config.DLL_BUILD, exist_ok=True)
    tcc = config.TINYCC.replace('\\', '/')
    cmds = []
    if parte in ('carregador', 'todas'):
        cmds.append(['gcc', '-shared', '-O2', '-Wall', '-Wno-format-truncation', '-Wno-restrict', '-static',
                     '-o', 'build/winmm.dll', 'ar_proxy.c', 'proxy.def'])
    if parte in ('nucleo', 'todas'):
        cmds.append(['gcc', '-shared', '-O2', '-Wall', '-Wno-format-truncation', '-Wno-restrict', '-static',
                     '-I' + tcc, '-o', 'build/ck3_custom_core.dll', 'ar_core.c', tcc + '/win32/libtcc.o'])
    out = ''
    for c in cmds:
        r = subprocess.run(c, cwd=config.DLL_SRC, capture_output=True, text=True)
        out += '$ %s\n%s%s' % (' '.join(c), r.stdout, r.stderr)
        if r.returncode:
            return False, out
    return True, out


def install(parte='nucleo'):
    """Copia para C:\\CK3\\binaries. O nucleo troca com o jogo aberto em -debug_mode; o carregador exige o jogo
    fechado. Sempre leva o ck3_api.h para ck3_custom\\include (os plugins incluem de la)."""
    feito = []
    if parte in ('carregador', 'todas'):
        if game.running():
            raise RuntimeError('o carregador (winmm.dll) so pode ser trocado com o jogo fechado')
        dst = os.path.join(config.BIN, 'winmm.dll')
        if os.path.exists(dst):
            shutil.copy(dst, dst + '.anterior')
        shutil.copy(os.path.join(config.DLL_BUILD, 'winmm.dll'), dst)
        feito.append('winmm.dll')
    if parte in ('nucleo', 'todas'):
        dst = os.path.join(config.BIN, 'ck3_custom_core.dll')
        if os.path.exists(dst):
            shutil.copy(dst, dst + '.anterior')
        shutil.copy(os.path.join(config.DLL_BUILD, 'ck3_custom_core.dll'), dst)
        feito.append('ck3_custom_core.dll')
    inc = os.path.join(config.BIN, 'ck3_custom', 'include')
    if not os.path.isdir(inc):
        # primeira instalacao: cabecalhos e libtcc1.a do TinyCC
        shutil.copytree(os.path.join(config.TINYCC, 'win32', 'include'), inc)
        os.makedirs(os.path.join(config.BIN, 'ck3_custom', 'lib'), exist_ok=True)
        shutil.copy(os.path.join(config.TINYCC, 'win32', 'lib', 'libtcc1.a'), os.path.join(config.BIN, 'ck3_custom', 'lib'))
        feito.append('ck3_custom/include + lib (TinyCC)')
    shutil.copy(os.path.join(config.DLL_SRC, 'ck3_api.h'), inc)
    feito.append('ck3_custom/include/ck3_api.h')
    reg = registro()
    for p in BINS:
        if BINS[p] in feito:
            reg[p] = {'fonte': fonte_sha(p), 'binario': sha(os.path.join(config.BIN, BINS[p])),
                      'data': time.strftime('%Y-%m-%d %H:%M')}
    with open(REGISTRO, 'w', encoding='utf-8') as f:
        json.dump(reg, f, indent=1)
    return feito


def status():
    def info(p):
        return {'existe': os.path.exists(p), 'tamanho': os.path.getsize(p) if os.path.exists(p) else 0}
    return {'jogo': game.version(), 'esperado': config.CK3_VERSION,
            'carregador': info(os.path.join(config.BIN, 'winmm.dll')),
            'nucleo': info(os.path.join(config.BIN, 'ck3_custom_core.dll')),
            'api_h': info(os.path.join(config.BIN, 'ck3_custom', 'include', 'ck3_api.h'))}
