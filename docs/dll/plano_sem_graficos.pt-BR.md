Versao em ingles: [plano_sem_graficos.md](plano_sem_graficos.md)

# Plano: DLL funcionando com o jogo sem graficos (`-nographics`) e painel de acompanhamento

Origem: teste de 2026-10-07 (docs/dossie_exercitos.md 9.6, docs/arquitetura_dll.md item 7). Com `-nographics` o
carregador, o nucleo e o plugin sobem, mas o processo nao cria janela. Todo pedido passa por
`PostMessage(WM_AR_COMMAND)` para a janela do jogo (`find_wnd` e `worker_thread` em `dll/ar_proxy.c`), entao sem
janela nada e atendido (descarte de 8 s). O `debug.log` parou no meio do "Load Save" e a memoria caiu para 0,8 GB: nao
se sabe se a simulacao anda sem graficos.

Fato conferido (objdump do ck3.exe 1.19.0.6): o jogo importa da nossa `winmm.dll` `timeGetTime`, `timeBeginPeriod`,
`timeEndPeriod`, `waveIn*` e `waveOut*`. Hoje essas exportacoes sao `jmp` direto para a DLL do sistema
(`dll/exports.h`); para servir de gancho, a escolhida tem que virar funcao C.

Regra: nada disto e feito com rodada em andamento (memoria "nao editar o mod com rodada no jogo").

## Ordem
1. Painel (so leitura do log) - feito em `docs/testes/painel.py`.
2. Fase 0 (diagnostico).
3. Status JSON no nucleo (alimenta o painel sem depender do vigia).
4. Fases 1 a 3. Fase 4 opcional.

## Fase 0: diagnostico
- Carregador: trocar o `jmp` do `timeGetTime` por invólucro que conta chamadas por thread; patch da IAT do `ck3.exe`
  para `QueryPerformanceCounter` e `Sleep` (todo laco de jogo chama).
- Thread de trabalho: batimento a cada 5 s no log com chamadas por segundo de cada gancho na thread principal, e
  amostra de onde a thread principal esta (`SuspendThread` + `GetThreadContext`, `RIP = exe+0x...`).
- Rodar com `-nographics` e separar:
  - thread principal em laco chamando um gancho: segue para a fase 1;
  - thread principal presa em ponto fixo (render/SDL): o RIP diz onde; provavel plano B;
  - processo voltou ao menu ou abortou a carga.

## Fase 1: canal de pedidos sem janela
- Separar o corpo do `ar_wndproc` em `q_drain()`; a WndProc continua chamando (nada muda com graficos).
- Bomba nova dentro do gancho escolhido na fase 0: so na `main_thread`, no maximo a cada ~50 ms, trava contra
  reentrada, mesmo `core_enter`/`run_command_guarded`.
- Risco: a WndProc roda num ponto seguro do quadro; `timeGetTime` pode ser chamado no meio de uma atualizacao.
  Mitigacao: so drenar quando o endereco de retorno for o ponto de chamada do laco principal achado na fase 0.
- Descarte de 8 s e `find_wnd` ficam so para o caminho com janela. O log diz o canal ativo (`janela` ou `bomba`).

## Fase 2: a simulacao anda?
- `state`, `velocidade 5`, `pause 0`, contar dias e pulsos. Se os dias nao andam, o laco de dias esta preso ao de
  quadros: parar e ir ao plano B.

## Fase 3: fidelidade e ganho
- `docs/testes/ng_eudes.py`, 60 dias, comparado pelo `dossie.py` com as 6 gravacoes de referencia. Medir ms por dia
  contra 153 a 184 ms com graficos.

## Fase 4 (opcional): varias instancias
- `ck3_custom_cmd.txt` e `ck3_custom_dll.log` sao unicos na pasta `binaries`: nomes por pid e `arlib` com alvo.
  Saves e autosaves tambem sao compartilhados.

## Plano B
Jogo com graficos minimos e janela escondida: o canal por WndProc continua; afrouxar o `IsWindowVisible` do
`find_wnd`. Ganho menor de velocidade, mas libera a tela e e mais seguro.

