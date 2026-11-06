# throw_release K: stand-ins vs disc

`throw_release` reports K, the ticks from pressing A to the grab completing
(`mIsHoldingThrowPiki`). The values below were measured 2026-10-27 on a
RelWithDebInfo build of `e10b00be`, macOS. Each run is deterministic: repeated
runs give the same K.

| Tree | 30 Hz | 60 Hz |
|---|---|---|
| Disc (VERSION_GPIE01_01, Forest of Hope) | 13 | 14 |
| Stand-ins (sandbox) | 12 | 23 |

At that point the stand-ins were unchanged, because the difference does not
come from a stand-in parameter, animation or timing value. The sandbox walls
were fitted later ("Fitted sandbox walls" below), then replaced by a gate the
squad works and gives up on, as on the disc ("The sandbox gate" below).

## What is the same on both trees

Temporary per-tick tracing of case 0 showed:

- **Selection.** The grabbed Pikmin is beyond `mPluckGrabRange` (15), so it
  goes to `PIKISTATE_GoHang` and walks to the hand.
- **Walk.** The target speed is 240 beyond 30 units (2 x grab range), then 120.
  This is the same on both trees.
- **Hand-over.** It happens when the Pikmin is within 15 units of the captain.
- **Grab.** ThrowWait (motion 36) starts at counter 0. `KEY_Action0` fires at
  counter 1.0, which is 1 tick later at 30 Hz and 2 ticks later at 60 Hz. This
  is the same on both trees.

## What differs

K is mostly the length of the walk. The walk starts where the Pikmin stands on
the tick the scenario presses A, and that position depends on the map and the
sim RNG.

- **Disc.** The 4 s walk away drives the captain into a wall near the ship. The
  squad leaves the plate, and later comes back in `PIKISTATE_Emotion`. The
  Settle phase presses A when the first Pikmin is Normal again:
  - At 30 Hz, the Pikmin is 58 units away and the press is on tick 239.
  - At 60 Hz, the Pikmin is 28 units away and the press is on tick 544.

  The walk is also slower than on open ground (55 to 130 u/s instead of up to
  220). The likely cause is map collision, because no other Pikmin is near for
  most of the walk. This cause is not checked.
- **Stand-ins.** On the open sandbox the squad trails the captain. A is pressed
  as soon as the nearest Pikmin is within 80, at about 79 units, at both rates.
  The time is about 0.4 s at both rates, so K roughly doubles at 60 Hz.

The disc's 13 and 14 are close only by chance. In seconds the two grabs take
0.43 s and 0.23 s, because the starting distances are different.

## Falsifying checks

- **Stand-in at the disc's press distance.** With the press forced at the disc's
  distance (temporary env override), stand-in K is 9 at 58 units (30 Hz) and 11
  at 28 units (60 Hz). The disc's slower walk explains the remaining 3 to 4
  ticks.
- **Particle RNG.** `zen::Rand` (`include/zen/Math.h`) is `gsys->getRand`, which
  is `pc_sim_randf`. `particleGenerator.cpp` and `zen/particle.h` draw from the
  sim stream for each emitted particle and on every frame. With only those draws
  moved to `pc_cosmetic_randf`, the disc K becomes 15 / 18 (2 runs each). The
  stand-in K stays at 12 / 23, because the inert stand-in effects never emit.
  Effect content therefore changes the squad's AI path, and so K. This matches
  the #72 overlay result (disc tree with the stand-in `effects.bun`: K = 8 at
  30 Hz).

## The sandbox gate (replaces the fitted walls)

The four fitted posts below are gone. The sandbox now has the mechanism the
disc trace shows: a gate in the captain's way that the squad tries to break
and gives up on (`standin_sandbox.h`, `standin_sandbox.cpp`).

### What the disc does

Temporary per-tick tracing of the disc run (Forest of Hope, 30 Hz, removed
before commit) showed:

1. The captain starts at (-528, 1974) and walks to -z at about 160 u/s for
   1.2 s (about 185 units).
