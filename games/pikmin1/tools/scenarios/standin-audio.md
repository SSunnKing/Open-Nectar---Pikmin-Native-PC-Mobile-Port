# Stand-in audio for native JAudio scenario builds

The default scenario build configures `PIKMIN_NATIVE_JAUDIO=OFF`, so the legacy
facade (`pc_port/dolphin_stubs/audio_stubs.cpp`) answers every `Jac_*` call and
no sound data is read. Release builds use `PIKMIN_NATIVE_JAUDIO=ON`, which runs
the original engine in `src/jaudio`. This note records what that engine needs to
boot and play a stage with no disc data, what the stand-in tree supplies, and
how to run the scenarios with it.

Measured on macOS (M3 Max), RelWithDebInfo, `VERSION_GPIE01_01`, 2026-10-28.

## What the native engine needs

`System::Initialise` calls `Jac_Start(..., "/dataDir/SndData/")`
(`sysDolphin/system.cpp`). Under `PIKI_PC_PORT` DVD tasks finish inline.

1. **`Seqs/pikiseq.arc`.** The `.arc` has no header of its own. Its table of
   contents (`pikiseq.hed`) is compiled into the executable as `HEAD_pikiseq`
   (`src/jaudio/pikiseq.c`, one table per game version), and `JV_LoadFile`
   (`virload.c`) reads `[offset, offset + size)` of the `.arc` for a sequence.
   `Jac_InitBgm` (`piki_bgm.c`) loads sequences 0, 1, 11, 13 and 16 and starts
   0 on root track 0, 1 on root track 1 and 13 on root track 2.
2. **Track 0x20000 with 16 children.** `Jac_InitEventSystem` (`pikiinter.c`)
   loops with no timeout until `Jam_GetTrackHandle(0x20000)` exists, then reads
   `children[0..15]`. A track gets a name only from sequence bytecode:
   `ConnectName` (0xD0) then `ConnectOpen` (0xE5) call `Jam_RegistTrack`
   (`jammain_2.c`). With no `.arc` the boot hangs here.
3. **The other names the game looks up.** `Jam_PauseTrack` and
   `Jam_UnPauseTrack` dereference their argument without a null check
   (`jammain_2.c`), and they are called on 0x20000 (`piki_scene.c`
   `Jac_SceneSetup(SCENE_Course)`, `verysimple.c`, `pikidemo.c`) and on 0x1000F
   (`verysimple.c`). The rest are null-checked or only written to: 0x10000 and
   0x30000 (`piki_bgm.c` `Jac_GameVolume`), 0x10003, 0x10007, 0x10008, 0x1000A
   (`piki_player.c`), 0x10009 (`verysimple.c`), 0x30000 and 0x1000F
   (`pikidemo.c`).
4. **`Jac_SceneSetup(SCENE_Title)` before the stage.** `Jac_SceneSetup` spins
   for any scene other than BootUp and Title until `first_load` is set, and only
   the Title scene sets it (`piki_scene.c`, `__Loaded` case 0x20000). The
   scenario boot skips the title section (`ninLogo.cpp`), whose constructor
   ends with that call (`titles.cpp`). This is a scenario-boot gap, not a data
   gap: a native build hangs on disc data too without the call (run on
   2026-10-28, stack in `Jac_SceneSetup`; `verification-log.md`, item 1). `pc_scenario_pre_stage` now makes the call in
   native builds (`#if PIKI_USE_JAUDIO`), standing in for the skipped section.
   The facade has no such wait, so OFF builds are unchanged.

Not needed:

- **`Banks/pikibank.bx`.** `Bank_Setup` (`bankloader.c`) returns when the file
  is missing, so no wave group exists, and every `WaveScene_Set/Load/Close`
  (`waveread.c`) returns early on a null group. Without a bank no wave archive
  (`Banks/*.aw`) is opened. Notes would find no instrument, but the stand-in
  sequences play none.
- **Streams (`*.stx`).** Only demos open them; the v1 scenarios play none.

## What the generator writes

One file, `assets/dataDir/SndData/Seqs/pikiseq.arc`, built by
`standInSndDataFiles` (`libs/nectar_assets/include/nectar_assets/standin_audio.h`)
from the `HEAD_pikiseq` the generator compiles in (`src/jaudio/pikiseq.c` with
this build's `PIKMIN_GAME_VERSION`). It has the same length as the table
describes (219,392 bytes for `VERSION_GPIE01_01`), with each sequence at its
table offset and zeros after it. No disc byte is used.

| Entry | Name | Started on | Tracks |
|-------|------|------------|--------|
| 0 | `pikise.jam` | root 0 | 0x10000, 16 children named 0x10001 to 0x1000F (slot 0 unnamed) |
| 1 | `sysevent.jam` | root 1 | 0x20000, 16 unnamed children |
| 13 | `demobgm.jam` | root 2 | 0x30000 |
| every other used entry | | loaded by `Jac_ReadyBgm` / `Jac_PlayBgm` | one unnamed track |

Every track ends in a wait of 0xFFFF ticks and a jump back to it, so it never
finishes and never plays a note. Which slot carries which name on the disc does
not matter: the game only looks tracks up by name. The bytecode uses five
opcodes, listed in `audio_seq.h` with their `jammain_2.c` source.

The OFF facade never reads the `.arc`: `pc_audio_load_wave_bank` loads it only
after `Banks/pikibank.bx` (`pc_audio.cpp`), which is not written. The OFF tree is
therefore the same as before apart from this one file.

### Writers and test

- `audio_seq.h`: `readSeqArcHeader` (BARC table to entries),
  `writeJamIdleSequence` (a `JamIdleTrack` tree to JAM bytecode) and
  `writeSeqArc` (sequences placed by the table). Plain input structs, bytes
  out, an error string, no file I/O.
- `standin_audio.h`: `standInSeqTracks` (the table above) and
  `standInSndDataFiles`.
- `nectar_audio_writer_test` (registered in `games/pikmin1/CMakeLists.txt`):
  reads the real `HEAD_pikiseq`, writes the `.arc`, loads it with the port's
  `PCSequenceArchive`, checks each sequence's bytes, runs each one for 300
  ticks with the port's `PCJamPlayer` (still running, no notes, the 16
  sysevent children open), walks the connection names, and checks the error
  cases. The engine's own reader (`virload.c`, `jammain_2.c`) needs the audio
  thread and DVD layer and does not link into a test; the scenario runs below
  are its check.

