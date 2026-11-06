# scenarios

Scenario test harness. A scenario boots the real game code straight into a
stage with no window on screen, feeds pad input from a script, reads game
objects directly and exits with a pass or fail. It lives in two places:
`pc_port/scenarios/` (the runner and the scenarios, compiled into `nectar`) and
this folder (the stand-in asset generator, the CTest runner script, the manifest
and the GL probe).

## Why it exists

The disc data cannot be shipped, so CI has no game assets. Scenarios therefore
run on **stand-in assets**: files that `nectar_standin_generator` writes from
code, with the same formats the game parses and none of the disc's content. The
game code under test is the real one. The same scenarios also run against a real
install (the `_disc` tests) when you have one.

## Build

Scenarios are off by default. Configure with the scenario option and native
JAudio off (the legacy audio facade needs no sound data). Run these from the
repository root. `-G Ninja` needs Ninja installed; leave it out to use the
default generator (the commands in this note were run with Unix Makefiles;
`-G Ninja` was not run, because Ninja is not installed on that Mac):

```sh
cmake -S games/pikmin1 -B build/pikmin1-scenarios -G Ninja \
    -DPIKMIN_NATIVE_JAUDIO=OFF -DPIKMIN_BUILD_SCENARIOS=ON
cmake --build build/pikmin1-scenarios --target pikmin_pc nectar_standin_generator
```

`PIKMIN_BUILD_TESTS` defaults to ON, so this configure also registers the unit
tests: `ctest -N` lists 128 entries, 50 of them `scenario_` entries (44 stand-in, 6 disc). Most test
executables are not built by the two targets above; CTest reports them as missing
and `-R ^scenario_` leaves them out.

The same scenarios also run with the original audio engine: configure a second
build with `-DPIKMIN_NATIVE_JAUDIO=ON`. The generator writes a stand-in
`SndData/Seqs/pikiseq.arc` that lets the engine boot with no sound data; see
[standin-audio.md](standin-audio.md).

`pikmin_pc` produces `build/pikmin1-scenarios/bin/nectar`.
`nectar_standin_generator` is a separate executable, so release packaging never
ships it. `PIKMIN_BUILD_SCENARIOS` also calls `enable_testing()` and defines
`PIKI_SCENARIOS=1`; every scenario source is wrapped in `#if PIKI_SCENARIOS`.

## Golden screenshots

`scenario_screenshot_goldens` captures six 640×480 pictures: the boot scene,
title prompt, title menu, file select, and idle and confirmed world maps.
`golden_shots.py` owns the shot table used by CTest, refreshes and before/after
captures. Expected images live in `golden/linux/`, `golden/macos/` and
`golden/windows/`. These are stand-in assets. Disc-quality pictures require a
local disc install or the self-hosted runner tracked by #47.

The check is blocking on all three operating systems. Each pixel's largest
channel difference may be at most 4; no pixels may exceed that threshold
(`golden/tolerance.json`). Wrong dimensions, blank pictures and missing goldens
also fail. A deliberate `PIKMIN_GOLDEN_ADVISORY=1` run reports differences as
warnings; normal CI passes `0`. A developer GPU may differ from the hosted
runner, so local advisory output is not permission to replace hosted goldens.
Failed comparisons upload the actual
pictures and expected/actual/difference composites as `scenario-screenshots-*`.

