# Arquitetura da DLL: nucleo generico + plugins de mod em fonte C

Criado em 2026-10-04 a pedido do usuario. Objetivo: separar a DLL em uma parte generica, para compartilhar com a
comunidade, e o que e so do Auto Raid, que passa a morar dentro da pasta do mod como plugin em fonte C, compilado
pela DLL com o TinyCC embutido. O que ja funciona e mantido; a mudanca e de lugar, nao de comportamento.

Estado: arquitetura confirmada pelo usuario em 2026-10-04, incluindo o plugin em fonte C (prova na secao 8). A2 e A1 feitas
em 2026-10-05 (secoes 10.1 e 10.2). Nome do projeto: `ck3_custom` (decisao do usuario, 2026-10-05).
Nada implementado alem da prova.

## 1. Camadas

```
C:\CK3\binaries\                         generico (comunidade)
  winmm.dll        carregador fixo: repassa a winmm do sistema, intercepta comandos, contem falhas,
                   acha os plugins dos mods ativos, le os manifestos, registra os efeitos, carrega o nucleo
  ck3_core.dll     nucleo generico: primitivas do jogo, comandos, pedidos de desenvolvimento,
                   libtcc embutido (compila e recompila os plugins)
  ck3\include\     ck3_api.h + cabecalhos do TinyCC
  ck3\lib\         libtcc1.a (rotinas de apoio do TinyCC)

mod\auto_raid\plugin\                    so do Auto Raid
  plugin.txt       manifesto
  auto_raid.c      logica do Auto Raid em C puro (hoje no ar_core.c); pode ter outros .c e .h
```

| Camada | Conteudo | Recarrega com o jogo aberto? |
|---|---|---|
| Carregador (`winmm.dll`) | o que o `ar_proxy.c` faz hoje, mais a busca de plugins e a leitura dos manifestos | Nao |
| Nucleo generico (`ck3_core.dll`) | primitivas e pedidos (secao 3), compilador de plugins | Sim, em `-debug_mode` |
| Plugin do mod (`.c`) | efeitos e regras do mod (secao 4) | Sim, em `-debug_mode`: recompila ao mudar o `.c` |

O libtcc fica no nucleo, e nao no carregador, para que uma correcao no compilador ou na `ck3_api` tambem recarregue
com o jogo aberto. O carregador so precisa ler texto (manifestos) antes de o jogo ler os scripts.

## 2. Como o carregador acha os plugins

Mesma leitura que o jogo faz (conferido nesta maquina em 2026-10-04):
1. `Documents\Paradox Interactive\Crusader Kings III\dlc_load.json`, campo `enabled_mods` (ex.:
   `"mod/auto_raid.mod"`). E o arquivo que o launcher grava e o jogo le; o `debug.log` do jogo lista a mesma decisao
   (`Auto Raid|mod/auto_raid.mod|Enabled`).
2. Cada `.mod` da lista: campo `path=` (pasta do mod).
3. Se existir `<path>/plugin/plugin.txt`, o mod tem plugin.

Mod desligado no launcher nao entra. A leitura acontece na inicializacao, antes de o jogo ler os scripts, porque os
efeitos precisam estar registrados nessa hora.

Casos especiais:
- Mod em `.zip` (`archive=` no lugar de `path=`, como os da Paradox Mods): o libtcc compila de memoria, entao da para
  ler o `.c` de dentro do zip. Primeira versao: so pasta; zip ignorado com aviso no log.
- Mod da Workshop da Steam: o `.mod` aponta o `path=` para a pasta da Workshop; vale a mesma regra. Falta conferir se
  a Workshop e a Paradox Mods aceitam um mod com arquivos `.c`.

## 3. Nucleo generico (fica em binaries)

O que ja existe no `ar_core.c` e nao tem nada do Auto Raid:
- Leitura de tabelas do jogo: unidades, exercitos, provincias, raids; posicao e mar de provincia; relacao de unidade.
- Primitivas: dias e caminho (`eta_days`, `eta_from`), caminho em curso (`unit_path`), previsao de combate
  (`predict_armies`), pode saquear, saque da provincia, dias de saque (`raid_days`), capacidade de saque.
- Variaveis: ler e gravar variavel de escopo (`get_var`, `set_scope_var`, `set_scope_var_scope`).
- Comandos: mover, rota, parar, modo saque, modo raid, juntar, dividir, dissolver, levantar, pausar, camera.
- Pedidos de desenvolvimento: `info`, `where`, `eta`, `move`, `route`, `units`, `threats`, `peek`, `console`,
  `cmdlog`, `state`, `events`, `salvar`, `carregar`, `cmdraw`.
- Novo: compilador de plugins (secao 7).
- Novo: primitivas que hoje vem dos triggers do script (suprimento, homens de armas, tamanho do exercito, guerra,
  `can_raid_trigger`, dono do condado, suserano de topo, independencia, `max_military_strength`, provincias do reino,
  capital). Necessarias para o decisor no plugin (docs/simulador.md, 7.1).
