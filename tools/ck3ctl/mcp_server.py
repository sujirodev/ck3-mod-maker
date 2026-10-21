# Servidor MCP: as mesmas operacoes do ck3ctl como ferramentas para o Claude (registrado em .mcp.json).
from mcp.server.mcpserver import MCPServer
from . import client, doctor, dll, game, logs, mods, reference, validate

mcp = MCPServer('ck3', instructions=(
    'Controle do Crusader Kings III 1.19.0.6 aberto com -debug_mode e a DLL ck3_custom. Antes de qualquer efeito, '
    'confira ck3_state: com "carregado" falso (carga em curso) um efeito derruba o jogo. Pedidos ate 500 letras; '
    'efeito maior vai para um scripted_effect do mod. Leia CLAUDE.md e dll/LEIAME.md.'))
C = client.Ck3()


@mcp.tool()
def ck3_doctor() -> str:
    """Inicio de sessao: versao do jogo, DLL instalada x build, referencia, jogo/DLL respondendo, mods e o resumo das
    funcionalidades de cada um. Rodar antes de trabalhar."""
    return doctor.fmt(doctor.run())


@mcp.tool()
def ck3_state() -> dict:
    """Pausa, velocidade e se ha partida carregada (velocidade 0 = sem partida ou carregando)."""
    return C.state()


@mcp.tool()
def ck3_game(acao: str, save: str = '', nographics: bool = False) -> str:
    """acao: 'status' | 'start' (abre em -debug_mode; com save, continua dele e espera carregar) | 'close'."""
    if acao == 'start':
        return str(game.start(save or None, nographics=nographics))
    if acao == 'close':
        game.close()
        return 'fechado'
    return str({'aberto': game.running(), 'dll_responde': game.running() and C.alive(), 'versao': game.version()})


@mcp.tool()
def ck3_pause(pausar: bool = True) -> bool:
    """Pausa ou despausa (confere pelo state)."""
    return C.pause(pausar)


@mcp.tool()
def ck3_speed(velocidade: int = 5) -> str:
    """Velocidade do jogo 1..5."""
    return C.speed(velocidade)


@mcp.tool()
def ck3_effect(texto: str) -> dict:
    """Roda um efeito de script no personagem do jogador (root). Ex.: 'add_gold = 100',
    'character:123 = { add_trait = brave }'. Devolve ok e a mensagem do jogo."""
    ok, msg, _ = C.effect(texto)
    return {'ok': ok, 'mensagem': msg}


@mcp.tool()
def ck3_eval(valor: str, escopo: str = '') -> float:
    """Valor de script no jogador ou no escopo (ex.: valor='gold', escopo='character:123'; valor='var:minha_var')."""
    return C.eval(valor, escopo or None)


@mcp.tool()
def ck3_trigger(texto: str, escopo: str = '') -> bool:
    """Testa um trigger no jogador ou no escopo. Ex.: 'is_at_war = yes', 'has_trait = brave'."""
    return C.trigger(texto, escopo or None)


@mcp.tool()
def ck3_request(linha: str, espera: float = 0.5) -> str:
    """Pedido cru a DLL (lista em dll/LEIAME.md: info, units, where, var, events, peek, cmdlog, cfg, mods...)."""
    return C.req(linha, espera)


@mcp.tool()
def ck3_dll_log(linhas: int = 40) -> str:
    """Fim do log da DLL (ck3_custom_dll.log): respostas, log dos plugins, FALHA CONTIDA."""
    return C.tail(linhas)


@mcp.tool()
def ck3_events(responder: bool = False, opcao: int = 0) -> str:
    """Eventos pendentes do jogador; responder=True responde todos (opcao 0 = primeira) e fecha sucessao."""
    return C.events(responder, opcao)


@mcp.tool()
def ck3_save_load(acao: str, save: str = '') -> str:
    """acao 'save' (grava autosave.ck3) ou 'load' (recarrega save games/<save>.ck3 com o jogo aberto, ~15 s)."""
    return C.save() if acao == 'save' else str(C.load(save))


