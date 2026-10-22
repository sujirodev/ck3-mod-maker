Portuguese version: [re_notas.pt-BR.md](re_notas.pt-BR.md)

# Reverse engineering notes (Phase P)

Target: `C:\CK3\binaries\ck3.exe`, version 1.19.0.6.
- Size: 95206520 bytes
- SHA-256: `42562e4aaebe9e0c9258083c3193c69635a1bad371e7504b8deecad1e660b3db`
- Image base: `0x140000000`. All addresses below are RVAs (add them to the base address of the running module).
- Script that generated the data: `docs/re/re1_rtti.py` (pure Python, only reads the file).

There is also `C:\CK3\binaries\ck3_D_opt.exe` (233 MB, optimized debug build): it may have more assert strings and
function names. Use it as an aid to understand the code; its addresses do NOT apply to `ck3.exe`.

## Sections
| Section | RVA | Size |
|---|---|---|
| .text | 0x1000 | 0x3fd013c |
| .rdata | 0x3fd2000 | 0xf69a24 |
| .data | 0x4f3c000 | 0x924028 |
| .pdata | 0x5861000 | 0x2a7e30 |

## P1 - static findings (2026-10-03)
The executable has RTTI (class names), which makes it possible to find the vtables.

Relevant command classes:
| Class | Main vtable (RVA) | Secondary vtable (offset 24) | 1st virtual function (probably the destructor) |
|---|---|---|---|
| `CMoveUnitCommand` | 0x432bef0 | 0x432bf88 | 0x26b46f0 |
| `CHaltUnitsCommand` | 0x432be58 | 0x432be28 | 0x26b5350 |
| `CToggleCompanionAICommand` | 0x432f6a0 | 0x432f5a8 | 0x801fd0 |
| `CSetCompanionAISettingsCommand` | 0x432f5d8 | 0x432f670 | 0x801fd0 |

Helper classes seen in the RTTI: `CJominiCommandHelper<CMoveUnitCommand>`, `CGameCommandHelper<CMoveUnitCommand>`
(and the same ones for `CHaltUnitsCommand`).

Virtual functions of the main vtable of `CMoveUnitCommand` (indices 0 to 11):
`0x26b46f0, 0x3b8afe0, 0x3418410, 0x3b8b060, 0x3418620, 0x7e6490, 0x26b4a00, 0x803620, 0x26c1e70, 0x23ceb90, 0x7e6490, 0x26c1f20`
- Shared with the other commands (base class): 0x3b8afe0, 0x3418410, 0x3b8b060, 0x3418620, 0x7e6490, 0x803620.
- Specific to `move_unit`: 0x26b46f0, 0x26b4a00, 0x26c1e70, 0x23ceb90, 0x26c1f20 (candidates for destructor,
  serialize, validate and execute).

Anchor strings:
| String | RVA |
|---|---|
| `commanddispatcher.cpp` | 0x44e1ec6 |
| `move_unit` | 0x4299448 |
| `unit_order_type` | 0x42a23c0 |
| `gathering_army` | 0x4299e48, 0x42a340d |

Command format (from `logs\message.log`):
```
move_unit={ default_state={ identity sender receiver attribs }
  object={ valid_for=ai unit=<id> target=<provincia> unit_order_type=set type=any gathering_army=default } }
```

## Strategy
`CHaltUnitsCommand` is reachable from the GUI (`PostCommand( Army.GetOrders.GetHalt )`). It serves as a model:
1. Find who writes vtable 0x432be58 (the Halt constructor) and who calls that constructor (`GetHalt`).
2. Find the `PostCommand` function (receives the ready command and hands it to the dispatcher).
3. Find who writes vtable 0x432bef0 (the `move_unit` constructor) and its arguments (unit, province,
   order type).
4. In the DLL: build the `move_unit` the way the game does and hand it over through the same function from step 2.

## P1 - disassembly result (2026-10-03, with `capstone` 5.0.7)
Helper script: `docs/re/relib.py` (functions `dis`, `xref_rip`, `xref_call`, function bounds from `.pdata`).

