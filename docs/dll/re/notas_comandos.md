Portuguese version: [notas_comandos.pt-BR.md](notas_comandos.pt-BR.md)

# Army commands (formats)

Captured in game on 2026-10-04 with the DLL's command log (`cmdlog on`) and resent by the DLL
successfully. `ck3.exe` 1.19.0.6, addresses as RVAs. Log: `docs/re/sessao_log_2026-10-04.txt`.

## Sending
- External function `Post(despachante, comando*, flags)` at 0x973e00: clones the command and calls the internal one.
- Internal function `(despachante, unique_ptr<comando>*, flags)` at 0x341d8e0: ALL commands go through here
  (225 places in the game call it directly). The general command log lives in it.
- Player flags: 0xE. Pause uses 7.

## Common header
| Offset | Content |
|---|---|
| 0x00 | main vtable |
| 0x08 to 0x17 | state (zeroed on send) |
| 0x18 | secondary vtable |
| 0x20 onwards | command fields |

## Commands
| Command | Vtable / secondary | Fields (from 0x20) | Tested from the DLL |
|---|---|---|---|
| `CMoveUnitCommand` | 0x432bef0 / 0x432bf88 | +0x20 valid_for (1 player), +0x24 unit, +0x28 province, +0x2C order type (0 replaces, 1 queue), +0x30 = 2, +0x34 = 1, +0x38 path list (constructor 0xc7ba80), +0x160 cost | Yes |
| `CToggleRaidingCommand` | 0x432C7E8 / 0x432C628 | +0x20 = 1, +0x24 player character, +0x28 army id | Yes (user saw the army change mode) |
| `CSplitHalfArmyCommand` | 0x432D408 / 0x432D4A0 | +0x20 = 1, +0x24 player character, +0x28 army id | Yes (1 unit became 2) |
| `CMergeUnitsCommand` | 0x432D1E0 / 0x432D1B0 | +0x20 = 1, +0x24 destination unit, +0x28 vector of unit ids {data, count, capacity, allocator 0x4FEADE0} | Yes (2 units became 1) |
| `CDisbandArmyCommand` | 0x432D340 / 0x432D3D8 | +0x20 = 1, +0x24 army id | Yes |
| `CRaiseTroopsCommand` | 0x4122750 / 0x4122720 | +0x20 character, +0x28 vector of rally points {data, count, capacity, allocator 0x4FEBA40}, each point = {province u32, color u32}; +0x40 = -1; +0x48 army type (1 when raising as raid) | Yes (army raised in the requested province, as raid) |
| `CToggleLootingCommand` | 0x432c750 / 0x432c720 | +0x20 = 1, +0x24 player character, +0x28 army id. Toggles the byte `[exercito+0x1d5]` (loot when stopped) | Yes (the byte changed and looting started) |
| `CHaltUnitsCommand` | 0x432be58 / 0x432be28 | +0x20 valid_for (1 player; the AI uses 2), +0x28 vector of UNIT ids {data, count, capacity, allocator 0x4FEADE0} | Yes (sent without rejection; does not interrupt looting) |
| `CSelectEventOptionCommand` | 0x4335150 / 0x4335120 | +0x20 event instance id, +0x24 option index | Yes (event answered, pause removed) |
| `CPauseGameCommand` | 0x432F300 / 0x432F2D0 | +0x20 = 1, +0x24 state (1 pause, 0 unpause) | Yes (confirmed by the user) |

Text of `raise_troops` in `message.log`: `character`, `rally_points={ { province color } }`, `raise_army_type`.

## Ids
- Unit: e.g. 2046820353 (0x7A000001). Index in the table = id & 0xFFFFFF.
- Army: e.g. 1979711491 (0x76000003). `[unidade+0x178]` gives the unit's army.
- Move and merge use a UNIT id; raid mode, split and disband use an ARMY id.
