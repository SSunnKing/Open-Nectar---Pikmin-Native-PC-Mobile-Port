# Render-to-sim coupling

The world sim and the world draw share a tick: the draw step decides part of the
sim. This note answers the question `rng-audit.md` raised: can the scenario
results or a real game depend on the window, the render settings or the render
timing? It covers the Pikmin head-look chain in detail and the camera culling that
gates it.

Short answer:

- **Scenario runs:** no. Every `--state-hash-log` of `throw_release`, `boot_smoke`
  and `day30_clock`, at 30 and 60 Hz, on the stand-in tree and on a real install,
  is byte-identical across window size, window aspect, render scale, the Aspect
  setting, a visible window and heavy CPU load (tables below). Deterministic mode
  pins every input that could differ.
- **Normal play:** yes, through the camera. The sim depends on the culling
  frustum, which on this port uses the window aspect. On a real install, with the
  normal-play culling, a 4:3 window and a 32:9 window give a different sim from a
  16:9 window within the first second of the stage (tick 21 and tick 7). The
  structure (AI culling and the head look gated by camera visibility and distance)
  is the original game's; the aspect input is the port's.
- **Local co-op split screen:** the world is drawn twice per tick, so the look
  timer ran at twice the rate (fixed, see "Co-op fix"; checked by the `coop_look`
  scenario) and the second view's camera decides the AI culling (unchanged).

No game code changed. A fix would change normal-play results (see Decision).

Measured 2026-10-28 on macOS arm64 (M3 Max, RelWithDebInfo, Unix Makefiles,
`PIKMIN_NATIVE_JAUDIO=OFF`, `PIKMIN_BUILD_SCENARIOS=ON`) at `ee769374`. Line
numbers are from that commit; function names are the stable pointers.

## Call structure

`System::run` (`system.cpp`) asks the frame scheduler for a number of logical
ticks. Each tick calls `PlugPikiApp::idle` (`plugPiki.cpp`) once, and `idle`
runs `update()` and then `renderall()` once. The world draw,
`GameCoreSection::draw`, calls `pikiMgr->refresh`, which calls
`ViewPiki::refresh` for each Pikmin. So in single-player there is one world draw
per sim tick in every scheduler mode:

| Scheduler mode | Ticks per loop | World draws per tick |
|----------------|----------------|----------------------|
| Fixed step (`frameScheduler.advance`), including catch-up | 0 or more | 1 (each tick is a full `idle`) |
| Monitor FPS (vsync locked) | 1 | 1 |
| Deterministic, unthrottled (scenarios, netplay) | 1 | 2 passes, see below |

No path skips `renderall` for a hidden or minimized window. A hidden scenario
window draws as a visible one does.

Deterministic mode (`pc_netplay_present_two_pass_active`, always on in a
scenario) draws twice per tick: an authoritative pass with a fixed sim camera,
then a presentation pass with the real camera. `pc_render_is_authoritative()` is
false in the presentation pass, so sim-side work in the draw runs once.

Local co-op split screen (`newPikiGame.cpp`, the `mSplitViews` loop) is the
exception: outside deterministic mode the whole world draw runs once per view,
two times per tick, and `pc_render_is_authoritative()` is true in both passes.
`mRenderPass` only stops the sound update in the second pass.

## The look chain

`ViewPiki::refresh` (`viewPiki.cpp:651`):

1. Camera visibility: `isPointVisible(mSRT.t, size * 4)`. Not visible calls
   `enableAICulling()`; visible calls `disableAICulling()` and sets `_528` to the
   camera distance (the LOD distance).
2. Head pose: `updateLook()` runs when all of these hold: (`useLOD` and
   `_528 < 1200` and `aiCullable()`) or not `useLOD`; a look target or a running
   `mLookTimer`; not Swallowed; not ExitMode; on the port,
   `pc_render_is_authoritative()`.

`Piki::updateLook` (`viewPiki.cpp:380`) turns the head, counts `mLookTimer` down
and calls `forceFinishLook()`, which clears `mLookatPosPtr`, `mLookTimer` and
`mLookAtCreature`.

