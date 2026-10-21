# ck3_custom: DLL generica de plugins de mod

Fonte movida do mod Auto Raid para o ck3-mod-maker em 2026-10-21. O Auto Raid continua sendo o primeiro mod que a usa
(`Documents/Paradox Interactive/Crusader Kings III/mod/auto_raid`).

Vale so para o `ck3.exe` 1.19.0.6. Em qualquer outro executavel o carregador fica inativo. Arquitetura em
docs/dll/arquitetura_dll.md.

## Partes
| Arquivo | Papel | Troca com o jogo aberto? |
|---|---|---|
| `winmm.dll` (fonte `ar_proxy.c`, `exports.h`, `proxy.def`) | Carregador fixo: repassa a `winmm.dll` do sistema, intercepta o envio de comandos, le os pedidos, protege contra falha, registra os efeitos declarados nos `plugin/plugin.txt` dos mods ativos, monta a aba Mods e carrega o nucleo | Nao (fechar o jogo) |
| `ck3_custom_core.dll` (fonte `ar_core.c`) | Nucleo generico: primitivas do jogo, comandos, variaveis, expressoes de script, gravador, pedidos de desenvolvimento e o compilador de plugins (libtcc embutido) | Sim, em `-debug_mode`: basta copiar o arquivo novo para `C:\CK3\binaries`; o carregador percebe e recarrega |
| `ar_host.h` | Interface carregador / nucleo | - |
| `ck3_api.h` | Interface nucleo / plugins de mod, por ids (docs/dll/arquitetura_dll.md, secao 5) | - |
| `<mod>/plugin/` (ex.: `auto_raid/plugin/`: `plugin.txt`, `auto_raid.c`, `ar_decisor.c/.h`) | Plugin de mod (o do Auto Raid como exemplo) em fonte C: efeitos `auto_raid_*`, regras do mod e decisor; compilado pelo nucleo com o TinyCC | Sim, em `-debug_mode`: recompila quando um `.c`/`.h` da pasta muda. Efeito ou variavel nova no `plugin.txt`: reabrir o jogo |
| `gen_exports.py` | Gera `exports.h` e `proxy.def` a partir da DLL do sistema | - |

## Compilar (na pasta `dll`, com o GCC do WinLibs no PATH)
Pelo repo: `uv run ck3ctl dll build [carregador|nucleo|todas]` (saida em `dll/build/`) e
`uv run ck3ctl dll install [nucleo|carregador|todas]`. A mao:
```
python gen_exports.py winmm
gcc -shared -O2 -Wall -Wno-format-truncation -static -o winmm.dll ar_proxy.c proxy.def
gcc -shared -O2 -Wall -Wno-format-truncation -Wno-restrict -static -I<tinycc> -o ck3_custom_core.dll ar_core.c <tinycc>/win32/libtcc.o
```
`<tinycc>`: fontes do TinyCC (`https://repo.or.cz/tinycc.git`, commit `43c7708`; nesta maquina em
`C:\Users\Matheus\src\tinycc`), com `win32/libtcc.o`, `win32/include` e `win32/lib/libtcc1.a` montados como em
`dll/re/prova_tcc/LEIAME.md`.

