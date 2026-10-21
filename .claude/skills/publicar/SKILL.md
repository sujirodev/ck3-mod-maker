---
name: publicar
description: Publicar versao de mod de CK3 - confere features, CHANGELOG, versao, zip pelo pipeline compartilhado, tag e release. Usar ao publicar.
---

# Publicar

O release roda no GitHub pelo pipeline compartilhado (`.github/workflows/mod-release.yml` do ck3-mod-maker), chamado
pelo `release.yml` do mod. A tag `vX.Y.Z` publica o GitHub Release e, se configurado, a versao no Nexus Mods.

1. `ck3_mod(acao="check")` limpo e todas as features de `dev/features.json` em `done` com
   evidencia da versao final (skill `testar-em-jogo`). Alguma pendente ou bloqueada: mostrar ao usuario antes.
2. `descriptor.mod`: subir `version`; conferir `supported_version`.
3. `CHANGELOG.md`: secao `## [x.y.z] - AAAA-MM-DD`, uma linha `- ` por mudanca, em ingles. Sem ela a tag falha.
4. Conferir o zip localmente: `ck3_mod(acao="package", nome)` e listar o conteudo. So conteudo do jogo (o
   `include` do `ck3mod.toml`); nada de `docs/`, `tools/`, arquivos `teste`/`debug`.
5. Mod com plugin C ou aba Mods: a pagina precisa dizer que exige a DLL `ck3_custom` e a versao exata do jogo, e o
   mod deve falhar de forma visivel e segura sem a DLL (ex.: aviso `ar_aviso_sem_dll` do Auto Raid).
6. Texto da pagina: `README.md`, `docs/nexus_summary.txt` (ate 350 letras) e `docs/nexus_description.bbcode`.
7. Mostrar ao usuario versao, changelog e lista do zip. Commit, `git tag vX.Y.Z` e `git push origin main vX.Y.Z` so
   depois do ok dele (acao externa). Depois, conferir o run do workflow (`gh run list` / `gh run view`).
