# Object stand-ins and the object sandbox

What the stand-in tree (`nectar_standin_generator`) writes for objects, plants,
gates, bosses and enemies, and the second stage that places some of them.
Issue #59 and #62 rules apply: nothing is copied from the disc. The numbers that
only the disc has were read once, read-only, and each is named in the file that
uses it.

## What the disc was read for

Per AnimMgr table and bundle (`libs/nectar_assets/src/standin_object_profiles.cpp`):

- the info count, and per info the frame count N, the anim-key frames, the
  info-key records (which anim key each `KEY_*` event sits on) and the `p00`
  flags and `spd` parameters;
- the joint count of the model (the first int of the bundle payloads).

Per model written here (`naka1`, `naka4`, `naka7`, `chappy`): the joint count and
parents, and the collision parts (id, code, joint, radius, centre), as for the
first 45 models (`standin_models.h`).

For the world models below (`standin_world.h`): the joint parents and the
collision parts (id, code, joint, radius, centre, or the platform `.mod` a part
names) of 15 models, and the numbers of the 31 `tekipara/*.bin` files and the two
boss `parms.bin` files, carried as words (a `tekipara` file is a version int,
eight ids and the type's int and float parameters in order; the counts differ per
enemy, so the words are not split).

Not read and not written: curves, geometry, textures, animation names, event keys
(sound and effect cues; their counts per info are 0 to 19), the `pelMgr` carry
weights, the plants' light-flare groups and anything else.

## What is written (`libs/nectar_assets/src/standin_objects.cpp`)

| Files | Notes |
|-------|-------|
| 48 `.anm` bundles and their AnimMgr tables (`anims.bin`, `.key`, `gate.bin`, `whiteN.bin`, `plants/*.bin`, `ufoparts/*.bin`, `ufo0705.bin`) | The 47 of the boot manifest and `gt_hard.anm` (counted on the generated tree, 2026-10-28; this file's own writer covers the 46 object sets, the manifest's Pikmin set is not checked here). Each info is a static identity DCK with the disc's frame count and keys, under invented names (`<id>_<nn>.dck`). |
| `ufoparts/naka1.mod`, `naka4.mod`, `naka7.mod` | One joint, one `cent` sphere. The disc's pellet table loads them by id. |
| `tekis/chappy/chappy.mod` | 17 joints and the disc's collision parts (`cent`, `head`, `body`, `slot`, `lleg`, `rleg`). |
| `parms/pelMgr.bin` | Twelve pellet configs: blue, red and yellow at 1, 5, 10 and 20. The carry numbers are this file's own, not the disc's. Then the Main Engine's, all the disc's (Tutorial stage, below), and the Whimsical Radar's (`uf01`, shape `fuzoku1`, ship part; 10 to 20 carriers, this file's own numbers, for `ship_part_carry`). Then six enemy corpses, for the stand-in enemies that leave one (`tkch` chappy, `tksw` swallow, `tkbe` beatle, `tkka` kabekuiA, `tkkb` kabekuiB, `tkot` otama): type 0, colour -1, model and shape id the enemy's own; carriers and seeds are this file's own numbers. `BTeki::dieSoon` halts the game when a dying enemy has no config, so `enemy_kills` (W7 #98) needs them. 20 configs in all (`pellet_table`). |
| `objects/pelAnim.bin` | 23 shape infos: the four pellets, the 16 ship parts the game always loads, and `un01`, `un04`, `un07`. With it the game loads those 20 resident shapes at stage start, as on the disc. |

Choices worth knowing:

- Event keys are dropped. They mostly produce sound and effect cues
  (`PaniAnimator` maps them to `KEY_PlaySound` and `KEY_PlayEffect`). Not all:
  the Onion's boot effect value 2 reopens its route way point, so the red boot
  keeps its five (Tutorial stage, below).
- One disc list is not ascending by frame (the Onion's `flowrot`: loop start,
  loop end, then an `Action0` on an earlier frame). `checkConstantKeys` fires such
  a key on the same animate call as the one before it, so the stand-in points it
  at that frame, which the writer accepts and which fires on the same tick.
- The disc's `naka1` table names three animations its bundle lacks (the game opens
  loose `.dck` files that do not exist and falls back to a dummy). The stand-in
  bundle has all of them. The three loose paths stay in the manifest allowlist.
- Sets whose model is not written (plants, gates, bosses, the other enemies) get a
  flat joint list (root, then every joint under joint 0) of the disc's joint
  count. The game stores the parents and never reads them back.

