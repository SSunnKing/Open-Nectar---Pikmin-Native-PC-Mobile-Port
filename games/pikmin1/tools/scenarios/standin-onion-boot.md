# Natural Onion boot route — W2-4

Measured 2026-11-05 for fork #100 W2-4, from base `c34b4e617`.
The stand-in profiles now reproduce the measured Onion boot metadata and
route-opening behavior. Original gameplay code is unchanged.

## Disc identity and read-only access

Local `Pikmin-USA-Rev1.iso` header bytes are `47 50 49 45 30 31 00 01`
(`GPIE01`, revision 1); the extracted install marker is `GPIE01 revision=1`.
Both measured goal files were also located through the ISO's FST and compared
byte-for-byte with the mounted extraction; both matched the hashes below.
The scenario build uses `VERSION_GPIE01_01`, RelWithDebInfo,
`PIKMIN_BUILD_SCENARIOS=ON`, `PIKMIN_NATIVE_JAUDIO=OFF`, macOS arm64.
A UDRO image made from the existing extracted install was attached with
`hdiutil attach -readonly -nobrowse`; `diskutil info` reported both
`Media Read-Only: Yes` and `Volume Read-Only: Yes (read-only mount flag set)`.
All disc reads/runs below used `/Volumes/Pikmin-W2-4-Disc`, holding `assets/`.
Scenario saves use the runner's temporary save directory.

The structural reader is reproducible without exporting curves, textures or geometry:

```sh
python3 games/pikmin1/tools/scenarios/measure_onion_boot.py /Volumes/Pikmin-W2-4-Disc
```

`assets/dataDir/objects/goal/` SHA-256:

- `anims.bin`: `96af228967f120be03939f064ba88a14f11b3c9b1ee6a18fb8994f5a04cafb4d`
- `goal.anm`: `f8b45c91df46ea19bc40aff2478c2a198b6ad00ecd4f63b8e8f4b5d559597ee1`

## AnimMgr and profile evidence

Table version 1, 13 infos; manager flags `a00=2`, base directory
`objects/goal/motion`. All three boot infos have `p00=2`, `spd=30`,
13-joint DCK payloads, and one info key `(anim-key index 1, type 2 / Action0)`.
`GoalAI::BootInit` selects motion `mOnionColour + 3`.

| Colour | Info | Bundle entry suffix | Payload frames | Raw anim keys | Loaded anim keys |
|---|---:|---|---:|---|---|
| Blue | 3 | `6_onyo_1.dck` | 360 | 0, 240, 444 | 0, 240, 359 |
| Red | 4 | `4_onyo_1.dck` | 472 | 0, 270, 471 | 0, 270, 471 |
| Yellow | 5 | `5_onyo_1.dck` | 360 | 0, 240, 444 | 0, 240, 359 |

`AnimInfo::checkAnimData` clamps keys beyond the payload to `N-1`.
The profiles store those loaded keys. Effect records below are **in file order**,
not frame order. Each is event type 1 (Action), command id 0, with the listed
frame and key type/value. `PaniAnimator::checkEventKeys` sends `KEY_PlayEffect`
with that value; `AICreature::animationKeyUpdated` calls `GoalItem::playEffect`.

| Colour | Effect `(frame, value)` records |
|---|---|
| Blue | (20,0), (126,1), **(194,2)**, (200,3), (25,4) |
| Red | (31,0), (153,1), **(221,2)**, (227,3), (32,4) |
| Yellow | (20,0), (126,1), **(194,2)**, (200,3), (22,4) |

For boot motions 3–5, `GoalItem::playEffect(2)` opens its route waypoint;
value 4 starts the colour fade, while 0/1/3 emit particles. `GoalItem::startAI`
closes the route for an unbooted Onion. `startBoot` starts BootInit at speed 30
and sets the boot bit. Action0 emits one seed through `GoalAI::BootEmit`.
The missing blue/yellow effects were therefore gameplay-relevant metadata.
Blue also had an intentionally shortened 100-frame profile with Action0 at 60;
that is replaced by the measured 360-frame profile.

## Natural boot and carry

The scenarios call the ordinary `startBoot` API on an existing, unbooted Onion
with its waypoint closed. They never skip a boot movie, call `pcSnapLanded`,
write the waypoint, or advance the animator manually. Every boot tick checks
that the route stays closed through its effect frame and opens afterward.
They require one seed at the measured Action0 frame, natural return to Wait,
then two newly born same-colour Pikmin use the real carry AI to deliver a red
1-pellet placed 100 units beside the Onion. Delivery must target that Onion.

| Colour | Rate | First open sample (motion frame) | Seed frame | Return to Wait, seconds |
|---|---:|---:|---:|---:|
| Blue | 30 | 195 | 240 | 11.93 |
| Blue | 60 | 194.5 | 240 | 11.95 |
| Yellow | 30 | 195 | 240 | 11.93 |
| Yellow | 60 | 194.5 | 240 | 11.95 |
| Red | 30 | 222 | 270 | 15.67 |
| Red | 60 | 221.5 | 270 | 15.68 |

