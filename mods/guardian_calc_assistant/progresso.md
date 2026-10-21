# Guardian Calculation Assistant: progresso

Arquivo curto: estado atual e proximo passo. Reescrever (nao acumular historico); decisoes duradouras vao para
`decisoes.md`, o estado de cada funcionalidade para `funcionalidades.json`.

## Agora
- Versao publicada: 1.0.2 (GitHub sujirodev/ck3-guardian-calc-assistant, Nexus Mods)
- Funcionalidade ativa: f06 (release pelo pipeline compartilhado)
- Estado: o `pipeline/package.py` do ck3-mod-maker gera zip identico byte a byte ao `tools/package.py` do GCA
  (conferido em 2026-10-21 com o `ck3mod.toml` abaixo). Migracao preparada localmente, sem push.
- Bloqueios: o ck3-mod-maker precisa estar publicado com a tag `v1` antes de o workflow do GCA chama-lo.

## Proximo passo
1. Publicar o ck3-mod-maker e criar a tag `v1`.
2. Revisar o branch local `pipeline-compartilhado` do GCA, dar push e conferir o run do workflow.

## Teste em jogo
- Save de teste: (definir)
- Ultima conferencia registrada pelo ck3ctl: nenhuma
