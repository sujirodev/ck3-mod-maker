Versao em ingles: [notas_unidades.md](notas_unidades.md)

# Unidades, localizacao e tempo de viagem

Analise estatica do `ck3.exe` 1.19.0.6 em 2026-10-04. Tudo aqui ainda precisa de confirmacao em jogo (testes Q1 a Q5
de `docs/funcionalidades.md`). Enderecos em RVA.

| Item | RVA / offset | Observacao |
|---|---|---|
| Ponteiro da tabela de unidades | 0x570cbe8 (unidade nula padrao em 0x570cbe0) | indice = id & 0xFFFFFF; limite em `[tab+0x2c]`; entradas de 16 bytes em `[tab+0x20]`, ponteiro em +8; conferir `[unidade+0x10] == id` |
| Segunda tabela (dono/exercito) | 0x570c6b0 | indexada por `[unidade+0x178]`, confere `[obj+0x10] == id` |
| Tabela de personagens (provavel) | 0x570c0b0 | indexada por `[unidade+0x174]`, confere `[obj+0x18] == id` |
| Provincia atual | `[unidade+0x20]` | ponteiro; id em `[provincia+0x10]` |
| Caminho em curso | lista em `[unidade+0x38]`, tamanho em `[unidade+0x44]` | indices de provincia |
| Localizacao (considera movimento) | 0x2246cf0 `(unidade)` -> provincia* | |
| Proxima provincia do caminho | 0x2246d70 `(unidade)` | |
| Destino final do caminho | 0x2246dc0 `(unidade)` | |
| Tabela de provincias | `[[global]+0xa0]`: vetor em +0x140, quantidade em +0x14c | global lido em 0x2246d15 |
| Data atual | 0x570edd8 | usada na comparacao de "esta em movimento" |
| Tooltip de marcha | funcao 0xc6f810 a 0xc711fb | monta `PROVINCE_TOOLTIP_ETA` (0xc70ee1), `..._NO_PATH_ALL` (0xc7091d) etc. |
| Contexto do buscador de caminho | 0x23c3310 `(ctx, unidade)` | ctx na pilha do tooltip em rbp+0x490 |
| Achar caminho | 0x23c33f0 `(ctx, inicio*, alvo*, 2, caminho_saida*)` | chamada em 0xc70e20 |
| Tempo de viagem | 0x2247340 `(unidade, &tempo, caminho*, inicio*)` | ponto fixo 100000 = 1 dia; caminho vazio -> 0 |
| Resto do passo atual | 0x2247600 `(unidade, &saida, 0)` | somado quando a unidade ja esta andando |
| "Esta em movimento" | 0x2247890 `(unidade, &saida)` | compara com a data atual |

## Dono e classificacao (estatico, 2026-10-04)
| Item | RVA / offset | Observacao |
|---|---|---|
| Id do personagem do jogador local | 0x4fe7ee0 (dword) | lido no tooltip de marcha (0xc6f95d) e indexado na tabela de personagens |
| Dono da unidade | `[unidade+0x174]` | id de personagem (tabela 0x570c0b0, confere `[obj+0x18]`) |
| Exercito da unidade | `[unidade+0x178]` | id na tabela 0x570c6b0 (confere `[obj+0x10]`); `[exercito+0x124]` volta ao id da unidade |
| Classe da unidade para o jogador | primeiro dword do `UnitItem` (item de tela) | 0 = minha, 1 = aliada, 2 = inimiga, 3 = hostil, 4 = neutra |
| `IsUnitItemMine/Ally/Enemy/Hostile/Neutral` | 0xc20560, 0xc20520, 0xc205a0, 0xc205e0, 0xc7e570 | so comparam o dword acima; registradas em 0x94510, 0x94680, 0x94970, 0x94af0, 0x947f0 |
| Quem calcula a classe | 0x224a610 `(unidade*)` -> 0 a 5 | TESTADO em jogo 2026-10-04. Compara o dono da unidade com o personagem do jogador: igual = 0 minha; em guerra = 2 inimiga; 0x2900490(jogador, dono, 0) = 3 hostil; 0x28ffe80(jogador, dono, 0) = 1 aliada; senao 4 neutra; 5 se `[unidade+0x18]` nao for 0 ou 1 |
| Lista de caminho (tipo) | 0x128 bytes: dados em +0, quantidade em +0xC, alocador em +0x10; custo total logo depois (+0x128) | mesmo tipo do sub-objeto +0x38 do `move_unit`; construtor 0xc7ba80 |
| Contexto do buscador | construtor 0x23c3310 preenche ate +0x68 | modo em +0x28 (2 no tooltip) |