**Every entry is identical on the disc and stand-ins.** Effects use the interval
`[oldFrame, newFrame)`, explaining the first sample *past* the effect frame.
Seed times from scenario tick 0 are 7.967/7.983 seconds for blue/yellow and
8.967/8.983 for red (30/60 Hz), also identical across assets.

All six runs delivered. Disc carry durations after Wait: blue 4.47/4.17 s,
yellow 4.50/4.13 s, red 4.43/4.13 s. Stand-in durations: blue 4.83/4.45 s,
yellow 4.40/4.00 s, red 4.83/4.35 s. Carry travel time is **not** asserted equal:
the maps, routes, and stand-in geometry differ. The measured behavior being
matched is boot metadata, route-opening/seed timing and natural completion;
carry proves the resulting route is usable.

```sh
ctest --test-dir build/pikmin1-scenarios \
  -R 'scenario_(blue|yellow|red)_onion_boot_' --output-on-failure
PIKMIN_SCENARIO_DATA_DIR=/Volumes/Pikmin-W2-4-Disc \
  ctest --test-dir build/pikmin1-scenarios \
  -R 'scenario_(blue|yellow|red)_onion_boot_.*_disc' --output-on-failure
```

Disc blue uses Forest Navel (`stages/stage2.ini`), red/yellow Forest of Hope
(`stages/stage1.ini`). Stand-in blue uses `sandbox_objects`, red/yellow
`sandbox_onions`. Stage setup is explicit in CTest.

Before the correction, the new blue/yellow checks failed at both rates; the
red comparison passed. With the corrected profiles they pass. The mutation
control copies the generated tree and changes only the two frame-194 effect
values from 2 to unused id 255. Blue/yellow must each exit 1 specifically at
the missing-route assertion; unrelated errors fail the control. Both rates
were run. The shared asset tree and gameplay code remain untouched.

```sh
python3 games/pikmin1/tools/scenarios/check_onion_boot_negative.py \
  --game build/pikmin1-scenarios/bin/nectar \
  --data build/pikmin1-scenarios/scenario-data --rate 30 \
  --work-dir /tmp/onion-boot-negative
# Repeat with --rate 60.
```

## Synthetic discovery fixture and limits

Restoring blue's Action0 to 240 caused unchanged `onion_skip` to fail its
watched case at both rates: its synthetic movie's EmitCone at 90 ended BootInit
before a seed. The blue fixture now uses Boot 10, EmitCone 270, end 300, like
the existing synthetic yellow movie. These **movie timings are fixture choices**,
not disc measurements. The post-seed START checkpoint moves from 80 to 260;
the scenario now checks seeds at START to enforce the named skip windows.
The natural boot/carry scenarios do not play these synthetic movies.

This is an asset-backed run of the native port, **not a GameCube CPU/Dolphin
binary execution**. The original behavior is characterized through disc
metadata and existing decompiled runtime paths. Proximity discovery, full
movie fidelity, first pluck, visual pose/effects fidelity, and disc-versus-stand-in
carry travel-time equality are outside this check. #129/#130's separate play
stage route change is not used as evidence for this animation correction.

## Regression and hash-log audit

The base suite ran before the profile change: 303/304 passed. The one failure
was `scenario_screenshot_goldens`, with `boot_smoke_tick100` differing on this
Mac (max channel delta 130, 0.159% pixels over tolerance 4); the other five
images passed. Goldens are not refreshed by this change.

The first full changed run exposed the asset unit test's former assumption
that only red may have effects. That assertion now covers all three measured
boots and their encoded Action records; its focused rerun passed. The six
new natural boot checks, both automated mutation-control tests and 12 existing
Onion Day 3 disc cases passed.

Of 176 pre-existing stand-in state-hash logs, 174 are byte-identical. Only
`scenario_onion_skip_30.hash.log` and `_60` change, first at global ticks 170
and 330: the RNG word changes in the base at the former short boot's seed,
while the corrected motion has not emitted yet. `GoalAI::BootEmit` consumes
the random values for that seed. The synthetic movie also lasts 300 instead
of 100 frames and the post-seed checkpoint is 260 instead of 80, extending
the logs from 466 to 838 lines (30 Hz) and 907 to 1650 (60 Hz). The normal
watch and all three skip windows still emit exactly one seed. No committed
hash log or screenshot golden was replaced.

Final `ctest -E _disc --output-on-failure -j 6`: **311/312 passed**;
only the same pre-existing `scenario_screenshot_goldens` boot image mismatch
remains. All scenario, mutation and object asset tests passed. The independent
source/diff review found no actionable issue. Linux/Windows and hosted CI
results are pending PR execution.