### Sending commands
| Item | RVA | Note |
|---|---|---|
| Dispatcher (global object) | 0x5762170 | Same address used by the AI, by the player click and by Halt |
| `Post(despachante, comando*, flags)` | 0x973e00 | Clones the command (virtual +0x40) and calls 0x341d8e0. Returns bool in `al` |
| Internal send | 0x341d8e0 | `(despachante, unique_ptr<comando>*, flags)` |
Observed flags: the AI uses 7; the player click uses 0xE (14).

### Layout of `CMoveUnitCommand` (size 0x168)
| Offset | Type | Field | Value on player click |
|---|---|---|---|
| 0x00 | ptr | main vtable (0x432bef0) | - |
| 0x08 | u8 | state/flags | 0 |
| 0x0C | u64 + u32 (up to 0x18) | `default_state` (identity, sender, receiver) | 0 |
| 0x18 | ptr | secondary vtable (0x432bf88) | - |
| 0x20 | u32 | `valid_for` | 1 (player); AI uses 2; factory default 0 |
| 0x24 | u32 | `unit` (unit id) | id of the selected army |
| 0x28 | u32 | `target` (province id) | province under the cursor |
| 0x2C | u32 | `unit_order_type` | return of 0x26b51d0 (0 or 1; 2 = invalid, not sent) |
| 0x30 | u32 | `type` | 2 |
| 0x34 | u32 | `gathering_army` | function argument; default 1 |
| 0x38 | sub-object (0x128) | internal list, initialized by 0xc7ba80 | empty |
| 0x160 | u64 | - | 0 |

### Where the game builds the `move_unit`
| Function RVA | Who | Note |
|---|---|---|
| 0xa83fb0 (builds at 0xa84722, sends at 0xa84778) | Player click on the map | Loop over the selected units; `valid_for=1`, flags 0xE. Single call coming from 0xa84ce8 |
| 0x186b1b0, 0x18721d0, 0x18726e0, 0x1873120, 0x1874a30, 0x18793d0, 0x18cb7b0, 0x18ce550, 0x18d0dc0 | AI | `valid_for=2`, flags 7 |
| 0x23c9f20 | To be identified | Two build sites |
| 0x26c6fa0 | Factory (allocates 0x168 and fills defaults) | Used when reading a command from the network/file |
| 0x26c1e70 | Clone | - |

Helper functions:
- 0x26b51d0 `(unidade*, provincia*, bool)` -> order type (0/1/2).
- 0x2248880 `(unidade*, tipo)` -> bool, validation done before sending on click.
- 0xc7ba80 `(sub*)` -> constructor of the sub-object at +0x38.

### Recipe for the DLL (to be confirmed in P2)
```
u8 cmd[0x168] = {0};
*(void**)(cmd+0x00) = base + 0x432bef0;
*(void**)(cmd+0x18) = base + 0x432bf88;
*(u32*)(cmd+0x20) = 1;          // valid_for = jogador
*(u32*)(cmd+0x24) = unit_id;
*(u32*)(cmd+0x28) = province_id;
*(u32*)(cmd+0x2C) = 0;          // unit_order_type = set
*(u32*)(cmd+0x30) = 2;
*(u32*)(cmd+0x34) = 1;
((void(*)(void*))(base+0xc7ba80))(cmd+0x38);
((bool(*)(void*,void*,int))(base+0x973e00))(base+0x5762170, cmd, 0xE);
```
Call it on the game's main thread (for example, inside the window's WndProc), not on a thread of its own.

### Loading the DLL without changing the exe
`ck3.exe` imports `VERSION.dll` and `WINMM.dll`. `VERSION.dll` does NOT work: Windows applies a compatibility
shim to every executable named `ck3.exe` (`apphelp.dll`, `AcGenral.DLL`) and it loads the System32 `VERSION.dll`
before the game's imports. `WINMM.dll` is not preloaded: a custom `winmm.dll` in the `binaries` folder, which
forwards the 180 functions to the system one, gets loaded on its own. To remove it, delete the file.