## Painel (`docs/testes/painel.py`)
- `rich` (instalado; `textual` nao). Atualiza a cada ~1 s. Uso: `python docs/testes/painel.py [--out nome] [--uma-vez]`.
- So leitura: o arquivo de pedidos guarda uma linha so; se o painel mandasse `state` junto com o vigia, um pisaria no
  outro. Le a cauda do `ck3_custom_dll.log` (o arquivo tem centenas de MB: comeca pelos ultimos 4 MB) e o
  `saida/<nome>.out` da rodada.
- Mostra: processo (pid, RAM, CPU, com ou sem janela), canal de pedidos (ultimo atendido, descartes), estado de pausa
  e velocidade, data/ouro/guerra e exercitos da ultima amostra do vigia, pulsos (ms por dia, dias por minuto, grafico
  de linha), decisor, falhas contidas, ultimas linhas relevantes do log e da rodada.
- Depois: trocar a amostra do vigia pelo `ck3_custom_status.json` gravado pelo nucleo a cada dia (temporario +
  rename) e mostrar o batimento da fase 0.

## Resultados (2026-10-07, tarde)

### Fase 0: diagnostico
Carregador com ganchos na tabela de importacao do ck3.exe (`QueryPerformanceCounter`, `Sleep`, `SleepEx`,
`timeGetTime`, `PeekMessageW`, `GetMessageW`, `SwitchToThread`, `WaitForSingleObject(Ex)`), contando so a thread
principal (`SwitchToThread` vai a milhoes por segundo nas outras threads: contador comum seria disputado), amostras da
thread principal a cada 250 ms (suspende, copia a pilha, retoma) e batimento a cada 5 s com `-nographics` ou
`AR_DIAG=1`.

| Situacao | Thread principal (chamadas/s) |
|---|---|
| Com graficos, menu/carga | `PeekMessageW` 60, `Sleep` 59 (limitador de quadros em exe+0x3E23F50), QPC ~950 |
| Com graficos, partida correndo | `PeekMessageW` 110 a 280, `SleepEx` 54 a 140, QPC 120 mil a 800 mil |
| Sem graficos, carga | `SwitchToThread` 0,5 a 2,5 milhoes (espera ativa em exe+0x3B67FE0), quase nada mais |
| Sem graficos, partida | `PeekMessageW` 150 a 260 mil, `SleepEx` 75 a 110 mil, QPC 450 a 670 mil: laco sem limitador |

O teste anterior (dossie 9.6) so concluiu que o canal estava morto: a carga sem graficos funciona (memoria 2,9 a 3,6
GB, `state` responde PAUSADO com o save carregado). A queda para 0,8 GB daquele teste nao se repetiu.

### Fase 1: canal sem janela
`q_drain()` separado da WndProc. A bomba roda no gancho escolhido a cada batimento (o primeiro, na ordem
`PeekMessageW`, `GetMessageW`, `timeGetTime`, `SleepEx`, `Sleep`, QPC, com >= 10 chamadas/s na thread principal),
no maximo a cada 50 ms, com trava contra reentrada. Decisao: `SwitchToThread` e as esperas ficaram fora da lista; na
primeira tentativa a bomba pegou `SwitchToThread` durante a carga (ponto no meio da carga) e parou de atender quando a
carga terminou. Com a lista nova a bomba fica em `PeekMessageW`, o mesmo ponto do laco de mensagens em que a WndProc
roda com graficos. Todos os pedidos (`state`, `velocidade`, `pause`, `events`, `carregar`, `cfg`...) respondem.

### Console sem graficos
O console do jogo (`CConsole`, global 0x570f7b0) nao e criado com `-nographics`: `console effect ...` (amostra do
vigia, paz, cenario ideal, configuracao) parava em "console do jogo indisponivel". Solucao no nucleo:
- `0xe3e8b0(resultado, leitor)` e o comando de console `effect` (o comando `run` o chama com um leitor de texto montado
  como o `layout` do nucleo). `efeito <texto>` monta o leitor e chama direto; sem console, `console effect ...` vai por
  ele. Recusa com a partida carregando (velocidade -1): era a causa das quedas em exe+0xe3ea3e (memoria
  console-durante-carga-derruba), que ficam no proprio comando.
- O comando escreve o resultado no console (`0x34b47d0`, console em rcx): desvio permanente que retorna se rcx e nulo,
  instalado so quando nao ha console.
