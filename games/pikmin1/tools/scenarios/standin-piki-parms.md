# Stand-in `parms/pikiMgr.bin`

`nectar_standin_generator` writes `dataDir/parms/pikiMgr.bin` (`parmFiles` in
`standin_generator.cpp`). The game reads it into the Pikmin's `CreatureProp`
list and then `PikiProp::Parms` (`include/PikiMgr.h`, `Parameters::read`): an
entry whose ID is not in the file keeps the code default.

Since 2026-11-04 the stand-in file carries every ID the disc's file sets, with
the disc's value, in the disc's order: the five CreatureProp values and all 77
`PikiProp::Parms` IDs. The generated file is byte-identical to the disc's
(664 bytes, checked with `cmp`). Before, it carried 14 Parms IDs (the walk,
size, collision and flower-gravity values that `throw_release` and
`throw_flight` need) and left the rest at the code defaults. Kyle decided on
2026-11-04 that the Pikmin file takes the disc's values, the same way
`aiconst.bin` (standin-aiconst.md) and the Bulborb's parameters already did.

Source: the VERSION_GPIE01_01 install's `parms/pikiMgr.bin` (Pikmin-USA-Rev1),
read once, read-only, decoded with the `Parameters::read` layout on 2026-11-04.
Only the numbers are written by the generator; no disc file is copied.

## Table (all 77 Parms IDs, disc order)

Bold: the disc differs from the code default. "Before" is what the stand-in
file had until 2026-11-04: `disc` (already written), `code` (left at a
differing code default, now the disc's) or `code (equal)` (not written, and
the disc equals the code default).

