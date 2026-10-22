# Machine paths. Each one can be overridden by an environment variable.
import os

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
GAME = os.path.normpath(os.environ.get('CK3_GAME', r'C:\CK3'))       # game install (with binaries\ and game\)
BIN = os.path.join(GAME, 'binaries')
VANILLA = os.path.join(GAME, 'game')
DOCS = os.path.normpath(os.environ.get('CK3_DOCS', os.path.join(os.path.expanduser('~'), 'Documents',
                                                                  'Paradox Interactive', 'Crusader Kings III')))
MOD_DIR = os.path.join(DOCS, 'mod')
SAVES = os.path.join(DOCS, 'save games')
GAME_LOGS = os.path.join(DOCS, 'logs')
DLC_LOAD = os.path.join(DOCS, 'dlc_load.json')
TINYCC = os.path.normpath(os.environ.get('CK3_TINYCC', os.path.join(os.path.expanduser('~'), 'src', 'tinycc')))

DLL_SRC = os.path.join(REPO, 'dll')
DLL_BUILD = os.path.join(DLL_SRC, 'build')
TEMPLATES = os.path.join(REPO, 'templates')
REFERENCE = os.path.join(REPO, 'reference')

AUTHOR = os.environ.get('CK3_AUTHOR', 'Matheus Alves')     # LICENSE of new mods
PIPELINE_REPO = 'sujirodev/ck3-mod-maker'                   # reusable release workflow used by the mods
PIPELINE_REF = 'v1'

CK3_VERSION = '1.19.0.6'   # the only executable the DLL knows (fixed addresses)
CMD_MAX = 500              # the loader cuts a request at 511 characters