Tests: `nectar_standin_objects_test` (`libs/nectar_assets/tests/standin_objects_test.cpp`)
reads every table back through the game's `Parameters`, `AnimMgr::Parms` and key
types, walks every bundle through a transcription of `StdSystem::loadBundle` and
`AnimDck::read`, parses the models with `nectar::parseMod` and the collision text
through the game's `CmdStream`, reads the pellet tables with Parm fields declared
as in `PelletConfig` and `PelletAnimInfo`, and compares the file lists with the
boot manifest.

## World models and parameter files (`standin_world.cpp`, `standin_world_data.cpp`)

Models for what had animations and no model, so a generator can place any of them
without aborting. Colour-only boxes (`buildStandInModel`) on the disc's joints,
with a collinfo text built from the disc's parts (`worldCollInfo`):

| Group | Files |
|-------|-------|
| Plants | `objects/plants/{chidome,clover,hae,hutaba,ine,tanpopo}.mod` |
| Bridges and the push box | `objects/bridge/brd_test.mod` (37 joints), `brd_long.mod` (91), `objects/hinderrock/cube10.mod` |
| Soft gate | `objects/gate/gt_soft.mod` (4 joints; parts `gate`, `side`, `flag`, `flg1`-`flg3`) and `gt_soft.anm` |
| Bosses | `bosses/{mizu,snake}/*.mod`, `parms.bin` |
| Enemies | `tekis/{beatle,iwagon,kabekuiA,kabekuiB,otama,palm,swallow}/*.mod` |
| Collision platforms | `tekis/beatle/p_{body,l_wing,nozzle,r_wing}.mod`, `tekis/swallow/swalcol{b,h}.mod`: a box whose triangles are also the collision (`writePlatformMod`) |
| Enemy parameters | `tekipara/*.bin`, 31 files |

Notes:

- The gate's `gate.bin`, `gt_hard.mod`, `centcoll.mod`, `sidecoll.mod` and
  `gt_hard.anm` are the sandbox gate's (`standin_sandbox.cpp`); the object
  profiles no longer have the two gate sets. `gt_soft.anm` reuses the sandbox
  table's info names and frame count with the soft gate's four joints.
- `standInObjectModelSpecs()` now includes these, so the animation sets take their
  parents from the models.
- With `tekipara/chappy.bin` the Bulborb of `sandbox_objects` runs on the disc's
  parameters, not the code defaults.

The every-object stage `stages/sandbox_all_objects.ini`
(`tools/scenarios/standin_all_objects_stage.cpp`, a flat floor, a third hidden
`stages.ini` entry) places, from `default.gen`: the soft gate (`sluice soft`), six
plants (clover, chidome, hutaba, ine, tanpopo, hae), two bridges and a push box
(`work` objects `bridge test` shapes `bridge 4` and `bridge 13`, `move stone`
shape `stone 10`), the snake and mizu bosses and eight enemies (iwagon, chappy,
swallow, palm, beatle, kabekuiA, kabekuiB, otama). `scenario_objects_smoke_30` and
`_60` boot it for 300 ticks and check the eight enemies were born; every model,
platform, table and parameter file loads or the process aborts.

New generator kinds in `ObjectGenSpec`: `Plant` (`plnt`), `Work` (`work` v0.3) and
`Boss` (`boss` v2).

## The generators

`nectar::writeObjectGen` / `writeObjectGenFile` (same file) serialize a `.gen`
generator: an `item` (the Onion is `goal`, parameter `p00` its colour), a `pelt`, a
`piki` (a buried sprout with `pikiState` 0) or a `teki` (version 10, a
`TekiPersonality` with the code defaults). Area is a point, type is one thing. The
test reads the bytes with a transcription of `Generator::read` and the part reads
over the game's `RamStream` and `Parameters`, and with `nectar::parseGen`.

An Onion the player does not own is only born by a generator named `next`
(`GenObjectItem::birth` checks `playerState->hasContainer`), which is what the
stage uses.

## The object sandbox (`stages/sandbox_objects.ini`)

`tools/scenarios/standin_objects_stage.cpp`. A second hidden entry in
`stages.ini` (also `STAGE_TESTMAP`). `stages/sandbox.ini`, its map and its
generators are untouched, so every existing scenario keeps its numbers (the
`throw_release` calibration K stays 15 at 30 Hz and 18 at 60 Hz).

- Map: a 1200 by 1200 heightfield with 50-unit cells. Points from x = 100 are 200
  units higher, so across the one cell from x = 50 to x = 100 the ground rises
  200 over 50 units (about 76 degrees, face normal y about 0.24). Flat floor and
  plateau.
- Routes: a route block from x = 75 east keeps every route point off the cliff
  and the plateau (26 points, all on the floor). The face is too steep to link,
  so plateau points formed an island with no path to the Onion, and
  `WayPoint::initLinkInfos` ran the original depth-first `PathFinder::findSync`
  (no visited set) from each of their links to each goal until every simple path
  was exhausted: about 17 s of stage load, now none. Seeds that land up there
  plant without a route.