- Novo: gravador generico (docs/simulador.md, secao 6): grava cada chamada de primitiva com argumentos e resultado.
  Serve ao simulador e a qualquer mod como depuracao.
- Novo, se a medida pedir: calculos pesados em laco oferecidos ao plugin (ex.: varrer provincias num raio, ordenar
  candidatas), porque o nucleo e compilado com `gcc -O2` e o plugin com o TinyCC (secao 8).

Regra: nenhum nome `ar_` ou `auto_raid_` dentro do nucleo. Pedidos mudam de nome so se o usuario decidir (secao 9).

## 4. Plugin do Auto Raid (vai para a pasta do mod)

O que hoje esta no `ar_core.c` e e so do mod:
- Efeitos `auto_raid_*` e o `on_effect` deles: `estado`, `dias`, `alvo`, `ameaca`, `seguro`, `casa`, `consulta`,
  `fuga`, `mover`, `parar`, `modo_saque`, `modo_raid`, `dividir`, `dissolver`, `levantar`, `pausar`, `camera`,
  `chamar`, `ping`.
- Regras: `threat_scan` (grupo de ameaca), `top_targets` (alvos, A2, A3), `target_safe` (regra 3), `levy_days`
  (C12), `route_slack` e a escolha da fuga, o acumulador de `casa`/`fuga`.
- Nomes do mod: `ar_cfg_*`, `ar_excl_*`, variaveis de resposta `ar_*`.
- Depois (A4 / SIM2): a maquina de estados que hoje esta no script.

O plugin nao le memoria do jogo. Tudo passa pela tabela de funcoes `ck3_api` que o nucleo entrega (secao 5). E isso
que deixa o simulador compilar o mesmo `.c` com outra tabela.

## 5. Interface (`ck3\include\ck3_api.h`)

Evolucao do `ar_host.h` atual (versao 3):

```c
typedef struct ck3_api {
	uint32_t version;
	void (*log)(const char *fmt, ...);
	// primitivas (ids do jogo; -1 = sem caminho / nao achado)
	int (*eta)(uint32_t unit, uint32_t prov, uint32_t *path, int *path_n, int path_max);
	int (*eta_from)(uint32_t unit, uint32_t from, uint32_t prov);
	int (*unit_path)(uint32_t unit, uint32_t *out, int max);
	double (*predict)(const uint32_t *ours, int n1, const uint32_t *theirs, int n2);
	int (*can_raid)(uint32_t army, uint32_t prov);
	double (*prov_loot)(uint32_t prov);
	int (*raid_days)(uint32_t army, uint32_t prov, double done);
	// ... leitura de unidades, provincias, personagens, variaveis, comandos
} ck3_api;

typedef struct ck3_plugin {
	uint32_t version;
	void (*on_effect)(const char *name, uint32_t scope_type, uint32_t scope_id, int64_t value, void *ctx,
	                  uint32_t thread);
	void (*on_command)(const char *line);   // pedidos do plugin, com prefixo (ex.: "auto_raid ...")
	void (*on_stop)(void);                  // antes de o plugin ser trocado ou descarregado
} ck3_plugin;

// definida pelo plugin
const ck3_plugin *ck3_plugin_start(const ck3_api *api);
```

- Os argumentos sao ids (unidade, provincia, exercito), nao ponteiros do jogo. O nucleo converte. Assim a tabela do
  simulador nao precisa imitar objetos do jogo.
- O plugin e compilado com `-nostdlib`: so enxerga a `ck3_api` e o que o nucleo liberar por nome (`memset`,
  `memcpy`, `memmove`; `snprintf` e matematica se forem precisos). Isso evita uso acidental da API do Windows; nao e
  protecao contra codigo malicioso (secao 9).
- `on_post` (interceptar comandos do jogo) fica so no nucleo; o plugin nao recebe.
- API 2 (2026-10-07, docs/perfil_resposta.md): `game_files(pasta, out, cap)` lista os `.txt` de uma pasta do jogo
  como o jogo os ve com os mods ativos (jogo base, depois os mods na ordem do `dlc_load.json`; arquivo de mesmo nome
  substitui; `replace_path` do `.mod` descarta os anteriores); `file_read`/`file_free` leem um deles. O `expr` passou a
  aceitar texto de qualquer tamanho (era cortado em ~290 caracteres) e guarda ate 1024 expressoes lidas (eram 96, e
  alem disso cada chamada lia o texto de novo). Plugin da api 1 nao carrega no nucleo 2 e vice-versa: trocar juntos o
  `ck3_custom_core.dll`, o `ck3_custom\include\ck3_api.h` e o plugin. No simulador (`sim3.py`) as tres funcoes ficam
  nulas e o plugin confere antes de usar.

## 6. Manifesto (`plugin/plugin.txt`)

Mesmo formato do `auto_raid_effects.txt` de hoje, mais o cabecalho:

```
nome auto_raid
api 1
fonte auto_raid.c
auto_raid_estado
auto_raid_pausar livre
var ar_alvo
```

