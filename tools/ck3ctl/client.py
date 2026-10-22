# Client for the ck3_custom DLL request channel (dll/README.md): writes one line to ck3_custom_cmd.txt, the loader
# reads it, deletes the file and runs it on the game's main thread; the answer goes to ck3_custom_dll.log.
# The DLL writes its log in Portuguese; the regexes below match that text on purpose.
import os, re, time
from . import config


class NoAnswer(Exception):
    """The loader did not pick up the request file: game closed, no DLL, or no -debug_mode."""


class Ck3:
    def __init__(self, instance=None):
        # instance: several games at once (AR_INSTANCIA=<x> in the game's environment); own log and request file
        suf = '_%s' % instance if instance else ''
        self.log = os.path.join(config.BIN, 'ck3_custom_dll%s.log' % suf)
        self.cmd = os.path.join(config.BIN, 'ck3_custom_cmd%s.txt' % suf)

    # ---- channel ----
    def size(self):
        return os.path.getsize(self.log) if os.path.exists(self.log) else 0

    def since(self, pos):
        if not os.path.exists(self.log):
            return ''
        with open(self.log, 'rb') as f:
            if os.path.getsize(self.log) < pos:   # log recreated (game restarted)
                pos = 0
            f.seek(pos)
            return f.read().decode('latin1')

    def send(self, line, timeout=5.0):
        if len(line) > config.CMD_MAX:
            raise ValueError('request has %d characters; the loader cuts at 511 (use a scripted_effect in the mod)'
                             % len(line))
        t0 = time.time()
        while os.path.exists(self.cmd):   # previous request not read yet
            if time.time() - t0 > timeout:
                raise NoAnswer('previous request was not read within %.0f s' % timeout)
            time.sleep(0.05)
        with open(self.cmd, 'w', encoding='latin1', newline='\n') as f:
            f.write(line + '\n')
        t0 = time.time()
        while os.path.exists(self.cmd):
            if time.time() - t0 > timeout:
                os.remove(self.cmd)
                raise NoAnswer('the DLL did not read the request within %.0f s (game open with -debug_mode?)' % timeout)
            time.sleep(0.05)

    def req(self, line, wait=0.4, until=None, timeout=8.0):
        """Send a request and return what the log gained. until: regex that ends the wait before `timeout`."""
        pos = self.size()
        self.send(line)
        time.sleep(wait)
        out = self.since(pos)
        t0 = time.time()
        while until and not re.search(until, out) and time.time() - t0 < timeout:
            time.sleep(0.1)
            out = self.since(pos)
        return out

    def tail(self, n=40):
        if not os.path.exists(self.log):
            return ''
        with open(self.log, 'rb') as f:
            f.seek(max(0, os.path.getsize(self.log) - 200 * n))
            return '\n'.join(f.read().decode('latin1').splitlines()[-n:])

    def alive(self):
        try:
            return 'state:' in self.req('state', 0.2, until=r'state:', timeout=2)
        except NoAnswer:
            return False

    # ---- game state ----
    def state(self):
        t = self.req('state', 0.2, until=r'state: .*velocidade=\d+')
        m = re.findall(r'state: (.*?) velocidade=(\d+)', t)
        if not m:
            return {'ok': False, 'text': t.strip()}
        s, v = m[-1][0], int(m[-1][1])
        return {'ok': True, 'text': s, 'speed': v, 'loaded': v > 0, 'running': s.startswith('CORRENDO'),
                'paused_by_player': '[pelo jogador]' in s, 'paused_by_event': 'evento/mensagem' in s,
                'paused_other': '[outra pausa]' in s}

    def pause(self, on=True):
        for _ in range(6):
            self.send('pause %d' % (1 if on else 0))
            time.sleep(0.3)
            st = self.state()
            if st['ok'] and (not st['running']) == on:
                return True
        return False

    def speed(self, v=5):
        return self.req('velocidade %d' % v, 0.2, until=r'velocidade:')

    def info(self):
        return self.req('info', 0.2, until=r'info:')

    def _require_loaded(self):
        st = self.state()
        if not st['ok'] or not st['loaded']:
            # an effect while a save is loading crashes the game (C0000005 at exe+0xe3ea3e)
            raise RuntimeError('no save loaded (%s); an effect now would crash the game' % st.get('text'))

    # ---- script ----
    def effect(self, text):
        """Run a script effect on the player character. Returns (ok, message, log)."""
        self._require_loaded()
        t = self.req('efeito ' + text, 0.3, until=r'console: (direto: |efeito recusado)')
        m = re.findall(r'console: direto: (ok|FALHOU)(?:: (.*))?', t)
        if not m:
            return False, (re.findall(r'console: (efeito recusado.*)', t) or ['no answer'])[-1], t
        return m[-1][0] == 'ok', m[-1][1], t

    def eval(self, value, scope=None):
        """Script value (e.g. 'gold', 'var:x', 'num_of_relation_friend') on the player or on the given scope
        (e.g. 'character:123', 'title:k_france.holder'). Read through the core's ck3_custom_ping effect."""
        eff = 'ck3_custom_ping = %s' % value
        if scope:
            eff = '%s = { %s }' % (scope, eff)
        return self.eval_raw(eff)

    def trigger(self, text, scope=None):
        """Test a script trigger on the player (or the scope). True/False."""
        inner = 'if = { limit = { %s } ck3_custom_ping = 1 } else = { ck3_custom_ping = 0 }' % text
        return self.eval_raw(inner if not scope else '%s = { %s }' % (scope, inner)) == 1

    def eval_raw(self, eff):
        """Run the effect and return the last ck3_custom_ping value it logged."""
        ok, msg, t = self.effect(eff)   # the ping runs inside the effect: its line comes before "direto: ok"
        v = re.findall(r'efeito ck3_custom_ping: .*?valor=([-0-9.]+)', t)
        if not ok or not v:
            raise RuntimeError('failed: %s' % (msg or 'no value in the log'))
        return float(v[-1])

    def var(self, kind, ident, name, value=None, days=None, delete=False):
        """Script variable on an object: kind 'char'|'prov'|'army' or a number (4, 8, 0x1b)."""
        kind = {'char': 4, 'prov': 8, 'army': 0x1b}.get(kind, kind)
        line = 'var %s %s %s' % (kind, ident, name)
        if delete:
            line += ' del'
        elif value is not None:
            line += ' set %s%s' % (value, ' %d' % days if days else '')
        return self.req(line, 0.2, until=r'var %s' % re.escape(name))

    def console(self, text):
        return self.req('console ' + text, 0.4, until=r'console:')

    # ---- events, saves, plugins ----
    def events(self, answer=False, option=0):
        return self.req('events answer %d' % option if answer else 'events', 0.3, until=r'events: ')

    def save(self):
        return self.req('salvar', 0.5, until=r'salvar')

    def load(self, name, timeout=300):
        """Reload save games/<name>.ck3 with the game open (~15 s) and wait for the paused game."""
        self.pause(True)
        t = self.req('carregar save games/%s.ck3' % name.removesuffix('.ck3'), 1.0, until=r'carregar: ')
        if 'pedido enviado' not in t:
            raise RuntimeError('load failed: %s' % t.strip())
        return self.wait_loaded(timeout, initial=6)

    def wait_loaded(self, timeout=600, initial=0):
        time.sleep(initial)
        t0 = time.time()
        while time.time() - t0 < timeout:
            try:
                st = self.state()
                if st['ok'] and st['loaded'] and not st['running']:
                    return st
            except NoAnswer:
                pass
            time.sleep(3)
        raise TimeoutError('save did not load within %d s' % timeout)

    def plugins(self, recompile=False):
        return self.req('plugin recompilar' if recompile else 'plugin', 0.5 if recompile else 0.2,
                        until=r'plugin', timeout=10)

    def faults(self, since=0):
        """FALHA CONTIDA (contained fault) lines in the DLL log from position `since`."""
        return [l for l in self.since(since).splitlines() if 'FALHA CONTIDA' in l]