| ID | Field | Meaning (header comment) | Code | Disc | Before |
|----|-------|--------------------------|------|------|--------|
| p00 | `mMinMoveSpeed` | walking speed | 84 | **10** | disc |
| p01 | `mMaxLeafMoveSpeed` | running speed | 162.5 | **120** | disc |
| p54 | `mMaxFlowerMoveSpeed` | flower piki speed | 220 | **170** | disc |
| p65 | `mMaxBudMoveSpeed` | bud piki speed | 160 | **140** | disc |
| p38 | `mPikiDisplayScale` | only used for "pressed" rescaling and restoring afterwards | 1 | 1 | code (equal) |
| p03 | `mPikiMaxHealth` | max health (but always instakilled) | 100 | **30** | code |
| p10 | `mSkinnyPikiHealth` | unused, named from DLL text | 150 | **10** | code |
| p11 | `mFatPikiHealth` | unused, named from DLL text | 200 | **10** | code |
| p04 | `mBlueAttackPower` | blue pikmin attack power | 10 | 10 | code (equal) |
| p12 | `mRedAttackPower` | red pikmin attack power | 15 | 15 | code (equal) |
| p13 | `mYellowAttackPower` | yellow/other pikmin attack power | 10 | 10 | code (equal) |
| p05 | `mBudEnergyRequirement` | unused, named from DLL text | 1 | 1 | code (equal) |
| p06 | `mFlowerEnergyRequirement` | unused, named from DLL text | 2 | **1** | code |
| p07 | `mPluckStrength0` | unused, from some abandoned behaviour | 0.8 | 0.8 | code (equal) |
| p08 | `mPluckStrength1` | unused, from some abandoned behaviour | 1.5 | 1.5 | code (equal) |
| p09 | `mPluckStrength2` | unused, from some abandoned behaviour | 1.2 | 1.2 | code (equal) |
| p14 | `mPressDeathFactor` | total time to die from crushing/also controls press scale | 5 | **2.5** | code |
| p15 | `mNormalPikiPressFactor` | related to scale normal piki expands to when pressed/crushed | 3 | **1** | code |
| p16 | `mWildPikiPressFactor` | unused, named from DLL text | 1 | **0.9** | code |
| p37 | `mFatPikiPressFactor` | unused, named from DLL text | 40 | **35** | code |
| p17 | `mStepSpeedThreshold` | below this = Wait anim, above this = Asibumi anim | 5 | 5 | code (equal) |
| p18 | `mWalkSpeedThreshold` | below this = Asibumi anim, above this = Walk anim | 35 | **8** | disc |
| p19 | `mRunSpeedThreshold` | below this = Walk anim, above this = Run anim | 70 | **20** | disc |
| p20 | `mEscapeSpeedThreshold` | below this = Run anim, above this = Nigeru anim | 160 | **95** | disc |
| p21 | `mMinWalkAnimSpeed` | slowest fps to do walk animation at | 30 | 30 | code (equal) |
| p22 | `mMaxWalkAnimSpeed` | fastest fps to do walk animation at | 60 | **50** | disc |
| p23 | `mMinRunAnimSpeed` | slowest fps to do run animation at | 30 | **40** | disc |
| p24 | `mMaxRunAnimSpeed` | fastest fps to do run animation at | 60 | 60 | code (equal) |
| p25 | `mEscapeAnimSpeed` | fps to do escape animation at | 30 | **60** | disc |
| p26 | `mUnusedAnimSpeed` | unused, probably another anim speed | 60 | **100** | code |
| p27 | `mMinPikiSize` | used for some niche things (shadow scale, culling, default collisions) | 6 | **3** | disc |
| p34 | `mMaxPikiSize` | pikis are randomly assigned between mMinPikiSize and mMaxPikiSize on init | 10 | **5** | disc |
| p28 | `mInvMass` | inverse mass for collision calcs | 100 | 100 | code (equal) |
| p39 | `mCollisionRadius` | radius for collision calcs | 8 | **8.5** | disc |
| p29 | `mFormationSlipRange` | if piki is this far or more from its squad slot, reassign it a new slot | 600 | 600 | code (equal) |
| p30 | `mFormationBreakRange` | if piki is this far or more from its squad slot, remove it from party | 1000 | 1000 | code (equal) |
| p31 | `mMaxLostChildTime` | if piki has been in "slip" range for this much time, remove it from party | 10 | 10 | code (equal) |
| p32 | `mUnusedWhiteFreeDistance` | unused - white = "slip range", unsure of purpose | 200 | **50** | code |
| p33 | `mUnusedGrayFreeDistance` | unused - gray = "break range", unsure of purpose | 800 | **250** | code |
| p35 | `mUnusedDynWallPushTime` | unused, named from DLL text | 0.6 | **2** | code |
| p36 | `mUnusedStaticWallPushTime` | unused, named from DLL text | 0.6 | **2** | code |
| p40 | `mDefaultFreeBoidTime` | default timer to use when making dismiss groups | 15 | **120** | code |
| p41 | `mIdleAttackSearchRange` | range to look for enemies, bridges and bomb spawners while idle | 100 | **70** | code |
| p42 | `mIdleWorkSearchRange` | range to look for pellets, grass, loose bombs, walls, sticks while idle | 100 | **55** | code |
| p43 | `mMaxFlickKnockDownTime` | max time to stay knocked down for after getting flicked | 0.7 | **3** | code |
| p74 | `mMinFlickKnockDownTime` | min time to stay knocked down for after getting flicked (random between) | 0.7 | **1** | code |
| p44 | `mUnusedLeafBudTime` | unused, probably related to GrowState | 10 | **2** | code |
| p45 | `mUnusedBudFlowerTime` | unused, probably related to GrowState | 10 | **2** | code |
| p46 | `mUnusedFlowerSeedTime` | unused, probably related to GrowState | 10 | 10 | code (equal) |
| p47 | `mUnusedSeedLeafTime` | unused, probably related to GrowState | 10 | 10 | code (equal) |
| p48 | `mFlowerGravityScale` | scaling factor used to modify flower piki throw trajectories | 0.01 | **0.5** | disc |
| p49 | `mSeedUpTime` | time until visible after getting planted | 1 | **0.1** | code |
| p50 | `mPluckWaitTime` | time until pluckable when planted | 1 | **5** | code |
| p51 | `mGrowUpTime` | time from seed -> bud and bud -> flower | 4 | **120** | code |
| p52 | `mWiltTime` | time from flower -> back to seed | 4 | **180** | code |
| p53 | `mRebirthSeedTime` | time after wilting before seed pops back up | 1 | **60** | code |
| p55 | `mPanicTime` | time to flail when on fire/bubbled before death | 3 | **2** | code |
| p56 | `mPikiLeaveSeedChance` | chance for a flower to leave a seed upon death | 0.8 | 0.8 | code (equal) |
| p57 | `mBombSetFuseTime` | time until a bomb explodes after being set down by a piki | 2 | 2 | code (equal) |
| p58 | `mBombExplodeRadius` | explosion radius to damage things within upon bomb detonation | 50 | 50 | code (equal) |
| p59 | `mBombDamageBomb` | damage bombs deal to other bombs; DLL name suggests it was meant for walls | 200 | **50** | code |
| p75 | `mBombDamageNavi` | damage bombs deal to captains (and walls!) | 10 | **20** | code |
| p76 | `mBombDamageEnemy` | damage bombs deal to tekis and bosses | 120 | 120 | code (equal) |
| p77 | `mBombDamagePiki` | damage bombs deal to pikis | 765 | 765 | code (equal) |
| p60 | `mSunsetSafetyRange` | radius around onions and ship that pikis are "safe" within at sunset | 200 | **300** | code |
| p61 | `mPostWorkJoinPartyRange` | max navi-piki range to rejoin squad after finishing an action | 250 | 250 | code (equal) |
| p62 | `mMinStartDrownFrames` | min frames from water contact to going into drown state | 30 | **10** | code |
| p63 | `mRandStartDrownFrames` | actual drown transit starts between min frames and (min + rand) frames | 10 | 10 | code (equal) |
| p64 | `mUnusedPushTime` | unused, sets a(n unused) variable in ActPush | 5 | **120** | code |
| p66 | `mBombTargetSearchRange` | how far to look for bomb (set or throw) targets (tekis, bosses, walls) | 300 | **170** | code |
| p67 | `mBombThrowMinDistance` | must be at least this far away from target to throw bomb | 110 | **100** | code |
| p68 | `mBombThrowMaxDistance` | must be at least this close to target to throw bomb | 160 | **130** | code |
| p69 | `mBombThrowTimeoutTime` | if we're too close for this amount of time, stop trying to throw and drop it | 1.5 | 1.5 | code (equal) |
| p70 | `mBombThrowSpeed` | horizontal speed to throw bomb at | 150 | 150 | code (equal) |
| p71 | `mBombWarnPikiRange` | range to "warn" pikis within when bomb is thrown or set | 300 | **150** | code |
| p72 | `mBombAimTime` | time to "aim" before trying to throw bomb | 0.65 | 0.65 | code (equal) |
| p73 | `mBombSetDistance` | if we're within this distance of a wall, set the bomb | 50 | 50 | code (equal) |