The sim reads the result. `Piki::startMotion` (`piki.cpp:2013`) draws from the
sim RNG when `isLooking()` is false (`mLookatPosPtr` null), and
`startHimaLook` (`viewPiki.cpp:348`) draws again. `Piki::updateLookCreature`
(from `realAI`) draws when the nearest enemy differs from `mLookAtCreature`. So
whether `updateLook` ran on a tick changes how many sim draws happen later.
`aiCullable()` also gates Pikmin, enemy, boss, item, pellet and plant AI
(`aiAttack.cpp`, `aiBridge.cpp`, `tekibteki.cpp`, `Boss.cpp`, `pelletMgr.cpp`,
`mapMgr.cpp`), so the culling result reaches far more than the look chain.

## Inputs that can differ between runs

| Input | Normal play | Scenario (deterministic) | GameCube |
|-------|-------------|--------------------------|----------|
| Camera position and direction | Decides visibility and `_528` | Ignored: `pc_netplay_sim_visible` returns true, `pc_netplay_sim_lod_distance` returns 0, `aiCullable()` returns true, and the sim pass's `isPointVisible` returns true | Decides visibility and `_528` |
| Frustum aspect | The window aspect (hor+), or the Aspect setting when it is not Auto; 4:3 when the 4:3 menu layout is on (`CullFrustum::update`, `camera.cpp:322`) | Ignored (as above) | 4:3 (callers pass 640/480) |
| Window size at one aspect | No effect: the frustum reads only the aspect | No effect | - |
| Render scale | No effect | No effect | - |
| Hidden, minimized or visible window | No effect on the tick structure | No effect | - |
| Vsync, frame pacing, CPU load | One draw per tick either way; but `mDeltaTime` is the wall clock (clamped to 1/30 s), so slow frames change the whole sim | No effect: fixed tick length, one tick per loop | Wall clock, normally steady |
| Co-op split screen | Two draws per tick: `updateLook` twice before the fix, once after; last view's culling wins | A co-op scenario (`PcScenario::coop`) turns the two-pass frame off and draws both views; the pins still decide culling; `coop_look` checks one look step per tick | No co-op |

The draw count per tick and the frame timing cannot change the look chain in
single-player. The camera can, as on GameCube, and the window aspect can, unlike
GameCube.

## Measurements

Each run is `nectar --scenario <name> --scenario-tick-rate <30|60>
--state-hash-log <file>`, compared byte for byte with the 64x36 hidden run of the
same scenario and rate. "Identical" means all six logs (three scenarios, two
rates) match. Stand-in runs use `stages/sandbox.ini`; disc runs use a real
install, read only.

**Shipped build (deterministic, as every scenario runs):**

| Condition | Stand-in | Disc |
|-----------|----------|------|
| Window 48x36 (4:3) | identical | identical |
| Window 128x36 (32:9) | identical | identical |
| Window 640x360 | identical | not run |
| Window 1280x720 | identical | identical |
| `--scenario-visible`, 64x36 | identical | not run |
| `--scenario-visible`, 640x360 | identical | identical |
| 12 busy-loop processes on the CPU | not run | identical |

**Experiment build** (local, not committed): environment switches for the render
scale, the Aspect setting and vsync in the scenario settings, and `G4_NORMAL_CULL=1`,
which turns off the deterministic visibility, LOD, `aiCullable` and two-pass
pins, so the sim sees the live camera as in normal play while the tick length
stays fixed.

| Condition | Stand-in | Disc |
|-----------|----------|------|
| Deterministic, switches unset | identical to the shipped build | identical to the shipped build |
| Deterministic, render scale 0.25 or 2 at 640x360 | identical | not run |
| Deterministic, Aspect 4:3 or 21:9 | identical | not run |
| Deterministic, vsync on, visible 640x360 | identical (whether vsync was applied was not confirmed) | not run |
| Normal culling, 64x36, run twice | identical to each other | identical to each other |
| Normal culling against deterministic, 64x36 | `throw_release` `piki` from tick 72 (30 Hz) and 90 (60 Hz); `boot_smoke`, `day30_clock` identical | `item` from tick 6 and `teki` from tick 51 in all six; `piki` from 56 or 62 in `throw_release`; `navi` later in `throw_release` and `boot_smoke` |
| Normal culling, 48x36 (4:3) against 64x36 (16:9) | identical | `item` from **tick 21**, all six logs |
| Normal culling, 128x36 (32:9) against 64x36 | identical | `item` from **tick 7**, all six logs |
| Normal culling, Aspect 4:3 in a 64x36 window | identical | identical to the 48x36 window |
| Normal culling, 640x360 at render scale 0.25 | identical | identical to 64x36 |

