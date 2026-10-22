Versao em ingles: [re_notas.md](re_notas.md)

# Notas de engenharia reversa (Fase P)

Alvo: `C:\CK3\binaries\ck3.exe`, versao 1.19.0.6.
- Tamanho: 95206520 bytes
- SHA-256: `42562e4aaebe9e0c9258083c3193c69635a1bad371e7504b8deecad1e660b3db`
- Image base: `0x140000000`. Todos os enderecos abaixo sao RVA (somar ao endereco base do modulo em execucao).
- Script que gerou os dados: `docs/re/re1_rtti.py` (Python puro, so leitura do arquivo).

Tambem existe `C:\CK3\binaries\ck3_D_opt.exe` (233 MB, build de depuracao otimizada): pode ter mais textos de
assert e nomes de funcao. Usar como apoio para entender o codigo; os enderecos dele NAO valem para o `ck3.exe`.

## Secoes
| Secao | RVA | Tamanho |
|---|---|---|
| .text | 0x1000 | 0x3fd013c |
| .rdata | 0x3fd2000 | 0xf69a24 |
| .data | 0x4f3c000 | 0x924028 |
| .pdata | 0x5861000 | 0x2a7e30 |

## P1 - achados estaticos (2026-10-03)
O executavel tem RTTI (nomes de classe), o que permite achar as vtables.

Classes de comando relevantes:
| Classe | Vtable principal (RVA) | Vtable secundaria (offset 24) | 1a funcao virtual (provavel destrutor) |
|---|---|---|---|
| `CMoveUnitCommand` | 0x432bef0 | 0x432bf88 | 0x26b46f0 |
| `CHaltUnitsCommand` | 0x432be58 | 0x432be28 | 0x26b5350 |
| `CToggleCompanionAICommand` | 0x432f6a0 | 0x432f5a8 | 0x801fd0 |
| `CSetCompanionAISettingsCommand` | 0x432f5d8 | 0x432f670 | 0x801fd0 |

Classes auxiliares vistas no RTTI: `CJominiCommandHelper<CMoveUnitCommand>`, `CGameCommandHelper<CMoveUnitCommand>`
(e as mesmas para `CHaltUnitsCommand`).

Funcoes virtuais da vtable principal de `CMoveUnitCommand` (indices 0 a 11):
`0x26b46f0, 0x3b8afe0, 0x3418410, 0x3b8b060, 0x3418620, 0x7e6490, 0x26b4a00, 0x803620, 0x26c1e70, 0x23ceb90, 0x7e6490, 0x26c1f20`
- Compartilhadas com os outros comandos (classe base): 0x3b8afe0, 0x3418410, 0x3b8b060, 0x3418620, 0x7e6490, 0x803620.
- Proprias do `move_unit`: 0x26b46f0, 0x26b4a00, 0x26c1e70, 0x23ceb90, 0x26c1f20 (candidatas a destrutor,
  serializar, validar e executar).

Textos-ancora:
| Texto | RVA |
|---|---|
| `commanddispatcher.cpp` | 0x44e1ec6 |
| `move_unit` | 0x4299448 |
| `unit_order_type` | 0x42a23c0 |
| `gathering_army` | 0x4299e48, 0x42a340d |

Formato do comando (de `logs\message.log`):
```
move_unit={ default_state={ identity sender receiver attribs }
  object={ valid_for=ai unit=<id> target=<provincia> unit_order_type=set type=any gathering_army=default } }
```

## Estrategia
`CHaltUnitsCommand` e alcancavel pela GUI (`PostCommand( Army.GetOrders.GetHalt )`). Ele serve de modelo:
1. Achar quem escreve a vtable 0x432be58 (construtor do Halt) e quem chama esse construtor (`GetHalt`).
2. Achar a funcao de `PostCommand` (recebe o comando pronto e entrega ao despachante).
3. Achar quem escreve a vtable 0x432bef0 (construtor do `move_unit`) e seus argumentos (unidade, provincia,
   tipo de ordem).
4. Na DLL: construir o `move_unit` como o jogo faz e entregar pela mesma funcao do passo 2.

## P1 - resultado da desmontagem (2026-10-03, com `capstone` 5.0.7)
Script de apoio: `docs/re/relib.py` (funcoes `dis`, `xref_rip`, `xref_call`, limites de funcao pela `.pdata`).

