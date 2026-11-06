# Verification log: items the stand-in notes left unverified

Experiments run on 2026-10-28, macOS arm64 (M3 Max), RelWithDebInfo, `VERSION_GPIE01_01`, hidden window,
`SDL_AUDIODRIVER=dummy`, hang guard `perl -e 'alarm N; exec @ARGV'`. "Stand-in tree" is the
`nectar_standin_generator` output of this branch. "Disc clone" is an APFS clone (`cp -cR`) of a real install's
`assets/`, changed as stated; the install itself was only read. Scenario runs on a disc clone boot
`stages/stage1.ini`. Exit 134 is `[PANIC]` then `abort()`, 139 is SIGSEGV, 142 is the SIGALRM hang guard.

## Results

| # | Item | Method | Result | Evidence |
|---|------|--------|--------|----------|
| 1a | Native JAudio boot without `pc_scenario_pre_stage`'s `Jac_SceneSetup(SCENE_Title)` hangs | `PIKMIN_NATIVE_JAUDIO=ON` build with the call disabled (`#if ... && 0`, reverted after), `boot_smoke` 30, alarm 60, `sample` at 40 s | **Confirmed, on both trees.** Stand-in tree: exit 142, last open `courses/sandbox/sandbox.mod`. Disc clone: exit 142, last open `courses/stage1/forest.mod`. Both main-thread stacks: `main` > `System::run` > `PlugPikiApp::idle` > `GameFlow::softReset` > `OnePlayerSection` > `NewPikiGameSection` > `NewPikiGameSetupSection` > `Jac_SceneSetup` (+116/+120, the `first_load` spin in `piki_scene.c`), `sched_yield` / `steady_clock::now` | The disc case is the one `standin-audio.md` had only read from code; it hangs the same way |
| 1b | Stand-in tree with the call passes all three scenarios with audio ON | Same build, call restored, each scenario at 30 and 60 | **Pass, 6 of 6** (exit 0). `throw_release` K = 15 at 30 Hz, 18 at 60 Hz, as in the OFF build. Only extra `FAILED to open`: `SndData/Banks/pikibank.bx` (allowlisted). Restored build re-checked with `boot_smoke` 30 after a rebuild: pass | |
| 2 | The stand-in `con01.blo` container-menu panes (`p264` window, `ws8c`, `p2c4`, `ws8u`, `ws8l`) work in `DrawContainer::start` | New permanent scenario `container_menu` (object stage): the captain walks onto the Onion's `cont` sphere, clicks A, the menu runs 4 s in Operation with a stick push (nothing to transfer), closes with B; again closing with A | **First run failed: the menu could not be opened at all.** The stand-in Onion's `cont` sphere was at y = -290 (290 below the ground) and `cent` at -16.7, because the disc offsets are relative to joints the disc rig places 291.5 and 109.8 up and every stand-in joint sits at the origin. **Fixed** (`standin_models.cpp`: `cont` y 1.5, `cent` y 93.1, the disc's world heights). Then: **pass at 30 and 60**, both cases reach Container, see statuses Wait, Start and Operation, return to Walk, Onion count unchanged, no abort | `sprout_landing` and `onion_skip` still pass at 30 and 60 after the fix; the other Onion spheres (`leg*`, `bas*`, `piki`, `eff*`, `llit`) keep the disc offsets on the identity rig and so sit lower than on the disc (no scenario depends on them) |
| 3a | Missing `consFont.bti` on the real install | Disc clone without it, `boot_smoke` 30; lldb for the stack | Exit **139**, as on the stand-in. SIGSEGV in `Font::setTexture` from `System::Initialise` (null texture), before any stage | Matches the stand-in result and the prediction |
| 3b | Missing `effects/shadow.txe` | Disc clone without it | Exit **139**, SIGSEGV in `GameCoreSection::GameCoreSection` from `NewPikiGameSetupSection` | Matches |
| 3c | `effects/tex/effects.bun` entry `ps_glow.bti` removed | Bundle rewritten without the entry (`.bun` layout of `StdSystem::loadBundle`) | Entry only: exit **0**, pass (the disc also ships a loose `effects/tex/ps_glow.bti`, used as fallback). Entry and loose file: exit **134**, `Cannot open file. [effects/tex/ps_glow.bti]` | The stand-in tree has no loose copies, so it aborts on the entry alone; same abort message |
| 3d | Whole `effects/tex/effects.bun` deleted | Disc clone | Exit **0** (all 59 entries also exist loose). With `star8_i.bti` also deleted: exit **134**, `Cannot open file. [effects/tex/star8_i.bti]`, the stand-in's first missing texture | Matches the stand-in once the loose fallback is removed |
| 3e | `hokori4.bti` entry and loose file removed | Disc clone | Exit **134**, `Cannot open file. [effects/tex/hokori4.bti]` (the open failure, not `NO TEX`) | Matches the stand-in |
| 3f | `screen/eng_tex/screen.bun` entry `0_64.bti` removed | Disc clone | Entry only: exit **0** (loose `screen/eng_tex/0_64.bti` exists). Entry and loose file: exit **134**, `can't load texture [0_64.bti]` | Same abort as the stand-in (which has no loose copy) |
| 3g | `effects/halowhit.txe` is not needed (null-checked, setting off by default) | Open log of the unmodified disc clone; clone without the file | **Not opened** on the boot path; deleting it: exit 0 | Prediction holds |
| 3h | `rootRing.txe` is `#ifdef WIN32` only | Open log; clone without it | **Not opened** on macOS; deleting it: exit 0 | Prediction holds for macOS; Windows not run |
| 3i | `intro/map1.bti` | `PIKMIN_LOG_FILE_OPENS` on the unmodified disc clone | **Opened (OK)** on the stage 1 boot (the stage banner). Deleting it: one FAILED line, exit 0 | The sandbox (stage id 5) never opens it, as recorded before |
| 3j | Negative glyph widths in `consFont` | lldb: breakpoint on `Font::setTexture` (second call, `System::Initialise`), read cell size and `FontChar::mWidth` of all 128 glyphs after it returns. Scratch stand-in trees with a replaced `consFont.bti` | Disc font: IA4 256x128, cells 16x16, widths **-15 to 16** (blank glyphs already come out negative on the disc). Stand-in IA4 128x64 alpha 0xF: cells 8x8, all widths **8**. All-zero alpha 128x64: all widths **-7** (= 1 - cell width, as read from the code). IA4 256x128 alpha 0xF: cells 16, widths -15 to 16. IA4 64x32: cells 4, widths -3 to 4. IA4 128x4: cell height **0**, widths 8 (the `y = -1` read happened and did not fault). Every variant passes `boot_smoke` | What cannot be observed headless: how a negative width draws (the console font is drawn only by debug overlays, none of which a scenario shows). Not explained: why the all-opaque 256x128 and 64x32 files give some negative widths. Side finding: the first `setTexture` call (`bigFont.bti`, stand-in) gives widths -23 to 24, so some stand-in bigFont glyphs are blank; drawing only |
| 4 | Code defaults vs disc values for every ID the v1 scenarios touch | `parms/gamePrms.bin`, `naviMgr.bin`, `pikiMgr.bin` of the install decoded read-only with the `Parameters::read` layout and diffed against the header defaults (with the `NaviProp` / `PikiProp` constructor overrides) and the stand-in files | See [Parameter diff](#parameter-diff-item-4). All stand-in values that are written equal the disc. Two comments were wrong (the CreatureProp "code" values ignored the constructor overrides); fixed. Pikmin walk, run and escape animation speeds (`p22`, `p23`, `p25`, read by the Pikmin walk animation in `throw_release`) differed with no comment: now written with the disc values. Every other difference now has a comment in `parmFiles`. Correction (throw_flight): `p48` flower gravity was missed and is now written (see the diff below). `throw_release` K stays 15 / 18 | |
| 5 | No scenario state waits on an animation key the stand-ins lack | Code scan of `procAnimMsg` handlers (below), then every stand-in scenario at 30 and 60 with the new `PIKMIN_SCENARIO_STATE_STATS` (longest stay per captain and Pikmin state) | **No hang.** All 14 runs pass. Every state that was entered also left, except Walk and Idle at the end of a run. See [State dwell](#state-dwell-item-5) | Idle (17) runs to the end of `boot_smoke` and `day30_clock` because the scenarios give no input; `NaviIdleState::exec` leaves on any button or stick, not on a key |
| 6 | G1: stand-in `aiconst.bin` equals the disc; extinction chain on the stand-in tree | Read the disc's `parms/aiconst.bin` read-only (decoded with the `Parameters::read` layout) and diffed all 23 IDs against `AIConstant::Parms`; re-ran every stand-in scenario with the old and the new file and compared the per-tick state hashes; traced the extinction chain in code (`GameMovieInterface::parse`, `NaviPikiZeroState`, `MessageModeState`, `moviePlayer.cpp` tables) and the disc's demo46 and demo47 `.cin` keys | aiconst: the disc differs from the code default in `p00` (560, already written), `p01` 0, `p08` 0, `p10` 0, `p13` 0 and `p19` 5; now written. `p21` 29 is the code default. All 15 stand-in runs hash-identical to the old file; K 15 / 18 on stand-ins and disc; table in [standin-aiconst.md](standin-aiconst.md). Extinction: stand-in `demo46.cin` and `demo47.cin` added, the new `extinction` scenario passes at 30 and 60 Hz and fails without them (captain re-enters PikiZero 17 / 31 times). No new open-allowlist entry was needed | Not verified: the behaviour of `p10`, `p13` and `p19` (no scenario exercises them); a Windows or Linux build; the extinction chain on a disc run with `PIKMIN_LOG_FILE_OPENS` (the file list comes from the code and the stand-in run, not a disc open log) |
| 7 | Stand-in `pikiMgr.bin` equals the disc (Kyle's decision, 2026-11-04) | Read the disc's `parms/pikiMgr.bin` read-only (decoded with the `Parameters::read` layout), wrote all 77 Parms IDs and the five CreatureProp values in the disc's order, `cmp` against the disc; full suite against the tree before the change (176 hash logs); the suite re-run with the old file plus one group of disc values at a time to name each log's cause | 35 IDs newly carry the disc value (health, flick knock-down, idle search ranges, sprout timers, drown, press, panic, bomb); the generated file is byte-identical to the disc's. 99 of 176 hash logs changed, 77 identical; causes per log in [standin-piki-parms.md](standin-piki-parms.md). `piki_hazard_deaths`, `onion_unlock_whistle` and `tutorial_firsts` had the code values built into a damage amount, sprout bounds and the captain driver; changed, passing with both files. `bulborb_fight` Flick 1.8 -> 3.3 s (disc 2.9 s); K 13 / 22 unchanged | Not verified: a Windows or Linux build; the new values on a disc run (the disc already has them). Golden shots `world_map_idle` and `world_map_confirm` changed through `p51` alone (the cursor's red Onion icon, see standin-piki-parms.md); refreshed from CI |

## Parameter diff (item 4)

`gamePrms.bin` (stand-in: empty). Disc differs from the code in `p00` daySpeed 27 (code 20), `p01` 5.25 (7), `s01` 7 (8),
`p02` 8 (9), `p03` 15 (17.5), `s03` 16.5 (18.5), `p04` 19 (19.5), `p05` nightWarning 18 (16). Equal: `t00` 7, `t01` 19,
`p06` 18.5, `x99` 1. No change: `p00` is overwritten on PC from the day-length setting (`gameCoreSection.cpp`, 27 by default),
the lighting blend ids do not touch the sim, and `day30_clock` starts Day 30 at 18:48, after both night-warning values.

`naviMgr.bin`. CreatureProp: effective code defaults 1.0, 0.5, 0.5, 0.2, 0.3; disc and stand-in 1.0, 0.5, 0.5, 0.1, 0.3.
The 27 Navi ids the stand-in writes all equal the disc. Disc values left at code defaults (none on a v1 path): `p01`-`p03`
whistle, `p29`/`p30` escape animation frames, `p31` pressed time, `p33` push start time, `p42` pluck loop count,
`p49` impatience time, `p51` punch damage, `p57`-`p59` Kinoko flick and bury escape, `p61` pluck zoom-out.

`pikiMgr.bin`. CreatureProp: effective code defaults 0.1, 0.5, 0.25, 0.1, 0.3; disc and stand-in 0.1, 0.5, 0.8, 0.14, 0.3.
The 10 Piki ids the stand-in wrote all equal the disc; `p22` 50 (code 60), `p23` 40 (30) and `p25` 60 (30) are added.
Correction: `p48` mFlowerGravityScale (disc 0.5, code 0.01) was left at the code default but is on a throw path: a thrown
flower Pikmin falls at that fraction of gravity after the peak, so on the stand-in it glided 48 ticks instead of 33 (30 Hz)
and 122-123 instead of 71 (60 Hz), with the same peak (`throw_flight_flower`). Now written as 0.5; the flower throw matches
the disc tick for tick. Still left at code defaults and on a flower path, not yet measured: `p06` flower energy (disc 1,
code 2), `p51` grow-up time (disc 120, code 4), `p52` wilt time (disc 180, code 4).
Not a parameter: the yellow throw on the stand-in gave 22 and 27 flight ticks for the disc's 37 (30 Hz) and 55 for 77
(60 Hz) on two of its 18 throws because the scratch spot (-228,-276) was 30 units from the stand-in red Onion; the yellow
Pikmin's +104 peak carries it onto the Onion (landing 85-108 over the map), which the lower leaf, bud and flower throws pass
under. Grab, release, launch speed 370 and peak 104.40 already matched the disc. `throw_flight` now fails a landing more
than 1 over the map (OFF GROUND), and from the stand-in spot `-350,350` all 18 yellow throws match the disc tick for tick:
grab 1/2, release 2/3, flight 37/77, peak 104.40/113.16, launch 370 at 30/60 Hz.
Left at code defaults (none on a v1 path): health and attack, press factors, seed timers, wall push, flick,
panic, bomb, drown, free-boid time, idle attack and work search ranges, sunset safety range.
Update (item 7, 2026-11-04): the stand-in file now carries every disc value; see standin-piki-parms.md.

## State dwell (item 5)

States whose `procAnimMsg` waits on a key (`naviState.cpp`, `pikiState.cpp`): on `KEY_Action0` the captain's Nuku, Attack,
Throw and ThrowWait, and the Pikmin's Nukare, AutoNuki, GrowUp, LookAt, Wave and KinokoChange; on `KEY_LoopEnd` the
captain's ThrowWait (charge level), Push, PushPiki, Release, Gather, Bury, Starting, and the Pikmin's Hanged, WaterHanged,
Cliff, Push, PushPiki and Absorb; most others on `KEY_Finished`, which a one-shot motion always sends. The stand-in
Pikmin table has `KEY_Action0` only on Throw and ThrowWait (and three disc-timed actions); every other motion is a 40-frame
one-shot with no loop keys.

Longest stay in ticks (entries) per run:

| Scenario | 30 Hz | 60 Hz |
|----------|-------|-------|
| boot_smoke | captain Walk 298, Idle 601 (to the end) | Walk 598, Idle 301 (to the end) |
| throw_release | captain Walk 273, ThrowWait 16 (3), Throw 9 (3); Pikmin Normal 250, GoHang 15 (3), Hanged 3 (2), Emotion 69 (3) | Walk 544, ThrowWait 34, Throw 18; Normal 444, GoHang 31, Hanged 3, Emotion 138 |
| day30_clock | captain Walk 298, Idle 847 (to the end) | Walk 598, ThrowWait 1, Attack 78, Idle 195 |
| sprout_landing | captain Walk 287 | Walk 553 |
| onion_skip | captain Walk 64 (5), DemoWait 329 (4) | Walk 124, DemoWait 659 |
| container_menu | captain Walk 52 (2), Container 155 (2) | Walk 104, Container 310 |
| objects_smoke | captain Walk 290, Pressed 9 | Walk 299 |

Notes: Hanged waits on `KEY_LoopEnd` but leaves on the throw, not on the key. Attack at 60 Hz in `day30_clock` is an A
press (meant for a ship text) landing after the text closed; it ends on `KEY_Finished` after 78 ticks, the 40-frame
one-shot (on the disc the punch has its own length and an Action0 hit). Emotion and DemoWait end on `KEY_Finished` and the
movie. The `objects_smoke` Pressed stay at 30 Hz only is a captain stepped on by a placed object; Pressed runs on timers.
Not covered by any scenario, so not checked: Nuku, NukuAdjust, Nukare, AutoNuki, GrowUp, LookAt, Wave, KinokoChange,
Push, PushPiki, Release, Gather, Bury, Starting, Cliff, Absorb, WaterHanged. A scenario that reaches one of them on the
stand-ins needs the key it waits on (Action0 or LoopEnd) in the stand-in table, or it will stay in that state.

## Tests added or changed

- `scenario_container_menu_30` and `_60` (stand-in, object stage).
- `scenario_extinction_30` and `_60` (stand-in, sandbox; G1).
- `PIKMIN_SCENARIO_STATE_STATS` in the runner (no CTest entry; a diagnostic switch).

## Not resolved

- 3j: the draw-side effect of negative glyph widths, and the negative widths of the all-opaque 256x128 and 64x32 fonts.
- 3h: `rootRing.txe` on a native Windows build (not built here).
- Item 5: the states listed as not covered.
