# Game process: start (optionally continuing a save), close, check the version.
import os, re, shutil, subprocess, time
from . import config
from .client import Ck3


def running():
    r = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq ck3.exe', '/NH'], capture_output=True, text=True)
    return 'ck3.exe' in r.stdout


def close():
    """Kill the process, without the exit autosave."""
    subprocess.run(['taskkill', '/F', '/IM', 'ck3.exe'], capture_output=True)
    for _ in range(60):
        if not running():
            break
        time.sleep(0.5)
    time.sleep(2)


def _continue_from(save):
    # --continuelastsave opens the save whose title is in continue_game.json
    cj = os.path.join(config.DOCS, 'continue_game.json')
    if os.path.exists(cj) and not os.path.exists(cj + '.original'):
        shutil.copy(cj, cj + '.original')
    j = open(cj, encoding='utf-8').read() if os.path.exists(cj) else '{\n\t"title":\t""\n}\n'
    j = re.sub(r'"title":\s*"[^"]*"', '"title":\t"%s"' % save.removesuffix('.ck3'), j)
    with open(cj, 'w', encoding='utf-8', newline='\n') as f:
        f.write(j)


def start(save=None, debug=True, nographics=False, wait=True, timeout=600):
    """Start the game. With a save, continue from it and (wait) block until it is loaded and paused."""
    if running():
        raise RuntimeError('the game is already open; use load(save) to switch saves without restarting')
    args = [os.path.join(config.BIN, 'ck3.exe')]
    if debug:
        args.append('-debug_mode')
    if save:
        _continue_from(save)
        args.append('--continuelastsave')
    if nographics:
        args.append('-nographics')
    c = Ck3()
    pos = c.size()
    subprocess.Popen(args, cwd=config.BIN)
    if not (wait and save):
        return None
    t0 = time.time()
    asked = False
    while time.time() - t0 < timeout:
        time.sleep(5)
        if not asked and time.time() - t0 > 240:
            # the game may stop at the main menu without continuing the save; the `carregar` request fixes it
            try:
                asked = 'pedido enviado' in c.req('carregar save games/%s.ck3' % save.removesuffix('.ck3'), 1.0)
            except Exception:
                pass
        try:
            st = c.state()
            if st['ok'] and st['loaded'] and not st['running']:
                return st
        except Exception:
            pass
    raise TimeoutError('save did not load within %d s (DLL log since start: %d bytes)' % (timeout, c.size() - pos))


def version():
    """Executable version from launcher-settings (the DLL only knows 1.19.0.6)."""
    p = os.path.join(config.GAME, 'launcher', 'launcher-settings.json')
    if os.path.exists(p):
        m = re.search(r'"rawVersion":\s*"([^"]+)"', open(p, encoding='utf-8').read())
        if m:
            return m.group(1)
    return None
