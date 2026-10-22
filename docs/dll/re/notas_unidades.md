Portuguese version: [notas_unidades.pt-BR.md](notas_unidades.pt-BR.md)

# Units, location and travel time

Static analysis of `ck3.exe` 1.19.0.6 on 2026-10-04. Everything here still needs in-game confirmation (tests Q1 to Q5
in `docs/funcionalidades.md`). Addresses as RVAs.

| Item | RVA / offset | Note |
|---|---|---|
| Unit table pointer | 0x570cbe8 (default null unit at 0x570cbe0) | index = id & 0xFFFFFF; limit at `[tab+0x2c]`; 16-byte entries at `[tab+0x20]`, pointer at +8; check `[unidade+0x10] == id` |
| Second table (owner/army) | 0x570c6b0 | indexed by `[unidade+0x178]`, checks `[obj+0x10] == id` |
| Character table (probable) | 0x570c0b0 | indexed by `[unidade+0x174]`, checks `[obj+0x18] == id` |
| Current province | `[unidade+0x20]` | pointer; id at `[provincia+0x10]` |
| Current path | list at `[unidade+0x38]`, size at `[unidade+0x44]` | province indices |
| Location (accounts for movement) | 0x2246cf0 `(unidade)` -> provincia* | |
| Next province on the path | 0x2246d70 `(unidade)` | |
| Final destination of the path | 0x2246dc0 `(unidade)` | |
| Province table | `[[global]+0xa0]`: vector at +0x140, count at +0x14c | global read at 0x2246d15 |
| Current date | 0x570edd8 | used in the "is moving" comparison |
| March tooltip | function 0xc6f810 to 0xc711fb | builds `PROVINCE_TOOLTIP_ETA` (0xc70ee1), `..._NO_PATH_ALL` (0xc7091d) etc. |
| Pathfinder context | 0x23c3310 `(ctx, unidade)` | ctx on the tooltip's stack at rbp+0x490 |
| Find path | 0x23c33f0 `(ctx, inicio*, alvo*, 2, caminho_saida*)` | called at 0xc70e20 |
| Travel time | 0x2247340 `(unidade, &tempo, caminho*, inicio*)` | fixed point 100000 = 1 day; empty path -> 0 |
| Remainder of the current step | 0x2247600 `(unidade, &saida, 0)` | added when the unit is already moving |
| "Is moving" | 0x2247890 `(unidade, &saida)` | compares with the current date |

## Owner and classification (static, 2026-10-04)
| Item | RVA / offset | Note |
|---|---|---|
| Local player's character id | 0x4fe7ee0 (dword) | read in the march tooltip (0xc6f95d) and indexed in the character table |
| Unit owner | `[unidade+0x174]` | character id (table 0x570c0b0, checks `[obj+0x18]`) |
| Unit's army | `[unidade+0x178]` | id in table 0x570c6b0 (checks `[obj+0x10]`); `[exercito+0x124]` leads back to the unit id |
| Unit class for the player | first dword of the `UnitItem` (screen item) | 0 = mine, 1 = allied, 2 = enemy, 3 = hostile, 4 = neutral |
| `IsUnitItemMine/Ally/Enemy/Hostile/Neutral` | 0xc20560, 0xc20520, 0xc205a0, 0xc205e0, 0xc7e570 | only compare the dword above; registered at 0x94510, 0x94680, 0x94970, 0x94af0, 0x947f0 |
| Who computes the class | 0x224a610 `(unidade*)` -> 0 to 5 | TESTED in game 2026-10-04. Compares the unit's owner with the player's character: equal = 0 mine; at war = 2 enemy; 0x2900490(jogador, dono, 0) = 3 hostile; 0x28ffe80(jogador, dono, 0) = 1 allied; otherwise 4 neutral; 5 if `[unidade+0x18]` is not 0 or 1 |
| Path list (type) | 0x128 bytes: data at +0, count at +0xC, allocator at +0x10; total cost right after (+0x128) | same type as the +0x38 sub-object of `move_unit`; constructor 0xc7ba80 |
| Pathfinder context | constructor 0x23c3310 fills up to +0x68 | mode at +0x28 (2 in the tooltip) |

