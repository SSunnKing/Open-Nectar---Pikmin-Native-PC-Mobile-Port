# Provenance of the disc-free scenario fixtures (#190)

The public CI fixtures are the generated stand-in tree and the code that writes it:
`libs/nectar_assets/src/standin_*.cpp`, the headers in
`libs/nectar_assets/include/nectar_assets`, `standin_generator.cpp` and the
`standin_*_stage.cpp` files in this folder. Each value in them is one of:

- **Source default**: a value the game source sets (a parameter's constructor
  default, a file path, pane tag, pane type or event ID the code names). Cited by
  file and function where it is used.
- **Authored**: a deterministic value chosen for the fixture (round numbers on an
  own grid), with a comment stating the code contract it keeps.

Nothing measured or copied from a retail disc is a fixture payload. Exact checks
against retail data are opt-in local lanes (last section). The README's
"Fidelity rule (#190)" states the policy.

## Fixture input classes

| Class | Where | Source | Disposition |
|---|---|---|---|
| Cinema `.cin` paths | `standin_cinema.cpp` | Code literals (`moviePlayer.cpp` `movieTitles` and translation tables) | Kept |
| Cinema key event types | `standin_cinema.cpp` | Code enums (`Interface.h` `MOVIECMD_*`, `demoEvent.cpp` `DemoEvent*`, `ogTutorial.h` `TUT_*`) | Kept: only the keys the game logic acts on |
| Cinema `.dsk` scene names | `standin_cinema.cpp` | Authored (`cinemas/standin/demoNN_a.dsk`) | Replaced retail scene names |
| Cinema frames, scene lengths, loop windows | `standin_cinema.cpp` | Authored on a 10-frame grid; each movie's comment states its contract (key inside a cut, key order, loop last) | Replaced retail-shaped schedules (demo17, 20, 26, 36, 46, 47, 73, 77, 78); `CleanupDayEnd` dropped from demo46 (no code path needs it) |
| Discovery cone frames (demo04/05/06) | `standin_cinema.cpp` | Tied to the stand-in Onion boot profile's `KEY_Action0` | Contract documented; values follow that profile (Onion boot item, see below) |
| Cinema cameras and lights | `standin_cinema.cpp` `standInScene` | Authored | Kept |
| `CinePlayerFlags` per movie | `standin_cinema.cpp` | NonGameMovie where the code needs time stopped (results backdrops); Localized where a movie is placed at a target; HideNavi/NaviNoAI authored | Kept |
| Layout file names | `standin_ui.cpp` | Code literals (`"screen/blo/<name>.blo"`, `bloFile_*` tables) | Kept |
| Pane tags and types | `standin_ui.cpp` | Code `search(.., true)` calls, cited per layout | Kept |
| HUD and Y-menu pane geometry | `standin_layout.h`, `standin_ui.cpp` | Authored on a 16-pixel grid; constraints from code (play10 drawn at x 0, play_day at the wide offset, play_day's y 300 split, `SunMove` between `stat` and `goal`, `ogDrawLR` arrow pivots) | Replaced; relations checked by `static_assert`, `checkStandInHudLayout`, `blo_writer_test` |
| Other layout geometry, text, colours | `standin_ui.cpp` | Authored; course names are game source literals (`DrawWorldMap.h`, `game.cpp`) | Kept |
| Texture names | `standin_textures.cpp` | Code literals (`loadTexExp` call sites, `effectMgr.cpp`, `DrawContainer.h`) | Kept |
| Particle names | `standin_pcr_names.cpp` | Code literals (`effectMgr.cpp`, `effectMgr2D.cpp`), drift-tested | Kept |
| Particle payload and version tag | `pcr.h`, `standin_ui.cpp` `inertPcrParticle` | Authored (inert particle, tag `"0-00"`; the port does not check the tag) | Tag replaced |
| Audio sequence archive | `standin_audio.cpp` | The game's BARC table compiled in (`src/jaudio/pikiseq.c`); no samples or banks | Kept |
| Parameter files (`parms/gamePrms.bin`, `aiconst.bin`, `demo.bin`, `cunit.bin`, `naviMgr.bin`, `pikiMgr.bin`) | `standin_generator.cpp` | Empty tagged lists: every parameter is its source default | Kept; `check_synthetic_parameters.py` enforces |
| `parms/key.bin` | `standin_generator.cpp` | Authored: six button masks, the Classic bindings (source default except extract on A) | Kept |
| `stages/stages.ini` entries | `standin_generator.cpp` | `practice.ini`, `stage1.ini` are code literals (`game.cpp` `setStage`); `course2.ini`..`course4.ini` authored | Retail-only names replaced |
| Stage `.ini` text | `standin_generator.cpp`, `standin_objects_stage.cpp` | `map_file` and `day_multiply 1.0`, the source default | Kept |
| Tutorial stage (`stages/practice/`) | `standin_practice_stage.{h,cpp}` | Authored placements, facings, push distance, route grid; object names and the push box's pushers and speed are code names and source defaults | Kept |
| Onion stage and world-map courses | `standin_objects_stage.{h,cpp}` | Authored positions; the `next` ownership gate is the game's (`GenObjectItem::birth`) | Kept |
| Open allowlist | `standin-open-allowlist.txt` | Policy file; paths are code literals or code-composed names | Kept; comments state what the chain needs, not retail contents |
| Golden screenshots | `golden/<os>/*.png` | Renders of the stand-in tree (outputs, not inputs) | macOS `boot_smoke_tick100.png` regenerated locally for the authored HUD; Linux and Windows `boot_smoke_tick100.png` need a hosted refresh (`refresh_goldens.sh`) |
| Synthetic throw expectations | `check_throw_flight.py` `SYNTHETIC_EXPECTED` | The port's own results on the authored fixtures | Kept (public CI profile) |
| Model rigs and collision spheres | `standin_models.cpp`, `piki_rig.h`, `standin_objects.cpp` (naka, chappy), `standin_world*.cpp` | Authored: flat rigs (root plus a fan), round-number spheres; only the ids the code looks up (`cent`, `slot`, `lleg`, `rleg`, `rhnd`, `ante`, `cont`, `piki`, ship parts) are required | Replaced |
| Pellet and Main Engine configuration, `pelAnim` override joints | `standin_objects.cpp` | Authored: carriers 12 to 24, source defaults elsewhere, every override joint -1 | Replaced |
| Enemy and boss parameters (`tekipara/*.bin`, boss `parms.bin`) | `standin_world.cpp`, `standin_world_data.cpp` | Not written (enemies) or empty lists (bosses): `TekiMgr` falls back to constructor defaults; `check_synthetic_parameters.py` enforces | Replaced |
| Sandbox gate, dressing path | `standin_sandbox.cpp`, `standin_map_dressing.cpp` | Authored placements and a 100-unit panel half-width | Replaced |
| Pikmin and captain animation schedules | `standin_anims.{h,cpp}` | Authored from the key each state machine reads: throw 40 frames (release 10); ThrowWait grab 10, loop 10 to 34 (charge +1 per loop end); hit cycle 10/15/30; PickLoop 12/16/28; Kizuku 19; Fue 24; Wait 40 loop; Attack 24; JHit/JKoke 20; StillJump 32; GetUp 15; Gakkari 60 | Replaced |
| Object animation profiles (45 bundles) | `standin_object_profiles.cpp` | Authored recipe (shared 40 frames; finish, loop and `KEY_Action0` infos from the code contracts) | Replaced |
| Onion boot motion | `standin_onion_boot.h`, `standin_object_profiles.cpp`; demo04/05/06 cone frames; `check_onion_boot_negative.py`; the `cinema_writer_test` demo05 relation | Authored: 300 frames, route-effect (value 2) at 100, `KEY_Action0` seed at 150, the same for every colour | Replaced |
| Scenario test controls | `scenario_ballistic.cpp` (flower glide scale 0.4, start height 100), `scenario_bulborb_fight.cpp` (AIConstant p08, p10 off), `scenario_pikmin_cap.cpp` (top-up to the limit before each emission) | Authored test controls, not fixture files; no retail value is used (0.4 was chosen so a glide reaches an apex and lands inside the two-second window) | Added

## Local lanes (opt-in; they read user-supplied data and skip, or do nothing, without it)

The CTest entries carry the `game-assets` label and `SKIP_RETURN_CODE 77`; the
`NECTAR_DISC_DATA` halves run inside the public writer tests only when the variable is set.

| Lane | Input | Notes |
|---|---|---|
| `scenario_<name>_<rate>_disc`, `scenario_onion_day3*_disc`, `scenario_*_onion_boot_*_disc` | `PIKMIN_SCENARIO_DATA_DIR` | The scenarios on a real install |
| `scenario_throw_fidelity_disc` (`run_disc_throw_fidelity.py`, `check_throw_flight.py --profile disc`) | `PIKMIN_SCENARIO_DATA_DIR` | Exact throw calibration |
| `check_ballistic.py --disc-reference`, `run_disc_ballistic_reference.py` | a disc run's logs | Not run by the public workflow |
| `scenario_standin_manifest` (`nectar_standin_generator --manifest-from-env`) | `PIKMIN_BOOT_MANIFEST`, made locally with `make_boot_manifest.py` | The tree against a boot open-log; `kManifestAllowlist` gives each tolerated miss a reason |
| `nectar_blo_writer_test`, `nectar_pcr_writer_test` disc halves | `NECTAR_DISC_DATA` | Tag-table and parser checks on the user's files |
| `nectar_mod_writer_test`, `nectar_anim_writer_test`, `nectar_standin_objects_test` manifest argument | a local boot manifest passed by hand | No longer passed by CTest |

Local-lane tools that store no disc data and may ship with a local-only note:
`make_boot_manifest.py`, `measure_onion_boot.py`, `run_disc_throw_fidelity.py`,
`run_disc_ballistic_reference.py`, `record_rng_provenance.py`.

## Not part of this package

Kept on the fork for the local exact lane and the tracker; none of it is a fixture
payload and none is shipped here:

| Item | Why it stays out |
|---|---|
| A retail boot open-log (`boot-manifest.txt`) | A file-name table of a disc run; the manifest lane generates one locally with `make_boot_manifest.py` |
| Retail executable traces for the ballistic arcs and their hashes | The `--disc-reference` half of `check_ballistic.py` reads them from `PIKMIN_BALLISTIC_EVIDENCE` |
| Retail parameter tables, per-frame retail measurements, disc run logs and personal decision records | Not needed by any public check; the local lanes (above) assert measured expected outputs against the user's own install |
| `onion_day3_disc_courses.inc` (Onion generator placements of a user's install) | Included by `scenario_onion_day3.cpp` only when present; without it the `_disc` entries of that scenario skip |
| Combat timing measurements against the retail game (issues #154, #180) | A local exact-lane question; the public `bulborb_fight` asserts smoke limits only |
| `libs/nectar_assets/art/` (Blender sources, exports and rig metadata) and the art-export workflow | Fork art pipeline; the stand-in models are authored in C++ |

## Measured expected outputs (kept separate from fixtures)

- `check_throw_flight.py` `EXPECTED` (profile `disc`): throw timing, peak and launch
  speed of the original game, asserted only by `scenario_throw_fidelity_disc`.
- `scenario_onion_boot_carry.cpp` `PIKMIN_ONION_BOOT_EXACT=1` frames: the original
  game's route-effect and seed frames, asserted only by the `_disc` entries.
- `SYNTHETIC_EXPECTED` and the cinema relations are the port's own results on the
  authored fixtures.
