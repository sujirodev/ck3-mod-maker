# Processo do jogo: abrir (opcionalmente continuando um save), fechar e conferir versao.
import os, re, shutil, subprocess, time
from . import config
from .client import Ck3


def running():
    r = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq ck3.exe', '/NH'], capture_output=True, text=True)
    return 'ck3.exe' in r.stdout


def close():
    """Encerra o processo, sem o autosave de saida."""
    subprocess.run(['taskkill', '/F', '/IM', 'ck3.exe'], capture_output=True)
    for _ in range(60):
        if not running():
            break
        time.sleep(0.5)
    time.sleep(2)


def _continuar(save):
    # o --continuelastsave abre o save cujo titulo esta no continue_game.json
    cj = os.path.join(config.DOCS, 'continue_game.json')
    if os.path.exists(cj) and not os.path.exists(cj + '.original'):
        shutil.copy(cj, cj + '.original')
    j = open(cj, encoding='utf-8').read() if os.path.exists(cj) else '{\n\t"title":\t""\n}\n'
    j = re.sub(r'"title":\s*"[^"]*"', '"title":\t"%s"' % save.removesuffix('.ck3'), j)
    with open(cj, 'w', encoding='utf-8', newline='\n') as f:
        f.write(j)


def start(save=None, debug=True, nographics=False, wait=True, timeout=600):
    """Abre o jogo. Com save, continua dele e (wait) espera a partida carregada e pausada."""
    if running():
        raise RuntimeError('o jogo ja esta aberto; use load(save) para trocar de save sem reabrir')
    args = [os.path.join(config.BIN, 'ck3.exe')]
    if debug:
        args.append('-debug_mode')
    if save:
        _continuar(save)
        args.append('--continuelastsave')
    if nographics:
        args.append('-nographics')
    c = Ck3()
    pos = c.size()
    subprocess.Popen(args, cwd=config.BIN)
    if not (wait and save):
        return None
    t0 = time.time()
    pediu = False
    while time.time() - t0 < timeout:
        time.sleep(5)
        if not pediu and time.time() - t0 > 240:
            # o jogo pode parar no menu principal sem continuar o save; o pedido carregar resolve
            try:
                pediu = 'pedido enviado' in c.req('carregar save games/%s.ck3' % save.removesuffix('.ck3'), 1.0)
            except Exception:
                pass
        try:
            st = c.state()
            if st['ok'] and st['carregado'] and not st['correndo']:
                return st
        except Exception:
            pass
    raise TimeoutError('partida nao carregou em %d s (log da DLL desde a abertura: %d bytes)'
                       % (timeout, c.size() - pos))


def version():
    """Versao do executavel pelo launcher-settings (a DLL so conhece a 1.19.0.6)."""
    p = os.path.join(config.GAME, 'launcher', 'launcher-settings.json')
    if os.path.exists(p):
        m = re.search(r'"rawVersion":\s*"([^"]+)"', open(p, encoding='utf-8').read())
        if m:
            return m.group(1)
    return None
