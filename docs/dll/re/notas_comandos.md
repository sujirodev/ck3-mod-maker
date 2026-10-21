# Comandos de exercito (formatos)

Capturados em jogo em 2026-10-04 com o registro de comandos da DLL (`cmdlog on`) e reenviados pela DLL com
sucesso. `ck3.exe` 1.19.0.6, enderecos em RVA. Log: `docs/re/sessao_log_2026-10-04.txt`.

## Envio
- Funcao externa `Post(despachante, comando*, flags)` em 0x973e00: clona o comando e chama a interna.
- Funcao interna `(despachante, unique_ptr<comando>*, flags)` em 0x341d8e0: por aqui passam TODOS os comandos
  (225 pontos do jogo a chamam direto). O registro geral de comandos fica nela.
- Flags do jogador: 0xE. Pausa usa 7.

## Cabecalho comum
| Offset | Conteudo |
|---|---|
| 0x00 | vtable principal |
| 0x08 a 0x17 | estado (zerado no envio) |
| 0x18 | vtable secundaria |
| 0x20 em diante | campos do comando |

## Comandos
| Comando | Vtable / secundaria | Campos (a partir de 0x20) | Testado pela DLL |
|---|---|---|---|
| `CMoveUnitCommand` | 0x432bef0 / 0x432bf88 | +0x20 valid_for (1 jogador), +0x24 unidade, +0x28 provincia, +0x2C tipo de ordem (0 substitui, 1 fila), +0x30 = 2, +0x34 = 1, +0x38 lista de caminho (construtor 0xc7ba80), +0x160 custo | Sim |
| `CToggleRaidingCommand` | 0x432C7E8 / 0x432C628 | +0x20 = 1, +0x24 personagem do jogador, +0x28 id do exercito | Sim (usuario viu o exercito mudar de modo) |
| `CSplitHalfArmyCommand` | 0x432D408 / 0x432D4A0 | +0x20 = 1, +0x24 personagem do jogador, +0x28 id do exercito | Sim (1 unidade virou 2) |
| `CMergeUnitsCommand` | 0x432D1E0 / 0x432D1B0 | +0x20 = 1, +0x24 unidade destino, +0x28 vetor de ids de unidade {dados, quantidade, capacidade, alocador 0x4FEADE0} | Sim (2 unidades viraram 1) |
| `CDisbandArmyCommand` | 0x432D340 / 0x432D3D8 | +0x20 = 1, +0x24 id do exercito | Sim |
| `CRaiseTroopsCommand` | 0x4122750 / 0x4122720 | +0x20 personagem, +0x28 vetor de pontos de reuniao {dados, quantidade, capacidade, alocador 0x4FEBA40}, cada ponto = {provincia u32, cor u32}; +0x40 = -1; +0x48 tipo de exercito (1 no levantamento como raid) | Sim (exercito levantado na provincia pedida, como raid) |
| `CToggleLootingCommand` | 0x432c750 / 0x432c720 | +0x20 = 1, +0x24 personagem do jogador, +0x28 id do exercito. Alterna o byte `[exercito+0x1d5]` (saquear ao parar) | Sim (o byte mudou e o saque comecou) |
| `CHaltUnitsCommand` | 0x432be58 / 0x432be28 | +0x20 valid_for (1 jogador; a IA usa 2), +0x28 vetor de ids de UNIDADE {dados, quantidade, capacidade, alocador 0x4FEADE0} | Sim (enviado sem recusa; nao interrompe saque) |
| `CSelectEventOptionCommand` | 0x4335150 / 0x4335120 | +0x20 id da instancia do evento, +0x24 indice da opcao | Sim (evento respondido, pausa removida) |
| `CPauseGameCommand` | 0x432F300 / 0x432F2D0 | +0x20 = 1, +0x24 estado (1 pausa, 0 despausa) | Sim (confirmado pelo usuario) |

Texto do `raise_troops` no `message.log`: `character`, `rally_points={ { province color } }`, `raise_army_type`.

## Ids
- Unidade: ex. 2046820353 (0x7A000001). Indice na tabela = id & 0xFFFFFF.
- Exercito: ex. 1979711491 (0x76000003). `[unidade+0x178]` da o exercito da unidade.
- Mover e juntar usam id de UNIDADE; modo raid, dividir e dissolver usam id de EXERCITO.