## Combat prediction (tested in game, 2026-10-04)
| Item | RVA | Note |
|---|---|---|
| Tooltip text | 0xc750d0 (called by 0xc71e10, `Army.GetCombatPredictionForTooltip` at 0xc7c9a0) | builds `PREDICTION` / `UNIT_NEUTRAL_COMBAT_PREDICTION` |
| Calculation | 0xc6e9c0 `(&resultado, vetor de exercitos A, vetor de exercitos B, k, k, 0, 0)` | vectors = {ARMY ids u32, count, capacity, allocator 0x4FEADE0}; k = qword at 0x57BFB48 |
| Result | fixed point, 100000 = 1.0 | chance of A winning: 0.5 against itself; 0.71 for A against B gave 0.28 for B against A |
| Level thresholds | pointer at 0x4F42D40, count at 0x4F42D4C | 0.30 / 0.45 / 0.60 / 0.80 -> 5 prediction levels |

## Camera and names
| Item | RVA | Note |
|---|---|---|
| `Province.PanCameraTo` | 0xa1c880 `(provincia*)` | centers |
| `Province.ZoomCameraTo` | 0xa1ca20 `(provincia*)` | centers and zooms in |
| Province text key | `std::string` at `[[provincia+8]+0x28]` | e.g. `stepstone_sea` |
| Map coordinates | `[[provincia+8]+0x10]` and `+0x14` (integers) | used by the camera |

## Loot and raid in progress (read in game, 2026-10-04; user check pending)
| Item | RVA / offset | Note |
|---|---|---|
| Army object | table 0x570c6b0, id at `[unidade+0x178]`, checks `[exercito+0x10]` | `[exercito+0x124]` = unit id |
| Is a raid army | `[exercito+0x1d4]` (byte) | |
| Carried loot | `[exercito+0x1e0]` (fixed point, 100000 = 1 gold) | screen function `Army.GetRaidLoot` -> 0x18cb390 |
| Loot capacity | 0x2272a80 `(exercito*, &saida)` | `Army.GetLootCap`; `Army.IsLootCapped` = 0x2272e00 |
| Is a barter / cargo army | `[exercito+0x1ec]` (byte), `[exercito+0x1f0]` | |
| Army's raid in progress | id at `[exercito+0x1e8]` (-1 = none) | table 0x57BFC38, checks `[raid+8]` |
| Raid object | `+0x10` army id, `+0x18` progress done, `+0x20` target (object with id at +0x10) | |
| Total progress required | 0x23a1040 `(&saida, alvo*)` | |
| Progress per day | 0x23a0cc0 `(&saida, exercito*, 0)` | pass the ARMY; with the unit it returns only the base (20) |
| Days to finish | `ceil((total - feito) / por dia)` | same calculation as `Raid.GetEta` (0xc30990) |
| Sea province | byte at `[[provincia+8]+0x1d]` | used to mark the path; land has a byte at +0x1b |

Contained failure observed: calling 0x23a1040 with a regular province object (instead of the target stored in the raid)
caused an access violation at exe+0x20AB991; the loader contained it and the game went on. The raid target is not the
same type of object as the province table: to be studied, to estimate the looting time of a location BEFORE going there.

## Loot target (tested in game, 2026-10-04)
| Item | RVA / offset | Note |
|---|---|---|
| Can raid this location | 0x293db30 `(exercito*, provincia*, texto ou NULL)` -> bool | with null text it only returns the result |
| Loot available at the location | 0x291dc90 `(&saida, provincia*, NULL, NULL)` | fixed point; 0 if recently raided |
| "Loot when stopped" mode | byte `[exercito+0x1d5]` | screen function `Army.HasLooterStance` (0x22069e0) |
| Province loot tooltip | function 0xc57840 | builds `RAID_PROVINCE_TOOLTIP_LOOT` |
| Province coordinates | integers at `[[provincia+8]+0x10]` and `+0x14` | used in the radius pre-filter |

## Pause state (tested in game, 2026-10-04)
Screen function `IsGamePaused` at 0xa21c10. Three sources of pause:
| Source | How to read | Note |
|---|---|---|
| Player pause | byte `[[exe+0x570F738]+0x20]` | the one `CPauseGameCommand` turns on and off |
| Pause from event/message | 0x2707280 `([[exe+0x570DFE8]+0xa0] + 0x2f4c0, 0)` -> bool | stays active until the player answers; `pause 0` does NOT remove it |
| Other pause | 0x346b8f0 `([exe+0x570F738])` -> bool | not observed yet |
Game speed: `[[exe+0x570DFE8]+0x70] + 1`.

