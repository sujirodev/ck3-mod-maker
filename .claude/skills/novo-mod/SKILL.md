---
name: novo-mod
description: Criar mod novo de CK3 a partir de uma ideia - repo git autossuficiente, script, features, localizacao, release pronto. Usar para "fazer um mod que...".
---

# Novo mod

1. Entender a ideia em uma frase: o que muda no jogo, para quem (jogador/IA), quando acontece (evento, decisao,
   on_action, interacao, GUI). Perguntar so o que mudar o desenho.
2. Nomes: `nome` = pasta e zip (minusculas com `_`); `prefixo` curto (2 a 16 letras, ex.: `tnt_ga`) em tudo que o
   mod cria (efeitos, eventos, variaveis, chaves); titulo legivel e resumo de uma linha em ingles.
3. Pesquisar antes de desenhar: `mcp__ck3__ck3_vanilla` para achar mecanica parecida no jogo base (decisoes em
   `common/decisions`, eventos em `events`, interacoes em `common/character_interactions`) e `ck3_ref` para cada
   efeito/trigger/on_action que pretende usar.
4. Criar: `mcp__ck3__ck3_mod(acao="new", nome, titulo, prefixo, resumo)`. Gera o repo git do mod em
   `Documents/.../mod/<nome>` (padrao guardian_calc_assistant: release pelo pipeline compartilhado, estado em `dev/`,
   `CLAUDE.md` com as regras) e o `.mod` do launcher. So em script: sem `plugin` e sem `config_mods`, que fazem o mod
   publicado exigir a DLL (CLAUDE.md, "Regra principal").
   - Opcoes do jogador: game rule (`common/game_rules`), nao a aba Mods.
   - Plugin C so depois de esgotar o script e com aprovacao do usuario; ai a skill `novo-plugin-c`.
5. Preencher `dev/features.json` (em ingles) com as features da ideia (cada uma com `test` = como conferir e
   `depends_on`), trocando o exemplo f02. Mostrar a lista ao usuario antes de implementar: e o escopo.
6. Escrever o mod, uma funcionalidade por vez (delegar a `ck3-script`, `ck3-localizacao`, `ck3-gui` quando for
   grande). Localizacao em `english` e `braz_por`, mesmas chaves.
7. `ck3_mod(acao="check")` ate sair limpo.
8. Ativar: `ck3_mod(acao="enable")` (vale na proxima abertura do jogo).
9. Testar com a skill `testar-em-jogo` (ela registra a evidencia de cada funcionalidade).
10. No repo do mod: `README.md` (features, compatibilidade) e `CHANGELOG.md` em ingles; commits pequenos com
    mensagem em ingles. Estado em `dev/progress.md` e `dev/decisions.md`; regras novas do mod no `CLAUDE.md` dele. Criar o repo no GitHub e dar push so com o
    pedido do usuario.
