# ck3-mod-maker

Repositorio agentico para criar mods de Crusader Kings III. O Claude escreve o mod, abre o jogo, testa e depura
sozinho pela DLL `ck3_custom`, que da acesso ao jogo aberto (efeitos, valores, eventos, saves, logs).

Repositorio publico, feito para estudo: nao escrever aqui segredo, chave de API, dado pessoal nem conteudo do jogo
(o `reference/` e os binarios ficam fora do git por isso).

Responder sempre em portugues do Brasil.

## Regra principal: mod em script; plugin C e a ultima opcao
A DLL tem dois papeis, e so um deles e livre:
- **Ferramenta de desenvolvimento (sempre):** o Claude usa a DLL para testar e depurar qualquer mod (efeitos,
  valores, eventos, saves, logs). O jogador nunca precisa dela por causa disso.
- **Dependencia do mod (ultima opcao):** plugin C (`plugin/plugin.txt` + `.c`) ou aba Mods (`plugin/config.txt`)
  fazem o mod publicado exigir a DLL, que so funciona numa versao exata do jogo (1.19.0.6), quebra a cada patch e
  executa codigo nativo na maquina do jogador. O Auto Raid so usa plugin porque nao havia outro caminho (ordem de
  marcha real, pathfinder e previsao de combate nao existem no script).

Antes de propor plugin C ou aba Mods:
1. Esgotar o script do jogo: efeitos, triggers, script_values, scripted_guis, game rules (`common/game_rules`) para
   opcoes do jogador, decisoes, eventos, on_actions. Procurar no jogo base (`ck3_vanilla`) como a Paradox resolve
   algo parecido.
2. Se ainda nao der, mostrar ao usuario o que o script nao alcanca (com a evidencia: efeito/trigger que falta) e o
   custo da dependencia, e so seguir com o plugin depois que ele aprovar.
3. Opcoes do jogador vao em game rule, nao na aba Mods, salvo pedido explicito.

## Mapa
| Pasta | O que e |
|---|---|
| `dll/` | Fonte da DLL `ck3_custom` (carregador `winmm.dll` + nucleo `ck3_custom_core.dll` + `ck3_api.h` dos plugins). Pedidos e efeitos em `dll/LEIAME.md`; arquitetura em `docs/dll/arquitetura_dll.md`; enderecos e engenharia reversa em `docs/dll/re*` |
| `tools/ck3ctl/` | Biblioteca Python, CLI (`uv run ck3ctl ...`) e servidor MCP `ck3` (ferramentas `mcp__ck3__*`) |
| `pipeline/` + `.github/workflows/mod-release.yml` | Pipeline de release compartilhado (zip, changelog, GitHub Release, Nexus), chamado pelo `release.yml` de cada mod numa tag fixa (`v1`) |
| `templates/` | `repo` (repo de mod, padrao do guardian_calc_assistant, com `dev/` e `CLAUDE.md`), `plugin` e `config` (ultima opcao) |
| `reference/` | Gerado (fora do git): `script_docs` do jogo indexado (`ck3ctl ref build`) |
| `.claude/skills/`, `.claude/agents/` | Fluxos (novo-mod, testar-em-jogo, debugar, novo-plugin-c, publicar) e especialistas |

## Mods sao repositorios separados
Cada mod e um repo git proprio, por padrao direto em `Documents\Paradox Interactive\Crusader Kings III\mod\<nome>`
(o jogo le dali), no padrao do guardian_calc_assistant: conteudo do jogo na raiz; `README.md`, `CHANGELOG.md`,
`CONTRIBUTING.md`, `LICENSE` e `docs/` em ingles; `ck3mod.toml` (o que entra no zip); `.github/workflows/release.yml`
de poucas linhas chamando o pipeline compartilhado. O repo e autossuficiente: carrega o proprio estado em `dev/`
(`features.json`, `progress.md`, `decisions.md`, em ingles) e um `CLAUDE.md` com as regras, para qualquer pessoa ou
agente contribuir so com o repo do mod. O ck3-mod-maker nao guarda registro: acha os mods em `mod/` pelo `ck3mod.toml`.
- Criar: `ck3ctl mod new <nome> "<Titulo>" --prefixo <curto> --resumo "<one line>"`. Prefixo curto em tudo que o
  mod cria (ex.: `tnt_ga`). Mod que ja existe: `ck3ctl mod init <nome> --prefixo <p>` (acrescenta
  `ck3mod.toml`, `CLAUDE.md` e `dev/` sem sobrescrever nada).
- Push, tag e release do repo de um mod sao acoes externas: so com o pedido do usuario.
- Mudanca no `pipeline/` que muda comportamento: nova tag (`v2`) e os mods migram um a um trocando o ref no
  `release.yml`; nunca mover uma tag ja usada.