### Envio de comandos
| Item | RVA | Observacao |
|---|---|---|
| Despachante (objeto global) | 0x5762170 | Mesmo endereco usado pela IA, pelo clique do jogador e pelo Halt |
| `Post(despachante, comando*, flags)` | 0x973e00 | Clona o comando (virtual +0x40) e chama 0x341d8e0. Retorna bool em `al` |
| Envio interno | 0x341d8e0 | `(despachante, unique_ptr<comando>*, flags)` |
Flags observados: IA usa 7; clique do jogador usa 0xE (14).

### Layout de `CMoveUnitCommand` (tamanho 0x168)
| Offset | Tipo | Campo | Valor no clique do jogador |
|---|---|---|---|
| 0x00 | ptr | vtable principal (0x432bef0) | - |
| 0x08 | u8 | estado/flags | 0 |
| 0x0C | u64 + u32 (ate 0x18) | `default_state` (identity, sender, receiver) | 0 |
| 0x18 | ptr | vtable secundaria (0x432bf88) | - |
| 0x20 | u32 | `valid_for` | 1 (jogador); IA usa 2; padrao da fabrica 0 |
| 0x24 | u32 | `unit` (id da unidade) | id do exercito selecionado |
| 0x28 | u32 | `target` (id da provincia) | provincia sob o cursor |
| 0x2C | u32 | `unit_order_type` | retorno de 0x26b51d0 (0 ou 1; 2 = invalido, nao envia) |
| 0x30 | u32 | `type` | 2 |
| 0x34 | u32 | `gathering_army` | argumento da funcao; padrao 1 |
| 0x38 | sub-objeto (0x128) | lista interna, iniciada por 0xc7ba80 | vazia |
| 0x160 | u64 | - | 0 |

### Onde o jogo monta o `move_unit`
| RVA da funcao | Quem | Observacao |
|---|---|---|
| 0xa83fb0 (monta em 0xa84722, envia em 0xa84778) | Clique do jogador no mapa | Laco sobre as unidades selecionadas; `valid_for=1`, flags 0xE. Chamada unica vinda de 0xa84ce8 |
| 0x186b1b0, 0x18721d0, 0x18726e0, 0x1873120, 0x1874a30, 0x18793d0, 0x18cb7b0, 0x18ce550, 0x18d0dc0 | IA | `valid_for=2`, flags 7 |
| 0x23c9f20 | A identificar | Dois pontos de montagem |
| 0x26c6fa0 | Fabrica (aloca 0x168 e preenche padroes) | Usada ao ler comando da rede/arquivo |
| 0x26c1e70 | Clone | - |

Funcoes auxiliares:
- 0x26b51d0 `(unidade*, provincia*, bool)` -> tipo de ordem (0/1/2).
- 0x2248880 `(unidade*, tipo)` -> bool, validacao feita antes de enviar no clique.
- 0xc7ba80 `(sub*)` -> construtor do sub-objeto em +0x38.

### Receita para a DLL (a confirmar em P2)
```
u8 cmd[0x168] = {0};
*(void**)(cmd+0x00) = base + 0x432bef0;
*(void**)(cmd+0x18) = base + 0x432bf88;
*(u32*)(cmd+0x20) = 1;          // valid_for = jogador
*(u32*)(cmd+0x24) = unit_id;
*(u32*)(cmd+0x28) = province_id;
*(u32*)(cmd+0x2C) = 0;          // unit_order_type = set
*(u32*)(cmd+0x30) = 2;
*(u32*)(cmd+0x34) = 1;
((void(*)(void*))(base+0xc7ba80))(cmd+0x38);
((bool(*)(void*,void*,int))(base+0x973e00))(base+0x5762170, cmd, 0xE);
```
Chamar na thread principal do jogo (por exemplo, dentro do WndProc da janela), nao em thread propria.

### Carga da DLL sem alterar o exe
O `ck3.exe` importa `VERSION.dll` e `WINMM.dll`. `VERSION.dll` NAO serve: o Windows aplica um shim de
compatibilidade a todo executavel chamado `ck3.exe` (`apphelp.dll`, `AcGenral.DLL`) e ele carrega a `VERSION.dll`
do System32 antes das importacoes do jogo. `WINMM.dll` nao e pre-carregada: uma `winmm.dll` propria na pasta
`binaries`, que repassa as 180 funcoes para a do sistema, e carregada sozinha. Para remover, apagar o arquivo.

