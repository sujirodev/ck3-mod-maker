Portuguese version: [notas_efeitos.pt-BR.md](notas_efeitos.pt-BR.md)

# Phase B - custom script effects (ck3.exe 1.19.0.6)

Static analysis from 2026-10-04 (script: docs/re/b1.py). Addresses are RVAs. NOTHING here was tested in game.
Model studied: `add_loot` (army-scope effect with one value).

## B1 - registering an effect
Each effect has a static initializer (the one for `add_loot` is 0x5c4300) that does:

| Step | Code | Detail |
|---|---|---|
| 1 | name registry: global pointer at 0x5772a38 (if null, 0x3b587c0 creates and returns it) | |
| 2 | `0x3b58280(registro, &texto)` -> name number (int) | text = { pointer, u32 length at +8, byte 0 at +0xc } |
| 3 | effect database (`CJominiEffectDatabase`): global pointer at 0x570f6f0 (if null, 0x337e050 creates it; reread the global) | |
| 4 | 0x18-byte entry (the game's operator new: 0x3e25d74) | +0 entry function table, +8 name pointer, +0x10 name number |
| 5 | `0x337e6b0(banco, numero, entrada)` registers | lock at banco+0x50, map number -> entry at banco+0x28, list at banco+0x58. If the number already exists, deletes the old entry and replaces it |

Entry function table (`CEffectEntry<T>`, the add_loot one is at 0x443cf30), 2 functions:
- [0] 0x995f10 `(entrada, flags)`: destructor; with flags&1 frees 0x18 bytes (0x3e26124).
- [1] factory `(entrada)` -> effect object. The add_loot one is 0x2e81070: new of 0x168 bytes, base constructor
  0x337ebf0, "script value" member constructed at objeto+0x60 (0x1928a80), function table 0x443cad8,
  `[objeto+8]` = name number (copied from `[entrada+0x10]`), `[objeto+0xc]` = 0.

The static initializers sit in a pointer table in .rdata (0x3fd3bc0 to 0x4013978); the add_loot one is
the entry at 0x40024f8. They run during executable initialization, AFTER our winmm.dll loads and before
any script is read.

## B2 - anatomy of the effect object
Function table of `CAddLootEffect` (0x443cad8), 24 functions. The ones that change from one effect to another:

| Index | add_loot | What it does |
|---|---|---|
| 0 | 0x2da4550 | destructor |
| 1 | 0x995de0 | reads the script argument: forwards to the script value at objeto+0x60 |
| 5 | 0x193c200 | accepted scope type: returns 0x1b (27 = army). An effect without a fixed scope returns 0 |
| 16 | 0x2eaea60 | effect name from `[objeto+8]` |
| 22 | 0x2e80510 | EXECUTES `(objeto, contexto)` |
| 23 | 0x2e806b0 | builds the description (tooltip) `(objeto, contexto, saida)`, returns 1 |
The other 18 are common (the same as those of `CDebugLogDateEffect`, 0x44786f0).

How execution receives the scope (function 22):
- `rdx` = context. `[contexto]` = pointer to the current scope: u16 type at +0, u32 id at +8.
- Type 0x1b = army; the id is the ARMY ID. The code finds the object in the army table (0x570c6b0, the same one
  from the unit notes): index = id & 0xFFFFFF, 16-byte entry at `[tabela+0x20]`, pointer at +8, checks
  `[exercito+0x10] == id`. Default null army at 0x570c6a0.
- The argument value: `0x9698b0(objeto+0x60, &saida, contexto, [contexto+0x28], objeto+0x10)`; output in fixed
  point (100000 = 1), like the loot.
- After that, add_loot changes `[exercito+0x1e0]` (carried loot), respecting the capacity (0x2272a80).

Consequences:
- Army identity resolved: the effect receives the army id; `[exercito+0x124]` gives the unit id.
- The argument can be a number, a variable or a script value (`auto_raid_x = 7336`, `= var:ar_alvo`), because the
  member at +0x60 is the game's value evaluator.

## Proposed recipe for our effect (to be proven in B3/B4)
1. Our entry (0x18 bytes, our memory): table of 2 functions of ours; destructor that frees nothing.
2. Our factory: calls the original add_loot factory (0x2e81070) passing our entry (it only reads
   `[entrada+0x10]`), and replaces the object's function table with a COPY of 0x443cad8 with function 22 (execute)
   and 23 (description) pointing to our code. The object stays a real add_loot for the game
   (original constructor, destructor, argument reading and army scope).
3. Registration: name number through 0x3b58280 and `0x337e6b0(banco, numero, entrada)`.
4. Timing (B3): replace a pointer in the initializer table (e.g. 0x40024f8) with a function of ours that calls
   the original and then registers the `auto_raid_*` effects. It is a pointer swap in memory, without changing code.

Open:
- B3: confirm in game that registering at that moment works and that `script_docs` lists the effect.
- B5: write a variable from the DLL (classes `CJominiSetVariableEffect`, `CJominiSavedVariables`, `CSetVariableCommand`).
- B7: which thread function 22 runs on.
- Effects in another scope (character) or with a block of fields: pick another effect as the model.
- Custom trigger (`CTriggerEntry<T>` exists, same design): would let the DLL answer a condition directly.

## In-game result: B3 and B4 (2026-10-04)
Registration done by the loader at initialization (main thread, 0.25 s after loading): `auto_raid_ping`, number
19698. Console `effect every_army = { auto_raid_ping = 7 }`: the DLL received type 0x1b, army 1979711491, unit
2046820353 (the player's only one, checked with the units request), value 7.000, main thread. No error in error.log.

## B5 - writing a script variable from the DLL (static; in-game test pending)
Read during execution of `set_variable` (`CJominiSetVariableEffect`, function 22 at 0x3393480):

| Step | Function | Detail |
|---|---|---|
| Scope variables | 0x3329990 `(escopo*)` | escopo* = `[contexto]`; returns the target's variables object, or null |
| Name number | 0x3b970f0 `()` -> registry; 0x3b96da0 `(registro, &numero, &texto)` | separate registry for variable names; text = { pointer, length, 0 } |
| Write | 0x3346b30 `(variaveis+8, numero, &valor, dias)` | 16-byte value: u16 type at +0 (1 = number), fixed-point value at +8; days <= 0 = no duration |
| Notify change | 0x33465f0 `(variaveis)` | the game calls it twice |

In the core: `set_scope_var(ctx, nome, valor)`. `auto_raid_ping = N` now writes `ar_resp = 2N` on the army.
When reading the script, `set_variable` also registers the name at banco+0xd8 (0x3382560); we do not do that, so the
game may complain in error.log about a variable that is read and never written. To be checked.

## In-game result: B5 (2026-10-04)
`effect every_army = { auto_raid_ping = 7 auto_raid_ping = var:ar_resp }`: first call value 7, the DLL wrote
ar_resp = 14; the second call arrived with value 14 (read by the script) and wrote 28. No failure. The predicted
warning was confirmed: `jomini_effect.cpp:1161: Variable 'ar_resp' is used but is never set`. Fix to do: register the
names of the response variables with 0x3382560 `(banco+0xd8, numero)` before the scripts are read, or write the
variable once through `set_variable` in the mod's script.

## The game console from the DLL (2026-10-04, tested in game)
| Item | Address | Detail |
|---|---|---|
| Console (`CConsole`) | global pointer at 0x570f7b0 | null = unavailable |
| Execute text | 0x34b1970 `(texto*)` | the function behind the UI's `ExecuteConsoleCommand`. texto* = the game's std::string (pointer at +0, length at +0x10, capacity at +0x18; only read). Trims the spaces at the ends and puts it in the console queue (0x34b6f20); the console executes it right after, on the main thread |
Core request: `console <texto>`. Tested: `console effect every_army = { auto_raid_ping = 5 }`.

## Scopes seen in game
| Type | Scope | id |
|---|---|---|
| 0x4 | character | character id (122714 = player) |
| 0x8 | province | province id |
| 0x1b | army | army id |
Effect for any scope: function 5 replaced by the one that returns 0 (0x7e9230). Accepted by the game on character and province.

## Registered variables
0x3382560 `(banco+0xd8, numero)` called at initialization for each response name: the "used but never set" warning
went away. The game now warns "set but is never used" for the names no script reads; register only what the mod uses.

## Reading a script variable from the DLL (2026-10-04, tested in game)
`get_var(tipo de escopo, id, nome)` in the core. Builds a scope of its own { u16 type at +0, u32 id at +8 } and calls
0x3329990 to find the object's variables. The list sits at variaveis+8: base at +8, count at +0x14, entries
of 0x20 bytes with the name number at +8, the duration at +0xc, the value type at +0x10 (1 = number) and the
fixed-point value at +0x18. Test: `set_variable = { name = ar_cfg_chance value = 0.5 }` on the character and the
query started using a threshold of 0.50.