- `fonte`: um ou mais `.c`, relativos a pasta `plugin`; os `.h` da mesma pasta entram pelo `#include`.
- Linha de efeito: nome do efeito (escopo exercito) ou `nome livre` (qualquer escopo).
- `var nome`: variavel de resposta (evita o aviso "used but never set").
- O carregador encaminha cada efeito ao plugin que o declarou. Dois plugins com o mesmo nome de efeito: o segundo e
  recusado com aviso no log.
- `api` diferente da versao do nucleo: plugin recusado com aviso.

## 7. Compilacao e recarga

- Na abertura: o carregador registra os efeitos de todos os manifestos e carrega o nucleo; o nucleo compila cada
  plugin em memoria (libtcc) e chama `ck3_plugin_start`.
- Erro de compilacao: log com arquivo e linha; sem plugin anterior, os efeitos dele so registram no log que o plugin
  nao carregou (o script cai no E6, "DLL nao respondeu").
- Com `-debug_mode` na linha de comando do jogo:
  - o nucleo vigia a data dos `.c` e `.h` de cada plugin; mudou = recompila; compilou = `on_stop` do antigo, troca,
    `ck3_plugin_start` do novo; falhou = o antigo continua;
  - o carregador vigia o `ck3_core.dll` como hoje (copia `_live_N`, espera as chamadas em curso, troca); o nucleo
    novo recompila todos os plugins.
- Sem `-debug_mode`: compila uma vez na abertura e nao vigia.
- Efeito ou variavel nova no manifesto continua exigindo reabrir o jogo (registro antes da leitura dos scripts).
- Troca so entre pulsos: o nucleo espera nao haver chamada ao plugin em curso (mesmo contador `core_inflight` de
  hoje).

## 8. Prova do plugin em fonte C (2026-10-04): PASSOU
Fontes e passo a passo em `docs/re/prova_tcc/`. TinyCC 0.9.28rc do repositorio oficial (commit `43c7708`).

| Item | Resultado |
|---|---|
| libtcc compilado com o GCC do WinLibs e embutido num hospedeiro de teste | OK (`libtcc.o` estatico; o nucleo de prova com o libtcc ficou com 486 KB) |
| Plugin `.c` compilado em memoria, chamando a `ck3_api` e respondendo um efeito | OK |
| Tempo de compilacao | plugin pequeno: 1 a 4 ms; o `ar_core.c` inteiro (2000 linhas + `windows.h`): 143 ms pelo `tcc.exe` |
| Recarga ao mudar o `.c` | OK: v1 -> v2 em 3 ms, o anterior so e descartado depois de o novo compilar |
| Erro de compilacao | OK: log com arquivo e linha (`plugin_erro.c:21: error: ';' expected`), plugin anterior segue respondendo |
| Extensoes do GCC usadas no `ar_core.c` | OK: `({ })`, `__typeof__`, `_Alignas`, `__attribute__((aligned))`; o `ar_core.c` inteiro compila sem erro |
| Desempenho do codigo gerado x `gcc -O2` | 3,2 a 4,6 vezes mais lento (Dijkstra 120x120 x 40: 19 ms x 61 ms) |
| Sem a biblioteca do sistema (`-nostdlib`) | OK: so o que o hospedeiro libera (`memset`, `memcpy`, `memmove` + `ck3_api`); chamar `ExitProcess` = recusado ("unresolved reference") |
| Dentro do jogo, sem reabrir | OK: nucleo de prova trocado com o jogo aberto; pedido `tcc <arquivo.c>` compilou em 2,7 ms; `effect auto_raid_ping_livre` pelo console chegou ao plugin, que chamou a `eta` real do jogo (37 dias, igual ao pedido `eta`) e gravou `ar_resp` no escopo; recarga v1 -> v2 e erro de compilacao repetidos no jogo com o mesmo resultado |

Conclusoes:
- O custo de compilar e desprezivel; pode recompilar a cada mudanca de arquivo.
- O que fica mais lento e a execucao do codigo do plugin, nao o carregamento: o TinyCC compila rapido justamente
  porque quase nao otimiza. Por isso calculo pesado em laco (Dijkstra, varreduras grandes) fica no nucleo generico,
  compilado com `gcc -O2`, e e oferecido ao plugin pela `ck3_api`; o plugin fica com as regras. Hoje a parte cara do
  pulso ja roda dentro do jogo (pathfinder, previsao de combate), entao a lentidao pesa pouco. Medir o pulso real
  depois da A2 (meta: media e pico iguais aos de hoje, ~7 ms e ~0,7 s, com folga de 20%).
- No simulador o mesmo `.c` e compilado com `gcc -O2`: a lentidao do TinyCC nao afeta as rodadas.
- `-nostdlib` com lista de simbolos liberados nao e caixa de areia: C com ponteiros ainda alcanca qualquer coisa.

## 9. Pontos em aberto