## P2 - confirmacao em jogo (2026-10-04)
Log: `docs/re/p2_log_2026-10-04.txt`. Confirmado: vtable, offsets dos campos, despachante 0x5762170, funcao 0x973e00.
- Jogador: `valid_for=1`, flags 0xE, thread principal (a mesma que carrega a DLL), `type=2`, `gathering=1`.
- `unit_order_type`: 0 = substitui a ordem, 1 = entra na fila (Shift).
- IA: `valid_for=2`, flags 7, enviados de varias threads de trabalho (a funcao de envio aceita chamadas de
  qualquer thread).
- Retorno 1 em todos os envios.

## P4 - envio pela DLL (2026-10-04)
Log: `docs/re/p4_log_2026-10-04.txt`. 50 envios, jogo estavel.
- O despachante valida ao processar: comando recusado gera "Invalid command" em `logs\message.log`
  (`commanddispatcher.cpp:78`) e nao tem efeito. Recusados nos testes: provincia 0, provincia inexistente,
  unidade inexistente, unidade de outro dono, e algumas ordens `append`.
- `Post` retorna 1 mesmo para comando que depois e recusado.
- Campos opcionais do comando (texto do `message.log`): `path={ ids de provincia }` e `total_cost=<numero>`.
  Correspondem ao sub-objeto em +0x38 (lista) e, provavelmente, ao campo em +0x160. A IA envia o caminho pronto;
  o clique do jogador envia a lista vazia e o jogo calcula.

## C1 - registro de configuracoes do motor (2026-10-04)
Para a aba "Mods" (docs/arquitetura_dll.md, secao 11). Estatico com `docs/re/c1_settings.py`; conferido no jogo
aberto com `peek` e o pedido novo `procura` (busca de um valor de 64 bits na memoria, em thread separada).

Classes (RTTI): `CPdxSetting` (base), `CPdxBoolSetting`, `CPdxEnumSetting`, `CPdxStringSetting`,
`CPdxCallbackSetting`, `CPdxValueSetting<int>` (`?$CPdxValueSetting@H`), `CPdxSettingsFile`,
`CJominiSettingsWindow`. Fonte do motor citada nos textos: `clausewitz\pdx_settings\source\pdx_settings_manager.cpp`.

Gerenciador (`CPdxSettingsManager`):
| Item | Endereco / campo |
|---|---|
| Ponteiro global | `exe+0x5710d58` |
| Criacao (aloca 0x80, grava o global, registra a lista pendente) | `0x3b44b40` (chamada por tabela, refs em `0x354d758` e `0x3b39a77`) |
| Registrar a lista pendente das opcoes estaticas | `0x3b45050` |
| `Registrar(gerenciador, &grupo, opcao, n)` | `0x3b44cf0`; `grupo` = `{const char *p; int32 len; uint8 0}`; cria o grupo se nao existir; `opcao+0xb8` = `n`; avisa o ouvinte `+0x30` (vfunc +8) |
| Achar grupo pelo nome | `0x3b45360` |
| Grupos | `+0x18` vetor de ponteiros, `+0x20` capacidade, `+0x24` quantidade (10 no jogo aberto) |
| Trava (`lockdown`) | byte `+0x79`; posta em `0x3554bb0`, depois de `LoadSettings` e do `compound_settings.txt`. Registrar depois: log `Setting '%s/%s' registered after setting lockdown, may get lost` (a opcao nao recebe o valor salvo) |

Grupo (no de 0x38 bytes criado em `0x3b44cf0`; no jogo os nos aparecem com cabecalho e indice): nome em
`std::string` (SSO), lista de `CPdxPtr<CPdxSetting>` em `+0x20` (alocador `exe+0x5003e10`). Grupos vistos:
`Tools`, `GUI`, `Audio`, `Account`, `game`, `interface`, `System`, `Graphics`, `accessibility`, `notifications`
(as secoes do `pdx_settings.txt`).

Opcao (`CPdxSetting`, campos comuns):
| Campo | Conteudo |
|---|---|
| `+0x00`, `+0x08` | vtables |
| `+0x10` | contador de referencias |
| `+0x18..+0x30` | lista de chamadas de mudanca (alocador `SPdxCallback<const CPdxSetting*>`) |
| `+0x78` | nome (`std::string`, ex.: `debug_saves`) |
| `+0x98` | chave de texto da interface (`std::string`, ex.: `SETTING_DEBUG_SAVES`) |
| `+0xb8` | `n` do registro |
| `+0xc0..+0xc4` | marcas (0x101, 0, int do 5o argumento da construtora) |