## P2 - in-game confirmation (2026-10-04)
Log: `docs/re/p2_log_2026-10-04.txt`. Confirmed: vtable, field offsets, dispatcher 0x5762170, function 0x973e00.
- Player: `valid_for=1`, flags 0xE, main thread (the same one that loads the DLL), `type=2`, `gathering=1`.
- `unit_order_type`: 0 = replaces the order, 1 = gets queued (Shift).
- AI: `valid_for=2`, flags 7, sent from several worker threads (the send function accepts calls from
  any thread).
- Return 1 on every send.

## P4 - sending from the DLL (2026-10-04)
Log: `docs/re/p4_log_2026-10-04.txt`. 50 sends, game stable.
- The dispatcher validates when processing: a rejected command produces "Invalid command" in `logs\message.log`
  (`commanddispatcher.cpp:78`) and has no effect. Rejected in the tests: province 0, nonexistent province,
  nonexistent unit, unit with another owner, and some `append` orders.
- `Post` returns 1 even for a command that is rejected later.
- Optional command fields (text from `message.log`): `path={ ids de provincia }` and `total_cost=<numero>`.
  They correspond to the sub-object at +0x38 (list) and, probably, to the field at +0x160. The AI sends the
  ready path; the player click sends the empty list and the game computes it.

## C1 - engine settings registry (2026-10-04)
For the "Mods" tab (docs/arquitetura_dll.md, section 11). Static with `docs/re/c1_settings.py`; checked in the
running game with `peek` and the new request `procura` (search for a 64-bit value in memory, on a separate thread).

Classes (RTTI): `CPdxSetting` (base), `CPdxBoolSetting`, `CPdxEnumSetting`, `CPdxStringSetting`,
`CPdxCallbackSetting`, `CPdxValueSetting<int>` (`?$CPdxValueSetting@H`), `CPdxSettingsFile`,
`CJominiSettingsWindow`. Engine source cited in the strings: `clausewitz\pdx_settings\source\pdx_settings_manager.cpp`.

Manager (`CPdxSettingsManager`):
| Item | Address / field |
|---|---|
| Global pointer | `exe+0x5710d58` |
| Creation (allocates 0x80, writes the global, registers the pending list) | `0x3b44b40` (called through a table, refs at `0x354d758` and `0x3b39a77`) |
| Register the pending list of static settings | `0x3b45050` |
| `Registrar(gerenciador, &grupo, opcao, n)` | `0x3b44cf0`; `grupo` = `{const char *p; int32 len; uint8 0}`; creates the group if it does not exist; `opcao+0xb8` = `n`; notifies the listener `+0x30` (vfunc +8) |
| Find group by name | `0x3b45360` |
| Groups | `+0x18` vector of pointers, `+0x20` capacity, `+0x24` count (10 in the running game) |
| Lock (`lockdown`) | byte `+0x79`; set at `0x3554bb0`, after `LoadSettings` and `compound_settings.txt`. Registering afterwards: log `Setting '%s/%s' registered after setting lockdown, may get lost` (the setting does not receive the saved value) |

Group (0x38-byte node created at `0x3b44cf0`; in the game the nodes show up with a header and an index): name in a
`std::string` (SSO), list of `CPdxPtr<CPdxSetting>` at `+0x20` (allocator `exe+0x5003e10`). Groups seen:
`Tools`, `GUI`, `Audio`, `Account`, `game`, `interface`, `System`, `Graphics`, `accessibility`, `notifications`
(the sections of `pdx_settings.txt`).