Flake measurement: [run 41815060332](https://github.com/knakamura13/native-macos-pikmin-1/actions/runs/41815060332),
2026-11-04, branch `golden-flake/r1`, head `5d2e25f27` lineage. Each OS had
20 fresh-runner captures, six pictures per capture (360 pictures total):

| OS | Captures with metrics | Captures with a mismatch | Largest channel difference | Pixels differing by more than 2 |
|---|---:|---:|---:|---:|
| Linux, Ubuntu 24.04 llvmpipe | 20 | 0/20 | 0 | 0 |
| macOS 26 arm64 | 20 | 0/20 | 1 | 0 |
| Windows MinGW/Mesa | 20 | 0/20 | 0 | 0 |

The measured macOS variation affected at most 0.004% of a picture and was one
channel value. The selected threshold of 4 leaves room above that noise while
rejecting layout changes. Twenty clean runs measure this sample, not a guarantee
against future driver changes. The one failed Windows scenario job in the same
run was the Python fake-game launch self-test, not a golden comparison; #151
fixes that fixture. The temporary 60-job flake matrix and dispatch input were
removed after collecting the measurement.

Linux uses `ubuntu-24.04` and the Mesa package version in `golden/linux/MESA_PIN`.
If the archive cannot supply that version, CI warns and reports the actual
version; golden mismatches remain blocking. Refreshes record the runner image,
source revision and driver/package information in each OS's `PROVENANCE.txt`.

For an intentional visual change, run `bash games/pikmin1/tools/scenarios/refresh_goldens.sh`
on the branch to update. It pushes a temporary `golden-refresh/**` branch,
downloads the three runners' `golden-update-*` artifacts, then removes the remote
branch. With `--run RUN_ID`, it imports an existing refresh run. Review the PNGs
and provenance before committing `golden/`. CI validates the complete six-image
set before uploading an update artifact. The helper validates all three OS sets,
provenance and the Linux Mesa pin before replacing any local golden.
This helper waits for CI; unattended
agents should start the refresh, retain its run ID, and resume when it finishes
rather than polling or repeatedly retrying unavailable runners.

`.github/workflows/screenshot-before-after.yml` builds the PR base and head on
Linux llvmpipe, captures the same shots and uploads `screenshot-before-after`:
expected, actual and difference panels plus a Markdown summary. It runs for
rendering, stand-in and capture-tool changes, or by manual dispatch where the
workflow is available on the default branch. A fork-token comment failure does
not discard the artifact or fail an otherwise successful comparison.

## Run

Through CTest. CTest does not build: build the two targets above first. The
`scenario_standin_generate` entry then writes the stand-in tree and the scenario
entries run on it:

```sh
ctest --test-dir build/pikmin1-scenarios --output-on-failure -R ^scenario_ -LE game-assets
```

Directly, from the repository root. `PIKMIN_SCENARIO_DATA_DIR` is resolved
against the directory the command starts in (the process then changes into it), so
a relative path like this one works only from the root. Write the stand-in tree
first, for example with the generator (see
[Stand-in assets](#stand-in-assets)):

```sh
build/pikmin1-scenarios/nectar_standin_generator build/pikmin1-scenarios/scenario-data
PIKMIN_SCENARIO_DATA_DIR=build/pikmin1-scenarios/scenario-data \
PIKMIN_SCENARIO_STAGE=stages/sandbox.ini \
    build/pikmin1-scenarios/bin/nectar --scenario boot_smoke --scenario-tick-rate 30
```

| Argument | Meaning |
|----------|---------|
| `--scenario <name>` | Scenario to run: `boot_smoke`, `title_boot`, `throw_release`, `day30_clock`, `extinction`, `captain_death`, `day_end`, `day_end_day29`, `extinction_day30`, `onion_unlock`, `onion_unlock_whistle`, `captain_damage`, `ship_parts`, `sprout_landing`, `onion_skip`, `container_menu`, `piki_hazard_deaths`, `piki_drown`, `piki_throw_water`, or `objects_smoke`, or a save scenario: `save_load`, `save_load_flags`, `results_nosave_map`, `results_nosave_cardselect`, `card_errors`, `card_errors_saved`, `pause_continue`, `permadeath_erase`, `permadeath_unsaved`, `permadeath_bad_slot`, `permadeath_off`. Also W2 (#94): `pellet_table`, `ufo_part_discovery`, `pellet_carry`, `ship_part_carry`, `nectar_growth`, `bomb_firsts`, `carry_blocked` (stages in the table below). Also W3 (#95): `pikmin_cap`, `pikmin_cap_1`, `pikmin_cap_250`, `pikmin_cap_999`, `day_clock`, `demo_timers`, `throw_levels`, `whistle_radius`, `navi_invincible`, `whistle_gather_release`, `whistle_pluck_held` (stages in the table below). Also W7 (#98): `enemy_kills`, `onion_menu_transfer`, `navi_states`, `gate_break` (stages in the table below). Also W9 (#99): `map_menu`, `map_menu_lockout`, `world_map_day_end`, `title_round_trip`, `day_end_handoff` (stages in the table below). Object-stage scenarios need `PIKMIN_SCENARIO_STAGE` (see the table below). `tutorial_skip`, `tutorial_firsts`, `tutorial_day1`, `ufo_part_discovery`, `ship_part_carry` and `demo_timers` need `stages/practice.ini`. |
| `--scenario-tick-rate 30\|60` | Ticks per second. Default `30`. Any other value (or a missing one) exits 2. |
| `--scenario-visible` | Show the window. Default is hidden. A visible window is also positioned and may move the real mouse pointer (see [Mouse pointer](#mouse-pointer)). |
| `--scenario-realtime` | Pace the ticks in real time at the tick rate (the fixed-step scheduler) instead of unthrottled. Deterministic mode stays on, so the per-tick state is the same (`scenario_realtime_determinism_first_yellow_30`). With `--scenario-visible` the window defaults to 1280x720. |
| `--scenario-handoff` | Play from the scenario's hand-off point: a visible 1280x720 window, real-time pacing, then the game is handed to the real keyboard, mouse and controllers. Only for scenarios that declare one (`free_play`, `first_yellow`); any other exits 2. See [Free play and hand-off](#free-play-and-hand-off). |
| `--scenario-screenshot <tick>:<path>` | Repeatable. After the sim and draw for that scenario tick (tick 0 is the first tick after the intro gate), writes a PNG of the finished frame to `path`. On capture ticks only, the render target is temporarily enlarged to **640×480** (override with `PIKMIN_SCENARIO_SCREENSHOT_WINDOW=WxH`; sizes other than the GameCube 640×480 virtual framebuffer can read back blank on headless CI) while normal ticks stay at the default 64×36. Uses the same offscreen colour buffer as `PIKMIN_FRAME_DUMP`. Does not change the per-tick state-hash log. |
| `--scenario-card-delay-ms <ms>` | Test knob: the memory-card thread sleeps this long before each command (0 to 60000; anything else exits 2). With the runner's card wait (see [The memory card](#the-memory-card)) the per-tick state is the same (`scenario_card_delay_determinism_challenge_extinction_30`); the run only takes longer. |
| `--scenario-card-async` | Turns the runner's card wait off, so card operations run on the card thread as in normal play and screens that poll it see them end on a tick that follows the thread's wall-clock speed. Only for the negative control of the card-delay check. |

| Environment variable | Meaning |
|----------------------|---------|
| `PIKMIN_SCENARIO_DATA_DIR` | Folder that holds `assets/`. The process changes into it. Unset or empty, the run exits 77. Set to a folder that cannot be entered, it exits 2. |
| `PIKMIN_SCENARIO_STAGE` | Stage `.ini` to boot, as written in `stages.ini`. Default `stages/stage1.ini` (Forest of Hope on disc). The stand-in tree uses `stages/sandbox.ini`, `stages/sandbox_objects.ini`, `stages/sandbox_all_objects.ini`, `stages/sandbox_water.ini` or `stages/practice.ini` (the tutorial stage). On the stand-in tree without it the run names `stage1.ini`, which has no files there, and hangs until the watchdog fails it (exit 1). An unknown stage name fails with exit 1 and `no stage with file ...`. |
| `PIKMIN_SCENARIO_TRACE` | Number of ticks, from tick 0, for which the runner logs captain state, movie and overlay flags, day and time (`[scenario-trace]` lines). |
| `PIKMIN_SCENARIO_WATCHDOG_SECONDS` | Wall-clock limit in seconds, default 540. A value that is not a positive integer is ignored. |
| `PIKMIN_SCENARIO_STATE_STATS` | Any value. With the verdict, prints per state id of the captain and of the Pikmin the longest uninterrupted stay in ticks and the entry count (`[scenario-states]` lines). A state stuck on an animation key the stand-ins lack shows as a long stay. |
| `PIKMIN_LOG_FILE_OPENS` | File path. Appends `<game path>\t<OK\|FAILED>` per `DVDOpen`. Works in any build, used to make the boot manifest. The file is appended to, so use a fresh path per run. |
| `PIKMIN_SCENARIO_WINDOW` | Window size `WxH`, default `64x36` (`1280x720` for a visible real-time run: `--scenario-handoff`, or `--scenario-realtime` with `--scenario-visible`). The scene renders at the same size (render scale 1, no 160x120 floor). Anything but two positive integers up to 8192 exits 2. See [Window size](#window-size). |
| `PIKMIN_SCENARIO_HANDOFF_PROBE` | `1` with `--scenario-handoff`: the headless hand-off check that `check_scenario_handoff.cmake` runs (hidden window, a virtual SDL game controller, a PASS or FAIL line from the probe, then exit). Test use only. |
| `PIKMIN_SCENARIO_SCREENSHOT_WINDOW` | Window and render size on capture ticks only, default **`640x480`** (GameCube GX virtual size). Smaller sizes (for example the old 128×72 default) often produce a blank readback in headless CI because the port maps GX in 640×480 space. The sim does not read the picture; state-hash logs match runs without `--scenario-screenshot`. |

**Screenshot verification (`scenario_screenshot_boot_smoke_30`):** every OS run checks width/height, non-blank pixels, at least 80 distinct RGB values and RGB variance ≥ 80 (catches the blank-frame regression). The CTest driver sets `PIKMIN_SCENARIO_WINDOW=640x480` for the whole run so CI does not rely on a mid-tick resize from 64×36 (headless stacks often read back an empty enlarged FBO). **Linux** also fuzzy-compares against `tools/scenarios/golden/boot_smoke_tick100_640x480.png` (llvmpipe, tolerance 10 per channel, up to 3% differing pixels). macOS (Apple GL) and Windows (Mesa WGL) are expected to differ in fine detail and colour; those runners skip the golden file and rely on the statistical guards. Same scenario and tick on one machine should be repeatable; do not expect byte-identical PNGs across GPU stacks.

A scenario run forces its own conditions:

- Dummy SDL audio (`SDL_AUDIODRIVER=dummy`).
- Netplay deterministic mode, forced in code (`pc_netplay_det_scenario_enable`):
  deterministic fixed-step mode and unthrottled, so each loop runs one tick with
  no vsync wait; the session seed is 0 and an ambient `PIKMIN_NETPLAY_*`
  variable cannot change it; the sim RNG is reseeded from that seed, the day and
  the stage at each new day; the floating-point environment is pinned every tick.
  Two runs on one machine are identical, nothing more is claimed.
- Settings that are neither read nor written: windowed 64x36 (`PIKMIN_SCENARIO_WINDOW`), vsync off, the
  frame rate set from `--scenario-tick-rate`, Infinite Day and Whistle Pluck set by the scenario
  (`pc_settings_scenario_set_infinite_day`, `pc_settings_scenario_set_whistle_pluck`).
- Story mode and No Day Limit off. A scenario whose `challengeMode()` returns true
  starts in challenge mode instead (CardSelect's path: `gameflow.mIsChallengeMode`,
  `PlayerState::setChallengeMode`; a stage with no `chid` becomes challenge course 0).
  No Day Limit is set by the scenario (`pc_settings_scenario_set_no_day_advance`).
- Achievements (`pikmin_achievements.txt`) are read and written in that run's
  `NECTAR_SAVE_DIR`, not in the data folder the process changes into
  (`pc_achievements.cpp`), so a run starts with none unlocked and writes nothing
  into the stand-in tree.
- A fresh `NECTAR_SAVE_DIR` under the system temp folder, named
  `nectar-scenario-<name>-<rate>-<pid>`. An ambient `NECTAR_SAVE_DIR` is not
  used. The folder is removed on every normal exit, on a watchdog failure and
  when a stage cannot be found, but a process killed by a signal (an abort, a
  crash, CTest's timeout) leaves it behind.
- A cleared list of environment variables that change the sim or the input
  (`kClearedEnv` in `pc_scenario.cpp`: `PIKMIN_INPUT_RECORD`, `PIKMIN_UNLOCK_ALL`,
  `PIKMIN_COOP`, `PIKMIN_COOP_CAPTAINS`, `PIKMIN_REPLAY_TEST`, `PIKMIN_NO_H4M`,
  `PIKMIN_NETPLAY_EXIT_AFTER_TICKS` and three `PIKMIN_NETPLAY_TEST_*` variables).

`--scenario-realtime` and `--scenario-handoff` change only the pacing (the
fixed-step scheduler instead of unthrottled). After a hand-off the game is no
longer deterministic and nothing is checked; see
[Free play and hand-off](#free-play-and-hand-off) for what stays.

### The memory card

The game runs memory-card operations on a card thread (`cardutil.cpp`), and some
screens poll it once per tick without waiting: `ogScrMakeDefaultMgr` (making the
card file, which every save screen does first on the runner's empty scratch card)
leaves its "Processing" state only when 2 s of game time have passed **and**
`MemoryCard::hasCardFinished()` says the thread is done. In an unthrottled run 2 s
of game time pass in a few hundred milliseconds, so the tick that state ends on
followed the card thread's wall-clock speed (issue #125). While a scenario runs,
`CardUtilCommand` now waits for the card thread before it returns
(`pc_scenario_card_sync`), so every poll sees the operation done and the outcome
depends on the tick count alone. After a hand-off, and in every build without
`PIKMIN_BUILD_SCENARIOS`, the card stays asynchronous as on the GameCube.
`--scenario-card-delay-ms` and `--scenario-card-async` exist to test this.

### Window size

Every tick draws a frame, and the world sim runs inside the draw step, so a
runner without a GPU (CI's macOS, Linux and Windows runners use a software
renderer) pays per pixel on every tick. No scenario reads the picture. The
default window is 64x36 and the scene renders at that size: the player's
render scale (2/3) and its 160x120 floor do not apply (`pc_gfx_set_min_render_size`).

Measured on an M3 Max with Apple's software renderer (the renderer macOS CI
gets), forced by a local, uncommitted `SDL_GL_ACCELERATED_VISUAL` 0 in
`pc_window.cpp`, `boot_smoke` at 30 Hz on the stand-in tree, 900 ticks plus boot:

| Window | Scene rendered at | Wall time |
|--------|-------------------|-----------|
| 640x360 (old default, render scale 2/3) | 427x240 | 34.5 s |
| 160x90 with the 160x120 floor | 160x120 | 9.8 s |
| 64x36 with the 160x120 floor | 160x120 | 7.9 s |
| 160x90 | 160x90 | 9.2 s |
| 128x72 | 128x72 | 7.6 s |
| 64x36 (default) | 64x36 | 4.1 to 4.9 s |

The same runs on the GPU take under a second at any of these sizes, so local
GPU timing cannot show this.

In a scenario the sim does not read the window size (in normal play it reads the
window aspect through the culling frustum, by decision: a wider view keeps more of the world
alive; see
[coverage/render-sim-coupling.md](coverage/render-sim-coupling.md)): every scenario's `--state-hash-log`
at 160x90, 128x72 and 64x36 is byte-identical to the 640x360 run of the same
scenario and rate, on the stand-in and the disc tree, and `throw_release`
measures the same K. Set `PIKMIN_SCENARIO_WINDOW=640x360` (or larger) to
compare, or with `--scenario-visible` to watch a run at a readable size.

### Exit codes

| Code | Meaning |
|------|---------|
| `0` | Pass. After a `--scenario-handoff`, closing the window (or any normal exit). |
| `1` | A check failed (one `[scenario] FAIL ...: expected ..., observed ...` line on stderr), the boot never reached the stage, the stage name is not in `stages.ini`, the watchdog fired, or the process ended before a verdict (closing the window before the hand-off included). |
| `2` | Unknown scenario, bad arguments or a rejected flag, `--scenario-handoff` with a scenario that has no hand-off point, or a data directory that is set but cannot be entered. Before any window exists. |
| `4` | Window or GL context creation failed (from the code; not reproduced, because this Mac has a GL context). |
| `77` | Skip: `PIKMIN_SCENARIO_DATA_DIR` unset or empty, no `assets/dataDir/parms/gamePrms.bin`, or the scenario does not support the rate (no scenario refuses a rate today). |

Exit 77 only reaches CTest as a skip for the `_disc` entries (`SKIP_RETURN_CODE
77`); a stand-in entry that exits 77 fails.

Three rules keep an abnormal exit from reading as a pass:

- `--scenario` rejects boot-path-changing flags with exit 2 before any window
  exists: `--coop-direct`, `--coop`, `--vs-map`, `--mission-map`, `--campaign`,
  `--netplay-host`, `--netplay-join`, `--netplay-host-ice`, `--netplay-join-ice`,
  `--netplay-exit-after-ticks` and `--input-replay` (plus `PIKMIN_INPUT_REPLAY`).
  `--input-record` stays allowed and records the scripted pads. The list is
  `kRejectedFlags` in `pc_scenario.cpp`. Co-op is a property of the scenario
  (`PcScenario::coop`, see [Co-op scenarios](#co-op-scenarios)), not of the
  flags, which stay rejected.
- A wall-clock watchdog fails the run with exit 1 and prints
  `[scenario] FAIL <name>: wall-clock watchdog after N s` (default 540 s,
  override with `PIKMIN_SCENARIO_WATCHDOG_SECONDS`). It sits under the CTest
  timeout of 600 s. On this Mac a stand-in scenario takes 1 to 2 s, so the
  default is a bound for slow runners (the source comment says the slowest takes
  about 85 s on software GL; that was not measured here). A hand-off disarms it.
- Any `exit()` before a verdict is a failure. The exit handler is armed once
  argument and data checks pass. `System::halt` and `PPCHalt` call `exit(0)`,
  which would otherwise pass. A GL context failure records exit 4 first, so the
  handler reports 4.

An ERROR or abort ends the process by signal, which CTest also reports as a
failure.

## GameStat invariant check

After every scenario's `onTick` the runner (`checkGameStatInvariants` in
`pc_scenario.cpp`) reads `GameStat` and fails the scenario on the first
violation (exit 1, one `[scenario] FAIL <name> tick <n>: expected ..., observed ...`
line with the values): no counter is negative; per colour `formationPikis +
freePikis + workPikis` equals the live Pikmin of that colour in `pikiMgr`;
`mapPikis` equals formation + free + me + work; `allPikis` equals `mapPikis +
containerPikis`; `maxPikis >= allPikis`; `0 <= minPikis <= maxPikis`. It is a
pure read, so no state-hash log changes. All existing stand-in scenarios pass
it unchanged. Not checked: `mePikis` against the live sprout items, and the
check does not run during the intro gate. Covers EC-05, EC-06, EC-08 and I-2.

## CTest entries

Registered in `games/pikmin1/CMakeLists.txt` under `if(PIKMIN_BUILD_SCENARIOS)`.
`ctest -N` lists 44 stand-in entries (`-R ^scenario_ -LE game-assets`, which counts the generator, the determinism pairs, and the exit-code and invariant helper entries too) and 6 disc
entries (`-L game-assets`).

All names are `scenario_<name>_<rate>`, at the rates in [Tick rates](#tick-rates).
The runner self-tests and `scenario_determinism_*` entries below are 8 more
stand-in entries (6 determinism, 1 determinism negative control, 1 GameStat negative control).

### Tick rates

A scenario is registered at 60 as well as 30 only when the 60 Hz run executes
code or timing the 30 Hz run does not. The disc entries keep `boot_smoke`,
`throw_release` and `day30_clock` at both rates (issue #61).

| Scenario | Rates | Why |
|----------|-------|-----|
| `boot_smoke` | 30, 60 | The rate is boot configuration (`fpsMode`, the tick length); each rate must boot and run. |
| `throw_release` | 30, 60 | K and the release timings are tick arithmetic and differ per rate (13 and 22 after #92's GameCube RNG order; 15 and 18 before). |
| `day30_clock` | 30, 60 | The clock moves by one frame time per tick, so the hold and the sunset arithmetic differ per rate. |
| `extinction` | 30, 60 | The captain's PikiZero countdown is 30 ticks at either rate, but a movie advances half a frame per tick at 60 Hz (the 60-frame stand-ins take 58 and 115 ticks against about 61 and 121), so the bounds in seconds and the run-once check run at both. |
| `captain_death` | 30, 60 | Bounds are in seconds and tick counts differ (`Dead` at tick 128 and 247, the results screen about 11 s later at both); the clock and the movie advance per tick. |
| `captain_damage` | 30, 60 | The bounds are in seconds and the tick counts differ (Flick lasts 117 ticks at 30 Hz and 236 at 60 Hz, Dead lands at t=743 and t=1471). The Flick's get-up wait is a timer summed from `gsys->getFrameTime()` and the motions advance per tick, so the invincibility window is arithmetic in the frame time. Read from code, not a known failing path. |
| `sprout_landing` | 30, 60 | `BuryInit` snaps the seed to the ground only above 30 Hz (`pc_seed_landing_snaps_to_ground`); 60 Hz is the only run of that path. |
| `onion_skip` | 30, 60 | The movie advances half a frame per tick at 60 Hz (330 ticks against 660), so the skip frames and the boot motion's key events land on different ticks. |
| `container_menu` | 30, 60 | Rate-specific code runs on both paths. `Navi::updateWalkAnimation` (the captain walks to the Onion and back to Walk) scales its speed window and motion cooldown with `pc_anim_ticks_per_30hz(frame time)`, and `DrawContainer` keeps its phase timers as sums of `gsys->getFrameTime()` against fixed thresholds (0.1 s steps, 0.5 s ramps), so the tick on which each status changes differs per rate. The scenario scales its Operation hold and its bounds by rate. None of it is a branch on the rate like `BuryInit`'s, so both rates are kept for that arithmetic, not for a known failing path; read from code, not shown to fail at either rate. |
| `onion_unlock`, `onion_unlock_whistle` | 30, 60 | Same reason as `container_menu`: the captain's walk to each sprout (`Navi::updateWalkAnimation`), `NaviNukuAdjustState`'s odometer window (0.4 s) and the 40-frame stand-in pluck motions run on frame-time arithmetic, so each pluck lands on different ticks per rate; read from code, not shown to fail at either rate. |
| `objects_smoke` | 30 | Loads the every-object stage and counts the enemies born; neither depends on the tick rate. |
| `tutorial_skip` | 30, 60 | The clock lock is checked per tick, and the skip's request, its application and the day-over exit each take a tick or two whose count differs per rate; the bounds are in seconds. Read from code, not shown to fail at either rate. |
| `tutorial_firsts` | 30, 60 | The captain's walks (the trigger radii are crossed on different ticks), the discovery movie (half a frame per tick at 60 Hz), the Onion's boot motion and the plucks run on frame-time arithmetic; the seed lands elsewhere at 60 Hz (`BuryInit`'s snap, `sprout_landing`). |
| `tutorial_day1` | 30, 60 | Push, carry and suck speeds are per-second arithmetic over per-tick steps, and the movies advance half a frame per tick at 60 Hz; both rates run the whole Day 1 to its end. |
| `ship_parts` | 30, 60 | Nothing in `getUfoParts` depends on the rate (it is direct calls, one part per tick); both rates are kept because the run costs half a second and the intro gate and the tick counter differ (intro 15 ticks on the stand-ins, 45 on the disc). Not shown to fail at either rate. |
| `first_yellow` | 30, 60 | The captain's walk to the sprout runs on frame-time arithmetic (`Navi::updateWalkAnimation`, as in `onion_unlock`), so where and when he stops differs per rate: parked at 2.00 s at both, 8.3 units from the sprout at 30 Hz and 9.0 at 60 Hz. |
| `coop_smoke` | 30, 60 | The rate is boot configuration, as for `boot_smoke`; each rate must boot two captains and draw split screen. |
| `coop_look` | 30 | `mLookTimer` counts `updateLook` calls, one per tick with the gate, at any rate; the view loop does not read the rate. |
| `scenario_handoff_free_play` | 30, 60 | The probe checks real-time pacing, and the fixed-step deadline is per rate. |
| `scenario_handoff_first_yellow` | 30 | The hand-off path is `free_play`'s; `first_yellow`'s scripted walk is covered at both rates by its own entries. |
| `scenario_handoff_day_end_handoff` | 30, 60 | W9 (#99): the hand-off comes after a day end and the world map, whose slide-in, confirm window and results timers are frame-time; both rates reach Day 3 and hand off. |
| `map_menu`, `map_menu_lockout` | 30, 60 | W9 (#99): the Y menu fades in and out over 0.3 s of frame time, and the lockout is an hour of the world clock; the script is in seconds. |
| `world_map_day_end`, `title_round_trip`, `title_boot` | 30, 60 | W9 (#99): the world map slides in for about 2 s and its confirm window turns in for about 1.5 s (frame-time tweens), so presses come at 2.5 s and 4.5 s after the map opens; the title and file-select fades run on frame time too. Measured: the Day 2 day end soft-resets at tick 502 (30 Hz) or 832 (60 Hz), and Day 3 is ready 262 or 393 ticks later. |
| `save_load`, `save_load_flags` | 30, 60 | The results screen and its save menus (`ogSaveMgr`, the card check, the slot screen) run on frame-time timers (0.25 s fades, the 1 s save notice, the 2 s default-file wait), as `day_end`'s results do; the save itself is not rate code. Read from code, not shown to fail at either rate. |
| `results_nosave_map`, `results_nosave_cardselect`, `card_errors`, `card_errors_saved` | 30 | The same screens and timers as `save_load`, which runs at both rates; these differ only in the answers and the injected card faults. |
| `pause_continue` | 30, 60 | The pause menu's fades are frame-time sums and the script is in seconds; the run costs under a second. Not shown to fail at either rate. |
| `permadeath_erase` | 30, 60 | `MessageModeState`'s desaturate and fade timers are frame-time sums (as in `captain_death`), and the erase is the port feature that deletes user data. |
| `permadeath_unsaved`, `permadeath_bad_slot`, `permadeath_off` | 30 | The same death chain as `permadeath_erase`, which runs at both rates; only the card set-up and the rule differ. |
| `ending_bad_day30`, `ending_neutral_day30`, `ending_happy`, `ending_happy_skip` | 30, 60 | The clock and the movies advance per tick (demo26's keys land on tick 52 or 104; demo77 and demo73 loop per tick), and the final results window's phases are timers summed from the frame time, so every step lands on different ticks per rate; bounds are in seconds. Read from code, not shown to fail at either rate. |
| `captain_death_day30`, `challenge_captain_death`, `challenge_extinction` | 30, 60 | As `captain_death` and `extinction`: MessageModeState's timers (0.5, 2, 3 s) and the game-over window's 2 s are sums of the frame time, the movies advance per tick. |
| `day_end_no_day_limit`, `challenge_day_end` | 30, 60 | As `day_end`: the clock jump is in clock ticks and the results windows run on frame-time timers. |
| `onion_day3`, `onion_day3_whistle`, `onion_day3_hidetext`, `onion_day3_reload`, `onion_day3_unplucked`, `onion_day3_skip` | 30, 60 (stand-in and disc) | Same reason as `onion_unlock` (the walk to the sprout and the pluck), and the discovery movie and the Onion's boot motion advance half a frame per tick at 60 Hz, so the seed, the skip at 1.5 s and the day end land on different ticks. Read from code, not shown to fail at either rate. |
| `piki_hazard_deaths`, `piki_drown`, `piki_throw_water` | 30, 60 | Hazard injections and drown timers use frame-time sums (`gsys->getFrameTime()`); whistle rescue and flick get-up bounds are in seconds. |
| `pikmin_cap`, `pikmin_cap_1`, `pikmin_cap_250`, `pikmin_cap_999`, `day_clock`, `demo_timers`, `throw_levels`, `whistle_radius`, `navi_invincible` | 30, 60 | The W3 (#95) bounds are in seconds and the demo timers, hurry-up banner, Pressed timer and whistle growth are frame-time sums. `throw_levels` compares peaks within one run: the peak itself differs by rate (+61.3 at 30 Hz, +68.0 at 60 Hz). |
| `whistle_gather_release`, `whistle_pluck_held` | 30, 60 | W4 (#96): the whistle's growth, hold and Release are frame-time sums, so tick counts differ by rate (`KEY_LoopStart` 1 tick after Gather at 30 Hz, 2 at 60 Hz; the 0.08 s pluck interval is 3 and 5 ticks); the scenarios assert seconds and the call site's own arithmetic. |
| `enemy_kills`, `onion_menu_transfer`, `navi_states`, `gate_break` | 30, 60 | The W7 (#98) bounds are in seconds. Kills, the menu's walk and timers, the geyser flight and the gate's hits land on different ticks per rate. `navi_states` asserts the Bury escape at 5 to 7 s: 6.30 s at both rates, since `mEscapeTimer` is scaled by `PC_TICKS30` (before that fix 60 Hz gave 3.80 s; W7 findings on #100). |

Wall time of every stand-in entry, one run at a time on an M3 Max, `/usr/bin/time -p`
(2026-10-28, base `ee769374`; the last three rows on base `a1065fef`, CTest's own per-test time, `-j 1`). "GPU" is the Mac's own GL, "software" is Apple's
software renderer, forced by a local, uncommitted `SDL_GL_ACCELERATED_VISUAL` 0 in
`pc_window.cpp` (the renderer macOS CI gets). The state-hash logs of the GPU and the
software run are byte-identical for every entry.

| Scenario | PASS tick at 30 / 60 | GPU 30 / 60 | Software 30 / 60 |
|----------|----------------------|-------------|------------------|
| `boot_smoke` | 899 / 899 | 2.1 s / 1.3 s | 5.6 s / 5.4 s |
| `throw_release` | 374 / 661 | 1.0 s / 1.0 s | 2.9 s / 4.4 s |
| `day30_clock` | 277 / 298 | 0.8 s / 0.7 s | 2.2 s / 2.4 s |
| `sprout_landing` | 287 / 554 | 0.8 s / 1.6 s | 2.1 s / 3.6 s |
| `onion_skip` | 367 / 718 | 0.9 s / 1.5 s | 2.2 s / 3.7 s |
| `container_menu` | 363 / 726 | 1.1 s / 1.5 s | 2.5 s / 4.1 s |
| `objects_smoke` | 299 / none | 1.0 s / none | 2.3 s / none |
| `ship_parts` | 30 / 30 | 0.7 s / 0.5 s (CTest wall time, 2026-10-28; not the `/usr/bin/time -p` method above) | not measured |
| `first_yellow` | 60 / 120 | 0.7 s / 0.8 s (CTest wall time, `-j 3`, 2026-10-28) | not measured |
| `coop_smoke` | 118 / 238 | 0.7 s / 0.7 s (CTest wall time, `-j 3`, 2026-10-28) | not measured |
| `coop_look` | 53 / none | 0.5 s / none (CTest wall time, `-j 3`, 2026-10-28) | not measured |
| `ending_bad_day30` | 625 / 1056 | 1.0 s / 1.6 s (CTest wall time, `-j 3`, 2026-10-28) | not measured |
| `ending_neutral_day30` | 623 / 1054 | 1.4 s / 1.9 s (as above) | not measured |
| `ending_happy` | 485 / 934 | 1.3 s / 1.3 s (as above) | not measured |
| `ending_happy_skip` | 443 / 856 | 0.8 s / 1.4 s (as above) | not measured |
| `captain_death_day30` | 757 / 1470 | 1.3 s / 2.0 s (as above) | not measured |
| `day_end_no_day_limit` | 496 / 826 | 1.0 s / 1.3 s (as above) | not measured |
| `challenge_day_end` | 382 / 687 | 0.9 s / 0.9 s (as above) | not measured |
| `challenge_captain_death` | 658 / 1281 | 1.1 s / 1.8 s (as above) | not measured |
| `challenge_extinction` | 412 / 777 | 1.3 s / 1.3 s (as above) | not measured |
| `onion_day3` (Day 2 and Day 3) | 1109 / 2057 | 1.9 s / 3.1 s (CTest wall time, `-j 3`, 2026-10-28); the five variants 1.7-1.9 s / 2.6-3.1 s; on the disc 7.0-9.0 s / 10.6-15.8 s | not measured |
| `tutorial_skip` | 93 / 183 | 0.7 s / 0.9 s (CTest wall time, `-j 3`) | not measured |
| `tutorial_firsts` | 1824 / 3501 | 4.8 s / 8.7 s (CTest wall time, `-j 3`) | not measured |
| `tutorial_day1` | 3778 / 7114 | 9.1 s / 12.8 s (CTest wall time, `-j 3`) | not measured |

The object stage costs nothing at load any more: `sprout_landing` (287 ticks, 0.8 s)
and `day30_clock` on the plain stage (277 ticks, 0.8 s) take the same time. The 17 s
`PathFinder::selectWay` stage load recorded earlier is gone (see `standin-objects.md`,
Routes); the time left is per-tick rendering, which grows with the tick count.

| Entry | Data | Label | Notes |
|-------|------|-------|-------|
| `scenario_standin_generate` | writes `<build>/scenario-data` | | `FIXTURES_SETUP scenario_standin_tree`, 120 s timeout. Passes `--manifest tools/scenarios/boot-manifest.txt`. |
| `scenario_<name>_<rate>` | stand-in tree | none | For `boot_smoke`, `throw_release`, `day30_clock`, `extinction`, `captain_death`, `extinction_day30`, `day_end`, `day_end_day29`, `onion_unlock`, `onion_unlock_whistle`, `captain_damage`, `ship_parts`, `sprout_landing`, `onion_skip`, `container_menu`, `tutorial_skip`, `tutorial_firsts` and `tutorial_day1` at 30 and 60, and `objects_smoke` at 30 (see Tick rates). `FIXTURES_REQUIRED scenario_standin_tree`, 600 s timeout, `PIKMIN_SCENARIO_STAGE` as in the stage table below. No skip code: a missing or broken tree fails. |
| `scenario_<name>_<rate>_disc` | a real install | `game-assets` | For `boot_smoke`, `throw_release` and `day30_clock` only, at 30 and 60 (6 entries). Runs in `<build>/scenario-scratch`, 600 s timeout, `SKIP_RETURN_CODE 77`. Set `PIKMIN_SCENARIO_DATA_DIR` to run it; otherwise CTest reports a skip. |
| `scenario_determinism_<name>_<rate>` | stand-in tree | none | EC-60. `boot_smoke`, `throw_release` and `day30_clock` at 30 and 60 (6 entries). `check_scenario_determinism.cmake` runs the scenario twice with `--state-hash-log` and fails if the two per-tick hash logs differ (the first differing line is printed). No scenario change. 1200 s timeout, `stages/sandbox.ini`. |
| `scenario_determinism_negative_boot_smoke_30` | stand-in tree | none | Negative control for the entry above: the second run boots `stages/sandbox_objects.ini`, and the test passes only if the logs differ (`-DEXPECT_DIFFER=1`, no `WILL_FAIL`). The runner clears `PIKMIN_NETPLAY_TEST_PREROLL_RAND`, so that variable cannot perturb a scenario run. |
| `scenario_<name>_<rate>` (endings block) | stand-in tree | none | `ending_bad_day30`, `ending_neutral_day30`, `ending_happy`, `ending_happy_skip`, `captain_death_day30`, `challenge_captain_death`, `challenge_extinction`, `day_end_no_day_limit` and `challenge_day_end`, at 30 and 60, in their own `foreach` block after `objects_smoke`'s; the stage per scenario as in the stage table below. Otherwise as the row above. |
| `scenario_onion_day3*_<rate>` | stand-in tree | none | `onion_day3` and its five variants at 30 and 60, `PIKMIN_SCENARIO_STAGE=stages/sandbox_onions.ini`, otherwise as `scenario_<name>_<rate>`. Registered in their own `foreach`. |
| `scenario_title_boot_30`, `_60` | stand-in tree | none | Title and file select on stand-ins (#99); `PIKMIN_SCENARIO_STAGE=stages/sandbox.ini`, 900 s timeout. |
| `scenario_map_menu*_<rate>`, `scenario_world_map_day_end_<rate>`, `scenario_title_round_trip_<rate>` | stand-in tree | none | W9 (#99), in the W3 `foreach` at 30 and 60: `map_menu` and `map_menu_lockout` on `stages/practice.ini`, `world_map_day_end` and `title_round_trip` on `stages/sandbox_objects.ini`. |
| `scenario_world_map_cancel_fail_<rate>`, `scenario_title_round_trip_continue_fail_<rate>`, `scenario_day_end_handoff_cancel_fail_<rate>` | stand-in tree | none | W9 (#99) negative controls through `check_scenario_exit.cmake` (`EXPECT=1`): each must fail with its own message and print no PASS or hand-off line. `day_end_handoff_cancel` runs with `--scenario-handoff` and the probe. |
| `scenario_onion_day3*_<rate>_disc` | a real install | `game-assets` | The same six scenarios at 30 and 60 on the runner's default stage (Forest of Hope), in their own `foreach` after the disc entries above, which are unchanged. |
| `scenario_gamestat_invariant_fail` | stand-in tree | none | Negative control for the GameStat invariant check (see below): the test-only scenario `gamestat_corrupt` raises `workPikis[Blue]` by 3 at tick 5 with no Pikmin behind it; `check_scenario_exit.cmake` asserts exit 1 and the `FAIL gamestat_corrupt tick 5` line. |
| `scenario_first_yellow_<rate>`, `scenario_coop_smoke_<rate>`, `scenario_coop_look_30` | stand-in tree | none | As `scenario_<name>_<rate>`, at the rates in [Tick rates](#tick-rates). `first_yellow` passes at its hand-off point (no `--scenario-handoff`). |
| `scenario_handoff_free_play_30`, `_60`, `scenario_handoff_first_yellow_30`, `scenario_handoff_day_end_handoff_30`, `_60` | a private copy of the stand-in tree | none | `check_scenario_handoff.cmake`, see [The hand-off check](#the-hand-off-check). About 6 to 8 s each: the scripted part and 2 s after the hand-off run in real time. 600 s timeout. |
| `scenario_realtime_determinism_first_yellow_30` | stand-in tree | none | `check_scenario_determinism.cmake` with `-DSECOND_ARGS=--scenario-realtime`: the per-tick state hash log of a real-time run equals the unthrottled one. |
| `scenario_card_delay_determinism_challenge_extinction_30` | stand-in tree | none | Issue #125. `check_scenario_determinism.cmake` with `-DSECOND_ARGS=--scenario-card-delay-ms;1000`: with the card thread 1 s slower per command, the per-tick hash log equals the normal run's (the challenge results window's save step makes the card file and polls the thread). About 12 s. |
| `scenario_card_delay_determinism_negative_challenge_extinction_30` | stand-in tree | none | Negative control for the entry above: the same slow card with `--scenario-card-async` (the game's own polling). Passes only if the logs differ, or if run 2 fails one of the scenario's checks (`-DEXPECT_DIFFER=1 -DSECOND_MAY_FAIL=1`): on a fast machine the window misses the scenario's 60 s bound, on a slower one it exits late. |
| `scenario_screenshot_verify_png_self` | n/a | none | `test_verify_scenario_png.py`: PNG filter decode and golden-compare sanity (regression for Codex P2). |
| `scenario_screenshot_boot_smoke_30` | stand-in tree | none | `check_scenario_screenshot.cmake` with `USE_FULL_WINDOW`: whole run at 640×480, same gates + Linux fuzzy golden. |
| `scenario_screenshot_boot_smoke_30_resize` | stand-in tree | none | Same gates without `PIKMIN_SCENARIO_WINDOW`: default 64×36 with capture-tick resize to 640×480 (`pc_window_poll_events` path). |
| `scenario_screenshot_hash_boot_smoke_30` | stand-in tree | none | `check_scenario_screenshot_hash.cmake`: the hash log with a capture at tick 50 equals the log without `--scenario-screenshot`. |
| `scenario_screenshot_map_menu_<rate>` | stand-in tree | none | W9 (#99), 30 and 60, `RUN_SERIAL`: `check_scenario_map_menu_screenshot.cmake` captures the Y menu's map page (2 s) and controller page (3.5 s) and checks colour regions (gold title, radar greens, counts panel, the one arrow) with `--region-colour` / `--max-region-colour`. |
| `scenario_screenshot_world_map_<rate>` | stand-in tree | none | W9 (#99), 30 and 60, `RUN_SERIAL`: `check_scenario_world_map_screenshot.cmake` captures `world_map_day_end` between stages, the map idle (between-tick 7/3 × rate) and with the "Land here?" window open (4 × rate). Idle: planet greens, gold pin, side panel, and no confirm window; confirm: gold title, the window's blue and gold text, planet, pin, panel. |
| `scenario_screenshot_title_screens_<rate>` | stand-in tree | none | W9 (#99), 30 and 60, `RUN_SERIAL`: `check_scenario_title_screens_screenshot.cmake` captures `title_boot` at boot ticks 314 (Press START), 350 (main menu open) and 528 (file select). Boot ticks match at both rates (the captures are pixel-identical). Each screen is checked for its own regions and against the others' (no menu on Press START, no file-select plate on the menu). |
| `scenario_exit_handoff_unsupported` | none | none | `--scenario-handoff` with `boot_smoke` (no hand-off point) exits 2 with `boot_smoke has no hand-off point`. |
| `scenario_<save scenario>_<rate>` | stand-in tree | none | The save scenarios (`save_load`, `save_load_flags`, `results_nosave_map`, `results_nosave_cardselect`, `card_errors`, `card_errors_saved`, `pause_continue`, `permadeath_erase`, `permadeath_unsaved`, `permadeath_bad_slot`, `permadeath_off`) at the rates in the Tick rates table, registered from one `name:stage:rates` list after the other stand-in lists. Same runner and properties as `scenario_<name>_<rate>`. |
| `scenario_save_dir_permadeath_erase` | stand-in tree | none | `check_scenario_save_dir.cmake` with `-DSCENARIO=permadeath_erase`: the Permadeath erase writes, deletes and rewrites files, and nothing outside its scratch save directory may change. The script (also behind `scenario_save_dir`, which keeps `boot_smoke`) checks the card and shader cache land in the scratch directory, that no `save/`, `shader_cache/`, `card0/` or `card1/` appears in the working, data or executable directory or the per-user default, and that every file under the data directory keeps its size and time stamp. |

Which stage each stand-in entry boots:

| Scenarios | `PIKMIN_SCENARIO_STAGE` |
|-----------|-------------------------|
| `boot_smoke`, `throw_release`, `day30_clock`, `captain_death`, `extinction_day30`, `day_end`, `day_end_day29`, `onion_unlock`, `onion_unlock_whistle`, `captain_damage`, `ship_parts`, `coop_smoke`, `coop_look` | `stages/sandbox.ini` |
| `pause_continue`, `permadeath_erase`, `permadeath_unsaved`, `permadeath_bad_slot`, `permadeath_off` | `stages/sandbox.ini` |
| `save_load`, `save_load_flags`, `results_nosave_map`, `results_nosave_cardselect`, `card_errors`, `card_errors_saved` | `stages/sandbox_objects.ini` (the sunset day end of `day_end`, with the blue Onion) |
| `sprout_landing`, `onion_skip`, `container_menu`, `first_yellow`; the hand-off checks (`free_play`, `first_yellow`) | `stages/sandbox_objects.ini`, the opt-in second stage with a cliff, an Onion, sprouts, a pellet and an enemy. See `standin-objects.md` for what the object stand-ins are, what was read from the disc and what remains, and `standin-cinema.md` for `onion_skip`. |
| `objects_smoke` | `stages/sandbox_all_objects.ini`, one of each plant, bridge, gate, boss and eight enemies. |
| `ending_bad_day30`, `ending_neutral_day30`, `ending_happy`, `ending_happy_skip`, `captain_death_day30`, `challenge_captain_death`, `challenge_extinction` | `stages/sandbox.ini` |
| `day_end_no_day_limit`, `challenge_day_end` | `stages/sandbox_objects.ini` (in challenge mode the stage has no Onion: see `challenge_day_end` below) |
| `onion_day3` and its variants | `stages/sandbox_onions.ini`, the Onion stage: the object stage's map with Onion generators placed as on the disc's courses (red, yellow and blue not named `next`, a `next` yellow elsewhere). See `standin-objects.md`. |
| `piki_hazard_deaths` | `stages/sandbox_objects.ini` (Dwarf Bulborb for swallow). |
| `piki_drown`, `piki_throw_water` | `stages/sandbox_water.ini`, flat map with one pond (`standin_water_stage.h`). |

`sprout_landing`, `onion_skip`, `container_menu`, `onion_unlock`, `onion_unlock_whistle`, `first_yellow` and `objects_smoke` have no `_disc` entry: their
stages do not exist on a real install. `coop_smoke` and `coop_look` have none either: they were not run on a real install.
| `pellet_table`, `ufo_part_discovery`, `nectar_growth`, `bomb_firsts` | `stages/sandbox.ini` |
| `blue_onion_boot`, `yellow_onion_boot`, `red_onion_boot` | Day 2; blue on object stage / disc Forest Navel; yellow/red on Onion stage / disc Forest of Hope | W2-4: starts the ordinary `startBoot` API on an unbooted Onion with its route closed. No movie skip, `pcSnapLanded`, animator-counter write or waypoint write. Every boot tick checks route closure through the measured effect frame and opening afterward; checks one seed at Action0, natural Wait, then real Pikmin AI carries a pellet to that Onion. Both 30/60 Hz, including `_disc`. Mutation controls disable only the generated blue/yellow value-2 cue and require this exact route assertion to fail. Measurements and scope: [standin-onion-boot.md](standin-onion-boot.md). |
| `pellet_carry`, `carry_blocked` | `stages/sandbox_objects.ini` |
| `pikmin_cap`, `pikmin_cap_1`, `pikmin_cap_250`, `pikmin_cap_999` | `stages/sandbox_objects.ini` (its blue Onion) |
| `day_clock`, `throw_levels`, `whistle_radius`, `navi_invincible`, `whistle_gather_release`, `whistle_pluck_held` | `stages/sandbox.ini` |
| `navi_states` | `stages/sandbox.ini` |
| `onion_menu_transfer` | `stages/sandbox_objects.ini` (its blue Onion; field limit 10) |
| `enemy_kills`, `gate_break` | `stages/sandbox_all_objects.ini` (its seven enemies, boulders, snake, geyser and soft gate) |
| `map_menu`, `map_menu_lockout` | `stages/practice.ini` (stage1 opens with a cinema that holds Y off; the sandbox is TESTMAP, see #100) |
| `world_map_day_end`, `title_round_trip`, `day_end_handoff` | `stages/sandbox_objects.ini` (Day 2; the world map then picks the Impact Site, `stages/practice.ini`, for Day 3) |
| `demo_timers` | `stages/practice.ini` (the tutorial stage, for its demo parms) |
| `ship_part_carry` | `stages/practice.ini` |
| `tutorial_skip`, `tutorial_firsts`, `tutorial_day1` | `stages/practice.ini`, the tutorial stage: the Impact Site's file name, first in `stages.ini` with `id 0`, so a new game's Day 1 there is the tutorial (`isTutorial()`). Red Onion, the not-yet-owned blue and yellow Onions, the ship, the Main Engine, the push box and a red pellet, from the disc's generator files' kinds and parameters (`standin-objects.md`, Tutorial stage). |

`sprout_landing`, `onion_skip`, `container_menu`, `onion_unlock`, `onion_unlock_whistle` and `objects_smoke` have no `_disc` entry: their
stages do not exist on a real install. The tutorial scenarios have none either: they depend on the stand-in stage's layout (on a real
install `stages/practice.ini` is the Impact Site).

The stand-in entries run through `run_standin_scenario.cmake`. It starts
`nectar`, keeps the scenario's exit code, and additionally fails the test on any
`FAILED to open assets/...` line whose path is not in
`standin-open-allowlist.txt` (one path relative to `assets/` per line, a trailing
`/` covers a folder, `#` starts a comment). Every allowlisted file is one the
game tolerates missing and no scenario depends on, with the reason beside it.
Under CTest it also passes `--state-hash-log` and writes
`<build>/scenario-logs/scenario_<name>_<rate>.hash.log` and `.log` (CI uploads
every `.hash.log` on every run and the whole folder when a test fails; see
[CI](#ci)). The hash log is one line per tick, written in binary mode, so it is
LF-terminated on every OS and byte-identical when the floats are.

### CI

`.github/workflows/ci.yml` configures and builds `build/pikmin1` (Release,
Ninja, `PIKMIN_BUILD_LAUNCHER=ON`, `PIKMIN_BUILD_TESTS=ON`,
`PIKMIN_NATIVE_JAUDIO=OFF`, `PIKMIN_BUILD_SCENARIOS=ON`; the whole project, not
only the two targets), then runs
`ctest -R ^scenario_ -LE game-assets --timeout 600 -j N`, so disc entries never
run (N is 4 on Linux and Windows and 3 on macOS). The unit tests run in a later step
with `-E ^scenario_`. The environment is `SDL_AUDIODRIVER=dummy`,
`LIBGL_ALWAYS_SOFTWARE=1`, `GALLIUM_DRIVER=llvmpipe` and, on Linux and Windows,
`LP_NUM_THREADS=1` (one llvmpipe rasteriser thread per process, so the four parallel
scenarios do not compete for the same cores).

- **Linux**: wrapped in `xvfb-run -a`, with Mesa packages installed.
- **macOS**: runs directly.
- **Windows (MinGW-w64)**: copies `opengl32.dll` and `libgallium_wgl.dll` from
  `/mingw64/bin` into `build/pikmin1/bin/` first. Without them Windows returns the
  GDI Generic GL 1.1 renderer.
- **`SCENARIO_SKIP_OS`** (an `env` entry in `ci.yml`, empty today): a list of
  `runner.os` values whose GL probe failed. Listed OSes skip the scenario test
  step only; their build and unit tests still run. Add an OS only with a linked
  tracking issue.
- **FP gate (Windows only, a step in the same job)**: `tools/netplay/fp_gate.py`
  disassembles the linked `nectar.exe` and fails on any x87 transcendental or
  `fsqrt`, any FMA and any CRT math import. It checks that the software libm is
  complete, for code the scenarios do not reach (MSYS2 has no Python by default;
  the step installs `mingw-w64-x86_64-python`, about 35 s).

Other jobs of the workflow:

- **`jaudio-build`** (Linux only): compiles the release configuration (native
  JAudio on, no scenarios).
- **`jaudio-scenarios`** (Linux, macOS, Windows): `PIKMIN_NATIVE_JAUDIO=ON` with
  `PIKMIN_BUILD_SCENARIOS=ON` in its own build directory, then
  `ctest -R ^scenario_(boot_smoke|throw_release|day30_clock)_ -E _disc` (the
  generator plus six scenarios, as in `standin-audio.md`). Same dependencies,
  `-UWIN32`, Mesa DLLs and environment as the main job. Nothing is skipped on any OS.
- **`gl-probe`** (Linux, Windows): the GL context probe described under [GL probe](#gl-probe).
  On macOS the probe and the `jaudio-scenarios` steps run inside macOS scenario shard 1, to stay
  under the free plan's limit of about 5 concurrent hosted macOS jobs.
- **`scenarios-merge`** (one per OS, on Linux): the main job runs macOS in 2 shards and Linux
  and Windows in 3 (`ctest -I <shard>,,<shards>`); this job joins them with `merge_scenario_shards.py`, which fails
  unless every scenario test ran in exactly one shard, then runs `ci_k_summary.sh` and
  `check_throw_flight.py` on the merged logs and uploads `scenario-hash-logs-<OS>`.
  Raw provenance is retained byte for byte under `scenario-evidence/<shard-name>/`;
  runner tool versions may differ. Scenario-log conflicts still fail the merge.
  Golden refresh: `golden-update-<OS>` comes from the shard that ran `scenario_screenshot_goldens`.
- **`scenario-hash-compare`**: see below.

#### Cross-OS state hashes

`scenarios-merge` (the main job's shards, merged) and `jaudio-scenarios` upload `build/pikmin1/scenario-logs/*.hash.log`
as artifacts on every run (`scenario-hash-logs-<OS>` and
`scenario-hash-logs-jaudio-<OS>`, 14 days; about 2.6 MB per OS for the 30 logs of the
main job and 0.5 MB for the 6 of the JAudio job, a zip of 380 KB and 100 KB).
`scenario-hash-compare` downloads all six and runs
`tools/scenarios/compare_hash_logs.py` three times: the main job's logs on the three
OSes, the JAudio job's logs on the three OSes, and all six sets over the scenarios
they share (native JAudio on against off). It compares bytes, with nothing
normalised, and fails when
a file differs (it prints the first differing line, its tick and which of the
columns `total navi piki teki item world rng mode` differ, with every OS's line),
when an OS has no log for a scenario another OS has, when a log is empty, and when
an OS's job produced no logs at all (a cancelled job included). The job runs even
when a build job failed, and is skipped only when the whole run is cancelled.

Result (branch `wip/close-ci`, 2026-10-29; GitHub's `macos-26` with Apple clang
21.0, `ubuntu-latest` as Ubuntu 24.04 with GCC 13.3, `windows-latest` with MSYS2
MINGW64 GCC 16.2): in runs 40815424995 and 40817545929 (the final workflow) all 30
stand-in scenario logs, and the 6 of
the native JAudio builds, are **byte-identical on macOS, Linux and Windows**
(2,594,893 bytes for the 30, sha1 `6d0bda3da4f1...`, on all three), and the
JAudio-on logs equal the JAudio-off logs of the same scenarios on every OS.
Run 40812788355 gave the same bytes on macOS and Linux. Its Windows logs had
the same hashes but CRLF line ends: `pc_state_hash.cpp` opened the log with
`fopen(path, "w")`, text mode, which MinGW expands, and the comparison of that run
and of run 40814599756 still ignored a CR. The log is now opened with `"wb"`
and the comparison ignores nothing (the readers, `tools/netplay/*.py` and the
scenario determinism check, read text or split on whitespace and do not depend on
the line end; `pc_input_log` and `pc_netplay_inlog` already use `"wb"`).

Negative control: a manual run with `float_det` unticked
(`gh workflow run ci.yml --ref <branch> -f float_det=false`, which passes
`-DPIKMIN_SCENARIO_FLOAT_DET=OFF` to the scenario builds) must go red. Run
40816367641 did: `scenario-hash-compare` failed with 0 of 30 identical across the
three OSes (first differences at tick 9 to 30), and the Windows FP gate failed
with 33 forbidden references (`fpatan`, `fsincos`, `fsqrt` in the MinGW libm,
and `acos`, `asin`, `atan`, `log10`, `tan` imported from `msvcrt.dll`). Pairwise,
with the option OFF: Linux against Windows 16 of 30 identical, macOS against
either 0 of 30.

Seen once, on 2026-10-31 (PR #122, run 41266102728, W4 #96): `scenario_challenge_extinction_30`
passed on all three OSes, but Linux's hash log had 463 lines against 421 on macOS
and Windows. The first 420 lines were identical. Linux's soft reset out of the
challenge results window came 42 ticks later, and the state hash did not change in
those ticks (the window waits, everything else is still). Locally (Linux,
2026-10-31), 8 runs gave 421 lines and the soft reset at t=412 every time.

Cause (issue #125, fixed): the card thread's wall-clock speed. The window's save
step (`DrawCMresult::modeSave`, `DrawOptionSave`, `ogScrMemChkMgr`) finds the
runner's scratch card empty and makes the card file through `ogScrMakeDefaultMgr`,
which queues the write on the card thread and then, each tick, waits for 2 s of game
time and `hasCardFinished()`. When the thread was still writing at the 2 s mark,
the screen stayed up until it was done; the scenario presses A every 6 ticks on
that screen, so the delay showed up in steps of 6 ticks (42 = 7 x 6). Evidence, on
macOS (2026-11-04), with the runner's card wait turned off (`--scenario-card-async`,
the code as it was): no delay or a 50 ms delay per card command gave the 421-line
log; a 200 ms delay gave 637 and then 541 lines in two identical runs, both passing,
with the first 420 lines equal to the CI Linux log; a 1 s delay missed the 60 s
bound. The same happened to `scenario_captain_death_30` (the story save screen
makes the card file the same way): a baseline suite run with `-j 6` gave 710 lines
where every other run gives 674. With the card wait on (see
[The memory card](#the-memory-card)), every run gives the normal log, also with a
1 s delay, and all 175 stand-in hash logs are byte-identical to those of the base
branch's CI run 41792644808. `scenario_card_delay_determinism_challenge_extinction_30`
and its negative control keep it that way.

#### macOS jobs cancelled with no runner

Seen on 2026-10-28 (for example runs 40744496133 and 40684014259): a `macOS (arm64)`
or `GL probe (macOS (arm64))` job ends `cancelled` 15 minutes (15:01 to 15:02) after
the run started, with an empty `runner_name` and no steps. The job's annotation says
"The job was not acquired by Runner of type hosted even after multiple attempts"
and "Due to capacity constraints, jobs targeting macOS arm64 runners may experience
longer queue times". The cause is GitHub's macOS arm64 runner capacity, not the
workflow:

- 7 of the 95 macOS jobs of the 51 runs up to then were cancelled this way, all
  between 11:53 and 18:37 UTC on 2026-10-28; no Linux or Windows job (231 of
  them) was ever without a runner, and the macOS queue time was about 0 before
  and after (a run at 20:21 UTC the same day: 0.1 min; the runs of 2026-10-29
  03:33 to 04:11 UTC: 2 s). Eight more macOS jobs in that window waited 5 to 10
  minutes before they started (the last at 19:10 UTC) and then ran normally;
  those queue times suggest retries at 5-minute spacing before the 15-minute
  cancellation, which GitHub does not say.
- It is not `timeout-minutes`: the main macOS job has 60 and was cancelled at 15,
  and a timeout counts from the job's start, which never came.
- It is not the `concurrency` group (`cancel-in-progress`). That cancels every
  job of a run, with its runner, when a newer push or dispatch of the same PR or
  ref replaces it: 42 of the 46 other cancelled jobs (all OSes) ended within 90 s
  after a newer run on the same ref was created. The no-runner jobs were cancelled
  when the other jobs of their run had already finished normally.
- GitHub does not document the 15 minutes. It documents a 24-hour queue limit and,
  for the Free plan, at most 5 concurrent macOS jobs for the whole account; waiting
  for that limit queues, it does not cancel.

Nothing in the workflow changes it. "Re-run failed jobs" re-acquires a runner (it
worked on the second attempt of runs 40684014259 and 40731460450). Each macOS job is its own
allocation and each run has three (`macOS (arm64)`, `GL probe (macOS (arm64))`,
and `Native JAudio scenarios (macOS)`), so each one is another chance to hit it;
folding the probe or the JAudio run into the main macOS job would leave one, at
the cost of a longer job. Not tried.

#### CI minutes added by the cross-OS work

Job times from run 40814599756 (warm ccache, restored from the first run of the
branch; run 40812788355 is the cold one). Only the jobs and steps this change
added or widened:

| Job or step | Cold | Warm |
|-------------|------|------|
| `Native JAudio scenarios (macOS)`, new | 4 min 35 s | 1 min 44 s |
| `Native JAudio scenarios (Windows)`, new | 9 min 4 s | 4 min 31 s |
| `Native JAudio scenarios (Linux)`, existing | 3 min 30 s | 2 min 16 s |
| `Cross-OS scenario state hashes`, new | 7 s | 10 s |
| Hash-log upload step, per build job | up to 8 s | up to 8 s |
| Windows FP gate step | not in that run | about 35 s |

That is about 7 more job-minutes per run when warm (macOS 2 + Windows 4.5 + FP
gate, compare and uploads 1) and about 15 when cold, none of it on the critical
path: the longest job is still `Windows (x86-64, MinGW-w64)` (7 min 24 s and
8 min 23 s in two warm runs, 35 s of each the FP gate; 14 min 37 s cold). The macOS
jobs started within 2 s of the run in all of these. On a public repository the
minutes are free; on a private one a macOS minute counts 10 times.

### Cross-platform floats

A scenario build (`PIKMIN_BUILD_SCENARIOS=ON`) also turns on
`PIKMIN_SCENARIO_FLOAT_DET` (default ON), so a state-hash log, K, or a stand-in
file from Linux or Windows means the same as one from macOS:

- `-ffp-contract=off` on the game, the legacy library, the generator,
  `nectar_assets` and `nectar_mapcore`. arm64 clang otherwise fuses
  multiply-adds; x86-64 GCC without `-march` has no FMA to fuse.
- The netplay build's software libm (`pc_port/netplay/pc_netplay_libm.c`)
  replaces the platform's `sinf`, `atan2f`, `powf` and the rest in `nectar`
  and the generator. On Apple, `pc_port/scenarios/pc_scenario_sincos_apple.c`
  also replaces `__sincosf_stret` and `__sincos_stret`, which clang calls when it
  fuses a sine and a cosine of the same value.

Release builds never set `PIKMIN_BUILD_SCENARIOS`, so their flags and libm are
unchanged. A `PIKMIN_NETPLAY_BUILD` already has the first two and, on Apple,
links the same `__sincosf_stret`/`__sincos_stret` replacement.
`-DPIKMIN_SCENARIO_FLOAT_DET=OFF` gives the platform's own floats again.

Measured 2026-10-28: macOS arm64 (Apple clang, Release, Unix Makefiles) against
Linux x86-64 (Ubuntu 24.04 in Docker `linux/amd64`, GCC 13.3, glibc 2.39,
Release, Ninja, the CI Linux packages, `xvfb-run`, llvmpipe). `--state-hash-log`
of `throw_release`, `boot_smoke` and `day30_clock` at 30 and 60 Hz on the
stand-in tree:

| Build | Identical across the two | First difference |
|-------|--------------------------|------------------|
| Default (option OFF) | none of 6 | tick 30 (30 Hz) or 23 (60 Hz), captain hash, in every scenario: arm64 FMA |
| `-ffp-contract=off` only | 4 of 6 (`boot_smoke`, `day30_clock`) | `throw_release` at tick 53 (30 Hz) or 52 (60 Hz), Pikmin hash: libm (`atan2f`) |
| Option ON (both) | 6 of 6 | none |

On Linux the default and the `-ffp-contract=off` logs are the same, as
expected without FMA. With the option ON the generator writes byte-identical
trees on both (by default `sandbox_objects.mod` and `sandbox_all_objects.mod`
differed), and the hash logs of all 18 stand-in scenario entries, the object
stages included, are byte-identical on the two; all 19 stand-in entries pass on
both. K is 13 at 30 Hz and 22 at 60 Hz after #92 (GameCube RNG draw order and stand-in
gate re-fit), on the stand-in tree
and on a real install.

On a real install (RelWithDebInfo, option ON), the six disc runs give the same
per-tick hashes on macOS arm64, Linux x86-64 and a MinGW-w64 Windows build
under wine (the log is now written in binary mode, so the files are
byte-identical; the wine run predates that and had CRLF line ends). Recipes and results: [linux-disc-run.md](linux-disc-run.md) and
[windows-disc-run.md](windows-disc-run.md).

Windows (MinGW-w64) was measured on CI on 2026-10-29 (see [Cross-OS state
hashes](#cross-os-state-hashes); MSYS2 MINGW64, GCC 16.2, Release): with the option
ON its 30 logs are byte-identical to macOS's and Linux's, and the linked
`nectar.exe` passes `fp_gate.py` (no x87 transcendental or `fsqrt`, no FMA, no CRT
math import). Run 40816367641 built every scenario binary with the option OFF
and compared against the option-ON logs of run 40815424995, per OS:

| Option OFF | Identical to option ON | First difference |
|------------|------------------------|------------------|
| macOS (Apple clang 21) | 0 of 30 | tick 30 (30 Hz) or 23 (60 Hz), navi hash: arm64 FMA |
| Linux (glibc, GCC 13.3) | 16 of 30 | `throw_release` at tick 56 (30 Hz) or 61 (60 Hz), piki hash: libm |
| Windows (MinGW libm, GCC 16.2) | 30 of 30 | none |

So Windows needed no change: even without the option its x87 libm (the binary
then has 33 forbidden references, mostly `fpatan`, `fsincos` and `fsqrt` in the
MinGW libm and five `msvcrt.dll` math imports) rounds to the same floats as the
software libm for every argument these 30 scenarios pass. That is a result for
these inputs, not a property of x87: with the option ON the FP gate keeps those
instructions out of the binary. Compile-time folding of math calls on constant arguments was audited on
2026-10-28: at all 30 sites the folded value equals the software libm's runtime
result. The sqrt, `frexpf` and `ldexpf` that stay with the platform are exact by
IEEE 754. Real Windows hardware is not measured (CI uses Windows runners; the disc
run above is wine 9.0).

## The scenarios

| Scenario | Start | What it asserts |
|----------|-------|-----------------|
| `boot_smoke` | Day 1 | Boots into the stage and passes after 900 ticks. Fails if the stage-ready hook does not fire within 20,000 boot ticks. |
| `title_boot` | Day 1 | Runs the stand-in title opening (`opening.cin`), press start, main menu and file select (`skipBootMenus()` false) before `stages/sandbox.ini`. PC new-game and captain prompts are auto-accepted. Passes at stage ready. |
| `map_menu` | Day 2 | The Y menu on stand-in screens (`cont_y`, `m_menu2`, `m_menu2m`, `cont_00`, `p_map0N`): no menu before Y, the map page (index 1) within 1 s of Y, the controller page within 0.5 s of R, back with L, gone within 1 s of the second Y with `mIsUIOverlayActive` cleared; the world clock holds while the menu is up. Milestone DY6's prerequisite. |
| `map_menu_lockout` | Day 2 | DY6: with the clock 0.005 h before `MAP_MENU_SUNSET_LOCKOUT` Y opens the menu (the negative control); 0.005 h after it, no menu and no UI overlay through 1.5 s. |
| `world_map_day_end` | Day 2 | WM1, WM2: the Day 2 day end soft-resets to MapSelect on Day 3, the real world map is built once from stand-in `w_*.blo` screens, A on the map and A on "Yes" of "Land here?" pick the course under the cursor, and Day 3 is ready on `stages/practice.ini` (id 0) within 20 s of the map opening. Pad input only. |
| `title_round_trip` | Day 2 | WM3: START on the world map, "Return to Title" on its pause window, then the title (within 15 s), file select (within 20 s) and a stage (within 60 s). The file continues the saved game: a second world map, then Day 3 on `stages/practice.ini`. |
| `day_end_handoff` | Day 2 | `world_map_day_end`, then the hand-off on the first tick after the Day 3 intro (`scenario_handoff_day_end_handoff_<rate>`). |
| `throw_release` | Day 1 | The squad is born only after the stage has been ready for 45 ticks (a hold of neutral input; 0 on the disc, whose intro already takes 45), then the captain walks away for 4 s with three blue Pikmin following. Three cases follow, each starting when the captain is in Walk with a Normal Pikmin in the plate within 80 units: a calibration that holds A until the grab completes and measures K (ticks from the press to the grab), a case with A down for one tick only (the early release), and a case that releases 2 ticks after that Pikmin's own grab completes. Each case checks that the Pikmin is Hanged before the captain enters Throw (except the early release), the captain enters Throw, the Pikmin leaves the hand (moves at least 5 units), flies and lands (Normal), and the captain returns to Walk. The grab, Throw and flight steps are bounded at 5 s, landing and the Settle wait at 10 s. Also asserts the throw key is bound to A and, before the calibration, that the ThrowWait animation and the AnimMgr tables are sane. |
| `day30_clock` | Day 2 | Infinite Day holds the world clock on Days 2 to 29 and not on Day 30 (regression test for upstream #100). Day 2 with the option off: the clock advances. Option on: it stays put. Each phase is 30 open ticks. Then Day 30 with the option on, started 120 clock ticks before the end hour (the start is computed from `mRealSecsPerGameHour` and the frame time, so it is the same tick count on every tree and rate): the clock advances to sunset and day end begins at or after the end hour. A build without the Day 30 exemption fails with `clock held on day 30` 30 ticks into Day 30. Passes at tick 277 at 30 Hz and 298 at 60 Hz (was 1,146 and 1,584). |
| `extinction` | Day 2 | Every Pikmin dies on a normal day and the extinction chain must end. Three blue Pikmin are born and killed with `InteractKill`. Asserts, each within a bound in seconds: the captain enters `NAVISTATE_PikiZero`; `mGameEndFlag` becomes `GAMEEND_PikminExtinction` with the extinction movie (demo46) playing; demo46's own `GameEndCondition` key sets the clock to the end hour; demo47's `ForceResults` key clears the flag and `DayOverModeState` starts (`mIsDayEndSeqStarted`). The captain must pass through PikiZero once and the flag must be set once: a missing movie replays the chain forever, which the bounds and that count catch. Passes before the day-end phases, which need results screens the stand-in tree lacks. See `standin-cinema.md` and `coverage/terminals.md` T04. |
| `captain_death` | Day 1 | The captain takes a Flick hit for 1000 damage at tick 10 (T06 in `coverage/terminals.md`). The real chain runs with no further input except A presses once the results window is up: `Dead` (29) within 8 s with `GameStat::orimaDead`, `GAMEEND_NaviDown` within 4 s, `MessageModeState`, the day advancing exactly once (`DayOverModeState` phase two opens the results window), then the soft reset toward `ONEPLAYER_MapSelect` at least 3 s after the results opened (the diary page, `account2.blo`, the save step) and within 60 s. `Dead` entered twice, a second day advance or a reset toward `GameExit` fail it. Needs the stand-in `demo36` (see `standin-cinema.md`): without it the end-of-day movie has zero length, phases three and four run the next tick and the day leaves for `GameExit` with the results unread. At 30 Hz: Dead t=128, NaviDown t=167, day advance t=340, reset t=676; at 60 Hz: 247, 325, 662, 1306. |
| `captain_damage` | Day 1 | Non-lethal captain damage, the Flick state's invincibility, DF29 and then the lethal hit (EC-14, EC-15; DF29 in `coverage/milestones.md`). Six `InteractFlick` hits with knock-back 0, damages 30, 30, 15, 1, 19 (health 100 to 70, 40, 25, 24, 5; each lowers it by exactly the damage, puts the captain in Flick (8) and brings him back to Walk (0)), then 30 (lethal). During hit 1's Flick a 1000-damage probe every tick is refused (`stimulate` false, health, knock-back and state unchanged): `NaviFlickState::invincible` is true (`NaviState.h:411`, `InteractFlick::actNavi` `navi.cpp:4738`). The next hit goes in on the first tick back in Walk and is applied in full: the invincibility is exactly the Flick state, no grace period after it. DF29 (`DEMOFLAG_OlimarLowHealth`) is clear after hits 1 to 3 and set by hit 4: `startDamageEffect` runs before the subtraction (`navi.cpp:4741-4749`), so it tests the health before the hit against 25, with `<=`. Hit 3 leaves the health at exactly 25 and does not set it; hit 4 reads 25 and does. The flag opens the `TUT_APunchUFO` window (stand-in `tu_tx31.blo`, closed with A). The lethal hit: Flick then Dead (29) once, `GameStat::orimaDead`, health 0 (`NaviDeadState::init`), held one second with Dead entered once (the NaviDown chain is `captain_death`'s). Bounds in seconds; Flick must last 0.5 to 8 s and the five undisturbed Flicks agree within 250 ms. Code constants: the health arithmetic, the threshold and its order, the invincibility, Dead on health <= 1. Stand-in animation lengths (invented): how long Flick lasts (about 3.9 s, JHit + JKoke + GetUp + a random wait of up to 0.1 s), and the hit-to-Dead time. At 30 Hz: Flick 116 to 118 ticks, Dead t=743, PASS t=773; at 60 Hz: Flick 233 to 238 ticks, Dead t=1471, PASS t=1531. |
| `container_menu` | Day 1, object stage | The captain walks onto the Onion's `cont` spot and presses A; the container menu (`DrawContainer`, stand-in `con01.blo` with its `p264` window) opens, passes Wait, Start and Operation, stays in Operation for 4 s with one stick push, and closes, once with B and once with A. The captain returns to Walk and the Onion count is unchanged. |
| `onion_skip` | Day 1, object stage | The blue Onion discovery movie (upstream #205), started with `DEMOFLAG_DiscoverBlueOnyon`. Four cases: watched to the end, START before Boot, between Boot and the first seed, and between the seed and EmitCone. Each asserts that the movie plays and ends, the Onion boots and reaches Wait, and exactly one seed comes out. The between case is the regression test for #205 (0 seeds without the fix). The #205 fix is now part of upstream main. See `standin-cinema.md`. |
| `sprout_landing` | Day 1, object stage | Six seeds launched at the cliff the way `GoalAI::BootEmit` launches them. Each must plant (Wait) within a bound, not on a steep face, not below the ground and not floating more than 30 units above it. Height and speed at the start of Bury are printed, not asserted (upstream #208, merged upstream 2026-10-28, changes them at 30 Hz). See `standin-objects.md`. |
| `ship_parts` | Day 1 | The ship-part upgrade chain (`coverage/milestones.md` SP1-SP11, SP13). `getUfoParts(id, true)` for all 30 ids, one per tick (ticks 0-29), the ids of the five optional parts last except Nova Blaster third. After each: `mCurrParts`, part visibility, per-stage counts (part i goes to stage i % 5; the sandbox's stage id 5 would write one byte past `mStagePartsCollected`, so the scenario swaps a valid id in around each call), `mRequiredUfoPartCount` and `happyEndable` (true at 25 required = part 26, not part 25), radar and jet flags, `mShipUpgradeLevel` and the open stages against the live `AICONST._154/_164/_174/_184` (5, 12, 29, 30 on both trees), `mPendingStageUnlockID`, the result flags (on 1, seen 2, `Collect10Parts` with the clock set to day 10 for one call), `getNextPowerupNumber` (parts left to the next level, not the 1,6,12,29,30 table), and that a second collection of the same id changes nothing (again for all 30 at the end). The stand-in tree registers no ship part, so the scenario registers the ids with `PlayerState::registerUfoParts`; on the disc all 30 are registered. Sets the scenario-only infinite-day setting so achievements are blocked (no icon read, no `pikmin_achievements.txt`). Also passes on the disc (not registered as a `_disc` entry). |
| `objects_smoke` | Day 1, every-object stage | Boots `stages/sandbox_all_objects.ini`, checks the eight enemies were born and passes after 300 ticks. A missing or malformed model, platform, table or parameter file aborts the run instead. |
| `day_end` | Day 2, object stage | The ordinary sunset day end to its terminal (T01, T03 in `coverage/terminals.md`). Two blue Pikmin (Formation and Free) and one red are spawned, Infinite Day is off, and the clock is set 60 open ticks before the end hour; the jump passes 13:00, so the first-noon text opens and A closes it. Asserted, each within a bound in seconds: the day end starts at or after the end hour, `DayOverModeState` follows, the day advances exactly once with no ending, `exitDayEnd` sends both blue Pikmin into the blue Onion and kills the red one (the stage has no red Onion), the diary entry for the day is `RESFLAG_Misc1` (`re_n1_00`, one page), and the soft reset goes to `ONEPLAYER_MapSelect` at least 3 s after the results opened and within 60 s. At 30 Hz: day end t=156, day advance t=160, reset t=496; at 60 Hz: 178, 182, 826. See `standin-cinema.md` (Day-end screens). |
| `day_end_day29` | Day 29, object stage | `day_end` on the last ordinary day: `cleanupDayEnd` sets `RESFLAG_FinalDay`, so the diary entry is `re_g_00`; the day becomes 30 and the exit is still MapSelect. Same ticks as `day_end` at 30 Hz. |
| `tutorial_skip` | Day 1, tutorial stage | The port's tutorial skip (ST7, DF08, the skip half of T16 in `coverage/`). Baseline: `isTutorial()`, Day 1 at 14.8 h, the red Onion in `GOAL_BootInit`, no blue or yellow Onion, a new game's flags and counts. For 3 s with Infinite Day off the clock stays exactly at 14.8 h. Then `pc_tutorial_skip_set_pending(true)`, as the speedrun shortcut does mid-tutorial; on the next update `pcApplyPendingTutorialSkip` must set exactly demo flags 0 3 4 7 8 9 10 12 13 14 15 30 (DF08 CameraInfo only here), the red container, display and boot bits, the Onion snapped to `GOAL_Wait`, the Main Engine as an invisible part (1 part, 1 required, level 1, Forest open and pending, one Impact Site part, its achievement and no other), 20 red Pikmin in the Onion (leaf count, `pikiInfMgr`, `containerPikis`, `bornPikis`, `allPikis` and the pluck counter each +20, none on the field) and the day-end trigger. `DayOverModeState`'s skip branch: Day 2, the request cleared, the Day 1 diary entry `RESFLAG_EndFirstDay` (two pages), the 20 still in the Onion, no movie from the request to the soft reset, which goes to MapSelect; `isTutorial()` false once `exitCourse` ran. Passes at the same ticks on a real install (not registered). |
| `tutorial_firsts` | Day 1, tutorial stage | The walking firsts (DF00, ON4's tutorial boot, DF03, DF12 by the captain, DF04, DF30) with the clock at 14.8 h throughout (T16). Each distance trigger comes within a tick of the captain being inside its radius (the disc's `parms/demo.bin`: 60, 25, 40) and not before he is within it plus one step, and adds exactly its own flag. The red Onion's discovery movie (stand-in demo04, its text closed with A) boots it: boot bit, `mShipEffectPartFlag` bit 8 cleared and the camera's controls back, exactly one seed, `GOAL_Wait`. DF03 arms the NoPikminTimeout timer with 40 s. DF30: a pluck at 14 Pikmin (12 spawned into the party) sets nothing, the next at 15 sets `DEMOFLAG_Pluck15thPikmin` and opens TUT_NukiAndFree (stand-in `tu_tx38_00/01`). On a real install the same assertions hold through DF03; the walk to the engine is blocked by the Impact Site's terrain there (not registered). |
| `pellet_table` | Day 1 | EC-46: every `pelMgr.bin` config has a sane type, colour and seed counts, and the count is the table's (20 on the stand-ins, 62 on the disc; a `_disc` entry exists). |
| `ufo_part_discovery` | Day 1 | DF32-61, SP14: for each of the 30 parts a ground pellet is born and the captain walks into the `demoParms` radius: flag 32+id only, ship text type PartDiscovery and the part index, the part's text window closed with A; a captain held just outside does not set it. Not stage-gated. |
| `pellet_carry` | Day 1, object stage | EC-44, EC-45, PL5, the DF10 stage gate: a 2-carrier minimum (one Pikmin moves nothing for 3 s, never more than the maximum on the pellet) and the seeds an Onion makes of red 1- and 5-pellets and a blue 5-pellet in the blue Onion (1, 1, 3). Flag 10 stays clear off the tutorial stage. |
| `ship_part_carry` | Day 1, tutorial stage | SP12: the Whimsical Radar is carried to the ship by 20 Pikmin (nine move nothing); with the engine flag already set it takes the part branch: ship text type PartCollect, part 5, demo78, text id 97, `mCurrParts` +1. |
| `nectar_growth` | Day 1 | DF26, PL1, EC-48: a FallWater nectar puddle; a flower Pikmin does not drink; a bud drinks and flowers (`RESFLAG_PikminSeeds`, flag 26, `TUT_Mitu`); a leaf too, with no second text. |
| `bomb_firsts` | Day 1 | DF18, DF20, DF27: a red Pikmin ignores a bomb, a yellow one picks it up (flag 18); a lit bomb explodes after its fuse (flag 20 1.5 s later, `TUT_BombInfo`); a bomb on a Pikmin sets flag 27. |
| `carry_blocked` | Day 1, object stage | DF28: carried over an open route, no flag; with the Onion's waypoint closed the carriers spin, flag 28 sets with `TUT_Rute`; reopened, the pellet is delivered. |
| `pikmin_cap` | Day 1, object stage | ON6, DF17, DF21-25, EC-01, EC-02, EC-04, EC-38: pool and plate sizing at the default limit (100); below the limit the Onion gives a seed; births to the limit, then every plain birth fails; the plate's slots match and are distinct; a sprout plucked at the limit still becomes a Pikmin; at the limit the Onion keeps the Pikmin and, per story stage, sets flag stage + 21 and opens `TUT_Limit100` once. |
| `pikmin_cap_1`, `pikmin_cap_250`, `pikmin_cap_999` | Day 1, object stage | EC-57: `pikmin_cap` at the port's limit 1, 250 and 999 (`PcScenario::pikiLimit`). At 1 the stage builds one sprout and DF17 sets (red Onion, no red Pikmin); clear in the others. |
| `day_clock` | Day 1 | DY3, DY4, DF19, DF31, T29, EC-17, EC-18: the 10, 13 and 16 h bells once each (flag 31 and `TUT_InfoDisplay` at noon), the sundown warning once at `mNightWarning` and the banner's end (flag 19, `TUT_DayEnd`), the countdown from 18:30, the day end at 19:00, the clock never going back. Characterised: the countdown is cut by the 19:00 day end; T29's guard is always true. |
| `demo_timers` | Day 1, tutorial stage | DF07, DF09, DF16, PL3, EC-36: NoPikminTimeout 40 s after DF03 (not at 39, by 41), Unk9 30 s after the first pluck, OnyonMenuInfo only off the tutorial and on a stage <= Forest, the pluck counter one up after the pluck, and no input acting during the red Onion's discovery movie until A skips it. |
| `throw_levels` | Day 1 | EC-28, EC-31, EC-40 in part, EC-42: A with no Pikmin punches and returns to Walk; throws at charge levels 0-3 go Hanged, Flying, Normal, land on the ground at the cursor and, with the disc's parms (min == max), peak the same; with the max height raised (control) level 3 peaks higher. |
| `whistle_radius` | Day 1 | EC-39: a full whistle calls every Free Pikmin inside its radius and leaves those outside Free; at Whistle Radius 50% the radius halves. The stand-in Fue motion's own `KEY_LoopStart` starts the circle (no poke since W4 #96). |
| `navi_invincible` | Day 1 | EC-37: `invincible()` in all 36 captain states against the list of 14; a Press hit in Walk is taken and puts the captain in Pressed, where Press, Flick, Suck and Bury are refused; back in Walk after 3 s a Suck hit is taken. |
| `whistle_gather_release` | Day 1 | EC-32, EC-39: the real whistle, no state poked. A full hold calls Free Pikmin inside R and leaves those outside; a half-grown partial calls at its scale; X disbands the squad to Free and back to Walk. Characterised (#100): a full hold never sets `mWhistleCallRadius`, so the circle's per-tick calls use 0 or a previous partial's scale. |
| `whistle_pluck_held` | Day 1 | PL4, MD1: Whistle Pluck at its `NaviGatherState::exec` call site, each tick checked against it: nearest sprout first, the growing radius, the 0.08 s interval, one Pikmin per pluck of the sprout's colour in AutoNuki, nothing outside R; with the setting off the same hold plucks nothing. |
| `enemy_kills` | Day 1, all-objects stage | EC-50, EN1: each of the seven enemies (chappy, swallow, palm, beatle, kabekuiA, otama, kabekuiB) at health 0 adds exactly 1 to `GameStat::killTekis`; one that has burrowed (kabekuiA) stays uncounted until the captain comes near; the Burrowing Snagret at life 0 is not counted until it surfaces, then once; the plugged geyser at life 0 counts nothing; Beatle's and Snake's diary flags become 2 (seen). Rolling boulders break by themselves and each adds 1; they are counted apart. |
| `onion_menu_transfer` | Day 1, object stage | EC-03, EC-35, EC-47: the Onion menu (field limit 10) pauses the world (`mPauseAll`, the clock stands still); four Pikmin go in (store, `containerPikis` +4, squad 2); B cancels a second transfer; at a field count of 9 a withdrawal stops at -1 with `MSG_SquadTotalFull`, and the field count ends at exactly 10. The field count includes the stage's three buried sprouts. |
| `navi_states` | Day 1 | EC-34, EC-51 (geyser): InteractGeyzer is taken once (the second is refused), flies the captain to the target (within 40 units) and ends in Walk; InteractBury is taken once (the second refused, invincible), costs its damage, and with no input the captain is out after the 5 s escape timer (150 ticks at 30 Hz, scaled by `PC_TICKS30`) plus his get-up motion, 5 to 7 s at both rates; at health 5 a Bury takes him to Dead. Rope, Funbari, Stuck, Pellet, Pick and Water are not driven (no injection path on a stand-in stage). |
| `gate_break` | Day 1, all-objects stage | EC-51 (gate): the soft gate (health 300 from the stand-in generator: days 12, hours 12) takes a 1-damage scratch with no stage change, then one stage per stage's worth of damage, its waypoint opens, and a completed gate refuses hits. The reinforced and bomb gates, the bridge and the push box are not covered. |
| `tutorial_day1` | Day 1, tutorial stage | The Pikmin firsts and the tutorial's own day end (DF14, DF15, DF10, DF12 by a Pikmin, DF13, T16). Red Pikmin are born free beside each object and handed to `Piki::actOnSituaton`, as a thrown Pikmin's landing does. Nine at the push box (the disc's 10 Pikmin, speed 50, 270 units) move nothing for 3 s; a tenth sets StartBoxPush as it starts; FinishBoxPush when it arrives 270 units on. One carries the red pellet into the red Onion (CollectFirstPellet). Twenty at the Main Engine set ApproachEngine (`Piki::demoCheck`, the captain over 1,000 away) and carry it to the ship: CollectEngine with ship text CollectEngine and no part id, then the ship takes it (a visible part, 1 part, level 1, Forest open). The day ends only through the ship-upgrade movie (stand-in demo17's ForceDayEnd) with the clock still at 14.8 h; `DayOverModeState` with no take-off movie, Day 2 once, the Day 1 diary entry `RESFLAG_EndFirstDay` (two pages), the results for at least 3 s, the soft reset to MapSelect, `isTutorial()` false. The captain plucks every sprout first (see the T16 notes in `coverage/terminals.md`). |
| `extinction_day30` | Day 30 | Every Pikmin dies on the last day (T05, EC-12). Three blue Pikmin are killed as in `extinction`. Asserted, each within a bound in seconds: PikiZero; `GAMEEND_PikminExtinction` with demo46 playing; demo46's own key resets the flag to `GAMEEND_None` while demo46 still plays and the clock is still below the end hour (the Day 30 branch of `GameMovieInterface::parse`); `DayOverModeState` follows at once and sets the clock to 19:00; the bad ending runs (demo73 with the final results window, which A presses take through the scores, the parts list and the options save, up at least 4 s); the soft reset goes to `ONEPLAYER_GameExit` within 60 s of demo73. Throughout: demo47 (ExtDayEnd) never starts, the flag is set once, the day stays 30, `mEndingType` stays None. A build without the Day 30 test at NPG:3323 fails with `demo47 started`. At 30 Hz: flag t=35, reset t=93, demo73 t=97, GameExit t=517; at 60 Hz: 35, 150, 157, 966. Needs the stand-in demo73 and the results screens (`standin-cinema.md`). |
| `save_load`, `save_load_flags` | Day 2, object stage | A day-end save through the real results screen and card stubs, read back from disk and loaded (T13, T14, T21 in `coverage/terminals.md`). `day_end`'s sunset (two blue Pikmin into the blue Onion, a red one lost) with a run state set before it: 7 ship parts (6 required), per-stage parts, upgrade level 1, a part on the day graph, red, blue and yellow container bits; `save_load_flags` also starts the run as Permadeath and Hard. On the results screen: Yes to "save?", the card check makes the default file, slot A, the save (ogSaveMgr 5, 6, 14, `mDidSaveFail` false), the exit to MapSelect. The scratch save directory holds the card file and its stat record, and `card0/days/slot0/day03.bin` only for the non-Permadeath run. The card file is read back from disk (in-memory image cleared), is not broken, slot A shows day 3, 7 parts and the Pikmin counts; after CardSelect's reset and `loadCurrentGame` every saved field equals the snapshot taken at the save (day, play state, parts, containers, per-stage and per-day parts, Onion Pikmin per colour and stage, demo and result flags). The slot's Permadeath and Hard marks and the rules the load adopts are the run's (off, or on), with the rules forced to the opposite before the load. PASS at tick 497 (30 Hz) and 827 (60 Hz). |
| `results_nosave_map`, `results_nosave_cardselect` | Day 2, object stage | The results screen's exits without a save (T13): "save?" No, then the second question Yes, to MapSelect; or B (the menu closes, ExitFailure 12, A again), No and No, to CardSelect (AlternativeExitSuccess 15). Both: the second question shown, no save ran, no card file in the scratch save directory. PASS at tick 306 and 344 (30 Hz). |
| `card_errors`, `card_errors_saved` | Day 2, object stage | Every memory-card error the day-end save handles, injected into the card stubs (`pc_card_fault.h`) one per attempt; each error screen is answered with No on "format?" and Retry, so the results screen takes A again. Per attempt: the fault was hit, the card check showed the error's state, no save ran (except the write failure: SavingInProgress, no SaveComplete, `mDidSaveFail`), and the card file is as before. Empty card: no card (NoCard), not a card (NotACard), unformatted (the format question, then UnformattedCard), broken (format question, BrokenCard), full (CardFull), unusable (UnusableCard), no directory entry (FileNotMade), the default file cannot be created (MakeDefaultFile, then BrokenCard). Saved card (slots A to C, the run playing A): the write fails, broken, no card. Then the save with no fault goes through to slot A (day 3) and leaves for MapSelect; slots B and C keep block, checksum and day. PASS at tick 1097 and 653 (30 Hz). |
| `pause_continue` | Day 2 | The pause menu's "Continue from last save" (T15): START, the cursor down twice, A, then Yes on the confirmation. The soft reset goes to CardSelect within 10 s of START, no day end, nothing saved (the scratch save directory unchanged). On Day 1 the menu skips "Go to sunset", which is why the scenario starts on Day 2. PASS at tick 180 (30 Hz) and 330 (60 Hz). |
| `permadeath_erase`, `permadeath_unsaved`, `permadeath_bad_slot`, `permadeath_off` | Day 1 | The Permadeath erase (T20). Through the game's card code the scenario writes the card file, slots A, B, C as normal saves on days 5, 4, 7 (each with its `card0/days` copy), slot B again as the run's Permadeath save on day 1, the run plays slot B. The captain takes a 1000-damage Flick at tick 10. `permadeath_erase`: CardSelect, no `DayOverModeState`, the notice queued; in the scratch save directory only the card file changes and `card0/days/slot1/` and its `day04.bin` go (the stat record stays); slot B reads back Fresh with no marks, slots A and C keep block, checksum, day and Pikmin; the banner and option sections are unchanged and exactly one game block changed, the spare. `permadeath_unsaved` (no card file), `permadeath_bad_slot` (play-state slot 3): CardSelect, the notice queued, the directory unchanged. `permadeath_off`: the same saves, a normal run: the death reaches `DayOverModeState` with the directory unchanged. PASS at tick 337 (30 Hz) and 659 (60 Hz). |

| `whistle_delay` | Day 1, any stage | Issue #133: six Free Pikmin 20 to 60 units from the captain, a full whistle. Per Pikmin: the tick it enters LookAt (the call) and the tick it is in Formation and out of LookAt. Asserts every Pikmin plays Kizuku in LookAt and joins 0.45 to 1 s after its call (#145), and prints the delays. Measured 2026-11-02, call to joined: disc 0.50 to 0.57 s at 30 Hz, 0.55 to 0.75 s at 60 Hz. Stand-ins before the fix: 0.07 to 1.40 s at 30 Hz, 1.45 to 1.58 s at 60 Hz (the Kizuku motion, info 66, was a generic 40 frames). After it carries the disc's timing (16 frames, KEY_Action0 and KEY_Action1): 0.07 to 0.60 s at 30 Hz, 0.65 to 0.78 s at 60 Hz. At 30 Hz three of six stand-in Pikmin still joined within 0.2 s: the stand-in Wait (info 12) was a 40-frame one-shot, and its KEY_Finished, landing in LookAt's random delay, ended LookAt without Kizuku (PikiLookAtState::procAnimMsg takes any KEY_Finished as Kizuku's end). The disc's Wait loops (20 frames, loop start and end at anim keys 0 and 1) and never sends one. With the disc's Wait (#145): stand-ins 0.53 to 0.70 s at 30 Hz, 0.65 to 0.78 s at 60 Hz; disc unchanged, 0.50 to 0.57 s and 0.55 to 0.75 s (2026-11-04). |
| `bulborb_fight` | Day 1, object stage or a disc stage | Issue #136: ten blue Pikmin born 60 units from a Dwarf Bulborb, the captain 90 units away, the real enemy AI, 30 s. Prints the Bulborb's health and the Pikmin by state each second, and Pikmin lost, Flick entries and Flick duration. Asserts the Bulborb dies within 10 s and a Flick lasts 4 s or less on average. Measured 2026-11-02, disc: dead at 4.0 s (30 Hz) and 2.5 s (60 Hz), 0 lost, Flick 2.9 s and 2.8 s. Stand-ins before the Flick motion (info 58) carried the disc's timing: dead at 14 s, 2 lost, 9 flicks, Flick 4.6 s. After it (10 frames, flags 6): dead at 4.3 s (30 Hz) and 4.5 s (60 Hz), 0 lost, Flick 3.6 s. Issue #154, 2026-11-04: Attack (info 18), StillJump (13), JKoke (57) and GetUp (59) carry the disc's timing too. Disc on that day: dead at 3.4 s (30 Hz) and 2.5 s (60 Hz), Flick 2.9 s and 2.8 s. Stand-ins: dead at 2.2 s and 1.5 s, 0 lost, Flick 1.8 s at 30 Hz (no flick at 60 Hz). Before, the stand-in Attack had no KEY_Action0, so ground attacks never hit and only Pikmin on the Bulborb's back did damage, at the same cadence at both rates; on the disc ground attacks hit too. The stand-ins are now faster than the disc. Their Pikmin parameters are the code defaults: knock-down 0.7 s, where the disc's give 1 to 3 s (with the disc's values the Flick is 3.3 s), so the Flick is shorter. The kill time is not explained by those values; the stand-in Bulborb's collision spheres sit lower than the disc's, and are the next candidate. |
| `flick_out_of_range` | Day 1, object stage or a disc stage | Issue #146: does a Pikmin flicked out of its work-search range go passive? The range is p41 `mIdleAttackSearchRange`, read only by `Piki::graspSituation` (`piki.cpp`); getting up from a flick does not call it: `PikiFlickState::cleanup` (`pikiState.cpp`) restarts a resumable action, `ActAttack::resumable` is true and `ActAttack::restart` re-inits on the same enemy (`mOther`) with no distance test (`aiAttack.cpp`). One blue Pikmin born 40 units from the Dwarf Bulborb is handed to `actOnSituaton`; when it reaches the Bulborb in attack mode the scenario flicks it as `BTeki::flickUpper` does, with 10 times the Bulborb's upper flick power (60 on both; at 3x it still lands inside the range). Prints state, mode, action, attack target and distance every 0.25 s for 6 s. Asserts it passes the range during the flick (distance over p41 plus the Bulborb's centre size, and `graspSituation` finds no enemy there), keeps the attack action on the Bulborb on every tick from getting up until it reaches it again, and reaches it within 5 s. Measured 2026-11-04, not passive on either: stand-ins out of range 0.20 s / 0.25 s after the flick (30 / 60 Hz), up after 2.23 s / 2.18 s, back at the Bulborb 2.03 s / 0.63 s later; disc (Forest of Hope) 0.17 s / 0.20 s, up after 3.83 s / 3.27 s, back 2.07 s / 0.80 s later. Stand-in differences, left as they are because writing them changes other scenarios' hash logs: p41 is 100 (code default, disc 70) and the knock-down p74/p43 is 0.7-0.7 s (disc 1-3 s), both unwritten in the stand-in `pikiMgr.bin` (`verification-log.md`, item 4); the stand-in Bulborb has a `cent` sphere of 10 (disc 0). The distance at getting up follows the awake Bulborb, which walks after the Pikmin on some runs. |
| `free_play` | Day 2, any stage | No assertions. Applies the disc's key bindings and Infinite Day (which holds Days 2 to 29), then hands off on the first tick after the intro. Headless it passes at tick 0; it exists to be played (`--scenario-handoff`) and for the hand-off check. |
| `first_yellow` | Day 1, object stage | The moment before the first yellow Pikmin (issue #76). The captain walks with the stick to the stage's yellow sprout at (-20, 180) and stops: in Walk, within 1 unit of one spot for 0.5 s, within 15 units of the sprout (the pluck reach, NaviMgr p62) and facing it within 30 degrees, within 15 s of tick 0 and 3 s of coming within 12 units. Yellow must still be undiscovered: the sprout can be plucked, `DEMOFLAG_PluckYellowPikmin` and the Yellow container and display bits are clear, no yellow Pikmin is on the field. Then the hand-off point (a pass without `--scenario-handoff`). The next A press is the first yellow pluck, which `onion_unlock` checks. The stand-in tree has no yellow Onion or discovery movie (demo05). Parked at 2.00 s at both rates. |
| `coop_smoke` | Day 1, co-op | Two captains in split screen (see [Co-op scenarios](#co-op-scenarios)), each driven by its own pad. 2 s with pad 0 east and pad 1 north: captain 1 travels at least 40 units east and captain 2 at least 40 units north, each less than half that across its direction (measured: 248 and 227). Then 2 s with pad 0 released and pad 1 south: over the last 1.5 s captain 1 moves less than 5 units (measured 0) and captain 2 at least 30 units south (measured 196). Both end in Walk. |
| `coop_look` | Day 1, co-op | The split-screen head-look fix (c53ce41e, upstream PR #228; `coverage/render-sim-coupling.md`, Co-op fix). Subjects: both captains (`Navi::updateHeadMatrix`) and one blue Pikmin in Free mode (`ViewPiki::refresh`). Once both captains are in Walk and not looking, each subject gets a look at a fixed point with `mLookTimer` 12; after every tick each timer must be exactly one lower with the target kept, and on the twelfth tick each look must end (no target, timer 0). Two rounds. Negative control (2026-10-28, local, not committed): with `pc_coop_first_view_pass()` returning true all three timers read 10 one tick after the start and the run fails at that tick, which also shows the two views per tick run. |
| `ending_bad_day30` | Day 30 | The bad ending at the Day 30 sunset (T08, DY7). 24 required + 5 optional = 29 parts set with `getUfoParts` and `ship_parts`' stage-id guard (`scenario_endings_common.h`), Infinite Day off, the clock 60 open ticks before the end hour (the first-noon text on the way). Asserted, each within a bound in seconds: the day end at or after the end hour with `mEndingType` None; from the sunset tick on, exactly one text window, `TUT_BadEnding` (`tu_tx35`), closed before `DayOverModeState`; no game-over window on this path; demo73 with the final results window (built, then started by demo73's key); `PC_ACH_BAD_ENDING`; the soft reset to GameExit at least 4 s after demo73 and within 60 s, with the clock at the start hour (phase three). Day 30 throughout. Windows and texts are read through the scenario-only accessors in `newPikiGame.cpp` (`pc_scenario.h`). At 30 Hz: day end t=156, `DayOverModeState` t=204, demo73 t=206, GameExit t=625. |
| `ending_neutral_day30` | Day 30 | The neutral ending (T09, DY7). As `ending_bad_day30` with 25 required + 4 optional = 29 parts: `ENDING_Neutral` from the sunset tick on, the same `TUT_BadEnding` text, `PC_ACH_NORMAL_ENDING`, demo77 (phase three) with the final results window started by its key, GameExit with `mEndingType` Neutral and the clock still at the end hour. At 30 Hz: demo77 t=207, GameExit t=623. |
| `ending_happy`, `ending_happy_skip` | Day 12, Day 13 | The happy ending (T10, DY8). 29 parts, then `PlayerState::preloadHenkaMovie` (which queues demo26) and the 30th part, as a delivery does. demo26's `SpecialDayEnd` key sets `ENDING_Happy` and triggers the day end before the end hour; `_skip` presses START at movie frame 20, before the key, and the skip's replay must still send it. Also asserted: `PC_ACH_BEST_ENDING`; `PC_ACH_SPEED_DEMON` on Day 12 and not on Day 13; no pause menu from the START; no day advance; demo77 with the final results window; GameExit at least 4 s after demo77. At 30 Hz: `ENDING_Happy` t=54 (skip t=22), GameExit t=485 (skip t=443). |
| `captain_death_day30` | Day 30 | The captain dies on the last day (T07). The `captain_death` hit; the game-over window starts once, in `MODE_NaviDown`, within 2 s of `GAMEEND_NaviDown` (MessageModeState's Starting state on Day 30) and before `DayOverModeState`; still set on the tick ForceDayEnd builds `DayOverModeState`, cleared by phase one; then the bad ending (demo73) and GameExit. Day 30 and `mEndingType` None throughout. At 30 Hz: window t=185, `DayOverModeState` t=338, GameExit t=757. |
| `day_end_no_day_limit` | Day 29, object stage | The No Day Limit setting (T12, MD4), on through `pc_settings_scenario_set_no_day_advance`: `day_end_day29`'s chain keeps the day at 29 through phase two and the soft reset to MapSelect, with no ending text and no ending; achievements blocked. Prints (does not assert) the diary slots: the Day 29 entry lands in Day 28's slot (terminals.md T12). |
| `challenge_day_end` | Day 1, object stage, challenge mode | The challenge-mode sunset (T17). `challengeMode()` true. The stage has no Onion in challenge mode (only `next` generators make owned Onions); two blue and one red Pikmin are spawned and killed by `exitDayEnd`; the day advances by one (as the code does); no diary entry; `DrawCMresult` (stand-in `cha_re`, `cha_rank`, `cha_best`) started with `GameStat::allPikis` (the stage's 3 sprouts) as the score, recorded as the course's best (rank 0); the soft reset to MapSelect. At 30 Hz: window t=64, MapSelect t=382. |
| `challenge_captain_death`, `challenge_extinction` | Day 1, challenge mode | The challenge-mode captain death and extinction (T18): the game-over window once (`MODE_NaviDown` 0.5 s after the flag, cleared by ForceDayEnd; `MODE_Extinction` with the extinction flag), no demo47, `DayOverModeState`, the day +1, `DrawCMresult` with score 0 (no rank), MapSelect. At 30 Hz: MapSelect t=658 and t=412. |
| `onion_day3` | Day 2 to Day 3, Onion stage or the disc's Forest of Hope | The yellow Onion unlocked on Day 2 is launched, listed at map select and born on Day 3 (ON2, ON4 in `coverage/milestones.md`; the owner's report). The first scenario that crosses a real day end into a second stage load (`crossesDays()`). Day 2: a new game owns red only; the red Onion and the `next` yellow Onion are born (the latter in `GOAL_BootInit`). The discovery runs as `Navi::demoCheck` starts it (on the disc the captain walks into its 60-unit radius; on the stand-in course the scenario sets the flag, demoCheck runs on Forest of Hope only) and the discovery movie demo05 plays; then the boot bit is set, the container and display bits are clear and exactly one seed is planted. The first yellow pluck by hand passes `onion_unlock`'s exact-state check. The day end as `day_end`: `exitDayEnd` sends the yellow Pikmin into the yellow Onion, `hasContainer(Yellow)` holds when the take-off movies are chosen, the soft reset goes to MapSelect on day 3. Map select lists red and yellow (`displayPikiCount`, what `DrawWorldMap` reads); on the disc the real world map is built and A picks the course. Day 3: container `0x26` (`0x36` on the disc, with the red boot bit Day 1 would set), display `0x06`, the yellow Onion from the generator not named `next` and none at the `next` spot, `GOAL_Wait`, on the ground (on the disc after the landing movie, watched), holding the one yellow Pikmin; red born, no blue. At 30 Hz on the stand-in: seed planted 16.9 s after the discovery, soft reset t=1108, PASS t=1109; on the disc the world map takes 245 ticks and the landing 280. |
| `onion_day3_whistle` | as `onion_day3` | The pluck by Whistle Pluck (`pc_whistle_pluck`, unlock in `PikiAutoNukiState::cleanup`), with `onion_unlock_whistle`'s check. |
| `onion_day3_hidetext` | as `onion_day3` | Hide Olimar's Messages on for both days (`pc_settings_scenario_set_hide_olimar_text`). |
| `onion_day3_reload` | as `onion_day3` | At map select the game is saved and read back as quitting and Continue do (`MemoryCard::writeCurrentGame`, the reset `CardSelectSection` does, `readCurrentGame`; `pcWriteQuickSaveBlock` and `pcLoadQuickSaveBlock` run exactly that). Asserted after the load: day 3, the yellow container, boot and display bits, the pluck flag and one yellow Pikmin in the Onion store. |
| `onion_day3_skip` | as `onion_day3` | START over every movie: the discovery no earlier than 1.5 s in (after its Boot key, before the seed), the pluck movie, the day end and, on the disc, the Day 3 landing. The regression test of the owner's report on the whole chain (fork issue #57, upstream #205): with the #205 diff reversed it fails at both rates on both trees with `Onion state 0, seeds born 0` (the Onion booted and in `GOAL_Wait`, no seed, so no yellow can be plucked). |
| `onion_day3_unplucked` | as `onion_day3` | The negative control of every Day 3 check: the Onion found, its seed left in the ground. As on the GameCube: boot bit set, no container or display bit, no take-off, map select lists red only, and on Day 3 the `next` yellow Onion is born again (booted, `GOAL_Wait`, empty) and none from the other generator. |

`throw_release` measures K at run time and never hard-codes it. On this tree both
the stand-in sandbox and a real install give K = 13 at 30 Hz and 22 at 60 Hz
(after #92's GameCube RNG draw order and the stand-in gate re-fit in
`standin-k-investigation.md`), so a change to the squad AI, collision or the sim
RNG can move the two apart.

## Writing a scenario

A scenario is a class in `pc_port/scenarios/scenario_<name>.cpp` that derives
from `PcScenario` (`pc_scenario.h`):

| Member | Purpose |
|--------|---------|
| `name()` | The `--scenario` argument. |
| `startDay()` | Day the stage starts on. Default 1. Set before the stage is built, because `GameCoreSection`'s constructor consumes it. |
| `supportsRate(int)` | Return false to skip a rate with exit 77. |
| `challengeMode()` | Return true to start in challenge mode (see the forced conditions above). Default false. |
| `onStageReady()` | Runs once, after the intro gate, before tick 0. Spawn objects here. |
| `providePads(tick, pad0)` | Fills pad 0 for this tick, before the sim. Pads 1 to 3 are neutral. |
| `onTick(tick)` | Runs after each tick's sim. Call `scenarioPass()`, or `scenarioFail()` / `scenarioFailf()` with expected and observed text. |
| `coop()` | True for a two-captain split-screen scenario (see [Co-op scenarios](#co-op-scenarios)). |
| `providePad1(tick, pad1)` | Co-op only: fills pad 1, the second captain's, after `providePads`. |
| `handsOff()` | True for a scenario that calls `scenarioHandOff()`; only those accept `--scenario-handoff`. |

`scenarioHandOff()` marks the hand-off point: with `--scenario-handoff` the
player takes over there, otherwise it is a pass. `scenarioApplyDiscKeyBindings()`
sets the disc's `parms/key.bin` bindings (pluck and throw on A). The stand-in
generator writes that file; the call restores the same six binds.
| `crossesDays()` | Default false. True lets the run continue past the soft reset that ends a day: the runner stops ticking the scenario from that tick (the stage's objects are gone), waits for the next stage, runs the intro gate again and calls `onStageReady()` once more; `pc_scenario_stage_number()` says which stage (1, 2, ...). Ticks keep counting across stages. Used by `onion_day3`. |
| `onMapSelect()` | For a `crossesDays()` scenario, when `OnePlayerSection` builds MapSelect: return a `stages.ini` `file` and the runner skips DrawWorldMap (direct `enterCourse`); return `nullptr` to run the real world map (stand-in BLOs or disc) and drive it with `providePadsBetweenStages` and/or `pc_scenario_request_map_course`. |
| `providePadsBetweenStages(ticks, pad0)` | Pad 0 between the soft reset and the next stage (the real world map). |
| `introSkipsMovies(stage)` | Default true: the intro gate pulses START over the stage's opening movie. False lets it run to its end (A still closes text). |
| `checksGameStat()` | Default true. A scenario turns the runner's GameStat check off only where the original game's own counting breaks it, and says why (`onion_day3` on a story course after `cleanupDayEnd`). |

The scenario reads game objects directly (`naviMgr`, `pikiMgr`, `gameflow`).
Never call `exit()`. Report only through the verdict functions.

Menus that keep their state in objects a scenario cannot reach report it through
`PC_SCENARIO_NOTE(key, value)` (`pc_scenario_note.h`; empty outside a scenario
build, a pure read): `ogSaveMgr::update` (`savemgr`, the save menu's status at
entry and exit), `ogScrMemChkMgr::update` (`memchk`, the card check's state) and
`ogNitakuMgr::update` (`nitaku`, an open Yes/No choice's state and cursor). A
scenario reads them with `pc_scenario_note_take`, `_count` and `_at`.
`scen_save::ResultsDriver` (`scenario_save_common.h`) answers each Yes/No from a
plan and presses A everywhere else. The memory card's failures are injected with
`pc_card_fault_set` (`dolphin_stubs/pc_card_fault.h`, compiled only in a scenario
build): no card, an immediate or an async mount error, a check error, a sector
size, no free bytes or directory entry, a create or a write error.

How a run flows:

1. `pc_scenario_parse_argv` (called from `pc_main.cpp`) checks arguments and the
   data directory, and sets up the environment above.
2. The title and file-select sections are skipped (`ninLogo.cpp`), and
   `GameSetup` goes straight to the one-player stage (`gameSetup.cpp`).
3. `pc_scenario_pre_stage` (`game.cpp`) does what the no-save new-game path would
   do, selects the stage and sets the start day and hour.
4. `pc_scenario_on_stage_ready` (`newPikiGame.cpp`, after `finalSetup`) marks the
   stage as ready.
5. `pc_scenario_on_tick_end` (`System::run`) counts ticks. The **intro gate**
   comes first: a new game opens with the landing cutscene and ship text, so the
   input provider pulses START while a movie plays and A while the UI overlay is
   up, and tick 0 is handed to the scenario only once both are gone and the
   captain is in Walk. If that takes more than 90 s of ticks, the run fails.
6. Input reaches the game through `pc_input_log_set_provider`.

To add a scenario: write the class, add a `pc_scenario_make_<name>()` factory
declaration in `pc_scenario_list.h`, return it from `pc_scenario_all()` in
`pc_scenario_list.cpp`, add the source to the `PIKMIN_BUILD_SCENARIOS` block in
`CMakeLists.txt`, and add the name to a `foreach(scenario ...)` list in the
scenario test block at the end of that file: the stand-in list for the stage it
runs on (`sandbox.ini`, `sandbox_objects.ini`, `sandbox_all_objects.ini` or `practice.ini`) and,
only if it works on a real install, the disc list. Then run it once on the
stand-in tree.
Expect a missing-file abort or null dereference if it needs something the tree
lacks; see [Adding a stand-in file](#adding-a-stand-in-file).

## Free play and hand-off

Issue #76. `--scenario-handoff` plays a scenario's scripted part in a visible
window at real-time pacing and then gives you the game at the point the
scenario declares. Two scenarios have one:

- `free_play`: the stage you name, Day 2, Infinite Day on so the clock never
  runs out. You get control right after the stage's intro.
- `first_yellow`: the object stage, Day 1. The captain walks to the yellow
  sprout and stops in front of it. You get control there: press A (the pluck
  button) for the first yellow Pikmin.

### Play it

Build once, from the repository root (any build folder works; this one is used
below). Ninja is optional:

```sh
cmake -S games/pikmin1 -B build/pikmin1-play -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DPIKMIN_NATIVE_JAUDIO=OFF -DPIKMIN_BUILD_SCENARIOS=ON
cmake --build build/pikmin1-play --target pikmin_pc nectar_standin_generator -j 4
build/pikmin1-play/nectar_standin_generator build/pikmin1-play/scenario-data
```

The generator writes the stand-in tree (no disc data). Then, still from the
repository root:

```sh
# The moment before the first yellow Pikmin:
PIKMIN_SCENARIO_DATA_DIR="$PWD/build/pikmin1-play/scenario-data" \
PIKMIN_SCENARIO_STAGE=stages/sandbox_objects.ini \
    build/pikmin1-play/bin/nectar --scenario first_yellow --scenario-handoff

# Free play on the play stage (the object stage with a red, a yellow and a blue Onion, all open, so a carried pellet always has an Onion to go to; or stages/sandbox.ini, stages/sandbox_all_objects.ini):
PIKMIN_SCENARIO_DATA_DIR="$PWD/build/pikmin1-play/scenario-data" \
PIKMIN_SCENARIO_STAGE=stages/sandbox_play.ini \
    build/pikmin1-play/bin/nectar --scenario free_play --scenario-handoff
```

Add `--scenario-tick-rate 60` for 60 ticks per second, and
`PIKMIN_SCENARIO_WINDOW=1920x1080` (any `WxH`) for another window size; the
default is 1280x720. The terminal shows `[scenario] hand-off: ...` when the game
is yours. If the window opened behind another one, click it so it has the
keyboard. Close the window, or press Ctrl+C in the terminal, to end; after the
hand-off the exit code is 0. (Checked 2026-10-28 on macOS with SIGTERM after a
hand-off, and SIGINT on a run without one: both take the normal quit path.)

The generator and the two play commands were run as written on 2026-10-28
from the repository root (macOS arm64, Unix Makefiles, a scratch build folder
configured as above plus `-DPIKMIN_BUILD_TESTS=ON` in place of
`build/pikmin1-play`; the configure and build lines themselves were not run in
that folder), with
`PIKMIN_SCENARIO_HANDOFF_PROBE=1` added so no window opened; the visible run
was checked separately (see [The hand-off check](#the-hand-off-check)).

What the run does and does not touch:

- Saves (the memory card, the day history, achievements, the shader cache) go
  to the run's scratch folder, `nectar-scenario-<name>-<rate>-<pid>` under the
  system temp folder (the terminal prints it as `[scenario] save dir`), never to
  your real save folder. It is deleted when the game exits; set
  `PIKMIN_SCENARIO_KEEP_SAVE_DIR=1` to keep it.
- Settings: `pikmin_settings.conf` is neither read nor written. You play with
  the defaults (as on a fresh install), plus the scenario's own: windowed, vsync
  off, the frame rate from `--scenario-tick-rate`, Whistle Pluck off and, in
  `free_play`, Infinite Day on. Changes in the F1 menu last until you close the
  window; the frame rate, Infinite Day and Whistle Pluck do not change from the
  menu.
- Whistle ring and cursor (issue #131): the stand-in effects bundle emits no particles,
  so `Navi::renderCircle` draws them with lines in a hand-off run on a stand-in stage: a
  white cross at the cursor always, a yellow ring while B is down. A call reaches only
  Pikmin within the ring, around the cursor (`Navi::callPikis`), so aim at them and hold
  B for a moment (the radius grows from its minimum over `mWhistleExpandTime`).
- Keys: the disc's game bindings (pluck and throw on A, whistle on B). The
  stand-in generator writes `parms/key.bin` with those binds. Keyboard, mouse and controller bindings
  are the port's defaults.
- No audio: the run uses SDL's dummy audio driver, and a
  `PIKMIN_NATIVE_JAUDIO=OFF` build has the legacy facade anyway.
- No disc data: the stand-in models are flat colours, the animations are
  stand-ins and most movies and screens are missing or plain
  ([Limitations](#limitations)). The stand-in tree has no yellow Onion and no
  yellow discovery movie.

What happens at the hand-off (`scenarioHandOff`, `pc_scenario.cpp`): the
scripted input provider is detached, so the game reads the real keyboard,
mouse and controllers; deterministic mode ends (wall-clock tick length, the
normal random numbers, the mouse-menu clicks that deterministic mode turns off),
the watchdog is disarmed, and no scenario code or invariant check runs again.
The scratch save folder and the settings layer stay.

Not verified: what follows a Pikmin extinction, the captain's death or a
quit from the pause menu after a hand-off. Since #99 the stand-in tree has the
world map, title and file-select screens, and `day_end_handoff` hands off
after a day end has gone through the world map into Day 3; the other
exits after a hand-off were not run. `free_play`'s Infinite Day keeps sunset away;
`first_yellow` runs Day 1, whose clock is not held.

### The hand-off check

`check_scenario_handoff.cmake` (CTest `scenario_handoff_<name>_<rate>`) runs
`--scenario-handoff` headless with `PIKMIN_SCENARIO_HANDOFF_PROBE=1`. The probe
keeps the window hidden and attaches a virtual SDL game controller (SDL 2.24 or
later) with its left stick held up, which the game opens through its own
hot-plug path like a real pad. In the run it asserts:

- before the hand-off, on every scripted tick where the window read the
  controller's stick (at least 10), the game read the scripted pad instead;
- the scripted part after the intro is paced in real time (only checked when
  it lasts at least a second: 60 ticks took 2.03 s in `first_yellow`);
- after the hand-off the scripted provider is never called, deterministic and
  unthrottled mode are off, the game reads the controller's stick on at least
  3/4 of the ticks (60 of 60 measured), the captain walks at least 30 units in
  2 s (about 300 measured) and those 2 s of ticks take at least 1.6 s (2.00 s);
- `NECTAR_SAVE_DIR` is still the scratch folder.

The script gives the run a private copy of the stand-in tree, an empty working
directory and empty `HOME`, `TMPDIR` (`TMP`, `TEMP`), `LOCALAPPDATA`, `APPDATA`
and `XDG_*` folders, and turns Mesa's shader disk cache off (a driver cache).
After the run the data copy must be unchanged (file list, sizes, MD5), the
working directory and `HOME` empty, `TMPDIR` must hold only the scratch save
folder (with `card0`), and the executable's folder must have gained none of
`save/`, `shader_cache/`, `card0/`, `card1/`, `pikmin_settings.conf` or
`pikmin_achievements.txt`.

Negative controls (2026-10-28, local, not committed): with the provider left
attached at the hand-off the probe fails with `after the hand-off the scripted
input provider is never called ... 1 calls after it`; a file written into the
working directory (the data folder) and one written into `$HOME` at the
hand-off each fail the script by name.

Not covered by the check: a visible window (it was run once by hand: a 1280x720
drawable, the process still running 4 s after the hand-off, exit 0 on SIGTERM,
the scratch folder removed), the real keyboard and mouse (SDL cannot inject
keyboard state; the controller path is the one checked), Linux and Windows.

## Co-op scenarios

A scenario with `coop()` true runs local co-op: the runner arms it as the
title's 2P choice does (`pc_coop_switch_apply`: pending co-op, Olimar and
Louie), so `GameCoreSection` builds two captains and the second camera. Pad 1
is connected and filled by `providePad1`; the intro gate presses only pad 0.

Scenarios run in deterministic mode, which draws the netplay two-pass frame:
one full-screen view of the local captain, so split screen's two views never
run (`detSingleView` in `newPikiGame.cpp`). A co-op scenario therefore turns the
two-pass frame off (`pc_netplay_present_scenario_split_views`), and the view
loop draws both views each tick as ordinary local co-op does. The sim stays
deterministic: fixed tick, sim RNG, the visibility pins. `coop_smoke` and
`coop_look` each give byte-identical state hash logs on two runs (checked by
hand with `check_scenario_determinism.cmake`, not registered). The flags
`--coop` and `--coop-direct` stay rejected.

## Stand-in assets

`nectar_standin_generator <out> [--manifest <boot-manifest.txt>]` writes
`<out>/assets/dataDir/...`, the layout the game opens (every file open resolves
to `assets/` plus the path the game passes). It builds the tree in memory twice
and fails if the two hashes differ (run-to-run identity on one machine only), and
treats every writer error as fatal. With `--manifest`, every path of the disc
boot manifest must be in the tree or on the generator's built-in allowlist
(`kManifestAllowlist` in `standin_generator.cpp`, each entry with a reason), or it
exits 1. It omits `assets/.pikmin-assets`, which only the launcher reads.

The tree holds: a sandbox stage (a flat floor with one cliff, `stages/sandbox.ini`
and its generator files); the tutorial stage (`stages/practice.ini`, stage id 0,
`standin_practice_stage.h`); `stages/stages.ini`; the parameter files; the 18
`.dir`/`.arc` archive pairs; and the models, animations, textures, screens and
particles from the writers below. Issue #99 adds stand-in world-map, title and
file-select screens (`w_*.blo`, `pause_w.blo`, `m_menu_r.blo`, `opening.cin`, and
related layouts) so `onion_day3`, `title_boot`, `world_map_day_end` and `title_round_trip` can run those flows in CI, and the Y menu's layouts for `map_menu`. The sandbox has no `id` line, so it is
`STAGE_TESTMAP`: not a tutorial, so the clock runs.

The writers live in `libs/nectar_assets`: `mod_writer.h` and `standin_models.h`
(`.mod`), `anim.h` and `standin_anims.h` (`.dca`, `.dck`, `animMgr.bin`),
`texture.h`, `bundle.h` and `standin_textures.h` (`.bti`, `.txe`, bundles),
`blo.h`, `pcr.h` and `standin_ui.h` (screens, particles), `cinema.h` (`.cin`
and `.dsk` cutscenes, see `standin-cinema.md`), and `parms.h`, `archive.h`,
`stages_ini.h` and `gen.h`. The stand-in `parms/aiconst.bin` carries the
disc's value for every ID that differs from the code default
(`standin-aiconst.md`), and the stand-in `parms/pikiMgr.bin` is byte-identical
to the disc's: all 77 Pikmin IDs with the disc's values (`standin-piki-parms.md`).

Each writer is a pure function (a plain input struct, bytes out, an error
string, no file I/O) with a write-then-read unit test registered in CTest, for
example `nectar_mod_writer_test`.

Some models are made in Blender (the pellets so far; more follow under #115).
Their `.blend` sources, the export script and the committed exports the
generator compiles in are in `libs/nectar_assets/art/` (see its README for the
one re-export command and `check_export.py`); the designs are in
`libs/nectar_assets/art/ART_DIRECTION.md`. Building never needs Blender. Art
models sit on the spec's own skeleton, so joint counts, joint bounds and
collision spheres do not change.

### Fidelity rule

A stand-in matches the real data **only where a scenario depends on it**: joint
indices, animation lengths, key-event frames, collision sizes and parameter
values. Take values from the decompiled source or code defaults first. A number
only the disc has is recorded in a comment saying where it came from (the
`throw_release` movement and grab parameters are the main case, see `parmFiles`
in `standin_generator.cpp`). No disc geometry, textures, audio or text is used.
Models are original art (flat colours, or Blender models with baked vertex
colours) on the disc's joint structure, animations are identity poses.

### The allowlist and the boot manifest

`boot-manifest.txt` lists every file the game opens on the scenario boot path on
a real install: path, `OK|FAILED`, and the scenarios that opened it. It is a
cross-check, not an input to the game. The generator checks the tree against it,
and some writer tests take it as an argument.

Regenerate it after a change that alters the boot path. On disc data, run each
scenario at least once with `PIKMIN_LOG_FILE_OPENS` set to a fresh file, then:

```sh
python3 tools/scenarios/make_boot_manifest.py \
    --version VERSION_GPIE01_01 --flags "RelWithDebInfo, PIKMIN_BUILD_SCENARIOS=ON, PIKMIN_NATIVE_JAUDIO=OFF" \
    boot_smoke=a.log throw_release=b.log day30_clock=c.log > tools/scenarios/boot-manifest.txt
```

(Run from `games/pikmin1`. Several logs per scenario go comma-separated.) The
script stores paths only, no sizes or contents.

### Adding a stand-in file

1. Run the scenario on the stand-in tree. A missing required file aborts, so
   gaps appear one at a time.
2. If the game needs the file, add it to the generator with the matching writer
   (add a writer and a test in `libs/nectar_assets` if the format has none).
3. If the game tolerates its absence and no scenario depends on it, add its path
   to `standin-open-allowlist.txt` with the reason, and to `kManifestAllowlist`
   if it is in the boot manifest.
4. If a new scenario depends on a value, apply the fidelity rule and cite where
   the number came from.

## GL probe

`gl_probe.c` checks that a runner can host the scenario: it creates a hidden SDL
window with the context profile the game requests (4.1 core on macOS, 3.3
compatibility elsewhere), prints `GL_VERSION`, `GL_RENDERER` and the GLSL
version, compiles and links a `#version 330 core` program, reads back a cleared
framebuffer and swaps 60 times. It prints `GL_PROBE_OK` and exits 0 only if every
step works; on Windows it also rejects the GDI Generic renderer. It exits
without GL teardown because llvmpipe's context destruction hangs on Windows
runners. The CI `gl-probe` job builds and runs it on each OS.

Build it with `cc gl_probe.c $(sdl2-config --cflags --libs) -o gl_probe`.

## Limitations

- Audio is off in the default scenario build (`PIKMIN_NATIVE_JAUDIO=OFF`,
  `SDL_AUDIODRIVER=dummy`). A `PIKMIN_NATIVE_JAUDIO=ON` build boots the
  original engine on stand-in sequences that play no notes, so it tests that
  the engine starts and keeps running, not what it sounds like
  ([standin-audio.md](standin-audio.md)).
- Cutscenes are stand-ins with no actors (`standin-cinema.md`). The sandbox
  plays no landing; it plays the stand-in post-extinction movie (demo60), which
  the intro gate skips on its first tick. `extinction` plays the stand-in
  extinction and day-end movies (demo46, demo47) to the end, `extinction_day30` demo46 and the bad ending's demo73 (its other movies, demo69 and demo74, stay missing). `onion_skip` plays and skips the
  stand-in blue Onion discovery (demo06) on the object stage. The sandbox has no
  enemies, bosses, plants, gates or bridges.
- Stand-in and disc runs can differ in timing. `throw_release` measures K, the
  press-to-grab delay, per run because the stand-in value need not equal the
  disc's. Today the two agree (13 at 30 Hz, 22 at 60 Hz), by a fitted sandbox
  gate after #92, not by construction.
- A scenario needs a GL context, even hidden. Without one the run exits 4. On
  Windows, Mesa's `opengl32.dll` and `libgallium_wgl.dll` must sit beside the
  executable.

## Mouse pointer

A hidden window (`--scenario`, the default) must not move the real mouse
pointer. `pc_window.cpp` skips `SDL_WarpMouseInWindow` when the window carries
`SDL_WINDOW_HIDDEN`, in `warpMouseToCentre()` and in the startup centre warp
(commit `3d1e66c0`). The cause had been inferred from the code. It was observed
on macOS (Apple M3 Max, 2560x1080 main display) on 2026-10-28 with a read-only
helper (`python3 -I` with pyobjc: `CGEventGetLocation` and
`NSEvent.mouseLocation`, in CoreGraphics coordinates, origin top left). The
helper never posts events. Each run is `boot_smoke` at 30 Hz on the stand-in tree.

| Case | Pointer before | Pointer after |
|------|----------------|---------------|
| Current binary, hidden | (-417.8, 964.0) | (-417.8, 964.0), unchanged |
| Both guards removed, hidden (one run) | (-417.8, 964.0) | (1280.0, 555.0), moved |
| Current binary, `--scenario-visible`, killed at the warp call | (1280.0, 555.0) | (1280.0, 555.0), no information (see below) |

Both guards removed moved the pointer to the centre of the 640x360 window (x
1280 is the middle of the 2560-wide display; y 555 is the window's content
centre, below the title bar; this reading of y is an inference). One run, so the
pointer moving is one observation, but it moved from a far position to the window
centre and the same binary with the guards did not move it. That confirms the
startup warp as the cause on this Mac.

For the visible case the pointer already sat at the warp target, so a position
check could not tell a warp from no warp. The run was repeated under `lldb` with
a breakpoint on `SDL_WarpMouseInWindow`, killed at the first hit so the warp never
ran: the visible run reaches the call (it warps, as before) and the hidden run
never reaches it (the whole scenario passed with the breakpoint armed). Pointer
positions were unchanged by those two runs.

Not covered: Linux and Windows (SDL's warp semantics differ; the guard is
platform-neutral C++ but was not compiled there), and the other
`SDL_SetRelativeMouseMode` call sites, which this change does not touch.

## Verification of this note

The commands in this note were run on 2026-10-28 on macOS arm64 (RelWithDebInfo,
Unix Makefiles, `PIKMIN_NATIVE_JAUDIO=OFF`):

- Configure and build of `pikmin_pc` and `nectar_standin_generator`: worked.
- `ctest -R ^scenario_ -LE game-assets`: 13 of 13 passed (generator plus 12
  scenario runs). `ctest -N -L game-assets` lists the 6 disc entries.
- The generator with `--manifest`: 314 files, 294 manifest paths in the tree,
  17 allowlisted, 0 missing. A manifest path that cannot be read exits 1; no
  arguments exits 2.
- The direct `nectar --scenario boot_smoke` command on the stand-in tree and
  on a disc tree (a scratch folder whose `assets` is a symlink to an install):
  both exit 0. The three disc scenarios pass at 30 Hz and `throw_release` at 60.
- `make_boot_manifest.py` with fresh `PIKMIN_LOG_FILE_OPENS` logs from the three
  disc runs reproduces `boot-manifest.txt` byte for byte.
- Exit codes 0, 1, 2 and 77, each rejected flag, the data-directory cases, the
  watchdog and `PIKMIN_SCENARIO_TRACE` were run. Exit 4 was not (no GL failure
  could be produced).
- `nectar_cinema_writer_test`, `nectar_blo_writer_test`, `nectar_pcr_writer_test`,
  `nectar_standin_objects_test` and `nectar_audio_writer_test` pass. Three rows of
  `standin-texture-experiments.md` re-run on the current tree (delete
  `consFont.bti`: exit 139; delete `effects/pcr/effects.bun`: exit 134 with
  `Cannot open file.[effects/pcr/ore_rec.pcr]`; delete `intro/nintendo.bti`: exit
  0) match.
- Not run: the native JAudio configuration, the Linux and Windows builds, CI, and
  `-G Ninja`.
- `gl_probe.c`, built with the command above, printed `GL_PROBE_OK` (4.1 Metal,
  Apple M3 Max).