Construtoras:
| Tipo | Tamanho | Construtora | Argumentos |
|---|---|---|---|
| liga/desliga | `new(0xF8)` | `0x3b46730` | `(obj, &nome, &chave_texto, padrao (byte), int 0)` |
| texto | `new(0x100)` | `0x3b4c290` | `(obj, &nome, &chave_texto, &?, int)` |
| botao com acao | ? | `0x3b4bf70` | a conferir |
| lista | ? | `0x3b4b480` / `0x3b4b520` | a conferir |
| barra inteira `CPdxValueSetting<int>` | `new(0x128)` | montada direto no codigo (ex.: `debug_saves` em `0x403ef0`: passo `+0xf8`, minimo `+0xfc`, maximo `+0x100`, texto de valor em `+0x108`); valor por `0xa89250(obj, &valor, 0)` | |

Os textos de nome e chave usam a mesma visao de texto do registro (`{ptr, int32 len, byte}`).

Consequencia para a C2: o carregador precisa registrar as opcoes dos mods logo depois de `0x3b44b40` (criacao do
gerenciador) e antes de `LoadSettings`, com um desvio nessa funcao. Muda o `winmm.dll` (reabrir o jogo).

Cuidado com o `procura`: a primeira versao lia toda a memoria do processo na thread principal (21 GB, incluindo
memoria de video) e segurava o jogo por cerca de 60 s. Agora le so memoria propria (`MEM_PRIVATE`/`MEM_IMAGE`, sem
`WRITECOMBINE`/`NOCACHE`) numa thread separada (~80 s por busca, jogo responde normalmente).

## C2b - janela de configuracoes montada pela DLL (2026-10-04/05)
| Item | Endereco / campo |
|---|---|
| Montagem das abas (`CJominiSettingsWindow`, vtable principal [1]) | `0x32d7700(janela)`: limpa `+0xd8` (`0x7e9ea0`), le `settings_layout.txt`, chama o leitor, liga as secoes. Prologo: `mov [rsp+0x10],rbx; mov [rsp+0x18],rsi` (10 bytes, desvio no carregador) |
| Abas | `janela+0xd8` ponteiro, `+0xe0` capacidade, `+0xe4` quantidade; pagina selecionada `+0x108` |
| Leitor do layout | sub-objeto `janela+0xc8` (vtable `0x44c33a0`), vtable[3] `0x3b8b060(leitor_janela, &leitor)`; cada bloco `page` (token `0x2d1`) cria pagina de 0x58 bytes (`0x32d6cc0`) e ACRESCENTA ao vetor |
| Pagina | nome traduzido (`std::string`) em `+0x10`, secoes em `+0x30` (ponteiro), `+0x38` capacidade, `+0x3c` quantidade |
| Ligar secao | `0x32d5230(secao)` |
| Texto da memoria | `CMemoryFile` (vtable `0x4559758`, 0x58 bytes: `+0x08`=1, `+0x10`=1, nome `std::string` vazia em `+0x18`, `+0x38` = buffer `{dados, ?, alocador char 0x4fea420}` preenchido por `0x3bb5200(buf, texto, tamanho)`, `+0x50`=-1); `CLexer` por `0x3b58cf0(lexer, &arquivo, 0)` e vtable trocada para `CTextLexer` (`0x4556c20`); leitor `0x3b8e670(leitor, &lexer)`; destrutoras `0x3b8e850` (leitor) e `0x3b58dc0` (lexer) |
| Trocar valor de opcao liga/desliga | `0x3b46990(opcao, valor, 1)`, chamado pelo clique da caixa (`0x32d6610`, ligado ao elemento `setting_bool_cb` em `0x32d6660`) |
| Valor da opcao liga/desliga | tres campos `SCopyableAnyTypeInPlace<bool>` em `+0xc8`, `+0xd8`, `+0xe8` (tipo, depois o byte do valor em +8) |

A janela nao tem ponteiro global (dezenas de referencias no heap, dos elementos da interface); o carregador a guarda
no desvio da montagem. O jogo remonta a janela sozinho (ex.: localizacao recarregada em `-debug_mode`).