## Previsao de combate (testado em jogo, 2026-10-04)
| Item | RVA | Observacao |
|---|---|---|
| Texto do tooltip | 0xc750d0 (chamada por 0xc71e10, `Army.GetCombatPredictionForTooltip` em 0xc7c9a0) | monta `PREDICTION` / `UNIT_NEUTRAL_COMBAT_PREDICTION` |
| Calculo | 0xc6e9c0 `(&resultado, vetor de exercitos A, vetor de exercitos B, k, k, 0, 0)` | vetores = {ids de EXERCITO u32, quantidade, capacidade, alocador 0x4FEADE0}; k = qword em 0x57BFB48 |
| Resultado | ponto fixo, 100000 = 1,0 | chance de A vencer: 0,5 contra si mesmo; 0,71 de A contra B deu 0,28 de B contra A |
| Limiares dos niveis | ponteiro em 0x4F42D40, quantidade em 0x4F42D4C | 0,30 / 0,45 / 0,60 / 0,80 -> 5 niveis de previsao |

## Camera e nomes
| Item | RVA | Observacao |
|---|---|---|
| `Province.PanCameraTo` | 0xa1c880 `(provincia*)` | centraliza |
| `Province.ZoomCameraTo` | 0xa1ca20 `(provincia*)` | centraliza e aproxima |
| Chave de texto da provincia | `std::string` em `[[provincia+8]+0x28]` | ex.: `stepstone_sea` |
| Coordenadas no mapa | `[[provincia+8]+0x10]` e `+0x14` (inteiros) | usadas pela camera |

## Saque e raid em andamento (lido em jogo, 2026-10-04; conferencia do usuario pendente)
| Item | RVA / offset | Observacao |
|---|---|---|
| Objeto do exercito | tabela 0x570c6b0, id em `[unidade+0x178]`, confere `[exercito+0x10]` | `[exercito+0x124]` = id da unidade |
| E exercito raid | `[exercito+0x1d4]` (byte) | |
| Saque carregado | `[exercito+0x1e0]` (ponto fixo, 100000 = 1 ouro) | funcao de tela `Army.GetRaidLoot` -> 0x18cb390 |
| Capacidade de saque | 0x2272a80 `(exercito*, &saida)` | `Army.GetLootCap`; `Army.IsLootCapped` = 0x2272e00 |
| E exercito de escambo / carga | `[exercito+0x1ec]` (byte), `[exercito+0x1f0]` | |
| Raid em andamento do exercito | id em `[exercito+0x1e8]` (-1 = nenhum) | tabela 0x57BFC38, confere `[raid+8]` |
| Objeto do raid | `+0x10` id do exercito, `+0x18` progresso feito, `+0x20` alvo (objeto com id em +0x10) | |
| Progresso total exigido | 0x23a1040 `(&saida, alvo*)` | |
| Progresso por dia | 0x23a0cc0 `(&saida, exercito*, 0)` | passar o EXERCITO; com a unidade devolve so a base (20) |
| Dias para terminar | `ceil((total - feito) / por dia)` | mesmo calculo de `Raid.GetEta` (0xc30990) |
| Provincia de mar | byte em `[[provincia+8]+0x1d]` | usado para marcar o caminho; terra tem byte em +0x1b |

Falha contida observada: chamar 0x23a1040 com um objeto de provincia comum (em vez do alvo guardado no raid) causou
violacao de acesso em exe+0x20AB991; o carregador conteve e o jogo seguiu. O alvo do raid nao e o mesmo tipo de
objeto da tabela de provincias: a estudar, para estimar o tempo de saque de um local ANTES de ir ate ele.

## Alvo de saque (testado em jogo, 2026-10-04)
| Item | RVA / offset | Observacao |
|---|---|---|
| Pode saquear este local | 0x293db30 `(exercito*, provincia*, texto ou NULL)` -> bool | com texto nulo so devolve o resultado |
| Saque disponivel no local | 0x291dc90 `(&saida, provincia*, NULL, NULL)` | ponto fixo; 0 se saqueado recentemente |
| Modo "saquear ao parar" | byte `[exercito+0x1d5]` | funcao de tela `Army.HasLooterStance` (0x22069e0) |
| Tooltip de saque da provincia | funcao 0xc57840 | monta `RAID_PROVINCE_TOOLTIP_LOOT` |
| Coordenadas da provincia | inteiros em `[[provincia+8]+0x10]` e `+0x14` | usadas no pre-filtro por raio |

## Estado de pausa (testado em jogo, 2026-10-04)
Funcao de tela `IsGamePaused` em 0xa21c10. Tres fontes de pausa:
| Fonte | Como ler | Observacao |
|---|---|---|
| Pausa do jogador | byte `[[exe+0x570F738]+0x20]` | e a que `CPauseGameCommand` liga e desliga |
| Pausa por evento/mensagem | 0x2707280 `([[exe+0x570DFE8]+0xa0] + 0x2f4c0, 0)` -> bool | fica ativa ate o jogador responder; `pause 0` NAO a remove |
| Outra pausa | 0x346b8f0 `([exe+0x570F738])` -> bool | nao observada ainda |
Velocidade do jogo: `[[exe+0x570DFE8]+0x70] + 1`.