Setting (`CPdxSetting`, common fields):
| Field | Content |
|---|---|
| `+0x00`, `+0x08` | vtables |
| `+0x10` | reference count |
| `+0x18..+0x30` | list of change callbacks (allocator `SPdxCallback<const CPdxSetting*>`) |
| `+0x78` | name (`std::string`, e.g. `debug_saves`) |
| `+0x98` | UI text key (`std::string`, e.g. `SETTING_DEBUG_SAVES`) |
| `+0xb8` | `n` from the registration |
| `+0xc0..+0xc4` | flags (0x101, 0, int from the constructor's 5th argument) |

Constructors:
| Type | Size | Constructor | Arguments |
|---|---|---|---|
| on/off | `new(0xF8)` | `0x3b46730` | `(obj, &nome, &chave_texto, padrao (byte), int 0)` |
| text | `new(0x100)` | `0x3b4c290` | `(obj, &nome, &chave_texto, &?, int)` |
| button with action | ? | `0x3b4bf70` | to be checked |
| list | ? | `0x3b4b480` / `0x3b4b520` | to be checked |
| integer slider `CPdxValueSetting<int>` | `new(0x128)` | built directly in the code (e.g. `debug_saves` at `0x403ef0`: step `+0xf8`, minimum `+0xfc`, maximum `+0x100`, value text at `+0x108`); value through `0xa89250(obj, &valor, 0)` | |

The name and key strings use the same string view as the registration (`{ptr, int32 len, byte}`).

Consequence for C2: the loader needs to register the mods' settings right after `0x3b44b40` (manager
creation) and before `LoadSettings`, with a hook in that function. This changes `winmm.dll` (reopen the game).

Careful with `procura`: the first version read all of the process memory on the main thread (21 GB, including
video memory) and held the game for about 60 s. Now it reads only its own memory (`MEM_PRIVATE`/`MEM_IMAGE`, without
`WRITECOMBINE`/`NOCACHE`) on a separate thread (~80 s per search, the game responds normally).

## C2b - settings window built by the DLL (2026-10-04/05)
| Item | Address / field |
|---|---|
| Building the tabs (`CJominiSettingsWindow`, main vtable [1]) | `0x32d7700(janela)`: clears `+0xd8` (`0x7e9ea0`), reads `settings_layout.txt`, calls the reader, wires up the sections. Prologue: `mov [rsp+0x10],rbx; mov [rsp+0x18],rsi` (10 bytes, hook in the loader) |
| Tabs | `janela+0xd8` pointer, `+0xe0` capacity, `+0xe4` count; selected page `+0x108` |
| Layout reader | sub-object `janela+0xc8` (vtable `0x44c33a0`), vtable[3] `0x3b8b060(leitor_janela, &leitor)`; each `page` block (token `0x2d1`) creates a 0x58-byte page (`0x32d6cc0`) and APPENDS it to the vector |
| Page | translated name (`std::string`) at `+0x10`, sections at `+0x30` (pointer), `+0x38` capacity, `+0x3c` count |
| Wire up section | `0x32d5230(secao)` |
| Text from memory | `CMemoryFile` (vtable `0x4559758`, 0x58 bytes: `+0x08`=1, `+0x10`=1, empty `std::string` name at `+0x18`, `+0x38` = buffer `{dados, ?, alocador char 0x4fea420}` filled by `0x3bb5200(buf, texto, tamanho)`, `+0x50`=-1); `CLexer` through `0x3b58cf0(lexer, &arquivo, 0)` and vtable swapped to `CTextLexer` (`0x4556c20`); reader `0x3b8e670(leitor, &lexer)`; destructors `0x3b8e850` (reader) and `0x3b58dc0` (lexer) |
| Change the value of an on/off setting | `0x3b46990(opcao, valor, 1)`, called by the checkbox click (`0x32d6610`, bound to the `setting_bool_cb` element at `0x32d6660`) |
| Value of an on/off setting | three `SCopyableAnyTypeInPlace<bool>` fields at `+0xc8`, `+0xd8`, `+0xe8` (type, then the value byte at +8) |

The window has no global pointer (dozens of references on the heap, from the UI elements); the loader stores it
in the build hook. The game rebuilds the window on its own (e.g. localization reloaded in `-debug_mode`).