- `default.gen`: a blue Onion at (-120, -250), three buried sprouts (blue, red,
  yellow), a `pr01` pellet and a Dwarf Bulborb (chappy) at (-330, 300). The
  captain starts at the origin.

Scenarios opt in with `PIKMIN_SCENARIO_STAGE=stages/sandbox_objects.ini`. The CTest
entries `scenario_sprout_landing_30` and `_60` do.

## The Onion stage (`stages/sandbox_onions.ini`)

Written by `addObjectsStage` too (`standin_objects_stage.cpp`): the same compiled
map and `.ini` text as `sandbox_objects`, its own generator folder
`stages/sandbox_onions/`, and a fourth hidden `stages.ini` entry, appended last so
no other entry's index moves. `default.gen` places only Onions, as the disc's story
courses do (Forest of Hope's `default.gen`, read with `nectar::loadGen`, 2026-10-28:
red, yellow and blue `goal` generators with the default name and a yellow one named
`next` at the discovery spot):

| Generator | Colour | At (x, z) | Born when |
|-----------|--------|-----------|-----------|
| default name | red | (-120, -250) | red is owned |
| default name | yellow | (-280, -250) | yellow is owned |
| default name | blue | (-200, -400) | blue is owned |
| `next` | yellow | (-300, 0) | yellow is not owned |

Used by `onion_day3` and its variants, which load it twice (Day 2, then Day 3
through MapSelect). The stage opens `1.gen`, `2.gen`, `init.gen` and `plants.gen`,
which do not exist (allowlisted).

## The play stage (`stages/sandbox_play.ini`)

Written by `addObjectsStage` too (`standin_objects_stage.cpp`, issues #127 and #129):
the same compiled map and `.ini` text as `sandbox_objects`, its own generator folder
`stages/sandbox_play/`, and a hidden `stages.ini` entry appended last so no other
entry's index moves. `default.gen` is the object stage's three sprouts, `pr01` pellet
and Dwarf Bulborb, and one Onion of each colour from a generator not named `next`:
blue at (-120, -250), red at (-280, -250), yellow at (-200, -400).

Why: on the object stage a new game has only the blue `next` Onion, so a red squad
carrying the `pr01` pellet finds no goal and `ActTransport` aborts with `zannnen`, as
the original does when no Onion of that colour exists (#127). A `next` Onion added for
yellow stayed unbooted (`GoalItem::startAI`: no boot bit means `GOAL_BootInit` and a
closed route waypoint), so a carrier circled it and the game showed `TUT_Rute` (#129).
`free_play` is played on this stage, and owns all three Onions' container and boot
bits before the stage loads (`PcScenario::ownsAllOnions`), so each is born waiting
with its route open; it prints one line per Onion. A yellow Onion at (-300, 0) sat in
the carriers' way and stalled `pellet_carry`, so it is not there. Existing scenarios
stay on the object stage and keep their hash logs.

## sprout_landing (upstream #208)

`pc_port/scenarios/scenario_sprout_landing.cpp`. Asserts the stage's content, then
launches six seeds the way `GoalAI::BootEmit` launches them, from the cliff's
foot toward the face, and follows each through Flying, Bounce and Bury to Wait. It
asserts only what holds with or without #208 (written before it merged): every seed plants within 30 s, none
on a steep face (ground normal y at least 0.5), none below the ground, none more
than 30 units above it. The height and speed at the start of Bury, which #208
changes at 30 Hz, are printed per seed, not asserted.

Observed (macOS, both rates): all six seeds first bury at x = 37, about 13 units
short of the face, and plant on the floor with gap 0; the push off a steep face
never runs. On this 76 degree face the seeds bounce off the wall; the case of
upstream issue #65 (blue Pikmin planted in the air; it is an issue, the fix's PR number is not recorded here) (a seed landing on an edge whose collision normal reads as ground while the
face normal reads as wall) needs a surface built for it. At 30 Hz the branch still
reaches Bury on the floor (gap 0.0). Upstream #208 (keep the GameCube sprout
landing at 30 FPS) was merged upstream on 2026-10-28 and is in this tree. With it
the printed speed at the start of Bury is 1.8 to 3.1 instead of 0.0 (30 Hz, re-run
2026-10-28); the height gap is still 0.0 because the seeds first bury on the
floor, so this stage cannot show the mid-air case.

## Tutorial stage (`stages/practice.ini`)

`tools/scenarios/standin_practice_stage.{h,cpp}`. The Impact Site's own file name,
which the stand-in `stages.ini` already listed first with `id 0`, so the game takes
it as `STAGE_Practice` at index 0 and a new game's Day 1 there is the tutorial
(`isTutorial()`). Used by `tutorial_skip`, `tutorial_firsts` and `tutorial_day1`.
The other stages are unchanged.

What is placed, and in which file, follows the disc's `stages/practice/` (read
once, read-only, 2026-10-28; kinds, names and parameters, no positions):

