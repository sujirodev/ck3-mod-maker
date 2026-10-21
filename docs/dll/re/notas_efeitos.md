# Fase B - efeitos de script proprios (ck3.exe 1.19.0.6)

Analise estatica de 2026-10-04 (script: docs/re/b1.py). Enderecos sao RVA. NADA daqui foi testado em jogo.
Modelo estudado: `add_loot` (efeito de escopo exercito com um valor).

## B1 - registro de um efeito
Cada efeito tem um inicializador estatico (o de `add_loot` e 0x5c4300) que faz:

| Passo | Codigo | Detalhe |
|---|---|---|
| 1 | registro de nomes: ponteiro global em 0x5772a38 (se nulo, 0x3b587c0 cria e devolve) | |
| 2 | `0x3b58280(registro, &texto)` -> numero do nome (int) | texto = { ponteiro, tamanho u32 em +8, byte 0 em +0xc } |
| 3 | banco de efeitos (`CJominiEffectDatabase`): ponteiro global em 0x570f6f0 (se nulo, 0x337e050 cria; reler o global) | |
| 4 | entrada de 0x18 bytes (operador new do jogo: 0x3e25d74) | +0 tabela de funcoes da entrada, +8 ponteiro do nome, +0x10 numero do nome |
| 5 | `0x337e6b0(banco, numero, entrada)` registra | trava em banco+0x50, mapa numero -> entrada em banco+0x28, lista em banco+0x58. Se o numero ja existe, apaga a entrada antiga e troca |

Tabela de funcoes da entrada (`CEffectEntry<T>`, a do add_loot fica em 0x443cf30), 2 funcoes:
- [0] 0x995f10 `(entrada, flags)`: destrutor; com flags&1 libera 0x18 bytes (0x3e26124).
- [1] fabrica `(entrada)` -> objeto do efeito. A do add_loot e 0x2e81070: new de 0x168 bytes, construtor base
  0x337ebf0, membro "valor de script" construido em objeto+0x60 (0x1928a80), tabela de funcoes 0x443cad8,
  `[objeto+8]` = numero do nome (copiado de `[entrada+0x10]`), `[objeto+0xc]` = 0.

Os inicializadores estaticos ficam numa tabela de ponteiros na .rdata (0x3fd3bc0 a 0x4013978); o do add_loot e
a entrada em 0x40024f8. Eles rodam na inicializacao do executavel, DEPOIS de a nossa winmm.dll carregar e antes
de qualquer script ser lido.

## B2 - anatomia do objeto do efeito
Tabela de funcoes do `CAddLootEffect` (0x443cad8), 24 funcoes. As que mudam de um efeito para outro:

| Indice | add_loot | O que faz |
|---|---|---|
| 0 | 0x2da4550 | destrutor |
| 1 | 0x995de0 | le o argumento do script: repassa para o valor de script em objeto+0x60 |
| 5 | 0x193c200 | tipo de escopo aceito: devolve 0x1b (27 = exercito). Efeito sem escopo fixo devolve 0 |
| 16 | 0x2eaea60 | nome do efeito a partir de `[objeto+8]` |
| 22 | 0x2e80510 | EXECUTA `(objeto, contexto)` |
| 23 | 0x2e806b0 | monta a descricao (tooltip) `(objeto, contexto, saida)`, devolve 1 |
As demais 18 sao comuns (iguais as do `CDebugLogDateEffect`, 0x44786f0).

Como a execucao recebe o escopo (funcao 22):
- `rdx` = contexto. `[contexto]` = ponteiro para o escopo atual: tipo u16 em +0, id u32 em +8.
- Tipo 0x1b = exercito; o id e o ID DO EXERCITO. O codigo acha o objeto na tabela de exercitos (0x570c6b0, a mesma
  das notas de unidades): indice = id & 0xFFFFFF, entrada de 16 bytes em `[tabela+0x20]`, ponteiro em +8, confere
  `[exercito+0x10] == id`. Exercito nulo padrao em 0x570c6a0.
- O valor do argumento: `0x9698b0(objeto+0x60, &saida, contexto, [contexto+0x28], objeto+0x10)`; saida em ponto
  fixo (100000 = 1), como o saque.
- Depois disso o add_loot mexe em `[exercito+0x1e0]` (saque carregado), respeitando a capacidade (0x2272a80).

Consequencias:
- Identidade do exercito resolvida: o efeito recebe o id do exercito; `[exercito+0x124]` da o id da unidade.
- O argumento pode ser numero, variavel ou valor de script (`auto_raid_x = 7336`, `= var:ar_alvo`), porque o
  membro em +0x60 e o avaliador de valores do jogo.

## Receita proposta para o efeito nosso (a provar em B3/B4)
1. Entrada nossa (0x18 bytes, memoria nossa): tabela de 2 funcoes nossas; destrutor que nao libera nada.
2. Fabrica nossa: chama a fabrica original do add_loot (0x2e81070) passando a nossa entrada (ela so le
   `[entrada+0x10]`), e troca a tabela de funcoes do objeto por uma COPIA da 0x443cad8 com a funcao 22 (executar)
   e a 23 (descricao) apontando para o nosso codigo. O objeto continua sendo um add_loot verdadeiro para o jogo
   (construtor, destrutor, leitura do argumento e escopo de exercito originais).