@mcp.tool()
def ck3_plugin(recompilar: bool = False) -> str:
    """Plugins C achados nos mods ativos e se carregaram; recompilar=True compila de novo (erros com linha)."""
    return C.plugins(recompilar)


@mcp.tool()
def ck3_errors(mod: str = '', desde: int = 0, todos: bool = False) -> str:
    """Erros do error.log do jogo, agrupados. mod: so os que citam arquivos de mods/<mod>. desde: posicao de ck3_mark."""
    return logs.fmt(logs.errors(None if todos or not mod else mods.path(mod), desde)) or 'nenhum erro'


@mcp.tool()
def ck3_mark() -> dict:
    """Posicoes atuais do error.log e do log da DLL, para ler so o que vier depois de uma acao."""
    return {'error_log': logs.mark(), 'dll_log': C.size()}


@mcp.tool()
def ck3_mod(acao: str, nome: str = '', titulo: str = '', prefixo: str = '', resumo: str = '', repo: str = '',
            plugin: bool = False, config_mods: bool = False) -> str:
    """acao: 'list' | 'new' (repo git do mod em Documents/.../mod/<nome>, padrao guardian_calc_assistant, + estado
    em mods/<nome>; prefixo curto ex. 'tnt_ga'; resumo em ingles) | 'adopt' (registra mod existente; repo opcional)
    | 'package' (zip pelo pipeline compartilhado) | 'link' (arquivo .mod do launcher) | 'enable' | 'disable' (dlc_load.json, vale ao reabrir) | 'check' (validacao) | 'status'
    (funcionalidades.json + progresso.md: ler ao comecar a trabalhar no mod).
    plugin (plugin C) e config_mods (aba Mods) sao a ULTIMA OPCAO: fazem o mod exigir a DLL no jogador. Deixar False,
    salvo quando o script comprovadamente nao alcanca e o usuario aprovou (CLAUDE.md, "Regra principal")."""
    if acao == 'new':
        return mods.new(nome, titulo or nome, prefixo or None, resumo, plugin, config_mods)
    if acao == 'adopt':
        return mods.adopt(nome, repo or None, prefixo or None)
    if acao == 'package':
        return mods.package(nome)
    if acao == 'link':
        return mods.link(nome)
    if acao in ('enable', 'disable'):
        return str(mods.enable(nome, acao == 'enable'))
    if acao == 'status':
        return mods.status(nome)
    if acao == 'check':
        return str({'arquivos': validate.check_mod(mods.path(nome)), 'localizacao_faltando': validate.missing_loc(mods.path(nome))})
    return str(mods.listing())


@mcp.tool()
def ck3_ref(termo: str, tipo: str = '', texto_completo: bool = False, limite: int = 15) -> str:
    """Documentacao do script_docs: tipo effect | trigger | on_action | event_target | event_scope | modifier |
    data_type. termo e regex no nome (texto_completo procura tambem na descricao)."""
    es = reference.lookup(termo, tipo or None, limite, texto_completo)
    return '\n\n'.join('## [%s] %s\n%s' % (e['kind'], e['name'], e['text']) for e in es) or 'nada'


@mcp.tool()
def ck3_vanilla(padrao: str, pasta: str = '', limite: int = 40, contexto: int = 0) -> str:
    """Regex nos arquivos do jogo base (C:/CK3/game). pasta ex.: 'common/scripted_effects', 'events', 'gui'."""
    return '\n'.join('%s:%d: %s' % (h['file'], h['line'], h['text'])
                     for h in reference.grep_vanilla(padrao, pasta, limit=limite, context=contexto)) or 'nada'


@mcp.tool()
def ck3_dll(acao: str = 'status', parte: str = 'nucleo') -> str:
    """acao 'status' | 'build' | 'install'. parte: 'nucleo' (troca com o jogo aberto) | 'carregador' (jogo
    fechado) | 'todas'."""
    if acao == 'build':
        return dll.build(parte)[1]
    if acao == 'install':
        return str(dll.install(parte))
    return str(dll.status())


def main():
    mcp.run()


if __name__ == '__main__':
    main()
