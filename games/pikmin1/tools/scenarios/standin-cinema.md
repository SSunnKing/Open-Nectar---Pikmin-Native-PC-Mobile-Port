# Stand-in cinemas

Cutscene support for the disc-free scenario tree. `nectar_standin_generator`
writes the movies the stand-in scenarios play. The game's real movie code
(`MoviePlayer`, `CinematicPlayer`, `SceneData`, `DemoEventMgr`) runs on them,
including the key events and the skip replay.

## Formats

Both formats are CmdStream text: tokens split on whitespace and commas. A token
that starts with `#` or `//` skips the rest of the line, but only where a token
starts. Inside a `{ }` section the loaders test for `}` before they skip
comments, so a comment there is read as tokens. The writers emit comments only
before the first command. CmdStream reads into one shared 0x8000-byte buffer.
Every stand-in file stays under 0x4000 bytes, so it is read in the first fill.

### `.cin`: the movie script

Read by `CinematicPlayer::loadCin` (`plugPikiColin/cinePlayer.cpp`). Path from
`movieTitles` in `moviePlayer.cpp`, after `translateIndex`. A stage with no `id`
(the sandbox, `STAGE_TESTMAP`) translates as Impact Site.

| Command | Meaning |
|---------|---------|
| `type N` | Read, never used. |
| `flags N` | `CinePlayerFlags` (`CinematicPlayer.h`). For example, Localized places the movie at the target's position, HideNavi hides the captain, NaviNoAI stops the captain's AI, and PauseAll pauses the game. |
| `addScene { scene <path.dsk> }` | One per scene, in index order. |
| `addActor { shape <mod> bundle <anm> anims <bin> }` | Loads a model through `gameflow.loadShape` and an AnimMgr. |
| `addCut { cut <scene> <start> <end> flags F actor <mod> acflags A anim S C keys N { type cmd value frame ... } }` | Plays scene frames `start` to `end`. The playback mode is `(F >> 1) & 7`: 3 plays once, 5 loops until skipped. |

Key `type 0` (Notify) calls `gameInterface->message(cmd, value)` with a
`MOVIECMD_*` (`Interface.h`). `cmd 0` is `TextDemo`: it opens the tutorial
text whose number is `value` (`ogTutorial.h`). Key `type 1` (Action) calls
`demoEventMgr->act(cmd, value)`. Here `cmd` is the sender and `value` is the
event (`demoEvent.cpp`, for example sender 3 = red Onion, event 5 = Boot).
`frame` is a scene frame. A key fires when playback passes that frame. A skip
replays the remaining keys, except `TextDemo` (`CinematicPlayer::skipScene`).

What crashes the loader, and what the writer therefore refuses:

- A cut when there is no scene. `addSceneCut` reads `findScene(0)->mNumFrames`.
- A cut actor that is not in the actor list.
- `acflags` or `anim` with no actor before it.

### `.dsk`: one scene's camera and lights

Read by `SceneData::parse` and `getAnimInfo` (`sysCommon/shapeBase.cpp`).

| Section | Content |
|---------|---------|
| `<SCENE_KEY_ANM_INFO> { numframes N  numcameras C  numDifLights L }` | Other lines are skipped one line at a time. Must come before the tables. |
| `<KEY_CAMERA_ANM> { size N  float v v v v v v v v  float ... }` | `DataChunk::getData`: a `float` word before every run of 8 values. Size 0 would swallow the closing brace, so an empty chunk is left out. |
| `<KEY_CAMERA_TABLE> { index i  name n  cam_pos_x E O F ... }` | One per camera. The fields are `cam_pos_xyz`, `cam_lat_xyz` (look-at), `cam_twist`, `cam_fovy`, `cam_near`, `cam_far`. |
| `<KEY_DIFFUSE_LIGHT_ANM>`, `<KEY_DIFFUSE_LIGHT_TABLE>` | The same for lights: `light_pos_xyz`, `light_r/g/b_param` (0 to 255), `light_visible` (on only at exactly 1.0). |

A track is `entries offset flags`. If entries is 0, the game uses its default:
0 for position, 1.0 for fovy (so the writer requires fovy). If entries is 1,
the value is `data[offset]`. If entries is 2 or more, each key is three floats
(frame, value, tangent), read as a Hermite curve (`extract`). Every movie with a
scene needs a camera, because `MovieInfo::setCamera` reads `mCameraData[0]`.

## Writers

`libs/nectar_assets/include/nectar_assets/cinema.h`:

- `writeCin(const Cinema&, std::string&, std::string& error)`
- `writeDsk(const DskScene&, std::string&, std::string& error)`
- `writeStandInCinemas(std::vector<StandInCinemaFile>&, std::string& error)`

These are plain input structs with text output and no file I/O. The generator
calls `writeStandInCinemas` from `standInCinemas()` in `buildTree()`.

Test: `nectar_cinema_writer_test`. It reads the output back with the real
`CmdStream`, through transcribed copies of `loadCin`, `SceneData::parse`,
`getAnimInfo` and `DataChunk::getData`. Those cannot link into a host test.
Drift note: update the copies if those functions change. The test checks round
trips, the refusals above, determinism, and the contracts listed below as
relations on the authored values (key present, key order, a key inside its cut,
the loop cut last).

## Provenance (#190)

Authored fixtures. The game code fixes only:

- which movie a `.cin` path is (`movieTitles` and the translation tables in
  `moviePlayer.cpp`);
- the `CinePlayerFlags` it reads;
- the key events a movie's game logic acts on, by their code enums
  (`demoEvent.cpp`, `Interface.h` `MOVIECMD_*`, `ogTutorial.h` `TUT_*`).

Everything else is chosen in `standin_cinema.cpp`: the scene names (all under
`cinemas/standin/`), the cameras and lights, every scene length, key frame and
loop window, on a 10-frame grid. There are no actors, so no model or animation is
opened. Each scene is a short dolly towards the origin (or towards the target,
for a Localized movie), 30 degrees, with one white light.

## Stand-in set

Every cut plays once except the last cut of demo36, demo73, demo77, demo20 and
demo78, which loops, so every other movie ends without a skip.