Every run exited 0. `throw_release` measured K = 15 at 30 Hz and 18 at 60 Hz in
every disc run with normal culling. The `rng` column never differed within these
runs: the aspect changed item state (AI culling), not the look-chain draws,
inside the scenario windows. The stand-in sandbox keeps every object near the
captain, so its result does not depend on the aspect even with live culling.

## When the coupling bites

1. Normal single-player play on any window or Aspect setting that is not 4:3:
   the culling frustum is wider than GameCube's, so objects at the side edges of
   the screen stay AI-active and their heads keep turning where GameCube would
   cull them. The results differ from GameCube and from a 4:3 window. The 4:3
   Aspect setting (or the 4:3 menu layout) gives GameCube's frustum.
2. Normal play at any aspect: camera movement changes the sim, as on GameCube.
   That is original behaviour, kept.
3. Normal play with uneven frame times: `mDeltaTime` follows the wall clock, so
   render cost changes the sim through the tick length. Original structure; on
   PC render cost varies more.
4. Local co-op split screen: `updateLook` twice per tick (fixed; `coop_look`
   shows it with the gate removed: the look timer drops by two per tick) and the
   second view's culling and LOD (inferred from the code, not run).
5. Scenarios and netplay (deterministic mode): never. The policy pins remove the
   camera, and the fixed tick removes the timing.

The earlier one-tick shift in `rng-audit.md` (removing the dead draw at
`viewPiki.cpp:355`) is therefore not a render-timing effect: in that run's
deterministic mode `updateLook` runs on every tick it is allowed to by sim state
alone. The shift is a consequence of the removed draws; its exact mechanism was
not traced.

## Decision

Decided by the fork owner on 2026-10-28.

- **Aspect: keep today's behaviour.** The sim keeps judging visibility with the
  window's own aspect. Matching GameCube's 4:3 culling would make the sim identical
  across window shapes, but it would also stop drawing every enemy that stands in the
  side strips of a wide window while it is on screen (see "Fix attempt" below).
  A wide window is meant to show more of the world and keep it alive, so widescreen
  play must not get worse. The price is documented here: the same recording can
  play out differently in windows of different aspect, because what counts as on
  screen decides which objects get the full AI update. The 4:3 Aspect setting gives
  GameCube's culling.
- **Co-op:** the second view's double `updateLook` is fixed separately (see "Co-op fix" below).
- **Moving the look draws to the cosmetic stream:** not done. It changes the sim
  RNG sequence and therefore GameCube fidelity.

## Fix attempt: sim frustum at the caller's aspect (not adopted)

Branch `wip/aspect-cull` (2026-10-28, kept for reference, not merged) adds a second set of culling planes to
`CullFrustum`, built at the aspect the game passes to `update()` (640/480 for
the gameplay and movie cameras, as on GameCube). Every visibility test that
writes sim state calls `isPointSimVisible` (Pikmin, `DualCreature`, the two
`ItemCreature` paths, UFO, Onion, boss, teki); draw culling keeps
`isPointVisible` and the hor+ planes. The draw planes are restored bit for bit
after the sim planes are built, and when the two aspects are equal (a 4:3
window, the 4:3 menu layout) `isPointSimVisible` is `isPointVisible`. The
movie camera passes its stored 4:3 back, and the camera AI framing
(`NCamera::getAspect`) reads the 4:3 value. The item draw and the Onion spot
effect follow the draw frustum.

Measured on the disc install at 30 and 60 Hz, all six logs each:

| Check | Result |
|-------|--------|
| Shipped pins, 640x480, base `57c9e1a4` against the fix, stand-in and disc | identical; K = 15 / 18 in both |
| `ctest -E _disc` and `ctest -R _disc`, base against the fix | same 122 and 6 passes; all 42 stand-in hash logs identical |
| Pins off, 640x480, base against the fix | identical |
| Pins off, base: 48x36, Aspect 4:3 against 640x480 | identical |
| Pins off, base: 64x36, 640x360 at render scale 0.25 or 2 | `item` from tick 21 |
| Pins off, base: 128x36, Aspect 21:9 in 64x36 | `item` from tick 7 |
| Pins off, fix: 48x36, 64x36, 128x36, Aspect 4:3 and 21:9, render scale 0.25 and 2, against 640x480 | identical |

With the pins off, `throw_release` measured K = 10 / 21 at 4:3 and 16:9 and
7 / 14 at 32:9 on the base build, and 10 / 21 at every aspect with the fix.

**Why it was not adopted.** The original game hides an AI-culled enemy:
`BTeki::refresh` skips `drawTekiShape` when the teki is culled and no Pikmin
or captain is in a neighbouring AI grid cell, and `BossMgr::refresh` skips
`drawShape` for a culled boss. On GameCube culled means off screen. With the
sim at 4:3 in a wider window, enemies in the side bands are culled but on
screen, so they disappear at the 4:3 edges. Counted with a local probe (teki
culled for the sim but inside the draw frustum): about 2 per tick at 16:9
(1800 in 900 ticks of `boot_smoke`) and 5 per tick at 32:9; no boss in these
scenarios. Drawing them needs a draw-only teki and boss path that neither
advances animation (`BaseShape::updateAnim` animates in the draw) nor writes
collision or platform state, and such enemies would still stand frozen until
they enter the 4:3 area. Co-op has the same coupling: taking the sim culling
from view 1 would hide enemies in view 2. The owner decided not to ship it.
## Co-op fix

The double look update in split screen is fixed (2026-10-28); the culling is
not changed. The view loop in `newPikiGame.cpp` records the view pass with
`pc_coop_set_view_pass` (`pc_coop.h`), and `ViewPiki::refresh` and
`Navi::updateHeadMatrix` call `updateLook()` only when
`pc_coop_first_view_pass()` is true. Both views still apply the head-turn
matrices, so the look timer and the end of a look run once per tick and the
head turns at the single-player speed. The AI culling flag and `_528` still
come from the last view drawn, as before.

Single player and netplay always run pass 0. Against a base build of
`57c9e1a4`: the same 122 stand-in and 6 disc `ctest` passes, all 44 stand-in
hash logs that `ctest` writes are identical, K = 15 / 18, and the
`boot_smoke`, `throw_release` and `day30_clock` logs at 30 and 60 Hz are
identical on the stand-in and the disc tree at 64x36 and 640x480. A
throwaway harness ran the view loop with the real `pc_coop.h` gate: a
10-tick look ends at tick 5 with two ungated views and at tick 10 with the
gate. Real co-op play was not run (scenarios rejected `--coop`).

Since 2026-10-28 the `coop_look` scenario (`scenario_coop_look_30`) runs it in
the game: two captains in split screen with both views drawn each tick (the
deterministic two-pass frame off for co-op scenarios, `tools/scenarios/README.md`,
Co-op scenarios), and a 12-tick look on each captain and on a Free blue Pikmin.
Each timer drops by one per tick and each look ends on tick 12, in two rounds.
With `pc_coop_first_view_pass()` forced true (local, not committed) all three
timers read 10 after the first tick and the scenario fails there.

## Regression coverage

The co-op look fix: `coop_look` (above). Otherwise no new test. The deterministic pins are already unit-tested by
`pc_netplay_policy_test` (`pc_netplay_sim_visible`, `pc_netplay_sim_lod_distance`
on and off). A stand-in hash comparison across window aspects would pass even
with the pins removed (the sandbox is aspect-insensitive, table above), so it
would guard nothing. The disc comparison does detect it but cannot run in CI.