Caminhos da maquina em `tools/ck3ctl/config.py`: jogo em `C:\CK3` (so a versao 1.19.0.6, a unica que a DLL
conhece), dados em `Documents\Paradox Interactive\Crusader Kings III`, TinyCC em `~\src\tinycc`.

## Como trabalhar
1. Duvida de script (efeito, trigger, escopo, on_action): `mcp__ck3__ck3_ref` e `ck3_vanilla` antes de escrever.
   Nunca inventar efeito ou trigger; o jogo base em `C:\CK3\game` e a fonte da verdade.
2. Editou arquivo de mod: o hook confere chaves, BOM e localizacao; corrigir o que ele apontar.
3. Antes de abrir o jogo: `ck3ctl mod check <nome>`.
4. No jogo: `ck3_mark` -> acao -> `ck3_errors(mod=...)` e `ck3_dll_log`. Valor e condicao: `ck3_eval` / `ck3_trigger`.
5. Escolha de caminho: ver "Regra principal" acima. Plugin C so com aprovacao do usuario.

## Sessao (estado fica em arquivo, nao na conversa)
Cada repo de mod tem `dev/features.json` (features com status, como testar e evidencia) e `dev/progress.md` (estado
atual e proximo passo, reescrito, nunca um historico crescente). O hook valida o `features.json`. Sao do mod:
escrever em ingles, como o resto do repo.
- Inicio: `ck3_doctor`; para trabalhar num mod, `ck3_mod(acao="status", nome)`.
- Durante: uma feature `in_progress` por vez. `done` so com evidencia do teste rodado (data, comando, resultado);
  "parece funcionar" nao conta. Sem como testar ainda: fica `in_progress` ou `blocked` com o motivo.
- Fim (ou antes de parar por bloqueio): atualizar `dev/features.json`, reescrever `dev/progress.md`, decisoes
  duradouras em `dev/decisions.md`.

## Regras do jogo e da DLL (aprendidas no Auto Raid; quebrar custa uma queda do jogo)
- Efeito ou console so com a partida carregada e pausada: durante a carga (`state` com velocidade 0) um efeito
  derruba o jogo (C0000005 em `exe+0xe3ea3e`). `ck3ctl` ja confere antes do `effect`.
- Pedido ate ~500 letras (o carregador corta em 511). Efeito maior: criar um `scripted_effect` no mod e chama-lo.
- Trocar de save sem reabrir: `ck3_save_load(load, nome)` (~15 s). Reabrir o processo (~3 min) so quando
  obrigatorio: carregador novo, efeito/variavel nova no `plugin.txt`, mudanca no `dlc_load.json`. Juntar essas
  necessidades numa reabertura so.
- Com `-debug_mode` o jogo recarrega script, localizacao e plugin C a quente. Nao editar o mod com o jogo correndo
  numa rodada de teste (ja derrubou o jogo): pausar, editar, recarregar o save.
- `reload gui` pelo console le `.gui` novo sem reabrir.
- Cliques sinteticos nao chegam ao jogo (o Windows nao traz o jogo para a frente). Teste de interface: pedir o clique
  ao usuario e conferir pela DLL/logs.
- Rodadas longas no jogo e baterias: `run_in_background`, nunca em primeiro plano.
- Telas que pausam o jogo (eventos, sucessao): `ck3_events(responder=True)`; anotar o que foi respondido.
- Sem graficos (`-nographics`): `console event <id>` nao funciona; usar `effect trigger_event = <id>` ou o
  conteudo do evento. Padrao: com graficos.
- Saves de teste: copiar antes de alterar; nunca mexer nos saves de jogo do usuario sem pedir.
- Depois de "FALHA CONTIDA" no log da DLL o estado do jogo pode estar inconsistente: nao salvar por cima.

## DLL
- `uv run ck3ctl dll build [nucleo|carregador]` e `dll install`. O nucleo troca com o jogo aberto em `-debug_mode`;
  o carregador so com o jogo fechado.
- Mudou a `ck3_api` (nova funcao): subir `CK3_API_VERSION`, documentar em `dll/ck3_api.h` e `dll/LEIAME.md`, e
  atualizar `CK3_API` em `tools/ck3ctl/validate.py`. Plugin de api diferente nao carrega.
- Nenhum nome de mod (`auto_raid_`, `ar_` etc.) dentro do nucleo: o que e de um mod vai no plugin do mod.
- O Auto Raid (`Documents\...\mod\auto_raid`) e o primeiro mod que usa a DLL; mudanca na DLL nao pode quebra-lo.
