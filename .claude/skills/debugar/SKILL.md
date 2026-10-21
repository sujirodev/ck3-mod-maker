---
name: debugar
description: Depurar mod de CK3 - erro no error.log, efeito sem efeito, evento que nao dispara, texto cru, FALHA CONTIDA. Usar quando algo do mod falha no jogo.
---

# Debugar

Nunca chutar a causa: ler o arquivo do mod e o log antes.

## Onde olhar
| Sintoma | Fonte |
|---|---|
| Erro de script (escopo, trigger desconhecido, chave) | `ck3_errors(mod=...)` (error.log agrupado, com arquivo e linha) |
| Texto aparece como chave crua | `ck3_mod(acao="check")`: BOM, `l_<idioma>:`, nome `_l_<idioma>.yml`, chave faltando num idioma |
| Efeito "nao faz nada" | `ck3_effect` devolve a mensagem do jogo; depois `ck3_eval`/`ck3_trigger` passo a passo no mesmo escopo |
| Evento nao dispara | trigger do evento com `ck3_trigger` no escopo certo; `ck3_ref(<on_action>, tipo="on_action")` mostra o escopo esperado e se vem do codigo |
| Plugin C | `ck3_plugin(recompilar=True)`: erro com arquivo e linha; `ck3_dll_log` para o log do plugin |
| FALHA CONTIDA | `ck3_dll_log`: endereco e pilha; estado do jogo suspeito, recarregar o save antes de continuar |
| Mod nao carrega | `Documents/.../logs/debug.log` (lista de mods ativos) e `ck3_mod(acao="list")` (juncao, ativo) |

## Metodo
1. Reproduzir com o menor efeito possivel pelo `ck3_effect`.
2. Bisseccao: comentar metade do bloco, esperar a recarga a quente (`-debug_mode`, 1-2 s), repetir.
3. Comparar com o jogo base: `ck3_vanilla` com o mesmo efeito/trigger para ver o uso correto e o escopo.
4. Corrigir, `ck3_mark`, repetir o teste, `ck3_errors(desde=mark)` limpo.

Relatar: causa (arquivo:linha), correcao, como foi conferido. Atualizar a funcionalidade afetada no
`dev/features.json` do repo do mod (evidence nova ou `blocked` com o motivo) e o `dev/progress.md`.