- `event <id>` nao tem caminho seguro: `trigger_event` na hora, fora do laco do jogo, falha no gerenciador de eventos
  (exe+0x356b6ca, chamada a partir de 0x278d6b0, eventmanager.cpp) depois de o evento rodar. `arlib.ligar()` e
  `desligar()` passaram a rodar os efeitos dos eventos auto_raid.2/.3 por `effect` (equivalente, nos dois modos).
- A linha FALHA CONTIDA do carregador agora traz a pilha (enderecos de retorno no exe).

### Fase 2: a simulacao anda
Sim. Sem nenhum pedido, o jogo foi de 867.01.01 a 867.06.18 em ~1 min de carga+corrida; pausas por evento aparecem e
`events answer` responde. Abertura do save: 69 s sem graficos contra 139 s com graficos.

### Data do jogo
`[game_state]+0x9c` = dias desde o ano 0 (365 por ano), achado com o pedido novo `busca32` e conferido com 35 dias de
avanco. Vai no `status.json`.

### Partida comeca correndo sem graficos
Sem a tela do "Jogar", o jogo manda sozinho, logo depois da carga, `CAddPlayer`, `CSetDoneReplayingCommandsCommand`,
`CSetObserverCommand` e `CStartGameCommand`, e os dias correm (a primeira rodada comecou em 867.01.27, a segunda em
868.07.01). Correcao no nucleo: o `status` (a cada 250 ms sem graficos) pausa na passagem carregando -> carregada e
segura a pausa ate o primeiro `pause 0` pedido (pausa de novo se o jogo despausar sozinho). Com isso a rodada comeca em
867.01.03, como as de referencia (867.01.02). O jogador continua humano (`is_ai = no` vale no `ligar`).

### Fase 3: fidelidade e ganho
Mesmo save (`ar_ideal_eudes_pagao`), mesmo roteiro (`ng_eudes.py`, 60 dias, `risco_combate` 0,25, gravacao densa),
mesmo codigo; `gr_eudes` com graficos (`ng_eudes.py gr_eudes 60 graficos`), na mesma hora e com a maquina igualmente
ocupada.

| | gr_eudes (graficos) | ng_eudes3 | ng_eudes4 |
|---|---|---|---|
| abertura do save | 147 s | 86 s | 83 s |
| ms por dia de jogo (mediana) | 222 | 117 | 126 |
| periodo / pulsos | 867.01.02 a 03.21 / 79 | 867.01.03 a 03.18 / 75 | 867.01.03 a 03.23 / 80 |
| linhas de raid por pulso / saqueando por pulso | 22,3 / 3,8 | 25,8 / 4,6 | 24,0 / 5,6 |
| exercitos da IA / saidas | 49 / 25 | 52 / 29 | 56 / 28 |
| escolhas confirmadas / truncadas | 16 / 26 | 17 / 28 | 21 / 25 |

As rodadas d0/d1a/d1b/d1c antigas (com graficos, codigo anterior) ficam em 23,7 a 27,6 linhas de raid por pulso, 3,3
a 4,5 saqueando por pulso e 49 a 58 unidades. Conclusao: sem graficos o jogo se comporta dentro da variacao entre
rodadas com graficos, abre o save em ~0,6 do tempo e roda cada dia em ~0,55 do tempo (1,8 vez mais rapido).

### Fase 4: varias instancias (codigo pronto, teste cancelado a pedido do usuario)
- Carregador: `AR_INSTANCIA=<nome>` -> `ck3_custom_dll_<nome>.log` e `ck3_custom_cmd_<nome>.txt`; copias do nucleo com o
  pid no nome (`ck3_custom_core_live_<pid>_<n>.dll`), limpeza por padrao (as carregadas nao se apagam).
- Nucleo: `ck3_custom\status_<nome>.json`.
- `arlib.usar_instancia(nome)`: caminhos proprios, `AR_INSTANCIA` passado ao jogo, `ck3_aberto`/`fechar_jogo` so no
  processo aberto por esta instancia. `ng_eudes.py` usa a instancia do ambiente. `painel.py --instancia <nome> --pid <n>`.
- Sem a variavel, tudo como antes (verificado nas rodadas acima com o carregador novo).
- Pendente: o teste com duas instancias (abrir uma, esperar carregar, abrir a outra, para o `continue_game.json` nao
  ser disputado; autosaves sao compartilhados).

