---
name: testar-em-jogo
description: Testar mod no CK3 aberto - save, efeitos, valores, triggers, error.log; registra evidencia no dev/features.json. Usar apos qualquer mudanca de mod.
---

# Testar em jogo

Ler antes as regras do CLAUDE.md ("Regras do jogo e da DLL").

## Preparar
1. `ck3_game(acao="status")`. Fechado: `ck3_game(acao="start", save=<save de teste>)` (espera carregar, ~3 min;
   pela CLI usar `run_in_background`). Aberto: se mudou `plugin.txt` ou `dlc_load.json`, reabrir; senao
   `ck3_save_load(acao="load", save=...)`.
2. Save de teste: um save proprio em `save games/` (ex.: `teste_<mod>`). Nunca usar o save de jogo do usuario sem
   pedir; copiar antes de alterar.
3. `ck3_state`: precisa de `carregado=true` e pausado.
4. `ck3_events(responder=True)` se houver tela pendente.

## Rodar
1. `ck3_mark` (guarda as posicoes dos logs).
2. Acionar o mod: `ck3_effect("trigger_event = <mod>.0001")`, `ck3_effect("<mod>_efeito = yes")`, ou despausar e
   deixar correr (`ck3_pause(False)`, `ck3_speed(5)`) quando depende de on_action/tempo.
3. Conferir resultado com `ck3_eval` (valores: `gold`, `var:x`, `num_of_...`) e `ck3_trigger` (condicoes), no
   jogador ou em `escopo="character:<id>"`.
4. `ck3_errors(mod=<nome>, desde=<mark>)` e `ck3_dll_log`. Erro do mod = falha do teste, mesmo que o efeito pareca
   ter funcionado.

## Registrar e relatar
1. `dev/features.json` do repo do mod: cada feature testada vira `done` com `evidence` = data, comando e resultado,
   em ingles (ex.: "2026-10-21 ck3ctl eval gold: 150 -> 250; error.log: nothing from mm"), ou `blocked` com o motivo.
   O hook recusa `done` sem evidence ou com dependencia que nao esta `done`.
2. Reescrever `dev/progress.md` (em ingles): estado atual, proximo passo, save de teste usado.
3. Ao usuario: tabela curta (testado, esperado, obtido, erros novos). Se algo falhou, seguir com a skill `debugar`.