## Results

Each run on the stand-in tree from the generator, with
`PIKMIN_NATIVE_JAUDIO=ON`, `SDL_AUDIODRIVER=dummy`, hidden window, hang guard
`perl -e 'alarm N; exec @ARGV'`.

| Tree | Result |
|------|--------|
| Stand-in tree with an empty `SndData/` | Hang. Five `pikiseq.arc` and one `pikibank.bx` FAILED lines, then no progress; killed by the 60 s alarm (exit 142). Matches the prediction in #68. |
| `pikiseq.arc` stand-in, without the title scene call | Passes `Jac_InitEventSystem`, then hangs after the map loads (in `Jac_SceneSetup(SCENE_Course)`); killed at 120 s. Re-run with `sample`: the main thread spins in `Jac_SceneSetup` under `NewPikiGameSetupSection`, and a disc clone hangs the same way (`verification-log.md`). |
| `pikiseq.arc` stand-in, with the title scene call | `boot_smoke`, `throw_release`, `day30_clock` at 30 and 60: all exit 0. |

The only new `FAILED to open` line is `SndData/Banks/pikibank.bx`, now in
`standin-open-allowlist.txt`. `throw_release` measures the same K as the OFF
build (15 at 30 Hz, 18 at 60 Hz) and the same throw distances, so the native
engine does not disturb the sim.

CTest in the JAudio-ON build: `ctest -R '^scenario_' -E '_disc$'` passes 7 of 7
(generator plus six scenarios). The disc entries in the same build
(`ctest -R '_disc$'` with `PIKMIN_SCENARIO_DATA_DIR` set to a real install)
pass 6 of 6 with real sound data and the title scene call.

On disc data the native `boot_smoke` at 30 opens, under `SndData/`:
`Seqs/pikiseq.arc`, `Banks/pikibank.bx` and the wave archives `pikise_0` to
`pikise_3`, `demose_0`, `diary1_0`, `diary2_0`, `play3_0`, `jungle_0` and
`boss2_0` (`.aw`). Paths only; nothing was copied.

## Running it

A second configure; the CTest entries are the same ones the OFF build
registers, so nothing extra is registered:

```sh
cmake -S games/pikmin1 -B build/pikmin1-scenarios-jaudio \
    -DPIKMIN_NATIVE_JAUDIO=ON -DPIKMIN_BUILD_SCENARIOS=ON
cmake --build build/pikmin1-scenarios-jaudio --target pikmin_pc nectar_standin_generator nectar_audio_writer_test
ctest --test-dir build/pikmin1-scenarios-jaudio --output-on-failure -R '^scenario_|^nectar_audio_writer_test$' -LE game-assets
```

`pc_jaudio_integration_test` (label `game-assets`) still needs real sound data;
`-LE game-assets` leaves it out.

### CI

`.github/workflows/ci.yml` job `jaudio-scenarios` is this configuration on
Linux, macOS and Windows (MinGW-w64): the build above without
`nectar_audio_writer_test` (the main jobs already run it), then
`ctest -R ^scenario_(boot_smoke|throw_release|day30_clock)_ -E _disc`
(7 entries with the generator) with the same environment as the OFF scenario step.
Each OS is its own build directory, since `PIKI_USE_JAUDIO` changes the compile
definitions of every game file and ccache shares little with the OFF build. The
job uploads its state-hash logs, and `scenario-hash-compare` checks that they
equal the OFF build's logs and each other (README, "CI").

## What remains

- **No bank, no notes.** Instrument lookup, `JAudioHost_LoadBX`, wave scene
  loads, ARAM and the DSP host get no work. A stand-in `pikibank.bx` with one
  silent wave and sequences that play it would cover them; the facade would
  then also load the `.arc` and run `PCJamPlayer` on it in OFF builds.
- **Demos and streams.** No scenario plays a cutscene. The demo paths wait on
  loads (`pikidemo.c`, `now_loading < 3`) that were not exercised here.
- **Event actions never finish.** The sysevent children ignore their command
  queues, so an event slot never reports done (`TrackReceive`,
  `MML_StopEventAction`). The v1 scenarios do not depend on it.
- **Platforms.** The CI job above compiles and runs the JAudio-ON build on all
  three OSes. The disc-data run (`_disc` entries, real sound banks) was done on
  macOS only.