## C4 - barras de valor (2026-10-05)
`CPdxValueSetting<int>` (vtables `0x4238228`/`0x4238218`) e `<float>` (`0x428cb28`/`0x428cb18`), 0x128 bytes, montadas
direto no codigo pelo jogo (exemplos: `debug_saves` em `0x403ef0`, `tooltip_delay` em `0x4045e0`).
| Campo | Conteudo |
|---|---|
| `+0x28` | alocador das chamadas de mudanca `0x4fea408` |
| `+0x78`, `+0x98` | nome e chave de texto (`std::string`, `0x7e9530` = assign) |
| `+0xbc` | numero do tipo: int `0x7ea910()`, float `0x7ea8a0()` (liga/desliga: `0x32dbfb0()`) |
| `+0xc0` | `0x101` (word) |
| `+0xc8`, `+0xd8`, `+0xe8` | campos de valor, tipo vazio `0x9ff600()` antes do primeiro valor |
| `+0xf8`, `+0xfc`, `+0x100` | passo, minimo, maximo (int32 ou float) |
| `+0x108` | chave de texto do formato do valor (`SETTING_VALUE_NONE` = `$VALUE$`, `SETTING_VALUE_PERCENTAGE`...) |
Valor: `0xa89250(opcao, &int, 0)` e `0xa89430(opcao, &float, 0)` (prendem entre minimo e maximo). No
`pdx_settings.txt`: `value=3` e `value=0.100000`. Lista: classe derivada por opcao, montada por `0x2049010(obj 0x188,
&nome, &chave, 0)`, itens por `0x809dc0` a partir de tabelas fixas, valor por `0x3b4b690(obj, indice, 0)`.

## C5 e C6 - botoes da aba Mods e leitura dos valores (2026-10-05)
| Item | Endereco / campo |
|---|---|
| Botao (`CPdxCallbackSetting`, vtable `0x4555d48`) | `new(0x138)`; `0x3b4bf70(obj, &nome, &chave, &funcao, 0)`; a funcao e um `std::function<void()>` do MSVC (0x40 bytes, implementacao no proprio buffer, ponteiro dela em `+0x38`), movido para `obj+0xf8`. Tipo usado pelo jogo: `_Func_impl_no_alloc<void(*)()>` (vtable `0x408a450`, ponteiro de funcao em `+8`), ex.: `reset_reactive_advice` em `0x4051a0` |
| Selecionar aba | `SettingsPage.OnClick` = `0x32d9cf0(pagina)`: janela = `pagina+0x08`; desmarca `[janela+0xd0]+0x50`, grava `janela+0xd0` = pagina, marca `pagina+0x50`, redesenha com `0x32d8940(janela)` |
| Aba selecionada | `janela+0xd0`; `SettingsPage.IsSelected` = byte `pagina+0x50` (`0xe37df0`) |
| Mostrar opcao | `JominiGUISetting.ShouldShow` = byte `+0x48` do objeto de interface da opcao (`0xd85550`), posto em 1 na criacao (`0x32d5230`, ctor `0x34a7260`) |
| `ret` do jogo | `0x7e6490` (`c2 00 00`) |
| Valor da opcao (leitura) | dado do primeiro campo de valor em `+0xd0` (bool: byte; int: int32; float: float); tipo pela vtable (bool `0x4555700`, int `0x4238228`, float `0x428cb28`) |

## A3 - valores que hoje vem dos gatilhos do script (2026-10-05, em andamento)
Gatilhos do jogo achados pelo RTTI: `CSupplyArmyTrigger` (army_supply), `CArmySizeTrigger` (army_size),
`CRaidLootTrigger`, `CIsAtWarTrigger`, `CIsIndependentRulerTrigger`, `CMaxMilitaryStrengthTrigger`, `CTopLiegeLink`.
Sao o mesmo molde generico (vtable igual, o valor vem de um sub-objeto em +0x58); o registro pelo nome so cria uma
entrada de fabrica (ex.: army_supply em `0x53d120`). Mais rapido achar o campo pela memoria, comparando com o valor
que o script da (efeito de teste `ping_livre` com `army_size` etc.) e com o pedido `peek army:<id>` (novo).

| Valor | Onde (objeto do exercito, 0x208 bytes) | Como foi conferido |
|---|---|---|
| tamanho (`army_size`) | int32 em `+0x24` (tambem `+0x1fc`) | 6205 -> 6783 -> 6877 durante o reforco, igual ao script |
| suprimento (`army_supply`) | ponto fixo em `+0x180` | 100 = 10000000; o efeito `refill_supply` (`CRefillSupplyEffect`, vtable[24] `0x2e80d30`) grava o maximo em `+0x180` |
| saque carregado, modo raid | `+0x1e0`, byte `+0x1d4` | ja usados pelo nucleo |

Ainda nao achados: regimentos de homens de armas (atual, maximo, contratado; as listas em `+0x38` e `+0xa0` nao sao
de regimentos: a primeira tem pares de numeros pequenos, talvez ids numa tabela; a segunda e texto), guerra do
jogador, dono/suserano/independencia/forca (para `ar_em_casa` e a recusa de alvo) e `can_raid_trigger`.
Nota: no `ar_deve_voltar`, `any_army_maa_regiment = { percent >= 0.5 ... }` quer dizer "pelo menos metade dos
regimentos" abaixo de `ar_cfg_tropas_volta` do maximo.