| File | Movie | Flags | Keys (authored frames) | Contract the code needs |
|------|-------|-------|------------------------|-------------------------|
| `demo60.cin`, `demo61.cin` (60 + 30 frames) | Post-extinction bonus seeds (`DEMOID_Unk60Cat`); the sandbox plays demo60 from `finalSetup` on Day 1, demo61 is the Forest of Hope's | HideNavi, NaviNoAI | BonusPiki from the blue, red and yellow Onions at 10; `TextDemo` `TUT_Zenmetu` (13) at 50 | A non-zero-length movie with a camera, so the intro's START skip has something to skip; BonusPiki acts only for an owned Onion; `TUT_Zenmetu` opens `tu_tx20.blo` |
| `demo41.cin` (90) | Forest of Hope landing (`DEMOID_LandingForest`), played when a scenario enters `stages/stage1.ini` | HideNavi, NaviNoAI | yellow Onion Land at 10, EmitCone at 70 | The two Onion keys the landing logic acts on (both guarded by `hasContainer`) |
| `demo45.cin` (40 + 40) | Pikmin into the Onion, Forest of Hope | HideNavi, NaviNoAI | none | A movie to play (`demo_timers` reaches it) |
| `demo04.cin` (330), `demo06.cin` (300), `demo05.cin` (300) | Red, blue and yellow Onion discovery (`DEMOID_FindRedOnyon`, the sandbox's translation of `DEMOID_FindOnyon`; `DEMOID_FindBlueOnyon` and the yellow movie 5, started untranslated by their `DEMOFLAG_Discover*`) | Localized, HideNavi, NaviNoAI | Boot at 10; EmitCone at 300 (red) or 270; red also `TextDemo` `TUT_RedOnion` (1) at 310 | Boot < Boot + the boot motion's `KEY_Action0` frame (first seed) < EmitCone, with frames free between, for the #205 skip cases. The seed frame is the stand-in Onion profile's (`standin_object_profiles.cpp`); the cone frames move with that profile |
| `demo46.cin` (60) | Pikmin extinction (`DEMOID_Extinction`), Localized at the captain | Localized, HideNavi, NaviNoAI | FadeOut at 30; `GameEndCondition` (PikminZero) at 50 | The second `GameEndCondition`: with the end flag set it starts `DEMOID_ExtDayEnd` and sets the clock to the end hour (`GameMovieInterface::parse`) |
| `demo47.cin` (60) | Day end after an extinction (`DEMOID_ExtDayEnd`, translated per stage; the sandbox plays Impact Site's demo47) | HideNavi, NaviNoAI | FadeIn at 0; `ForceResults` at 50 | `ForceResults` clears the end flag, releasing `MessageModeState` into the day end; without it the chain loops |
| `demo36.cin` (30 + 15) | End-of-day results backdrop (`DEMOID_EndOfDayResults`, started by `DayOverModeState::initialisePhaseTwo` with the results window) | NonGameMovie | none | Stays active until `exitDayEnd` drops it (the last cut loops), since `DayOverModeState::update` waits on the movie; NonGameMovie stops the clock |
| `demo73.cin` (40 + 15) | Bad ending, Olimar fails to escape (`DEMOID_BadEndingFailEscape`) | NonGameMovie | `StartTotalResults` at 20 in the first cut | The key starts the final results window; the loop keeps the movie active until the window reports done and `DayOverModeState::update` skips the scene |
| `demo77.cin` (40) | Flying through space (`DEMOID_EndingSpace`): the neutral ending's phase three, the happy ending's phase four | NonGameMovie | FadeIn at 0, `StartTotalResults` at 20; then frames 30-40 in a loop | As demo73; without the loop the neutral ending runs phase four (GameExit) with the window unread |
| `demo26.cin` (60) | The last ship upgrade (`DEMOID_ShipUpgradeLast`), queued when a delivery brings the 30th part | HideNavi, NaviNoAI | `SpecialDayEnd` at 45 listed before FadeOut at 40 | `SpecialDayEnd` is the happy ending's trigger. Listing it first exercises the skip replay's frame-order sort (`pc_cine_sort_skip_keys`), which `ending_happy_skip` checks |
| `demo20.cin`, `demo78.cin` (130) | The Main Engine reaching the ship on the Impact Site (`DEMOID_CollectPartPractice`); the Whimsical Radar reaching the ship (`DEMOID_CollectRadar`) | HideNavi, NaviNoAI | `TextDemo` `TUT_GetParts` (17) at 100 in a 120-frame cut, then frames 120-130 in a loop | The cut outlasts the engine's suck into the ship (about 2 s); the key skips the scene (CollectEngine) or opens the part's text, whose close skips it (PartCollect), which ends the loop |
| `demo17.cin` (60) | The ship's first upgrade on the Impact Site (`DEMOID_ShipUpgradePractice`) | HideNavi, NaviNoAI | ship Henka Action at 10, `ForceDayEnd` at 40, `TextDemo` `TUT_GetEngine` (9) at 50 | `ForceDayEnd` ends the tutorial's Day 1 (its clock never reaches sunset); `TUT_GetEngine` opens `tu_tx15.blo`; Henka is optional |
| `opening.cin` (30) | Title opening (`titles.cpp` `CinematicPlayer`) | none | Notify 0 at 0 | ShowStartMenu at frame 0 |

A missing `.cin` is tolerated: the movie finishes in one tick and drops its keys.
The movies the scenarios do not need (the tutorial's flag movies demo03, demo07,
demo09, demo14, demo15 and demo16, the other stages' translations, the good-ending
movies demo75, demo113 and demo76, and others) play as zero-length movies and are
listed in `standin-open-allowlist.txt`. The scenarios assert the flags and the
state the code sets, not those movies.

The text keys need their screens: `TUT_Zenmetu` opens `tu_tx20.blo`,
`TUT_RedOnion` `tu_tx03.blo`, `TUT_GetEngine` `tu_tx15.blo` (`ogTutorialData.cpp`).
A missing screen panics (`Cannot open file`), so `standin_ui.cpp` writes each as a
one-box tutorial page.

## Who plays what

- Every scenario's intro: demo60 on the sandbox; the runner's intro gate presses
  START on the first movie tick, and the skip replays the BonusPiki keys, which do
  nothing without an owned Onion.
- `onion_skip` (demo06), `onion_day3` and its variants (demo05, demo41),
  `tutorial_firsts`, `tutorial_day1` (demo04): the discovery skip
  cases; see `scenario_onion_skip.cpp` for the START frames and assertions.
- `extinction`, `extinction_day30`, `challenge_extinction` (demo46, demo47; on Day
  30 demo46 then demo73).
- `day_end`, `captain_death`, `challenge_day_end`, `challenge_captain_death` (demo36).
- `ending_bad_day30` and `ending_neutral_day30` (demo73 or demo77), `ending_happy` and
  `ending_happy_skip` (demo26, then demo77).
- `tutorial_day1` (demo20, then demo17), `ship_part_carry` (demo78).
- `title_boot`, `title_round_trip`, `world_map_day_end` (opening).

## Exact checks

The retail movies' own schedules are not part of this tree and are not recorded
here. Comparing a scenario's behaviour with a real install is a local lane only
(the `_disc` CTest entries, label `game-assets`, with `PIKMIN_SCENARIO_DATA_DIR`
pointing at the user's own install).