49 IDs differ from the code default: 14 were already written with the disc
value, 35 are new. The other 28 equal the code default and are written anyway,
so the file matches the disc's byte for byte.

## What the new values change

Measured on macOS with the full scenario suite, against the stand-in tree
before this change (176 per-tick hash logs). The cause of each change was found
by re-running the suite with the old file plus one group of disc values at a
time.

- `p03` mPikiMaxHealth 30 (code 100): every Pikmin is born with this health
  (`Piki::init`, `piki.cpp`), which is part of the Pikmin state hash, so most
  logs with a Pikmin change from its first tick. 44 logs change from this
  alone; their scenarios pass with the same events.
- `p49` mSeedUpTime 0.1 and `p50` mPluckWaitTime 5 (code 1 and 1): a planted
  sprout becomes pluckable about 3 s later (PikiHeadAI Bury and Tane,
  `itemAI.cpp`), so every scenario that plants or plucks sprouts runs longer
  and draws its random numbers at other ticks.
- `p51`-`p53` grow-up, wilt and rebirth times 120, 180, 60 (code 4, 4, 1): a
  sprout in the ground no longer turns bud, flower and back every few seconds.
- `p40`-`p42` free-boid time 120 (15), idle attack and work search ranges 70
  and 55 (100 and 100): `bulborb_fight`, `pellet_carry`, `piki_hazard_deaths`.
- `p43`/`p74` flick knock-down 3 and 1 s (0.7 and 0.7): `bulborb_fight` 30 Hz,
  `piki_hazard_deaths` 60 Hz.
- Drown, press, panic and bomb values (`p62` 10 frames, code 30; `p14`/`p15`;
  `p55`; `p59`, `p75`, `p66`-`p71`): `piki_drown`, `piki_hazard_deaths`.

Golden shots: `world_map_idle` and `world_map_confirm` (`world_map_day_end` on
`sandbox_objects`) change through `p51` alone. With the code's 4 s the stage's
sprouts grow up every few seconds and each grow-up casts the PikiGrowup
particles (PikiHeadAI, `itemAI.cpp`), which draw from the cosmetic random
stream. With 120 s they do not, so that stream is at another point when the map
opens, and the bob and scale timer of the red Onion icon beside the cursor
(`WorldMapCursorOnyon`, `drawWorldMap.cpp`, `pc_cosmetic_randf`) starts
elsewhere: a few pixels around that icon differ. The goldens were refreshed from
CI for all three OSes.

99 of the 176 logs changed and 77 are byte-identical. With the old file the
suite gives the old logs except `tutorial_firsts_60`, whose driver changed (below).

`bulborb_fight` (30 Hz / 60 Hz): Bulborb dead at 2.2 / 1.5 s before, 2.4 / 1.6 s
now (disc 3.4 / 2.5 s); Flick 1.8 s before, 3.3 s now (disc 2.9 s). `throw_release`
K stays 13 / 22.

## Scenarios changed with it

- `piki_hazard_deaths`: the "non-lethal" attack was 40 damage, written for the
  code's 100 health; with 30 it kills. It is now 40% of `p03` (40 at 100, 12 at
  30), with the same 75% drop check.
- `onion_unlock_whistle` and `tutorial_firsts`: the sprout bounds were a fixed
  10 s, the `p49` + `p50` code defaults plus 8 s. They are now `p49` + `p50` +
  8 s (10 s with the code values, 13.1 s with the disc's).
- `tutorial_firsts` driver: (1) the later sprout lets the captain go Idle
  before the walk to it; the scenario steered only in Walk, so he stayed. It now
  sends one tick of stick to wake him and waits for Walk (Idle runs no demo
  check, so walking while still Idle would cross the DF03 radius unseen).
  (2) TUT_NukiAndFree opens while the captain is still plucking; the A that
  closed it queued the next pluck, and with no sprout left he grabbed and threw
  the nearest Pikmin. That already happened with the old file, to another Pikmin;
  with the disc values the 15th Pikmin was the nearest. B closes the text while
  he plucks.
All three pass at 30 and 60 Hz with both the old and the new file.
