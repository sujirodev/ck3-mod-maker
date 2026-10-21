# Guardian Calculation Assistant: progresso

Arquivo curto: estado atual e proximo passo. Reescrever (nao acumular historico); decisoes duradouras vao para
`decisoes.md`, o estado de cada funcionalidade para `funcionalidades.json`.

## Agora
- Versao publicada: 1.0.2 (GitHub sujirodev/ck3-guardian-calc-assistant, Nexus Mods)
- Funcionalidade ativa: f06 (release pelo pipeline compartilhado)
- Estado: o `pipeline/package.py` do ck3-mod-maker gera zip identico byte a byte ao `tools/package.py` do GCA
  (conferido em 2026-10-21 com o `ck3mod.toml` abaixo). Migracao preparada localmente, sem push.
- Bloqueios: nenhum. O ck3-mod-maker esta publicado (sujirodev/ck3-mod-maker) com a tag `v1` desde 2026-10-21.

## Proximo passo
1. Usuario revisa o branch local `pipeline-compartilhado` do GCA (`git diff main pipeline-compartilhado`).
2. Com o ok dele: merge no main, push, conferir o run do workflow (`gh run list`). Push sem tag nao publica release.
3. f06 vira `feito` com o link do run verde como evidencia.

## Teste em jogo
- Save de teste: (definir)
- Ultima conferencia registrada pelo ck3ctl: nenhuma