## C4 - value sliders (2026-10-05)
`CPdxValueSetting<int>` (vtables `0x4238228`/`0x4238218`) and `<float>` (`0x428cb28`/`0x428cb18`), 0x128 bytes, built
directly in the code by the game (examples: `debug_saves` at `0x403ef0`, `tooltip_delay` at `0x4045e0`).
| Field | Content |
|---|---|
| `+0x28` | allocator of the change callbacks `0x4fea408` |
| `+0x78`, `+0x98` | name and text key (`std::string`, `0x7e9530` = assign) |
| `+0xbc` | type number: int `0x7ea910()`, float `0x7ea8a0()` (on/off: `0x32dbfb0()`) |
| `+0xc0` | `0x101` (word) |
| `+0xc8`, `+0xd8`, `+0xe8` | value fields, empty type `0x9ff600()` before the first value |
| `+0xf8`, `+0xfc`, `+0x100` | step, minimum, maximum (int32 or float) |
| `+0x108` | text key for the value format (`SETTING_VALUE_NONE` = `$VALUE$`, `SETTING_VALUE_PERCENTAGE`...) |
Value: `0xa89250(opcao, &int, 0)` and `0xa89430(opcao, &float, 0)` (clamp between minimum and maximum). In
`pdx_settings.txt`: `value=3` and `value=0.100000`. List: a derived class per setting, built by `0x2049010(obj 0x188,
&nome, &chave, 0)`, items through `0x809dc0` from fixed tables, value through `0x3b4b690(obj, indice, 0)`.

## C5 and C6 - Mods tab buttons and reading the values (2026-10-05)
| Item | Address / field |
|---|---|
| Button (`CPdxCallbackSetting`, vtable `0x4555d48`) | `new(0x138)`; `0x3b4bf70(obj, &nome, &chave, &funcao, 0)`; the function is an MSVC `std::function<void()>` (0x40 bytes, implementation in its own buffer, pointer to it at `+0x38`), moved to `obj+0xf8`. Type used by the game: `_Func_impl_no_alloc<void(*)()>` (vtable `0x408a450`, function pointer at `+8`), e.g. `reset_reactive_advice` at `0x4051a0` |
| Select tab | `SettingsPage.OnClick` = `0x32d9cf0(pagina)`: window = `pagina+0x08`; unselects `[janela+0xd0]+0x50`, writes `janela+0xd0` = page, selects `pagina+0x50`, redraws with `0x32d8940(janela)` |
| Selected tab | `janela+0xd0`; `SettingsPage.IsSelected` = byte `pagina+0x50` (`0xe37df0`) |
| Show setting | `JominiGUISetting.ShouldShow` = byte `+0x48` of the setting's UI object (`0xd85550`), set to 1 on creation (`0x32d5230`, ctor `0x34a7260`) |
| The game's `ret` | `0x7e6490` (`c2 00 00`) |
| Setting value (read) | data of the first value field at `+0xd0` (bool: byte; int: int32; float: float); type by the vtable (bool `0x4555700`, int `0x4238228`, float `0x428cb28`) |

## A3 - values that currently come from script triggers (2026-10-05, in progress)
Game triggers found through RTTI: `CSupplyArmyTrigger` (army_supply), `CArmySizeTrigger` (army_size),
`CRaidLootTrigger`, `CIsAtWarTrigger`, `CIsIndependentRulerTrigger`, `CMaxMilitaryStrengthTrigger`, `CTopLiegeLink`.
They are the same generic template (same vtable, the value comes from a sub-object at +0x58); the registration by
name only creates a factory entry (e.g. army_supply at `0x53d120`). Faster to find the field through memory, comparing
with the value the script gives (test effect `ping_livre` with `army_size` etc.) and with the request `peek army:<id>` (new).

| Value | Where (army object, 0x208 bytes) | How it was checked |
|---|---|---|
| size (`army_size`) | int32 at `+0x24` (also `+0x1fc`) | 6205 -> 6783 -> 6877 during reinforcement, same as the script |
| supply (`army_supply`) | fixed point at `+0x180` | 100 = 10000000; the `refill_supply` effect (`CRefillSupplyEffect`, vtable[24] `0x2e80d30`) writes the maximum at `+0x180` |
| carried loot, raid mode | `+0x1e0`, byte `+0x1d4` | already used by the core |

