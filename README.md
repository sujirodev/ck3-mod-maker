# ck3-mod-maker

> **Projeto de estudo.** Feito para aprender modding de Crusader Kings III, engenharia reversa e desenvolvimento
> com agentes de IA (Claude Code). Nao e afiliado nem endossado pela Paradox Interactive. A DLL so roda numa versao
> exata do jogo (1.19.0.6), le e altera a memoria do processo e serve para uso local e single-player, em
> `-debug_mode`. Use por sua conta e risco; nada aqui e um produto suportado.
>
> **Study project.** Built to learn Crusader Kings III modding, reverse engineering and AI-agent-driven development
> (Claude Code). Not affiliated with or endorsed by Paradox Interactive. The DLL only works on one exact game build
> (1.19.0.6), reads and patches the game process memory, and is meant for local single-player use in `-debug_mode`.
> Use at your own risk; nothing here is a supported product.

Repositorio para criar mods de CK3 com o Claude Code de forma agentica: o Claude escreve o mod, abre o jogo, roda
efeitos, le valores e logs e corrige sozinho, pela DLL `ck3_custom`.

Os mods sao feitos em script do jogo. A DLL e ferramenta de desenvolvimento (teste e depuracao); um mod que depende
dela (plugin C ou aba Mods) e a ultima opcao, so quando o script nao alcanca, como no Auto Raid.

## Pecas
- **DLL `ck3_custom`** (`dll/`): carregador `winmm.dll` + nucleo `ck3_custom_core.dll`. Canal de pedidos com o jogo
  aberto, efeitos de script novos em C (plugins compilados com TinyCC dentro do jogo), aba Mods nas configuracoes.
  Notas de engenharia reversa em `docs/dll/`. Binarios nao sao publicados: compile a partir da fonte.
- **`ck3ctl`** (`tools/ck3ctl/`): biblioteca, CLI e servidor MCP `ck3`.
- **Pipeline de release compartilhado** (`pipeline/`, `.github/workflows/mod-release.yml`): cada mod tem um
  `release.yml` de poucas linhas que chama este workflow numa tag fixa; zip reproduzivel, notas do `CHANGELOG.md`,
  GitHub Release e Nexus Mods opcional.
- **Modelo de repo de mod** (`templates/repo`): um mod = um repo git, no padrao do
  [Guardian Calculation Assistant](https://github.com/sujirodev/ck3-guardian-calc-assistant).
- **Harness** (`.claude/`, `mods/<nome>/`): skills (`novo-mod`, `testar-em-jogo`, `debugar`, `novo-plugin-c`,
  `publicar`), subagentes, hook de validacao e estado por mod (funcionalidades com evidencia, progresso, decisoes).

## Usar o pipeline num mod
`.github/workflows/release.yml` do mod:
```yaml
on: { push: { branches: [main], tags: ["v*"] }, pull_request: {}, workflow_dispatch: {} }
permissions: { contents: write }
jobs:
  release:
    uses: sujirodev/ck3-mod-maker/.github/workflows/mod-release.yml@v1
    with:
      pipeline_ref: v1
```
e `ck3mod.toml` na raiz:
```toml
[mod]
dir = "my_mod"
title = "My Mod"
```
Detalhes (Nexus Mods, `include`) no cabecalho de `.github/workflows/mod-release.yml` e em `templates/repo/README.md`.

## Preparar a maquina
```
uv sync
uv run ck3ctl dll build          # GCC (WinLibs) e TinyCC em ~/src/tinycc (dll/LEIAME.md)
uv run ck3ctl dll install todas  # com o jogo fechado
uv run ck3ctl ref build          # indice do script_docs (rodar `script_docs` no console uma vez antes)
uv run ck3ctl doctor
```
Caminhos em `tools/ck3ctl/config.py` (variaveis `CK3_GAME`, `CK3_DOCS`, `CK3_TINYCC`, `CK3_AUTHOR`).

## Uso rapido (CLI)
```
uv run ck3ctl mod new meu_mod "My Mod" --prefixo mm --resumo "does something"
uv run ck3ctl mod check meu_mod
uv run ck3ctl mod enable meu_mod
uv run ck3ctl game start teste_meu_mod
uv run ck3ctl effect "trigger_event = mm.0001"
uv run ck3ctl eval gold
uv run ck3ctl errors --mod meu_mod
uv run ck3ctl mod package meu_mod
```
No Claude Code as mesmas operacoes aparecem como ferramentas `mcp__ck3__*`.

## Licenca
[MIT](LICENSE) para o codigo e os textos deste repositorio. O nucleo da DLL embute o TinyCC (LGPL) ao ser compilado;
o TinyCC nao e distribuido aqui. Crusader Kings III e marca da Paradox Interactive AB; nenhum arquivo do jogo e
distribuido aqui.