Instalar em `C:\CK3\binaries`: `winmm.dll`, `ck3_custom_core.dll` e a pasta `ck3_custom\` com o apoio do TinyCC
(`include\` = o `win32/include` do TinyCC mais o `ck3_api.h`; `lib\libtcc1.a`). Os plugins ficam na pasta de cada mod
(`plugin/plugin.txt`) e sao achados pelos mods ativos (`dlc_load.json`). Desinstalar: apagar os dois arquivos e a pasta
`ck3_custom`.

## Producao x desenvolvimento (2026-10-09)
Sem `-debug_mode` (producao): o carregador nao le `ck3_custom_cmd.txt` nem pede o `status`, o nucleo descarta o log dos
plugins (api 7, `debug_mode()`) e o plugin do Auto Raid nao roda o painel de debug (`ar_dbg_*`), o
`painel_auto_raid.json` nem a conferencia do atrito. Com `-debug_mode` tudo funciona como antes. O pacote de publicacao
(auto_raid: `python docs/pacote.py`) deixa de fora os arquivos de teste e de depuracao do mod (nomes com `teste`/`debug`, o
`gui/hud_sidebars.gui` do painel de debug), docs, fontes da DLL e copias. O jogador liga e desliga em Configuracoes >
Mods > Auto Raid > "Ligado" (`auto_raid_ligado`, padrao desligado); nao ha decisao.

## Uso
- Pedido: escrever uma linha em `C:\CK3\binaries\ck3_custom_cmd.txt`. O carregador le, apaga o arquivo e executa
  na thread principal do jogo. Pedido que o nucleo nao conhece vai aos plugins.
- Resposta: `C:\CK3\binaries\ck3_custom_dll.log`.
- Dados: `C:\CK3\binaries\ck3_custom\` (`gravacao\`, `mapa\`, `configuracoes_mods.txt`).
- Painel: o plugin do Auto Raid grava `ck3_custom\painel_auto_raid.json` a cada pulso (API 3, `file_write`);
  `docs/testes/painel.py` do auto_raid mostra o jogo (governante, exercitos, decisoes e resultados) sem a interface.
- Jogo sem graficos (`-nographics`): nao ha janela; a fila e atendida dentro de `PeekMessageW` na thread principal
  (gancho na tabela de importacao do ck3.exe, escolhido pelo batimento a cada 5 s). Com `-nographics` ou `AR_DIAG=1` o
  carregador grava batimentos (chamadas por segundo dos ganchos e amostras de onde a thread principal esta);
  `AR_BOMBA=<funcao>` forca o gancho. Ver docs/dll/plano_sem_graficos.md.
- Se um pedido ou efeito causar violacao de acesso, o carregador contem a falha e grava "FALHA CONTIDA" com o
  endereco, em vez de derrubar o jogo. Depois de uma falha contida o estado do jogo pode estar inconsistente: nao salvar.

## Pedidos
| Pedido | O que faz |
|---|---|
| `info` | Base, personagem do jogador, tamanho das tabelas, ultima unidade comandada |
| `where <unidade\|last>` | Provincia atual, dono e caminho em curso da unidade |
| `eta <unidade\|last> <provincia>` | Dias ate a provincia (calculo do tooltip do jogo) e o caminho achado |
| `move <unidade\|last> <provincia> [queue] [ia]` | Ordem de marcha (`queue` = entra na fila; `ia` = enviada como a IA, `valid_for` 2: aceita, mas ignorada no nosso exercito) |
| `route <unidade\|last> <p1> <p2> ...` | Rota por pontos: o primeiro substitui, os demais entram na fila |
| `units [all]` | Lista as unidades do jogador (ou todas) |
| `threats <unidade\|last> [dias]` | Unidades alheias cujo caminho cruza a nossa provincia ou a nossa rota, com dias ate o cruzamento |
| `peek <endereco \| exe+RVA \| unit:<id> \| prov:<id> \| army:<id>> [bytes]` | Mostra a memoria (leitura segura) |
| `procura <valor hex \| exe+RVA> [max]` | Enderecos da memoria propria do processo que guardam o valor de 64 bits (thread separada, ~80 s; uma busca por vez) |
| `layout <janela hex> <arquivo>` / `layout_tirar <janela hex> <indice>` | Prova C2b: acrescenta / tira paginas da janela de configuracoes |
| `config recarregar` | Rele os `plugin/config.txt` dos mods, registra opcoes novas e refaz a aba Mods (carregador; em `-debug_mode` e automatico quando o arquivo muda) |
| `cfg [nome valor]` | Lista as opcoes da aba Mods com o valor; com nome e valor, muda a opcao pelo caminho da interface |
| `mods` / `mods_sel <n>` | Mostra a lista de botoes da aba Mods / as opcoes do mod n (-1 = lista) |
| `gravar on <nome>` / `gravar off` / `gravar` | Gravacao (auto_raid: docs/simulador.md, 6.1) em `ck3_custom\gravacao\<nome>.jsonl` |
| `mapa <unidade> <raio px> <nome> [vizinhanca px]` | Grafo das provincias ao redor da unidade pelo pathfinder do jogo (auto_raid: docs/simulador.md, 6.2), em `ck3_custom\mapa\<nome>.jsonl`; usar com o jogo pausado |
| `tabela <unidade> <raio px> <nome> <de> <ate> [raio_dest px]` | Ponte de extracao (auto_raid: docs/ia_reativa.md, 9.3): ETA exata do jogo (pathfinder + tempo de viagem, com a velocidade da unidade no lugar onde ela esta) das origens de indice [de, ate) no raio ate as provincias a ate `raio_dest` px (padrao 400), em `ck3_custom\mapa\<nome>.bin`; em blocos de poucas origens (o jogo para enquanto o bloco roda; `de` = 0 recomeca o arquivo) |
| `etafrom <unidade> <de> <ate>` | Dias de `de` ate `ate` com a velocidade da unidade (sonda de velocidade) |
| `var <tipo> <id> <nome> [set <valor> [dias] \| del]` | Le (tipo, valor, prazo) ou muda uma variavel de script no objeto do escopo (tipo 4 personagem, 8 provincia, 0x1b exercito) |
| `expr <texto>` / `ctxprova <tipo> <id> <texto>` | Provas A3 e SIM2b: o jogo le a expressao de script; o nucleo a avalia no proximo `ck3_custom_ping` (no exercito / no escopo dado, com o contexto daquele efeito) |
| `console <texto>` | Poe o texto na fila do console do jogo, como se fosse digitado (ex.: `console effect ...`) |
| `cmdlog on|all|off` | Grava todo comando enviado pela thread principal (nome da classe e bytes); `all`: de todas as threads, classes de exercito (Move, Halt, Raid, Loot, Unit, Army...) com a thread e a marcha inteira (0x168 e o caminho), demais classes so pelo nome ||all|off` | Grava todo comando enviado pela thread principal (nome da classe e bytes), para descobrir os outros comandos; `all`: de todas as threads, so classes de exercito (Move, Halt, Raid, Loot, Unit, Army...), com a thread |\|off` | Grava todo comando enviado pela thread principal (nome da classe e bytes), para descobrir os outros comandos |
| `state` | Estado de pausa (jogador, evento/mensagem, outra) e velocidade |
| `events [dump \| answer [opcao]]` | Lista os eventos pendentes do jogador; `answer` responde todos (padrao: primeira opcao) e fecha a tela de sucessao |
| `salvar` | Salva a partida (grava `autosave.ck3`, girando os autosaves anteriores) |
| `carregar [testar] save games/<nome>.ck3` | Recarrega um save com o jogo aberto (~15 s); `testar` so confere se o arquivo existe |
| `velocidade <1..5>` | Grava a velocidade do jogo (campo lido pelo `state`); para rodadas sem janela (`-nographics`) |
| `status <canal>` | Grava `ck3_custom\status.json` (pausa, velocidade, data do jogo em `[game_state]+0x9c`); o carregador pede a cada segundo, fora do log (painel `docs/testes/painel.py` do auto_raid) |
| `efeito <texto>` | Roda um efeito de script no personagem do jogador pela funcao do comando de console `effect` (`0xe3e8b0`), sem passar pelo console. Sem console (`-nographics`), `console effect ...` e `console event <id>` (vira `trigger_event`) vao por este caminho |
| `busca32 <gs\|gs2\|endereco> <valor> [bytes]` | Deslocamentos do bloco que guardam o valor de 32 bits (para achar campos) |
| `mudo on\|off` | Teste: o nucleo deixa de repassar os efeitos aos plugins (simula o jogo sem a DLL) |
| `plugin [recompilar]` | Plugins achados e se estao carregados; `recompilar` compila de novo |
| `cmdraw <vtable> <vtable2> <flags> [campos]` | Envia um comando simples do jogo (campos de 32 bits a partir de +0x20), para investigacao |
| `levas`, `target`, `scan` | Do plugin do Auto Raid |

`last` = ultima unidade que o jogador comandou por clique desde que o nucleo foi carregado.

## Efeitos de script
O carregador registra os efeitos na inicializacao do jogo, antes da leitura dos scripts, a partir do
`plugin/plugin.txt` de cada mod ativo, mais o `ck3_custom_ping` (qualquer escopo) do proprio nucleo. Linhas do
manifesto (formato completo em docs/dll/arquitetura_dll.md, secao 6):
- `nome <mod>`, `api <versao>`, `fonte <arquivo.c>`: para o nucleo.
- `<efeito>`: efeito de escopo exercito com um valor (`auto_raid_mover = 7336`, `= var:ar_alvo`).
- `<efeito> livre`: o mesmo, aceito em qualquer escopo.
- `var <nome>`: variavel de resposta gravada pelo plugin (evita o aviso "used but never set" no error.log).
Nome repetido em outro plugin e recusado com aviso no log. A execucao vai ao nucleo, que a entrega ao plugin que
declarou o efeito; nome novo de efeito ou de variavel exige reabrir o jogo. Enderecos em docs/dll/re/notas_efeitos.md.

Efeitos do Auto Raid (`plugin/auto_raid.c`):
| Efeito | Resposta |
|---|---|
| `auto_raid_ping`, `auto_raid_ping_livre`, `auto_raid_chamar = 1` | `ar_resp` = dobro do valor; o `chamar` do jogador tambem copia a aba Mods para as `ar_cfg_*` e marca `ar_gravando` com a gravacao ligada |
| `auto_raid_chamar = 2` (personagem do jogador) | roda o decisor (`plugin/ar_decisor.c`), a maquina de estados do ciclo; avisos em `ar_av_<chave>` e `ar_pedido_levantar`, mostrados pelo `ar_pos_decisor` do script |
| `auto_raid_estado` | `ar_saque`, `ar_capacidade`, `ar_provincia`, `ar_movendo`, `ar_destino` (0 = parado), `ar_saqueando`, `ar_saque_dias` |
| `auto_raid_dias = <provincia>` | `ar_dias` (-1 = sem caminho) |
| `auto_raid_alvo = <raio>` | `ar_alvo` (0 = nenhum), `ar_alvo_prov`, `ar_alvo_levas`, `ar_alvo_ouro_dia`, `ar_alvo_ouro`, `ar_alvo_marcha`, `ar_alvo_saque_dias` |
| `auto_raid_ameaca` | `ar_ameaca` (tamanho do grupo), `ar_ameaca_dias`, `ar_chance`, `ar_perigo` (1 = grupo com chance abaixo do limite) |
| `auto_raid_seguro = <provincia>` | `ar_seguro` (1 = seguro ate o fim do saque mais a margem), `ar_chance` |
| `auto_raid_casa`/`auto_raid_fuga = 0` ... `auto_raid_consulta = 1` em cada provincia ... `= 1` | `ar_casa`, `ar_casa_dias` / `ar_fuga`, `ar_fuga_dias`, `ar_fuga_segura` |
| `auto_raid_retrato` (provincia) | linha `ret` na gravacao (retrato mensal, SIM1b) |
| `auto_raid_mover`, `auto_raid_mover_fila`, `auto_raid_parar`, `auto_raid_modo_saque`, `auto_raid_modo_raid`, `auto_raid_dividir`, `auto_raid_dissolver`, `auto_raid_pausar`, `auto_raid_camera`, `auto_raid_levantar` (provincia) | comando |
| `auto_raid_juntar`, `auto_raid_saque_fim` | nome reservado |

Configuracao lida pelo plugin em variaveis do personagem do jogador (`ar_cfg_*`, copiadas da aba Mods). So para
teste: `ar_cfg_teste_todos = 1` trata qualquer unidade alheia como hostil; `ar_ia_exp` no jogador (experimentos da IA
reativa) faz o pulso gravar sem rodar o decisor; o pedido `ia_area [raio]` grava no proximo pulso fe, cultura,
heranca, alianca e titulo de jure de cada provincia (hipoteses da area amiga). Gravacao (auto_raid: docs/ia_reativa.md, 9): linha
`vel` por pulso (sondas de velocidade e comandante), `dn` nas linhas `uni` e linha `recusa` (entradas vivas da recusa
de alvo).

## Configuracoes dos mods (aba Mods)
O carregador acha os mods ativos (`dlc_load.json`) que tem `plugin/config.txt`, registra as opcoes no grupo `mods`
do `pdx_settings.txt` e acrescenta a aba "Mods" na janela de configuracoes do jogo, gerada a partir desses arquivos.
Em `-debug_mode`, uma mudanca no `config.txt` refaz as opcoes e a aba com o jogo aberto. Copia da secao `mods` em
`C:\CK3\binaries\ck3_custom\configuracoes_mods.txt`, devolvida ao `pdx_settings.txt` se o jogo a apagou (aberto sem a
DLL). A aba mostra um botao por mod; o clique mostra as opcoes daquele mod (nucleo). Formato e estado:
docs/dll/arquitetura_dll.md, secao 11. Tipos: `liga`, `inteiro` e `real` (barra).
