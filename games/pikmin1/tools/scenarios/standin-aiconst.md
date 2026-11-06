# Stand-in `parms/aiconst.bin`

`nectar_standin_generator` writes `dataDir/parms/aiconst.bin` (`parmFiles` in
`standin_generator.cpp`). The game reads it into `AIConstant::Parms`
(`include/AIConstant.h`, `AIConstant::read`, `Parameters::read`): an entry whose
ID is not in the file keeps the code default.

The stand-in file now carries the disc's value for every ID that differs from the
code default. Before, it carried only `p00` (gravity). Source: the
VERSION_GPIE01_01 install's `parms/aiconst.bin`, read once, read-only, decoded
with the `Parameters::read` layout on 2026-10-28. Only the numbers are used; no
disc file is copied.

## Table (all 23 IDs)

| ID | Field | Meaning (header comment) | Code | Disc | Stand-in |
|----|-------|--------------------------|------|------|----------|
| p00 | `mGravity` | gravity | 550 | 560 | 560 (was already) |
| p01 | `_34` | Pikmin auto-attack | 1 | **0** | 0 |
| p02 | `_44` | Pikmin auto-help pluck | 1 | 1 | code |
| p03 | `_54` | captain: cancel pluck | 0 | 0 | code |
| p04 | `_64` | press every frame | 1 | 1 | code |
| p05 | `mDoCStickAttack` | C-stick contact attack | 1 | 1 | code |
| p06 | `_94` | free Pikmin go to the ship | 1 | 1 | code |
| p07 | `_84` | free Pikmin flee enemies | 1 | 1 | code |
| p08 | `_A4` | return after attacking | 1 | **0** | 0 |
| p09 | `_B4` | formation sort | 1 | 1 | code |
| p10 | `_C4` | attack once = 1, until death = 0 | 1 | **0** | 0 |
| p11 | `mJumpTriangleAngleThreshold` | auto-jump angle | 90 | 90 | code |
| p12 | `_E4` | throw = 1, roll = 0 | 1 | 1 | code |
| p13 | `mDoPluckWithCursor` | cursor pluck | 1 | **0** | 0 |
| p14 | `mDoScaleHappaMoveSpeed` | bud and flower Pikmin walk faster | 1 | 1 | code |
| p15 | `mMaxPikisOnField` | field limit | 100 (`MAX_PIKI_ON_FIELD`) | 100 | code |
| p16 | `_124` | bomb mode | 1 | 1 | code |
| p17 | `mWeakSlipFactor` | weak slip | 1.0 | 1.0 | code |
| p18 | `mStrongSlipFactor` | strong slip | 2.5 | 2.5 | code |
| p19 | `_154` | UFO level 2 part count | 6 | **5** | 5 |
| p20 | `_164` | UFO level 3 part count | 12 | 12 | code |
| p21 | `_174` | UFO level 4 part count | 29 | 29 | code |
| p22 | `_184` | UFO level 5 part count | 30 | 30 | code |

The stand-in differs from the code defaults in six IDs: p00, p01, p08, p10, p13
and p19. The other 17 equal the code defaults, so the stand-in omits them. The
task note that named "p21 = 29" is the code default too; only p19 differs there.

The disc install also has `プレス用aiconst.bin` and `本物aiconst.bin`. The game
reads `aiconst.bin`; the other two were not used.

## What reads these values

Only some fields have a reader in this tree (`rg 'AICONST\.'`):

- `p00` gravity: the throw flight.
- `p10` `_C4`: `aiAttack.cpp`, whether a Pikmin attacks once or until the enemy dies.
- `p13` `mDoPluckWithCursor`: `navi.cpp` (plucking with the cursor: selecting a
  sprout under the cursor and the plucked-Pikmin grab).
- `p19`-`p22`: `playerState.cpp`, the ship's level from the part count.
- `p01` `_34` and `p08` `_A4` have no reader in this tree. They are written so
  the stand-in file equals the disc anyway.

## Scenario effect (macOS, 2026-10-28)

No change. All 15 stand-in scenario runs (`boot_smoke`, `throw_release`,
`day30_clock`, `extinction`, `sprout_landing`, `onion_skip`, `container_menu`
at 30 and 60 Hz, `objects_smoke` at 30 Hz) were run twice, once with the old
`p00`-only `aiconst.bin` and once with this table, and their per-tick state
hash logs (`--state-hash-log`) are byte-identical. That is expected: no
scenario hurts or attacks with a Pikmin, plucks, or reads the ship level, which
are the only places a changed field is read (`p10`, `p13`, `p19`). `throw_release`
K is 15 at 30 Hz and 18 at 60 Hz, on the stand-ins and on the disc. The disc
scenarios read the disc's own `aiconst.bin`, so they are unaffected by design.

Not verified: the changed fields' behaviour (`p10`, `p13`, `p19`) itself. No
scenario exercises it.