2. He meets a reinforced gate (`sluice bomb`, init.gen, at 203 units ahead and
   34 units to his left, turned 35 degrees), slides along it and stops against
   it at tick 54, about 225 units from the start.
3. The squad catches up. Each Pikmin in formation that touches the gate's
   `gate` platform switches to `BreakwallMode` (Piki::collisionCallback).
4. `ActBreakWall` attacks on `KEY_Action0` of Kuttuku. A bomb gate refuses
   every hit (`InteractAttack::actItem`), so after 3 to 5 refused hits the
   Pikmin gives up, `Sad`, and plays Gakkari in `PIKISTATE_Emotion`.
5. The scenario presses A when the first Pikmin is Normal in the plate again:
   tick 283, the Pikmin 59.5 units away (60 Hz: tick 544, 39 units).

### What the sandbox now has

- **The gate.** A `sluice bomb` item, born from an item generator in
  `stages/sandbox/default.gen` (`writeItemGenerator`, gen.h). It stands where
  the disc's gate stands relative to the captain (34 units to his left), 197
  units ahead and turned 36 degrees (the disc's: 203 and 35 degrees; see
  "Exact K on the integrated tree" below).
- **Its files, ours.** `objects/gate/gt_hard.mod`: two posts on joint 0 and a
  panel on joint 1, plain boxes (210 x 90 x 24 panel, 20-unit posts), with the
  collinfo the game reads (`gate` and `side` platforms, `flag` sphere).
  `centcoll.mod` and `sidecoll.mod`: the same boxes as collision
  (`writePlatformMod`, mod_writer.h). `gate.bin` and `gt_hard.anm`: six static
  motions, because `BuildingItem::startAI` starts motion 0 and BaseShape
  panics ("no joint anim!!") without one. No disc geometry, texture or
  animation curve is used.
- **The work and give-up timing.** The stand-in Pikmin animations had no
  `KEY_Action0` on Kuttuku, so the squad stood at the gate in `BreakwallMode`
  forever. Kuttuku (info 71), Job2 (info 41) and Gakkari (info 6) now carry
  the disc's anim keys, info keys (loop start, `KEY_Action0/1`, loop end), the
  hit-effect event key, flags 5 and speed 30 (`standin_anims.h`). Only that
  timing is read from the disc, as for ThrowWait and Throw.