### A3 resolvida pelo caminho do proprio jogo: expressoes de script avaliadas pelo C (2026-10-05)
Em vez de achar cada campo, o C pede ao jogo para ler e avaliar uma expressao de script, com o mesmo codigo que o
script usa. Pedido de prova `expr <texto>` no nucleo; avaliado a cada `auto_raid_ping_livre` em exercito.
| Passo | Endereco |
|---|---|
| Objeto que guarda a expressao | um efeito `add_loot` pela fabrica do jogo `0x2e81070(entrada)` (0x168 bytes; numero do nome em `entrada+0x10`); o valor de script fica em `+0x60` (construtor `0x1928a80`) |
| Texto | `valor = <expressao>` num `CMemoryFile` + `CLexer`/`CTextLexer` + leitor `0x3b8e670`, como no layout (C2b) |
| Avancar ate o valor | `0x3b90120(leitor)`: le `chave =` e para no valor (o laco do leitor `0x3b8b090` chama antes de entregar cada valor) |
| Ler | metodo 1 do efeito: `(efeito, leitor, tipo de escopo)` (repassa a `efeito+0x60`, vtable[3]); tipo do exercito `0x1b`. Sem o `0x3b90120` antes: `Cannot read [] as a script value` |
| Avaliar | `0x9698b0(efeito+0x60, &saida, contexto, contexto[5], efeito+0x10)` com o contexto de um efeito em execucao no escopo (o mesmo que o carregador usa nos `auto_raid_*`) |
Conferido no exercito 1996488707: `army_supply` 100 = 100, `army_size` 6877 = 6877,
`{ if = { limit = { ar_em_casa = yes } add = 1 } }` 1 = 1. Um mecanismo so cobre todos os gatilhos e valores do
decisor (homens de armas com `every_army_maa_regiment`, guerra com `root = { is_at_war = yes }`, forca do dono com
`var:ar_alvo_prov.county.holder.top_liege.max_military_strength`...). Limite: precisa do contexto de um efeito em
execucao no escopo certo (o decisor no jogo roda dentro de um efeito chamado em cada exercito).

### Avaliar em outro escopo e mexer em variaveis sem script (SIM2b, 2026-10-05)
| Item | Endereco / campo |
|---|---|
| Contexto de efeito | `ctx[0]` = escopo atual (`{u16 tipo, u32 id em +8}`), `ctx[2]` = root (no efeito do jogador, o mesmo escopo), `ctx[5]` = 4o argumento do avaliador |
| Avaliar em outro escopo | copiar 16 qwords do contexto e trocar so `ctx[0]` por um escopo proprio; root continua o do efeito. Conferido: do `auto_raid_ping_livre` no personagem, `army_size` = 6877, `army_supply` = 100 e `ar_em_casa` = 1 no exercito, iguais a memoria |
| Tipo de escopo na leitura | a expressao e lida com o tipo do escopo onde sera avaliada (exercito 0x1b, personagem 0x4, provincia 0x8) |
| `remove_variable` | `CJominiRemoveVariableEffect<0>` (vtable `0x44d3148`), funcao 22 em `0x3393c00`: `0x3345af0(variaveis, numero do nome)` e `0x33465f0(variaveis)`; `variaveis` = `0x3329990(escopo*)` (sem o +8 do gravar) |
| `set_variable` com prazo | `CJominiSetVariableEffect<0>` (vtable `0x44d1ed8`), funcao 22 em `0x3393480`: o prazo vem de `0x3372f50(objeto+0x208, ctx, ctx[5])` e vai como 4o argumento de `0x3346b30` em dias. Conferido: `days = 3` pelo script e `dias = 3` pela DLL gravam o mesmo campo `+0xc` (17) na entrada |
Pedidos de teste no nucleo: `ctxprova <tipo> <id> <texto>` e `var <tipo> <id> <nome> [set <valor> [dias] | del]`.

## Pendente
- Preencher `path`/`total_cost` no comando para impor a rota (achar o layout da lista e quem a calcula).
- Detectar na DLL quando o comando e recusado (hoje so aparece no `message.log`).
- Entender por que algumas ordens `append` sao recusadas.
- Ponte script -> DLL: como o script descobre o id da unidade e passa o pedido.