## Pendencias e decisoes para o usuario
- `console event <id>` nao funciona sem graficos (sem caminho seguro); usar `effect` com o conteudo do evento.
- A bomba pode atender pedidos em `QueryPerformanceCounter` durante a carga (ate o batimento seguinte trocar para
  `PeekMessageW`); na carga o vigia so manda `state`.
- Os ganchos de importacao ficam instalados sempre (custo pequeno: uma comparacao de thread por chamada); os
  batimentos e as amostras so com `-nographics` ou `AR_DIAG=1`.

## Painel do jogo (2026-10-07, pedido do usuario: acompanhar o jogo sem interface)
- O plugin do mod grava a cada pulso diario `ck3_custom/painel_auto_raid.json` (funcao `painel_gravar` em
  plugin/auto_raid.c, depois do decisor): governante (ouro, prestigio, piedade, idade, saude, forca do exercito, guerra,
  se e IA), o mod (ligado, risco de combate) e cada exercito (provincia, destino, estado, soldados, suprimento, saque e
  capacidade, alvo com ouro/dia, marcha e dias de saque, candidatos e inseguros, ameaca, chance, fuga, casa, combate).
- Eventos (os 40 ultimos, com a data do jogo): avisos do decisor (os mesmos textos que o jogo mostra), alvo escolhido e
  por que, inicio de saque, volta para casa (saque terminado, depois da fuga, desistiu do alvo), entrega do saque, fuga
  (para onde, ameaca em quantos dias, chance de vitoria), entrada e saida de combate (soldados antes e depois), exercito
  novo e exercito sumido (destruido em combate ou dissolvido). Volta e fuga saem no pulso seguinte, porque o script
  preenche casa e fuga depois do decisor.
- O TinyCC que compila o plugin no jogo nao liga funcoes de arquivo (fopen, fclose, rename, vsnprintf): a API ganhou
  `file_write(nome, texto, tamanho)` (ck3_api.h, api 3), que o nucleo grava em ck3_custom/ com temporario e troca e com o
  sufixo de AR_INSTANCIA. O plugin monta o JSON num buffer com snprintf.
- `docs/testes/painel.py` (rich): cabecalho (data, pausa, velocidade, dias por minuto, canal, processo), governante, Auto
  Raid, tabela de exercitos com o nome da provincia e o que cada um esta fazendo, eventos coloridos e uma faixa da DLL
  (falhas contidas, pedidos sem resposta). Nomes de provincia: `common/landed_titles` + localizacao braz_por do jogo
  (cache em saida/nomes_provincias.json); textos dos avisos: localizacao do mod.
- Janela propria (pedido do usuario: a CLI ficou feia): `docs/testes/painel_qt.py` (PySide6, instalado em
  2026-10-07), tema escuro: cabecalho (data, correndo/pausado, sem graficos, dias/min, ms/dia, processo), cartoes do
  governante (ouro, prestigio, piedade, idade, saude, guerra, forca), do Auto Raid e do placar de saques (tentados,
  iniciados, terminados sem e com combate, interrompidos, entregas e ouro entregue, voltas vazias, fugas, combates,
  destruidos), grafico do ouro, tabela de exercitos (barras de suprimento e saque), barra do tempo em cada estado (dias
  de jogo e segundos reais) e a lista de decisoes e resultados. Abrir: `pythonw painel_qt.py` em docs/testes. O placar
  e os tempos contam desde que a janela abriu (vem dos eventos do plugin).
- Placar exato (2026-10-07, pedido do usuario: "precisos igual os que voces estao vendo"): o plugin conta todo pulso,
  desde o carregamento do save (zera quando a data volta ou pula), com as definicoes do dossie.py (saida, saque com
  combate / interrompido / sem combate, entrega com ouro, saque perdido, destruido) e o tempo em cada estado (dias de
  jogo e segundos reais); grava `inicio`, `real_s`, `placar` e `tempo` no painel_auto_raid.json, e a janela so exibe.
  Conferido em `ng_placar` (3 anos): 8 saidas, 8 voltas vazias, 3 saques com combate, 3 combates, 0 ouro, 0 perdido,
  identico ao dossie.py da mesma gravacao.