3. Registro: numero do nome por 0x3b58280 e `0x337e6b0(banco, numero, entrada)`.
4. Momento (B3): trocar um ponteiro da tabela de inicializadores (ex.: 0x40024f8) por uma funcao nossa que chama
   o original e em seguida registra os efeitos `auto_raid_*`. E troca de ponteiro em memoria, sem alterar codigo.

Em aberto:
- B3: confirmar em jogo que o registro nesse momento funciona e que o `script_docs` lista o efeito.
- B5: gravar variavel a partir da DLL (classes `CJominiSetVariableEffect`, `CJominiSavedVariables`, `CSetVariableCommand`).
- B7: em que thread a funcao 22 roda.
- Efeitos em outro escopo (personagem) ou com bloco de campos: escolher outro efeito como modelo.
- Gatilho proprio (`CTriggerEntry<T>` existe, mesmo desenho): permitiria a DLL responder direto numa condicao.

## Resultado em jogo: B3 e B4 (2026-10-04)
Registro feito pelo carregador na inicializacao (thread principal, 0,25 s apos a carga): `auto_raid_ping`, numero
19698. Console `effect every_army = { auto_raid_ping = 7 }`: a DLL recebeu tipo 0x1b, exercito 1979711491, unidade
2046820353 (a unica do jogador, conferida com o pedido units), valor 7,000, thread principal. Sem erro no error.log.

## B5 - gravar variavel de script pela DLL (estatico; teste em jogo pendente)
Lido na execucao do `set_variable` (`CJominiSetVariableEffect`, funcao 22 em 0x3393480):

| Passo | Funcao | Detalhe |
|---|---|---|
| Variaveis do escopo | 0x3329990 `(escopo*)` | escopo* = `[contexto]`; devolve o objeto de variaveis do alvo, ou nulo |
| Numero do nome | 0x3b970f0 `()` -> registro; 0x3b96da0 `(registro, &numero, &texto)` | registro proprio de nomes de variavel; texto = { ponteiro, tamanho, 0 } |
| Gravar | 0x3346b30 `(variaveis+8, numero, &valor, dias)` | valor de 16 bytes: tipo u16 em +0 (1 = numero), valor em ponto fixo em +8; dias <= 0 = sem prazo |
| Avisar mudanca | 0x33465f0 `(variaveis)` | o jogo chama duas vezes |

No nucleo: `set_scope_var(ctx, nome, valor)`. O `auto_raid_ping = N` passou a gravar `ar_resp = 2N` no exercito.
Na leitura do script, o `set_variable` tambem registra o nome em banco+0xd8 (0x3382560); nao fazemos isso, entao o
jogo pode reclamar no error.log de variavel lida e nunca gravada. A conferir.

## Resultado em jogo: B5 (2026-10-04)
`effect every_army = { auto_raid_ping = 7 auto_raid_ping = var:ar_resp }`: primeira chamada valor 7, DLL gravou
ar_resp = 14; segunda chamada chegou com valor 14 (lido pelo script) e gravou 28. Sem falha. Confirmado o aviso
previsto: `jomini_effect.cpp:1161: Variable 'ar_resp' is used but is never set`. Correcao a fazer: registrar os
nomes das variaveis de resposta com 0x3382560 `(banco+0xd8, numero)` antes da leitura dos scripts, ou gravar a
variavel uma vez por `set_variable` no script do mod.

## Console do jogo pela DLL (2026-10-04, testado em jogo)
| Item | Endereco | Detalhe |
|---|---|---|
| Console (`CConsole`) | ponteiro global em 0x570f7b0 | nulo = indisponivel |
| Executar texto | 0x34b1970 `(texto*)` | a funcao por tras de `ExecuteConsoleCommand` da interface. texto* = std::string do jogo (ponteiro em +0, tamanho em +0x10, capacidade em +0x18; so e lido). Tira os espacos das pontas e poe na fila do console (0x34b6f20); o console executa em seguida, na thread principal |
Pedido do nucleo: `console <texto>`. Testado: `console effect every_army = { auto_raid_ping = 5 }`.

## Escopos vistos em jogo
| Tipo | Escopo | id |
|---|---|---|
| 0x4 | personagem | id do personagem (122714 = jogador) |
| 0x8 | provincia | id da provincia |
| 0x1b | exercito | id do exercito |
Efeito de qualquer escopo: funcao 5 trocada pela que devolve 0 (0x7e9230). Aceito pelo jogo em personagem e provincia.

## Variaveis registradas
0x3382560 `(banco+0xd8, numero)` chamado na inicializacao para cada nome de resposta: o aviso "used but never set"
sumiu. O jogo passa a avisar "set but is never used" para os nomes que nenhum script le; registrar so o que o mod usa.

## Leitura de variavel de script pela DLL (2026-10-04, testado em jogo)
`get_var(tipo de escopo, id, nome)` no nucleo. Monta um escopo proprio { tipo u16 em +0, id u32 em +8 } e chama
0x3329990 para achar as variaveis do objeto. A lista fica em variaveis+8: base em +8, quantidade em +0x14, entradas
de 0x20 bytes com o numero do nome em +8, o prazo em +0xc, o tipo do valor em +0x10 (1 = numero) e o valor em ponto
fixo em +0x18. Teste: `set_variable = { name = ar_cfg_chance value = 0.5 }` no personagem e a consulta passou a usar
limite 0,50.
