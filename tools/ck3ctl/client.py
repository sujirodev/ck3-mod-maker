# Cliente do canal de pedidos da DLL ck3_custom (dll/LEIAME.md): escreve uma linha em ck3_custom_cmd.txt, o
# carregador le, apaga o arquivo e executa na thread principal do jogo; a resposta sai em ck3_custom_dll.log.
import os, re, time
from . import config


class SemResposta(Exception):
    """O carregador nao apagou o arquivo de pedidos: jogo fechado, sem a DLL, ou sem -debug_mode."""


class Ck3:
    def __init__(self, instancia=None):
        # instancia: varios jogos ao mesmo tempo (AR_INSTANCIA=<x> no ambiente do jogo); log e pedidos proprios
        suf = '_%s' % instancia if instancia else ''
        self.log = os.path.join(config.BIN, 'ck3_custom_dll%s.log' % suf)
        self.cmd = os.path.join(config.BIN, 'ck3_custom_cmd%s.txt' % suf)

    # ---- canal ----
    def size(self):
        return os.path.getsize(self.log) if os.path.exists(self.log) else 0

    def since(self, pos):
        if not os.path.exists(self.log):
            return ''
        with open(self.log, 'rb') as f:
            if os.path.getsize(self.log) < pos:   # log recriado (jogo reaberto)
                pos = 0
            f.seek(pos)
            return f.read().decode('latin1')

    def send(self, line, timeout=5.0):
        if len(line) > config.CMD_MAX:
            raise ValueError('pedido com %d letras; o carregador corta em 511 (use um scripted_effect do mod)'
                             % len(line))
        t0 = time.time()
        while os.path.exists(self.cmd):   # pedido anterior ainda nao lido
            if time.time() - t0 > timeout:
                raise SemResposta('pedido anterior nao foi lido em %.0f s' % timeout)
            time.sleep(0.05)
        with open(self.cmd, 'w', encoding='latin1', newline='\n') as f:
            f.write(line + '\n')
        t0 = time.time()
        while os.path.exists(self.cmd):
            if time.time() - t0 > timeout:
                os.remove(self.cmd)
                raise SemResposta('a DLL nao leu o pedido em %.0f s (jogo aberto com -debug_mode?)' % timeout)
            time.sleep(0.05)

    def req(self, line, wait=0.4, until=None, timeout=8.0):
        """Envia e devolve o que o log ganhou. until: regex que encerra a espera antes de `timeout`."""
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
        except SemResposta:
            return False

    # ---- estado do jogo ----
    def state(self):
        t = self.req('state', 0.2, until=r'state: .*velocidade=\d+')
        m = re.findall(r'state: (.*?) velocidade=(\d+)', t)
        if not m:
            return {'ok': False, 'texto': t.strip()}
        s, v = m[-1][0], int(m[-1][1])
        return {'ok': True, 'texto': s, 'velocidade': v, 'carregado': v > 0, 'correndo': s.startswith('CORRENDO'),
                'pelo_jogador': '[pelo jogador]' in s, 'evento': 'evento/mensagem' in s, 'outra': '[outra pausa]' in s}

    def pause(self, on=True):
        for _ in range(6):
            self.send('pause %d' % (1 if on else 0))
            time.sleep(0.3)
            st = self.state()
            if st['ok'] and (not st['correndo']) == on:
                return True
        return False

    def speed(self, v=5):
        return self.req('velocidade %d' % v, 0.2, until=r'velocidade:')

    def info(self):
        return self.req('info', 0.2, until=r'info:')

    def _carregado(self):
        st = self.state()
        if not st['ok'] or not st['carregado']:
            # efeito durante a carga derruba o jogo (C0000005 em exe+0xe3ea3e)
            raise RuntimeError('partida nao carregada (%s); efeito agora derrubaria o jogo' % st.get('texto'))

    # ---- script ----
    def effect(self, text):
        """Roda um efeito de script no personagem do jogador. Devolve (ok, mensagem, log)."""
        self._carregado()
        t = self.req('efeito ' + text, 0.3, until=r'console: (direto: |efeito recusado)')
        m = re.findall(r'console: direto: (ok|FALHOU)(?:: (.*))?', t)
        if not m:
            return False, (re.findall(r'console: (efeito recusado.*)', t) or ['sem resposta'])[-1], t
        return m[-1][0] == 'ok', m[-1][1], t

    def eval(self, value, scope=None):
        """Valor de script (ex.: 'gold', 'var:x', 'num_of_relation_friend') no jogador ou no escopo dado
        (ex.: 'character:123', 'title:k_france.holder'). Le pelo efeito ck3_custom_ping do nucleo."""
        eff = 'ck3_custom_ping = %s' % value
        if scope:
            eff = '%s = { %s }' % (scope, eff)
        return self.eval_raw(eff)

    def trigger(self, text, scope=None):
        """Testa um trigger de script no jogador (ou no escopo). True/False."""
        inner = 'if = { limit = { %s } ck3_custom_ping = 1 } else = { ck3_custom_ping = 0 }' % text
        return self.eval_raw(inner if not scope else '%s = { %s }' % (scope, inner)) == 1

    def eval_raw(self, eff):
        """Roda o efeito e devolve o ultimo valor de ck3_custom_ping que ele gravou."""
        ok, msg, t = self.effect(eff)   # o ping roda dentro do efeito: a linha dele vem antes de "direto: ok"
        v =re.findall(r'efeito ck3_custom_ping: .*?valor=([-0-9.]+)', t)
        if not ok or not v:
            raise RuntimeError('falhou: %s' % (msg or 'sem valor no log'))
        return float(v[-1])

    def var(self, tipo, ident, nome, valor=None, dias=None, apagar=False):
        """Variavel de script no objeto: tipo 'char'|'prov'|'army' ou numero (4, 8, 0x1b)."""
        tipo = {'char': 4, 'prov': 8, 'army': 0x1b}.get(tipo, tipo)
        line = 'var %s %s %s' % (tipo, ident, nome)
        if apagar:
            line += ' del'
        elif valor is not None:
            line += ' set %s%s' % (valor, ' %d' % dias if dias else '')
        return self.req(line, 0.2, until=r'var %s' % re.escape(nome))

    def console(self, text):
        return self.req('console ' + text, 0.4, until=r'console:')

    # ---- eventos, saves, plugins ----
    def events(self, answer=False, option=0):
        return self.req('events answer %d' % option if answer else 'events', 0.3, until=r'events: ')

    def save(self):
        return self.req('salvar', 0.5, until=r'salvar')

    def load(self, nome, timeout=300):
        """Recarrega save games/<nome>.ck3 com o jogo aberto (~15 s) e espera a partida pausada."""
        self.pause(True)
        t = self.req('carregar save games/%s.ck3' % nome.removesuffix('.ck3'), 1.0, until=r'carregar: ')
        if 'pedido enviado' not in t:
            raise RuntimeError('carregar falhou: %s' % t.strip())
        return self.wait_loaded(timeout, inicio=6)

    def wait_loaded(self, timeout=600, inicio=0):
        time.sleep(inicio)
        t0 = time.time()
        while time.time() - t0 < timeout:
            try:
                st = self.state()
                if st['ok'] and st['carregado'] and not st['correndo']:
                    return st
            except SemResposta:
                pass
            time.sleep(3)
        raise TimeoutError('partida nao carregou em %d s' % timeout)

    def plugins(self, recompilar=False):
        return self.req('plugin recompilar' if recompilar else 'plugin', 0.5 if recompilar else 0.2,
                        until=r'plugin', timeout=10)

    def faults(self, desde=0):
        """Linhas FALHA CONTIDA no log da DLL a partir da posicao `desde`."""
        return [l for l in self.since(desde).splitlines() if 'FALHA CONTIDA' in l]
