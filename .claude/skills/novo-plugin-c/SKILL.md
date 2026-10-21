---
name: novo-plugin-c
description: ULTIMA OPCAO - plugin C da DLL ck3_custom. So com script comprovadamente insuficiente e aprovacao do usuario, ou para manter plugin existente (Auto Raid).
---

# Plugin C

## Antes de tudo: precisa mesmo?
Plugin C faz o mod publicado exigir a DLL: so roda no `ck3.exe` 1.19.0.6, quebra a cada patch, roda codigo nativo
no jogador. O Auto Raid so tem plugin porque ordem de marcha, pathfinder e previsao de combate nao existem no script.

So seguir se todos forem verdade:
1. O script foi esgotado: efeitos, triggers, script_values, scripted_guis, game rules, on_actions, e o jogo base
   (`ck3_vanilla`) nao mostra outro jeito.
2. Da para apontar o que falta (o efeito/trigger/informacao que nao existe), conferido com `ck3_ref`.
3. O usuario viu essa evidencia e o custo da dependencia e aprovou o plugin.

Se algum falhar, voltar para o script. Manter ou corrigir um plugin que ja existe nao precisa dessa aprovacao.

Referencias: `dll/ck3_api.h` (tudo que o plugin pode chamar), `docs/dll/arquitetura_dll.md` secoes 5 a 7,
`templates/plugin/plugin/` (modelo), o plugin do Auto Raid (`Documents/.../mod/auto_raid/plugin/auto_raid.c`) como
exemplo grande.

## Regras
- O plugin so fala com o jogo pela `ck3_api`, por ids (personagem 0x4, provincia 0x8, exercito 0x1b). Sem ponteiro
  do jogo, sem API do Windows (compila com `-nostdlib`; o nucleo libera `memset`, `memcpy`, `memmove` e poucas mais).
- Valores de script em ponto fixo: 100000 = 1.
- Efeito novo: declarar no `plugin/plugin.txt` (`<efeito>` = escopo exercito; `<efeito> livre` = qualquer escopo) e
  a variavel de resposta (`var <nome>`). Nome novo exige reabrir o jogo; o `.c` recompila sozinho em `-debug_mode`.
- Prefixo do mod em tudo. Nome repetido em outro plugin e recusado.
- Codigo do TinyCC e 3 a 5 vezes mais lento que gcc -O2: laco pesado (varrer todas as provincias, Dijkstra) pede
  uma primitiva nova no nucleo (`dll/ar_core.c` + `ck3_api.h`, sobe `CK3_API_VERSION`), oferecida a todos os mods.
- `A->log` so grava em `-debug_mode` (api 7).

## Passos
1. Escrever o efeito/pedido no `.c` e no `plugin.txt`.
2. `uv run ck3ctl check mods/<mod>/plugin/<arquivo>.c` (tcc fora do jogo; o hook ja faz ao editar).
3. No jogo: `ck3_plugin(recompilar=True)` -> "compilado e carregado".
4. Testar: `ck3_effect("<efeito> = 5")` e ler a resposta com `ck3_eval("var:<var>")`; ou `ck3_request("<pedido>")`.
5. Funcao nova na `ck3_api`: implementar no nucleo, `ck3ctl dll build nucleo`, `ck3ctl dll install nucleo` (troca a
   quente), conferir que o Auto Raid continua compilando (`ck3_plugin`).