1. FECHADO (2026-10-05): nome `ck3_custom` (`ck3_custom_core.dll`, pasta `ck3_custom\`). A interface em C manteve o
   nome `ck3_api` (`ck3_api.h`).
2. FECHADO (2026-10-05): pedidos em `ck3_custom_cmd.txt`, respostas em `ck3_custom_dll.log`; `arlib.py` ajustado.
3. Seguranca: todo mod ativo com plugin executa codigo. Proposta minima: o LEIAME da comunidade diz isso com clareza,
   e o nucleo registra no log cada plugin compilado (nome, caminho, hash dos fontes).
4. Versao do jogo: o nucleo so funciona na 1.19.0.6; cada patch muda enderecos. Plugins nao dependem disso, so do
   `api`.
5. Licenca e repositorio da parte generica. O TinyCC e LGPL: com o nucleo em codigo aberto nao ha conflito.
6. Regra da Workshop e da Paradox Mods para mod com arquivos `.c`.
7. (Resolvido em 2026-10-07, docs/plano_sem_graficos.md: bomba da fila em `PeekMessageW`, `efeito` sem console, pausa
   segurada depois da carga.) Jogo sem graficos (`-nographics`), testado em 2026-10-07 (docs/dossie_exercitos.md 9.6): a opcao existe e o
   carregador, o nucleo e o plugin sobem normalmente, mas o processo nao cria janela nenhuma e o canal de pedidos
   (`ck3_custom_cmd.txt` -> fila -> `PostMessage(WM_AR_COMMAND)` na janela principal, `find_wnd` em `ar_proxy.c`)
   fica sem destino: todo pedido sai "sem resposta em 8 s". A simulacao tambem nao deu sinal de andar (nenhum pulso,
   memoria caiu para 0,8 GB depois do "Load Save"). Ideia para uma segunda tentativa: atender a fila de dentro de
   uma chamada que a thread principal ja faz sem graficos, e o proprio proxy e a `winmm.dll` (exporta `timeGetTime`
   e afins, chamadas por quadro); confirmar a thread e a frequencia da chamada sem graficos, atender com o mesmo
   `core_enter`/`run_command_guarded`, e so entao ver se `pause 0` faz os dias andarem. Ganhos ja aproveitados:
   pedido `velocidade <1..5>` no nucleo e modo `SEM_JANELA` do vigia (`docs/testes/ideal.py`).

## 10. Etapas (mantendo o que funciona)

| # | Etapa | Conferencia |
|---|---|---|
| A1 FEITA (10.2) | Carregador: busca de plugins (secao 2), leitura dos manifestos, registro dos efeitos de cada plugin, recarga do nucleo so em `-debug_mode`. Sem plugin achado, carrega o `auto_raid_core.dll` como hoje | jogo abre e o ciclo roda igual |
| A2 FEITA (10.1) | Separar o `ar_core.c` em `ck3_core.dll` (com o libtcc, secao 7) + `mod\auto_raid\plugin\auto_raid.c` (secoes 3 e 4), sem mudar comportamento; ids no lugar de ponteiros na interface | mesma resposta em cada efeito, comparada pelo log e pela gravacao (SIM1); custo do pulso medido (secao 8) |
| A3 | Primitivas novas dos triggers no nucleo generico | cada uma conferida contra o trigger do script no mesmo pulso |
| A4 | Decisoes do script para o plugin (docs/simulador.md, SIM2) | decide igual ao gabarito em 100% dos pulsos |
| A5 | LEIAME da comunidade e do plugin | instalacao do zero seguindo so o LEIAME |
| A6 | Configuracao dos mods (secao 11): prova C1 a C7, depois implementacao | secao 11.6 |

### 10.1 A2 feita (2026-10-05)
- Nucleo generico: `dll/ar_core.c` (ainda com o nome `auto_raid_core.dll`, que o carregador procura; o nome generico
  entra com a A1). Primitivas, comandos, variaveis, expressoes de script, gravador, aba Mods, pedidos de
  desenvolvimento e o compilador de plugins (libtcc embutido; o nucleo ficou com ~510 KB). Implementa a `ck3_api`
  (`dll/ck3_api.h`, versao 1) por ids.
- Plugin do Auto Raid: `plugin/plugin.txt` (nome, api, fontes), `plugin/auto_raid.c` (efeitos `auto_raid_*`, grupo de
  ameaca, alvos, regra 3, contra-saque, casa, fuga, configuracao `ar_cfg_*`, linhas da gravacao do mod, pedidos
  `levas`/`target`/`scan`, mundo do decisor) e `plugin/ar_decisor.c/.h` (maquina de estados, SIM2). O nucleo acha o
  plugin pelos mods ativos (`dlc_load.json`), compila ao carregar (8 a 18 ms) e, em `-debug_mode`, quando um `.c`/`.h`
  da pasta muda; erro de compilacao = log com arquivo e linha, e o plugin anterior continua (conferido no jogo). Os
  efeitos seguem registrados pelo carregador a partir de `auto_raid_effects.txt` ate a A1; o nucleo repassa cada efeito
  ao primeiro plugin que o trata.
- O nucleo guarda a tabela de provincias no inicio de cada efeito e pedido: cada leitura segura e uma chamada ao
  sistema (`ReadProcessMemory`), e sem isso os lacos do plugin sobre todas as provincias custavam ~30% a mais.
- Conferencia (`docs/testes/a2_respostas.py`): no save fixo `a2_base`, jogo pausado, o mesmo roteiro de efeitos e
  pedidos com o nucleo antigo e com nucleo + plugin; a gravacao (respostas, primitivas, alvos, levas, previsoes,
  consultas e o pulso do decisor) deu 2812 de 2812 linhas iguais (o roteiro repetido com o mesmo nucleo tambem da 0
  diferencas). Rodada de 1 ano do S1: conferidor do SIM2 371 de 371; pulso medio 8,7 ms e pico 658 ms (antes ~7 a
  10 ms e ~700 ms). Conferidor do SIM2a com o decisor na pasta nova: 100% nos gabaritos de 10 anos.
- O que ficou para a A1 esta na 10.2.

### 10.2 A1 feita (2026-10-05, com reabertura do jogo)
- Nomes `ck3_custom` em `C:\CK3inaries`: `ck3_custom_core.dll` (copias `ck3_custom_core_live_N.dll`),
  `ck3_custom_cmd.txt`, `ck3_custom_dll.log` e a pasta `ck3_custom\` (gravacao, mapa, `configuracoes_mods.txt`,
  `include\` e `lib\` do TinyCC). O carregador continua `winmm.dll`. Os arquivos antigos ficaram com o sufixo
  `.antes_a1` (`winmm.dll`, `auto_raid_core.dll`, `auto_raid_effects.txt`).
- Carregador: acha os mods ativos (`dlc_load.json`, a mesma leitura das configuracoes) e registra os efeitos e as
  variaveis de cada `plugin/plugin.txt`; nome repetido em outro plugin e recusado com aviso. O
  `auto_raid_effects.txt` deixou de existir. Registra tambem `ck3_custom_ping` (qualquer escopo), do proprio nucleo:
  as provas `expr`/`ctxprova` passaram para ele. O nucleo e carregado na abertura e so e recarregado ao mudar em
  `-debug_mode`.
- Nucleo: le os efeitos de cada manifesto e entrega cada efeito ao plugin que o declarou.
- Na mesma reabertura entrou o retrato mensal do SIM1b (`ar_retrato` no script, efeito `auto_raid_retrato`), so com
  a gravacao ligada (variavel `ar_gravando`, posta pelo plugin a cada pulso).
- Conferido depois da reabertura: 25 efeitos e 17 variaveis registrados, plugin compilado em 8,8 ms, error.log sem
  erro do mod; gabarito `a2_respostas.py` 2812 de 2812 linhas iguais ao do nucleo de antes da A2; rodada de 1 ano do
  S1 com gravacao: 368 de 368 pulsos iguais no conferidor, pulso medio 8,8 ms, pico 652 ms, retrato com ~406
  provincias por mes; troca do nucleo com o jogo aberto (plugin recompilado junto); `mudo` desligou o ciclo no
  segundo pulso sem resposta (E6).

Ordem com o simulador: SIM1 (gravador) vem antes de A2, para a gravacao do comportamento atual servir de gabarito
da separacao.

## 11. Configuracao dos mods (pedido do usuario em 2026-10-04)

### 11.1 O que se quer
- A DLL acrescenta uma aba "Mods" na janela de configuracoes do jogo.
- A aba lista um botao por mod que tem configuracao; o botao mostra as opcoes daquele mod.
- Cada mod declara as opcoes numa estrutura propria: quais sao, de que tipo, padrao, limites e como aparecem.
- Mod sem configuracao nao aparece. Nenhum mod com configuracao = a aba nao aparece.
- Para o Auto Raid: configuracao global (vale para todos os saves), com sobrescrita por save para os testes.

### 11.2 Caminho (decidido)
A. Aba nativa (principal): a DLL registra as opcoes no registro de configuracoes do motor, num grupo `mods`; a pagina e as secoes vem do `game/settings_layout.txt` (page > category > order), que o mod substitui com uma pagina a mais (achado na C2). A janela monta as
abas a partir de `JominiSettingsWindow.AccessPages` (`game/gui/settings/setting_types.gui`) e ja tem os tipos
`jomini_setting_bool`, `_enum`, `_value_scrollbar`, `_string`, `_callback` e `jomini_settings_grouping`. Os valores
ficam no `pdx_settings.txt` da pasta do usuario, gravados pelo proprio jogo (hoje com as paginas "Tools", "game"
etc.). Funciona no menu principal e durante a partida, sem substituir nenhum `.gui` (sem conflito com outros mods).
Exige engenharia reversa do registro (prova 11.6).

B. Janela propria (reserva, se A nao der): uma GUI generica que acompanha a DLL, aberta por um botao no menu de
pausa; os botoes chamam `scripted_gui` com efeitos da DLL, que grava um arquivo de configuracao. So durante a
partida, e substitui o `frontend_ingame_menu.gui` (pode conflitar com mods que mexem no mesmo arquivo).

### 11.3 Estrutura que o mod declara (`plugin/config.txt`)
```
mod auto_raid
titulo ar_cfg_titulo                     # chave de localizacao do botao do mod
grupo seguranca ar_cfg_grupo_seguranca
  real chance 0.80 0.50 0.99 0.01        # tipo chave padrao minimo maximo passo
  inteiro margem 5 0 30
  inteiro horizonte 25 5 60
  inteiro parado 15 5 40
grupo alvo ar_cfg_grupo_alvo
  inteiro raio 150 50 400 10
  real min_ouro_dia 0.5 0 5 0.05
  lista modo_seguro 0 nao sim            # valores na ordem; o valor gravado e o indice
grupo avisos ar_cfg_grupo_avisos
  liga pausa_fuga 1
  liga pausa_guerra 1
```
- Tipos: `liga <chave> <padrao>`; `inteiro <chave> <padrao> <min> <max> [passo] [formato]`; `real <chave> <padrao> <min> <max> <passo> [formato]`. `formato` = chave de texto do valor do jogo (`SETTING_VALUE_NONE` se faltar; ex.: `SETTING_VALUE_PERCENTAGE`, `SETTING_VALUE_SECONDS`). Feitos: liga, inteiro, real (C4). Planejados: `lista` (enum), `texto` (string).
- Nome da opcao no jogo: `<mod>_<chave>`, no grupo `mods` do `pdx_settings.txt`. Arquivo: `plugin/config.txt`.
- Textos: `<mod>_cfg_<chave>` e `<mod>_cfg_<chave>_TOOLTIP` (convencao do jogo) na localizacao do proprio mod; titulo da aba `SETTINGS_PAGE_MODS` e da secao a chave de `titulo` ou `grupo` (o jogo ja carrega, porque o
  mod esta ativo). Chave sem texto aparece pelo nome.
- A lista completa do Auto Raid sai do `ar_configurar` atual (23 parametros).

### 11.4 Valor que vale (Auto Raid)
Ordem: sobrescrita do save > global > padrao do `config.txt`.
- Global: o valor da aba (no `pdx_settings.txt`).
- Sobrescrita do save: variavel `ar_fixo_<chave>` no personagem do jogador, posta por console ou pelos testes
  (`docs/testes/arlib.py`, `cfg()`, grava `ar_fixo_*` desde 2026-10-05).
- Enquanto as decisoes estiverem no script (antes da A4): a cada pulso o nucleo grava o valor que vale em
  `ar_cfg_<chave>` (no `auto_raid_chamar`), e o script continua lendo `root.var:ar_cfg_*` como hoje. O `ar_configurar`
  continua criando as `ar_cfg_*` com os padroes (o nucleo so atualiza as que existem); como a sobrescrita e a
  `ar_fixo_*`, os padroes do script nao viram sobrescrita.
- Depois da A4: o plugin le direto pela `ck3_api` (`cfg(mod, chave)`), sem variavel no save.

### 11.5 Interface
Acrescimos a `ck3_api`: `double (*cfg)(const char *mod, const char *key)` e, no `ck3_plugin`,
`void (*on_cfg)(const char *key)` (avisa quando o jogador muda um valor). O carregador le os `config.txt` junto dos
manifestos (secao 2) e registra a aba; sem nenhum `config.txt` com opcao, nao registra.

### 11.6 Prova (C1 a C7 feitas; C4 sem lista e texto; C7 na abertura a conferir)
| # | Item | Como confere |
|---|---|---|
| C1 | Achar o registro de configuracoes no `ck3.exe` 1.19.0.6 | OK (2026-10-04): gerenciador `exe+0x5710d58`, registrar `0x3b44cf0(gerenciador, &grupo, opcao, n)`, construtoras por tipo, trava depois de `LoadSettings`; detalhes em `docs/re_notas.md`, secao C1. Para a C2: registrar logo depois da criacao do gerenciador (`0x3b44b40`), com desvio no `winmm.dll` |
| C2 | Registrar na inicializacao uma pagina "Mods" com uma opcao liga/desliga | OK na memoria (2026-10-04): desvio no `winmm.dll` em `0x3b44b40` registrou `mods/auto_raid_teste` antes da leitura do `pdx_settings.txt`, sem o aviso de trava; com o `settings_layout.txt` do mod (vanilla + pagina `SETTINGS_PAGE_MODS`), a `CJominiSettingsWindow` (`+0xd8`) montou 4 paginas, a quarta "Mods" com 1 opcao. Visual OK no menu principal: aba Mods com a secao "Auto Raid" e "Opcao de teste do Auto Raid" em portugues. Achados: a caixa aparece desmarcada com padrao 1 na construtora (conferir na C3); com 4 abas a janela passa da borda direita numa tela de 1707 px (conferir se o original tambem corta) |
| C3 | Valor gravado e lido de volta no `pdx_settings.txt`, inclusive depois de reabrir o jogo; o jogo nao apaga opcao que ele nao conhece | PARCIAL (2026-10-04): o jogo grava sozinho, a cada mudanca, uma secao nova `"mods"={ "auto_raid_teste"={ version=0 enabled=yes } }`; reaberto com a DLL, o valor volta (1 nos tres campos de valor). FALHA: aberto SEM a DLL, o jogo regrava o arquivo na abertura e apaga a secao `"mods"` (correcao em 11.7). Pendente: o padrao da construtora (1) nao vale quando a opcao nao esta no arquivo (aparece 0) |
| C4 | Barra de valor (inteiro e real), lista e texto | PARCIAL (2026-10-05): inteiro e real OK. `inteiro raio 150 50 400 10` e `real chance 0.8 0.5 0.99 0.01 SETTING_VALUE_PERCENTAGE` apareceram como barras (150 e 80%); o usuario mudou para 400 e 99% e o jogo gravou `value=400` e `value=0.990000`; reaberto, os valores voltaram do arquivo; `inteiro margem 5 0 30` acrescentado com o jogo aberto entrou pela recarga com o padrao 5. Lista e texto ficam para depois (o Auto Raid nao usa): a lista do jogo e uma classe derivada por opcao (`0x2049010`) com itens em tabela fixa no codigo; texto tem construtora `0x3b4c290` |
| C5 | Botao por mod que troca as opcoes mostradas | OK (2026-10-05, sem reabrir o jogo, no nucleo): a aba Mods mostra uma lista de botoes (`CPdxCallbackSetting` `mods_botao_<mod>`, texto = `titulo` do mod; acao = `std::function<void()>` do tipo do jogo `_Func_impl_no_alloc<void(*)()>` apontando para uma funcao do nucleo); o clique (adiado para a fila da thread principal) troca a aba Mods pelas secoes daquele mod com um botao `mods_voltar`, e seleciona a aba pelo mesmo caminho do clique na aba (`0x32d9cf0`). Um vigia (thread do nucleo) acha a janela no ponteiro guardado pelo carregador e troca de novo quando a aba completa do carregador volta (janela remontada, config recarregada). Ao trocar o nucleo, os botoes apontam para um `ret` do jogo ate o novo nucleo religar. Conferido pelo usuario: lista, Auto Raid, Voltar, e uma opcao mudada e gravada |
| C6 | Ler o valor pela DLL na partida e ser avisado da mudanca | OK (2026-10-05, sem reabrir o jogo): o nucleo le as opcoes do grupo `mods` direto dos objetos (pedido `cfg` lista; `cfg <nome> <valor>` muda pelo caminho da interface). A cada pulso, no `auto_raid_chamar` do personagem do jogador, grava `ar_cfg_<chave>` com o valor da aba, ou `ar_fixo_<chave>` se o save tiver; so nas 23 `ar_cfg_*` que o `ar_configurar` cria. Conferido no `auto_raid_teste`: aba raio 250 -> `ar_cfg_raio` 250; `ar_fixo_raio` 200 -> 200; sem o `ar_fixo` -> 250; aba 300 -> 300. A mudanca vale no pulso seguinte (esse e o "aviso") |
| C7 | Sem `config.txt` com opcao: aba nao registrada | OK com o jogo aberto (2026-10-05): `config.txt` trocado por um sem opcoes -> o nucleo tirou a aba Mods (3 abas) e o carregador, ao recarregar, nao acrescentou a dele; `config.txt` devolvido -> 4 abas com a lista de botoes. O nucleo agora rele os `config.txt` a cada troca e o vigia observa a data deles. Na abertura (nenhum mod com `config.txt`): pelo codigo, o carregador sai do `cfg_install` antes dos desvios (nao registra opcao nem aba); conferir na proxima reabertura do jogo que for necessaria |

Riscos: registrar pagina pode exigir que seja antes de a janela ser criada (por isso no carregador, na inicializacao);
o jogo pode descartar do `pdx_settings.txt` opcoes que nao registrou numa abertura sem a DLL; cada patch do jogo
muda os enderecos (como o resto do nucleo).

### 11.6b Menu montado pela DLL, com recarga (pedido do usuario em 2026-10-04): prova C2b
Objetivo: o mod nao traz `settings_layout.txt`; a DLL gera a pagina "Mods" a partir dos `config.txt` e, em
`-debug_mode`, refaz o menu quando um `config.txt` muda.

Como o jogo monta as abas (metodo da `CJominiSettingsWindow` em `0x32d7700`, 2o da vtable principal):
1. limpa o vetor de abas (`+0xd8`, `0x7e9ea0`);
2. le o `settings_layout.txt` (`0x3b589f0` monta o caminho, `0x3b8e670` le);
3. entrega o lido ao leitor da janela (objeto em `+0xc8`, funcao 0x18 da vtable), que cria paginas e secoes;
4. para cada secao de cada pagina (`pagina+0x30` lista, `+0x3c` quantidade), liga as opcoes (`0x32d5230`).

Plano:
- Abertura: a DLL registra as opcoes (como na C2) e acrescenta ao texto do layout uma pagina gerada; o arquivo do
  jogo fica intacto.
- Recarga (`-debug_mode`): `config.txt` mudou = registra as opcoes novas, aplica os valores pela copia propria
  (11.7, porque o registro depois da trava nao recebe o valor do `pdx_settings.txt`) e refaz os passos 1 a 4 com
  a janela fechada.
- Textos: localizacao do mod, recarregada pelo proprio jogo em `-debug_mode`.

| # | Item | Estado |
|---|---|---|
| C2b.1 | Como acrescentar texto ao layout: desvio na leitura (`0x3b8e670`) do `settings_layout.txt`, ou o leitor de `+0xc8` chamado de novo com o nosso texto (se acrescenta em vez de substituir) | OK (2026-10-04): o leitor (`janela+0xc8`, vtable[3] `0x3b8b060`, por bloco `page` = token `0x2d1` em `0x32d6cc0`) acrescenta paginas ao vetor `janela+0xd8` sem limpar. Texto da memoria: `CMemoryFile` (vtable `0x4559758`; buffer `{dados, ?, alocador char 0x4fea420}` por `0x3bb5200`), `CLexer` por `0x3b58cf0(lexer, &arquivo, 0)` com a vtable trocada para `CTextLexer` (`0x4556c20`), leitor `0x3b8e670(leitor, &lexer)`; depois `0x32d5230(secao)` para cada secao nova. Pedidos de teste `layout <janela> <arquivo>` e `layout_tirar <janela> <indice>` no nucleo |
| C2b.2 | Refazer os passos 1 a 4 com o jogo aberto e a janela fechada, sem recriar a janela; a interface mostra as abas novas ao abrir | OK (2026-10-04, jogo aberto, menu principal): tirada a aba do `settings_layout.txt` do mod e acrescentada uma montada pela DLL com a secao "Idioma"; ao abrir a janela, a 4a aba mostrou "Idioma" com a opcao do Auto Raid, sem falha. Falta: achar a janela sem o endereco passado a mao; liberar a memoria das paginas tiradas |
| C2b.3 | Registrar opcao nova depois da trava e aplicar o valor dela pela DLL | OK (2026-10-04): pedido `opcao mods auto_raid_teste2 AR_CFG_TESTE2 1` com o jogo aberto; o jogo avisa `registered after setting lockdown, may get lost` (so quer dizer que nao recebe o valor do arquivo); o valor e aplicado por `0x3b46990(opcao, valor, 1)`, o mesmo do clique na caixa (`0x32d6610`); a opcao apareceu na aba refeita e, mudada pelo usuario, foi gravada no `pdx_settings.txt` junto da outra |
| C2b.4 | Pedido de teste `config recarregar` e, depois, vigia do `config.txt` em `-debug_mode` | OK (2026-10-05): no carregador (`ar_proxy.c`, secao "configuracoes dos mods"). Abertura: acha os mods pelo `dlc_load.json`, le `plugin/config.txt`, registra as opcoes na criacao do gerenciador e, por desvio no metodo de montagem da janela (`0x32d7700`), acrescenta a pagina Mods gerada e guarda a janela (o jogo remonta a janela sozinho, ex.: ao recarregar a localizacao em `-debug_mode`, e o desvio acrescenta de novo). Recarga: a vigia (a cada ~1 s, so em `-debug_mode`) viu `liga teste3 1` acrescentado ao `config.txt`, registrou a opcao com o jogo aberto, aplicou o padrao e refez a aba; o usuario viu as tres opcoes com os valores certos. O mod nao traz mais `settings_layout.txt` |

### 11.7 Correcoes que a prova pediu
- Opcoes apagadas quando o jogo abre sem a DLL (C3): FEITO e conferido em 2026-10-05 (jogo aberto sem a DLL apagou a secao; aberto de novo com a DLL, o log mostrou `secao "mods" devolvida ao pdx_settings.txt a partir da copia` antes do registro das opcoes). o carregador guarda uma copia propria das secoes dos mods
  (ex.: `C:\CK3\binaries\ck3\configuracoes_mods.txt`), atualizada sempre que o jogo regrava o `pdx_settings.txt`
  com elas. Na abertura, antes de o jogo ler o arquivo (no `DllMain`), se a secao de um mod registrado faltar no
  `pdx_settings.txt`, o carregador a devolve a partir da copia.
- Padrao que nao vale sem entrada no arquivo (C3): FEITO pelo segundo caminho (o carregador aplica o padrao do `config.txt`, com `0x3b46990`, as opcoes sem entrada no arquivo, logo depois da montagem da janela; conferido com a `teste3`). Primeiro caminho, se um dia precisar: investigar como o jogo aplica o padrao das opcoes dele (a
  `prompt_for_beta` tambem passa 1 na construtora) ou, depois do `LoadSettings`, gravar o padrao do `config.txt` nas
  opcoes sem entrada pelo mesmo caminho da interface.
- Cliques sinteticos nao chegam ao jogo: o Windows nao deixa um processo em segundo plano trazer o jogo para a frente
  (os cliques cairam no app do Claude). Nos testes de interface, o usuario clica; a leitura e pela memoria e pelo
  arquivo.
