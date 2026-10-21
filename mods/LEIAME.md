# Estado dos mods

Cada mod e um repositorio git proprio (por padrao em `Documents/Paradox Interactive/Crusader Kings III/mod/<nome>`,
criado com `uv run ck3ctl mod new` a partir de `templates/repo`). Aqui fica so o estado do harness de cada um, fora
do repo do mod:

| Arquivo | O que e |
|---|---|
| `mod.json` | onde esta o repo do mod, prefixo, GitHub |
| `funcionalidades.json` | funcionalidades com status, comando de teste e evidencia (validado pelo hook) |
| `progresso.md` | estado atual e proximo passo, reescrito a cada sessao |
| `decisoes.md` | decisoes duradouras com data e motivo |

Mod que ja existe: `uv run ck3ctl mod adopt <nome>`.