| File | Generators |
|------|------------|
| `default.gen` | The red Onion (item `goal`, colour 1, facing 100), the blue and yellow Onions (colours 0 and 2, facing 180 and 320; generators not named `next`, so `GenObjectItem::birth` makes them only for an owned colour: never on a new game), the ship (item `ufo`, facing 256). |
| `init.gen` | The Main Engine (pellet `ust5`) and the push box (`move stone`, shape `stone 10`, `p02` 10 Pikmin, `p03` speed 50, pushed 270 units towards -z, unrotated). |
| `0.gen` (Day 1) | One red 1-pellet (`pr01`). The disc's has four, two red 5-pellets and four pellet posies. |

The positions are invented: a flat 1,600-unit floor, the captain at the origin, the
red Onion, the pellet and the box on one side, the engine and the ship on the
other, each far from the others' trigger radii and outside the route grid.

The route grid is 3 by 3 (`routeStep` 400). A carry plans its route with
`PathFinder::findSyncOnyon`, the original depth-first search with no visited set:
on the mapcore default (150-unit steps with diagonals, 81 points here) one pellet's
route search did not end in two minutes (a `sample` of the process showed
`selectWayOnyon` recursion). An Onion, the ship or the box on a route line blocked
the carriers (`TUT_Rute` opened), so all of them sit outside the grid.

What this stage needed that the tree did not have, each with the disc's numbers:

- `parms/demo.bin` (`standin_generator.cpp`): the demo-flag radii and timer, 60,
  25, 40 s, 30, 40 (code defaults 150, 100, 6, 30, 150). Before it was allowlisted
  as missing. Read only by tutorial and stage-gated checks; every other hash log is
  unchanged.
- The Main Engine's config in `parms/pelMgr.bin` (`standin_objects.cpp`): the disc's
  config 23 (type 3, colour -1, 20 to 40 carriers, dynamics 10 / 4 / 4, seeds 1 / 1,
  scale 1.65, height 30, sounds 1 / 1, ship joint 3). Every stage now registers it
  as a ship part at load; `ship_parts` skips registered ids, so it reports one part
  registered by the tree.
- The push box is solid: `objects/hinderrock/cube10.mod` is a platform whose
  collision is the 120-unit block its four `vtx` parts (the disc's, at +-60) bound
  (`StandInModelSpec::platform`, `standin_world.cpp`). Without it the pushers walked
  into the box, left `HinderRock::getPlaneFlag`'s face and dropped out of ActPush,
  and the box moved 7 units. The every-object stage's box is solid too; its hash log
  is unchanged.
- The Pikmin PickLoop motion (info 68) has the disc's timing (`standin_anims.h`): 30
  frames, KEY_Action0 at 10 (`ActTransport::animationKeyUpdated`, which
  `ActTransport::doLift` waits for), loop 13 to 18, KEY_Action1 at 20. Before it no
  pellet could be lifted on the stand-in tree.
- The red Onion's boot motion (goal info 4) carries the disc's five effect events
  (`ObjectAnimInfoProfile::effectKeys`): value 2 at frame 221 reopens the Onion's
  route way point, which the boot closed (`GoalItem::startAI`); 4 starts the colour
  fade; 0, 1 and 3 are particles. Without it a booted red Onion accepts no pellet.
  The blue and yellow boots keep none (no scenario boots them and then carries).
- `intro/map0.bti` (the Impact Site's area banner, null-checked).

`stages/practice/plants.gen` is allowlisted (guarded, as for the sandboxes).

## What remains

- A surface that makes a seed land on a steep edge (the real #65 case).
- The sprout is launched by the scenario, not by the Onion. Making the Onion shoot
  its own seeds needs its enter animation and a container state (`aiEnter`).
- Enemy, boss, plant and bridge behaviour is not asserted: they are placed and
  loaded and survive 300 ticks, nothing more.
- Models exist only for the objects in the boot manifest; the 23 other enemies, the
  other bosses, the other plants and the slopes have none.
- The plants' light-flare groups (`hae`) are not stand-ins.
- Event keys (sounds, effects) are not stand-ins, except the red Onion boot's (above).
- Disc-only checks: a `_disc` variant of `sprout_landing` does not exist, because
  the stage does not exist on a real install.
