# Build and install the ck3_custom DLL (dll/README.md).
import hashlib, json, os, shutil, subprocess, time
from . import config, game

PARTS = ('loader', 'core', 'all')
# gcc builds are not byte-for-byte reproducible: install records the hash of the source and of the installed binary
SOURCES = {'loader': ['ar_proxy.c', 'exports.h', 'proxy.def', 'ar_host.h'],
           'core': ['ar_core.c', 'ck3_api.h', 'ar_host.h']}
BINS = {'loader': 'winmm.dll', 'core': 'ck3_custom_core.dll'}
RECORD = os.path.join(config.BIN, 'ck3_custom', 'installed.json')


def sha(*paths):
    h = hashlib.sha256()
    for p in paths:
        h.update(open(p, 'rb').read())
    return h.hexdigest()[:16]


def source_sha(part):
    return sha(*[os.path.join(config.DLL_SRC, f) for f in SOURCES[part]])


def record():
    return json.load(open(RECORD, encoding='utf-8')) if os.path.exists(RECORD) else {}


def build(part='all'):
    """part: 'loader' (winmm.dll), 'core' (ck3_custom_core.dll) or 'all'. Returns (ok, output)."""
    os.makedirs(config.DLL_BUILD, exist_ok=True)
    tcc = config.TINYCC.replace('\\', '/')
    cmds = []
    if part in ('loader', 'all'):
        cmds.append(['gcc', '-shared', '-O2', '-Wall', '-Wno-format-truncation', '-Wno-restrict', '-static',
                     '-o', 'build/winmm.dll', 'ar_proxy.c', 'proxy.def'])
    if part in ('core', 'all'):
        cmds.append(['gcc', '-shared', '-O2', '-Wall', '-Wno-format-truncation', '-Wno-restrict', '-static',
                     '-I' + tcc, '-o', 'build/ck3_custom_core.dll', 'ar_core.c', tcc + '/win32/libtcc.o'])
    out = ''
    for c in cmds:
        r = subprocess.run(c, cwd=config.DLL_SRC, capture_output=True, text=True)
        out += '$ %s\n%s%s' % (' '.join(c), r.stdout, r.stderr)
        if r.returncode:
            return False, out
    return True, out


def install(part='core'):
    """Copy to C:\\CK3\\binaries. The core can be swapped with the game open in -debug_mode; the loader needs the
    game closed. Always copies ck3_api.h to ck3_custom\\include (plugins include it from there)."""
    done = []
    if part in ('loader', 'all'):
        if game.running():
            raise RuntimeError('the loader (winmm.dll) can only be replaced with the game closed')
        dst = os.path.join(config.BIN, 'winmm.dll')
        if os.path.exists(dst):
            shutil.copy(dst, dst + '.previous')
        shutil.copy(os.path.join(config.DLL_BUILD, 'winmm.dll'), dst)
        done.append('winmm.dll')
    if part in ('core', 'all'):
        dst = os.path.join(config.BIN, 'ck3_custom_core.dll')
        if os.path.exists(dst):
            shutil.copy(dst, dst + '.previous')
        shutil.copy(os.path.join(config.DLL_BUILD, 'ck3_custom_core.dll'), dst)
        done.append('ck3_custom_core.dll')
    inc = os.path.join(config.BIN, 'ck3_custom', 'include')
    if not os.path.isdir(inc):
        # first install: TinyCC headers and libtcc1.a
        shutil.copytree(os.path.join(config.TINYCC, 'win32', 'include'), inc)
        os.makedirs(os.path.join(config.BIN, 'ck3_custom', 'lib'), exist_ok=True)
        shutil.copy(os.path.join(config.TINYCC, 'win32', 'lib', 'libtcc1.a'), os.path.join(config.BIN, 'ck3_custom', 'lib'))
        done.append('ck3_custom/include + lib (TinyCC)')
    shutil.copy(os.path.join(config.DLL_SRC, 'ck3_api.h'), inc)
    done.append('ck3_custom/include/ck3_api.h')
    rec = record()
    for p in BINS:
        if BINS[p] in done:
            rec[p] = {'source': source_sha(p), 'binary': sha(os.path.join(config.BIN, BINS[p])),
                      'date': time.strftime('%Y-%m-%d %H:%M')}
    with open(RECORD, 'w', encoding='utf-8') as f:
        json.dump(rec, f, indent=1)
    return done


def status():
    def info(p):
        return {'exists': os.path.exists(p), 'size': os.path.getsize(p) if os.path.exists(p) else 0}
    return {'game': game.version(), 'expected': config.CK3_VERSION,
            'loader': info(os.path.join(config.BIN, 'winmm.dll')),
            'core': info(os.path.join(config.BIN, 'ck3_custom_core.dll')),
            'api_h': info(os.path.join(config.BIN, 'ck3_custom', 'include', 'ck3_api.h'))}