Not found yet: men-at-arms regiments (current, maximum, hired; the lists at `+0x38` and `+0xa0` are not
regiments: the first has pairs of small numbers, maybe ids in a table; the second is text), the player's
war, owner/liege/independence/strength (for `ar_em_casa` and the target refusal) and `can_raid_trigger`.
Note: in `ar_deve_voltar`, `any_army_maa_regiment = { percent >= 0.5 ... }` means "at least half of the
regiments" below `ar_cfg_tropas_volta` of the maximum.

### A3 solved through the game's own path: script expressions evaluated from C (2026-10-05)
Instead of finding each field, the C code asks the game to read and evaluate a script expression, with the same
code the script uses. Proof request `expr <texto>` in the core; evaluated on each `auto_raid_ping_livre` on an army.
| Step | Address |
|---|---|
| Object that holds the expression | an `add_loot` effect through the game's factory `0x2e81070(entrada)` (0x168 bytes; name number at `entrada+0x10`); the script value sits at `+0x60` (constructor `0x1928a80`) |
| Text | `valor = <expressao>` in a `CMemoryFile` + `CLexer`/`CTextLexer` + reader `0x3b8e670`, as in the layout (C2b) |
| Advance to the value | `0x3b90120(leitor)`: reads `chave =` and stops at the value (the reader loop `0x3b8b090` calls it before handing over each value) |
| Read | effect method 1: `(efeito, leitor, tipo de escopo)` (forwards to `efeito+0x60`, vtable[3]); army type `0x1b`. Without `0x3b90120` first: `Cannot read [] as a script value` |
| Evaluate | `0x9698b0(efeito+0x60, &saida, contexto, contexto[5], efeito+0x10)` with the context of an effect running in the scope (the same one the loader uses in the `auto_raid_*`) |
Checked on army 1996488707: `army_supply` 100 = 100, `army_size` 6877 = 6877,
`{ if = { limit = { ar_em_casa = yes } add = 1 } }` 1 = 1. A single mechanism covers all the triggers and values of the
decision maker (men-at-arms with `every_army_maa_regiment`, war with `root = { is_at_war = yes }`, owner strength with
`var:ar_alvo_prov.county.holder.top_liege.max_military_strength`...). Limit: it needs the context of an effect
running in the right scope (the decision maker in the game runs inside an effect called on each army).

### Evaluating in another scope and handling variables without script (SIM2b, 2026-10-05)
| Item | Address / field |
|---|---|
| Effect context | `ctx[0]` = current scope (`{u16 tipo, u32 id em +8}`), `ctx[2]` = root (in the player's effect, the same scope), `ctx[5]` = 4th argument of the evaluator |
| Evaluate in another scope | copy 16 qwords of the context and replace only `ctx[0]` with a scope of our own; root stays the effect's. Checked: from `auto_raid_ping_livre` on the character, `army_size` = 6877, `army_supply` = 100 and `ar_em_casa` = 1 on the army, same as memory |
| Scope type when reading | the expression is read with the type of the scope where it will be evaluated (army 0x1b, character 0x4, province 0x8) |
| `remove_variable` | `CJominiRemoveVariableEffect<0>` (vtable `0x44d3148`), function 22 at `0x3393c00`: `0x3345af0(variaveis, numero do nome)` and `0x33465f0(variaveis)`; `variaveis` = `0x3329990(escopo*)` (without the +8 used for writing) |
| `set_variable` with a duration | `CJominiSetVariableEffect<0>` (vtable `0x44d1ed8`), function 22 at `0x3393480`: the duration comes from `0x3372f50(objeto+0x208, ctx, ctx[5])` and goes as the 4th argument of `0x3346b30` in days. Checked: `days = 3` from the script and `dias = 3` from the DLL write the same field `+0xc` (17) in the entry |
Test requests in the core: `ctxprova <tipo> <id> <texto>` and `var <tipo> <id> <nome> [set <valor> [dias] | del]`.

## Pending
- Fill `path`/`total_cost` in the command to force the route (find the list layout and who computes it).
- Detect in the DLL when the command is rejected (today it only shows up in `message.log`).
- Understand why some `append` orders are rejected.
- Script -> DLL bridge: how the script finds out the unit id and passes the request.