- **Terrain.** Still the flat floor. The disc's ground near the gate is close to
  flat (the captain's y changes by 1 unit on the walk), so no slope was added.

With the gate, the stand-in run follows the disc step by step: the captain
walks about 185 units, slides, and stops at about 225 units (tick 54 at
30 Hz, as on the disc); the squad enters `BreakwallMode`, gives up through
`Emotion`, and A is pressed with a Pikmin standing still.

### K before the cinema merge (`f9b3c0b4`)

RelWithDebInfo, macOS, `nectar --scenario throw_release`, 3 runs per cell.
Each run is deterministic. Superseded by "Re-fit on the merged tree".

| Tree | 30 Hz | 60 Hz |
|---|---|---|
| Disc (Forest of Hope) | 15, 15, 15 | 18, 18, 18 |
| Stand-ins, gate turned 35 degrees (the disc's turn), one run | 13 | 16 |
| Stand-ins, gate turned 36 degrees (committed) | 14, 14, 14 | 17, 17, 17 |

Press state with the committed gate: 30 Hz tick 251, Pikmin 68.6 units away
(disc 283, 59.5); 60 Hz tick 566, 34.9 units (disc 544, 39).

### How 36 degrees was chosen then, and how fragile it is

The disc's own layout (35 degrees) gives 13 / 16. Moving one gate parameter
at a time (about 40 layouts, each at both rates; all runs passed):

| Change from the disc layout | K pairs (30 Hz / 60 Hz) |
|---|---|
| turn 25 to 45 degrees | 13/39, 9/19, 10/17, 10/24, 14/17 (36), 16/26, 8/18, 11/30 |
| position along the walk, -195 to -212 | 12/33, 10/27, 11/29, 16/16, 16/16, 8/39 |
| position across the walk, -24 to -44 | 8/16, 10/32, 12/46, 12/30, 7/34, 11/25 |
| panel half-width 60 to 100 | 12/34, 19/38, 11/34, 15/26, 15/44 |
| panel half-depth 8 to 20 | 10/25, 8/31, 10/72, 12/23 |
| 36 degrees plus one 1-2 unit change | 7/17, 13/22, 8/19, 11/32, 15/41, 10/17, 8/26, 11/24 |

Only 36 degrees put both rates within one tick of the disc. That is a search
over legitimate gate parameters, not a derivation: K is a chaotic outcome of
which Pikmin gives up first and where it stands, and a 1-unit move of the gate
can change K60 by 20 ticks. The disc's K is presumably just as sensitive to
its own layout and RNG draws. What the gate does buy over the posts:

- the same sequence of states as the disc (stop against a wall, BreakwallMode,
  Emotion, press from rest), so `throw_release` exercises the same code paths
  on both trees;
- the same press situation in kind (a Pikmin standing still 35 to 70 units
  away), instead of a trailing, moving squad.

### Re-fit on the merged tree (stand-in cinemas and audio)

The stand-in cinema branch made the intro gate end on its first START press:
a stand-in tree's intro is now 15 ticks. The disc's landing movie on the same
merged tree ends on the third press, 45 ticks at both rates (it was 92 / 182
before the merge). With the gate unchanged (36 degrees) the stand-in K went to
11 / 35; the disc K stayed 15 / 18 (3 runs each).

**No plateau.** A grid of the gate's turn (30 to 42 degrees) and position
along the walk (-201, -203, -205), 39 layouts at both rates, gave K30 from 7
to 15 and K60 from 16 to 100, with neighbours one degree or two units apart
differing by up to 60 ticks. Only one layout was within one tick at both
rates (36 degrees, -205: 15 / 17), and its neighbours were not.

**Same start tick on both trees.** `throw_release` now holds neutral input
until 45 ticks after the stage is ready before the squad is born and the walk
away starts (`kWalkStartTicks`, `scenario_throw_release.cpp`; the runner gives
the intro length through `pc_scenario_intro_ticks`). On the disc the intro
already takes 45 ticks, so the hold is 0 ticks and the run is unchanged:
disc K 15, 15, 15 / 18, 18, 18 and the 6 disc tests pass. On the stand-ins
the hold is 30 ticks. The assertions are unchanged. This removes the
intro-length difference between the trees, but it does not remove the
layout sensitivity: the same 39-layout grid with the hold gave K30 8 to 16 and
K60 13 to 77, again with no plateau.

**Chosen point (superseded below).** Of the gate layouts tried with the hold (39-layout grid and
the 27-layout neighbourhood below), one is within one tick of the disc at both
rates: z -201, x -34, 34 degrees. It is committed. Its neighbourhood, gate
moved 1 unit (x, z) or 1 degree:

| turn | z | x -33 | x -34 | x -35 |
|---|---|---|---|---|
| 33 | -200 | 12/22 | 10/23 | 12/17 |
| 33 | -201 | 13/20 | 12/21 | 12/19 |
| 33 | -202 | 8/19 | 8/18 | 11/22 |
| 34 | -200 | 12/21 | 12/18 | 12/19 |
| 34 | -201 | 12/20 | **16/18** | 10/16 |
| 34 | -202 | 9/30 | 9/80 | 8/31 |
| 35 | -200 | 8/20 | 12/17 | 10/68 |
| 35 | -201 | 8/19 | 8/77 | 8/34 |
| 35 | -202 | 14/64 | 13/27 | 10/21 |

The block turn 33 to 34, z -200 to -201 is the calmest region found (K30 10
to 13, K60 16 to 23), but its typical value is about 12 / 19, three ticks short
of the disc at 30 Hz. The committed point is an outlier in it.

| Tree (merged tree, 3 runs per cell) | 30 Hz | 60 Hz |
|---|---|---|
| Disc (Forest of Hope) | 15, 15, 15 | 18, 18, 18 |
| Stand-ins, gate 36 degrees, no hold (as merged) | 11, 11, 11 | 35, 35, 35 |
| Stand-ins, gate 34 degrees at z -201, 45-tick walk start (committed) | 16, 16, 16 | 18, 18, 18 |

### Exact K on the integrated tree (`aceb6995`)

On the integrated tree (gate, stand-in cinemas, audio, objects) the committed
layout above still gave 16 / 18 against the disc's 15 / 18. The aim was then
the disc's K exactly.

**The 30 Hz offset is not the hand.** A trace of the walk to the hand showed
the stand-in captain's `rhnd` sphere 3.24 units to his left at ground level,
the disc's 3.66 to his right and 4.56 up. Putting the disc's hand position
into every stand-in captain pose (a translation of joint 7; ThrowWait alone
does not move it, the lower-body animator drives that joint) put the sphere
exactly where the disc's is, but the 49-layout grid's K30 did not move up
(7 to 16, median 11). The experiment was reverted; `standin_anims.*` are
unchanged. The acceleration and speeds of the walk are the disc's
(`pikiMgr.bin` CreatureProp and PikiProp, `Creature::moveVelocity`); the
remaining difference is the path, which other Pikmin block.

**Panel width is the lever.** K30 follows where the squad works the panel,
because the first Pikmin to give up walks back from there. Widening the
panel moves that spot away from the captain. Over a 300-layout random search
(turn 30 to 40 degrees, z -197 to -207, x -30 to -38, panel half-width 75 to
110, half-depth 10 to 16), the K30 median was 12 at half-width 75 and 13 at
100 to 110; at 105 it was 13 with the neighbourhood of the hit centred on 15.
9 of the 300 layouts failed the scenario (a case-1 or case-2 assertion at one
rate), so a layout change needs both rates checked.

**Exact hit.** One layout gave 15 / 18: half-width 105 (a 210-unit panel),
z -197, x -34, 36 degrees, half-depth 12. It is committed. Its
neighbourhood, one parameter or the 3 x 3 x 3 cube of turn, z and x moved by
1:

| turn | z | x -33 | x -34 | x -35 |
|---|---|---|---|---|
| 35 | -196 | 13/18 | 16/29 | 14/31 |
| 35 | -197 | 13/18 | 16/18 | 13/33 |
| 35 | -198 | 13/29 | 13/18 | 16/28 |
| 36 | -196 | 14/18 | 16/18 | 19/19 |
| 36 | -197 | 14/30 | **15/18** | 16/31 |
| 36 | -198 | 15/28 | 16/20 | 17/28 |
| 37 | -196 | 15/20 | 15/29 | 19/34 |
| 37 | -197 | 16/32 | 15/50 | 17/30 |
| 37 | -198 | 8/32 | 15/29 | 18/34 |

| Other neighbour of the hit | K (30 Hz / 60 Hz) |
|---|---|
| half-width 100, 102, 108, 110 | 13/32, 15/19, 15/28, 14/36 |
| half-depth 11, 13 | 13/40, 13/16 |

At 30 Hz this is the calmest region found: 15 of 26 neighbours are within one
tick of 15 and all but one give 13 to 19. At 60 Hz the neighbours split: 9 of 26 give 16 to 20, the others
28 to 50 (the first Pikmin back is then one on the far side of the panel). So
the exact 60 Hz match is still a chosen point, not a plateau.

| Tree (integrated tree, 3 runs per cell) | 30 Hz | 60 Hz |
|---|---|---|
| Disc (Forest of Hope) | 15, 15, 15 | 18, 18, 18 |
| Stand-ins, gate 34 degrees at z -201, 150-unit panel (as merged) | 16, 16, 16 | 18, 18, 18 |
| Stand-ins, gate 36 degrees at z -197, 210-unit panel (committed) | 15, 15, 15 | 18, 18, 18 |

After merging scenario integration branch rebuild v1 (upstream PRs #205, #206, #209, #211 and
#212, all still open upstream on 2026-10-28, and others), K did not move on either
tree. Re-measured 2026-10-28 on this tree: stand-in 15 / 18, disc 15 / 18:

| Tree (merged tree, 3 runs per cell) | 30 Hz | 60 Hz |
|---|---|---|
| Disc (Forest of Hope) | 15, 15, 15 | 18, 18, 18 |
| Stand-ins, gate 36 degrees at z -197, 210-unit panel | 15, 15, 15 | 18, 18, 18 |

### Re-fit for GameCube RNG draw order (#92, 2026-10-31)

Pinning Metrowerks' last-argument-first draw order at the eight two-RNG sites
(`fix/rng): draw the eight random-draw sites in the original GameCube order`)
moves disc `throw_release` K from 15 / 18 to **13 / 22** at 30 / 60 Hz (Forest
of Hope, particle draws on the cosmetic stream). Stand-ins were **15 / 30** with
the old gate layout under the new order, so the sandbox gate was searched again
(turn, position, panel width and depth). The committed layout is 42 degrees,
z -207, x -31.5, panel half-width 111 (222 units), half-depth 8. RelWithDebInfo,
Linux x86-64, three runs per cell: stand-in **13, 13, 13 / 22, 22, 22**; disc
not re-run here (no ISO in CI).

| Tree | 30 Hz | 60 Hz |
|---|---|---|
| Disc (Forest of Hope, GC draw order, issue #92) | 13 | 22 |
| Stand-ins, gate 42° at z -207, 222-unit panel (committed) | 13, 13, 13 | 22, 22, 22 |

### Limits

- The exact match depends on the chosen layout: at 30 Hz its neighbours stay
  near 15, at 60 Hz about half of them jump to 28 to 50 (above). A change to
  the squad AI, collision, animations, or the sim RNG draws can move both trees'
  K independently. Re-measure both after such a change.
- The stand-in gate is not the disc's shape. Its panel (210 units) is chosen
  for K, not measured, and it has no break motions (they never play on a
  bomb gate).
- Only Kuttuku, Job2 and Gakkari have disc timing. The walk and turn motions
  of the Pikmin are still plain 40-frame stand-ins, so the walk to the hand can
  still differ in speed from the disc (the disc's Pikmin starts more slowly).
  With the 150-unit panel the stand-in K30 sat about 3 ticks below the disc's
  across most layouts; the hand position was ruled out as the cause (above),
  and the wider panel removes most of the offset without explaining it.
- The 45-tick walk start is the disc's intro length on this tree. If the disc
  intro changes (for example the intro gate's START cadence), re-measure: a
  longer disc intro keeps the hold at 0, a shorter one would hold the disc
  run too and change its K.

## Fitted sandbox walls (after the particle RNG change, superseded)

This section records the previous fit (`afb9c3cc`). The posts and
`kSandboxWalls` were removed when the gate came in.

With the particle draws on the cosmetic stream (`standin-particle-rng.md`), the
disc K is 15 / 18. The stand-in generator now builds the sandbox with four
short posts across the walk instead of the wall at x = 700
(`kSandboxWalls` in `standin_generator.cpp`), and the stand-in K equals the
disc K.

RelWithDebInfo, macOS, `nectar --scenario throw_release`, 3 runs per cell, all
runs equal and passing. The disc tree is the install with the particle RNG
change; the stand-in data is `nectar_standin_generator` output.

| Tree | 30 Hz before | 60 Hz before | 30 Hz after | 60 Hz after |
|---|---|---|---|---|
| Disc (Forest of Hope) | 15, 15, 15 | 18, 18, 18 | 15, 15, 15 | 18, 18, 18 |
| Stand-ins (sandbox) | 12, 12, 12 | 23, 23, 23 | 15, 15, 15 | 18, 18, 18 |

"Before" is `ae235144`. The game binary is the same before and after (only
stand-in data changed), so the disc row is one set of runs.

The walls (centre lines, thickness 40, height 150; the captain starts at the
origin and walks to -z):

| Post | a (x, z) | b (x, z) |
|---|---|---|
| 1 | (-20, -445) | (-2, -426) |
| 2 | (60, -517) | (42, -482) |
| 3 | (21, -466) | (-4, -455) |
| 4 | (73, -486) | (45, -471) |

### How the posts set K

Temporary per-tick tracing (removed before commit) showed:

- The captain reaches the posts in the last second of the 4 s walk and is
  deflected between them. Collision is resolved per tick, so he ends the walk
  at z = -510 at 30 Hz and at z = -483 at 60 Hz.
- The squad trails, as on open ground. At 60 Hz the captain is further back, so
  the nearest Pikmin is closer when A is pressed: 76 units at 30 Hz, 52 units
  at 60 Hz. It walks to the hand around the posts.

The disc reaches its K from a different situation. The trace of the disc run
shows the captain walking about 1.3 s, sliding along a wall, and stopping
about 220 units from the start. The squad catches up, then switches to
`BreakwallMode` (a Forest of Hope gate), and returns in `PIKISTATE_Emotion`.
A is pressed when the first Pikmin is Normal in the plate again, from rest:
62 units at 30 Hz, 39 units at 60 Hz. The sandbox has no gate, so it matches
the resulting K, not the squad behaviour.

### How the posts were found

The posts are fitted by search, not derived from the disc. Each layout was
generated, then run at 30 and 60 Hz (about 2,900 layouts):

- One wall across the walk at 200 to 650 units and -60 to 60 degrees: K pairs
  (30 Hz / 60 Hz) up to 13 / 23. A straight wall stops the captain early, a
  Pikmin settles within the 15-unit grab range, and K = 1 (fails K >= 3).
- One to three random long walls: no pair with K30 >= 13 and K60 <= 21. On open
  ground K60 is close to twice K30.
- A stopper wall plus three to five posts: pairs such as 15 / 22 and 16 / 15.
  Small random moves around the closest layouts gave six layouts in range,
  including 15 / 18. Its stopper was then removed: the captain does not reach
  it, and K stays 15 / 18.

### Limits

- The match is fragile. Moving every post end by about 2 units (40 random
  moves) gave pairs from 8 / 17 to 15 / 29; only 3 of 40 stayed in range and passed.
- A change to the squad or captain AI, to collision, or to the sim RNG draws
  can move the stand-in K and the disc K independently. Re-measure both after
  such a change, and re-fit the posts if the stand-in K must match again.
- Matching K does not mean the grab starts from the same distance or state as
  on the disc (76 / 52 units moving, against 62 / 39 units from rest).

## Consequences

- (Written before the particle RNG change and the fitted walls.) Matching K
  within one tick would mean building sandbox terrain that copies the Forest
  of Hope wall and its RNG-dependent behaviour of the squad. It would also mean
  drawing the disc's particle RNG sequence, which needs every disc emitter's
  rates and jitter flags. That is disc data. The particle draws have since
  moved, and the fitted walls match K without copying the squad behaviour.
- Stand-in K is stable, and the scenario asserts only behaviour. Both trees
  pass.
- Moving particle draws to the cosmetic stream would remove the effect-content
  coupling. It is a game-code change, and it would change deterministic-mode
  state hashes and replays. It was made later (`standin-particle-rng.md`).
- (Written when case 2 released at `K + 2` after the press; it now releases 2
  ticks after that Pikmin's own grab completes.) The case-0 Pikmin calibrated K,
  so if the case-2 Pikmin started farther away, the release came before the grab. With a forced
  short K (6 at 30 Hz, 11 at 60 Hz) on the stand-ins, case 2 failed with "never
  Hanged during ThrowWait". The same failure can happen on the disc when K gets
  shorter, which is consistent with the #72 overlay failure at 60 Hz.
