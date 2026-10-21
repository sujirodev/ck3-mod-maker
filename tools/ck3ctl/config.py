# Caminhos da maquina. Cada um pode ser trocado por variavel de ambiente.
import os

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
GAME = os.path.normpath(os.environ.get('CK3_GAME', r'C:\CK3'))                       # instalacao do jogo (com binaries\ e game\)
BIN = os.path.join(GAME, 'binaries')
VANILLA = os.path.join(GAME, 'game')
DOCS = os.path.normpath(os.environ.get('CK3_DOCS', os.path.join(os.path.expanduser('~'), 'Documents', 'Paradox Interactive',
                                                                  'Crusader Kings III')))
MOD_DIR = os.path.join(DOCS, 'mod')
SAVES = os.path.join(DOCS, 'save games')
GAME_LOGS = os.path.join(DOCS, 'logs')
DLC_LOAD = os.path.join(DOCS, 'dlc_load.json')
TINYCC = os.path.normpath(os.environ.get('CK3_TINYCC', os.path.join(os.path.expanduser('~'), 'src', 'tinycc')))

DLL_SRC = os.path.join(REPO, 'dll')
DLL_BUILD = os.path.join(DLL_SRC, 'build')
TEMPLATES = os.path.join(REPO, 'templates')
REFERENCE = os.path.join(REPO, 'reference')

AUTHOR = os.environ.get('CK3_AUTHOR', 'Matheus Alves')                  # LICENSE dos mods novos
PIPELINE_REPO = 'sujirodev/ck3-mod-maker'                          # workflow reutilizavel dos mods
PIPELINE_REF = 'v1'

CK3_VERSION = '1.19.0.6'   # unico executavel que a DLL conhece (enderecos fixos)
CMD_MAX = 500              # o carregador corta o pedido em 511 letras
