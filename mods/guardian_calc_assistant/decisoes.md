# Guardian Calculation Assistant: decisoes

Uma linha por decisao duradoura, com data e motivo. Mais nova em cima.

- 2026-10-21: adotado no ck3-mod-maker; o padrao dele (repo = pasta do mod, CHANGELOG como fonte das notas, release
  por tag, zip reproduzivel) virou o modelo `templates/repo` e o pipeline compartilhado.
- Os dois `.gui` sao gerados por `tools/gen_rows.py` e `tools/gen_confirm.py` (ver README do mod): nao editar a mao.
  `gui/interaction_confirmation.gui` sai do arquivo vanilla e precisa ser regenerado a cada patch do jogo.
- O aviso `Event target 'tnt_ga_other' is used but is never set` no error.log e esperado (escopo preenchido pela GUI).
- Prefixo `tnt_ga`.
