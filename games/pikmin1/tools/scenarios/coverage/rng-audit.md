# RNG draw audit: sim-stream draws by class

Base: `3c7d100b` (branch `knakamura/disc-free-gameplay-bfb538`). Line numbers are at that commit; the Action column says what this audit changed. Static read plus `rg`; no draw was traced at runtime except where noted.

Scope: every call that reaches `pc_sim_rand` (`gsys->getRand`, `getHalfRand`, `zen::Rand`/`RandShift`, `NsMathF/I::getRand`, `NSystem::random`/`randomInt`, `NMathF/I::rangeRandom`/`rateRandom`/`occurred`, bare `rand()`), in `games/pikmin1/src`, `include` and `pc_port`.

Classes: GAMEPLAY (the result changes sim state or the draw count is part of sim control flow), EFFECT-ONLY (the result reaches only visuals, audio or camera), UNCLEAR (plausibly visual but entangled with state or shared with a sim caller), INACTIVE (the `#else` branch of a PIKI_PC_PORT block, not compiled for PC).

Threads: all draw sites below run from the sim tick or game-flow code on the main thread (the DVD, audio and loader threads never call them; JAudio uses its own generator). `pc_sim_rng.cpp` also logs once to stderr if a deterministic draw comes from another thread; the check runs found no such line.

## Summary

| Class | Sites at base |
|---|---|
| GAMEPLAY | 232 |
| EFFECT-ONLY | 19 |
| UNCLEAR | 13 |
| INACTIVE | 13 |
| plus header rows below | 8 |

Sites are source lines (a line with two draws counts once). Of the EFFECT-ONLY lines, 12 moved to the cosmetic stream (see Action).

## Per-site table

| Site | Function | Class | Why | Action |
|---|---|---|---|---|
| src/plugPikiColin/lightPool.cpp:29 | LightPool | INACTIVE | #else branch of a PIKI_PC_PORT block; the PC branch already draws from the cosmetic stream | - |
| src/plugPikiKando/aiAction.cpp:722 | update | GAMEPLAY | Pikmin boredom levels | - |
| src/plugPikiKando/aiAttack.cpp:251 | decideTarget | GAMEPLAY | Pikmin attack target pick, stick / attack rolls | - |
| src/plugPikiKando/aiAttack.cpp:614 | exec | GAMEPLAY | Pikmin attack target pick, stick / attack rolls | - |
| src/plugPikiKando/aiAttack.cpp:617 | exec | GAMEPLAY | Pikmin attack target pick, stick / attack rolls | - |
| src/plugPikiKando/aiBore.cpp:51 | init | GAMEPLAY | Pikmin idle behaviours: timers, direction, rest and talk choices | - |
| src/plugPikiKando/aiBore.cpp:141 | determine | GAMEPLAY | Pikmin idle behaviours: timers, direction, rest and talk choices | - |
| src/plugPikiKando/aiBore.cpp:240 | init | GAMEPLAY | Pikmin idle behaviours: timers, direction, rest and talk choices | - |
| src/plugPikiKando/aiBore.cpp:346 | determine | GAMEPLAY | Pikmin idle behaviours: timers, direction, rest and talk choices | - |
| src/plugPikiKando/aiBore.cpp:456 | startTalk | GAMEPLAY | Pikmin idle behaviours: timers, direction, rest and talk choices | - |
| src/plugPikiKando/aiBore.cpp:638 | init | GAMEPLAY | Pikmin idle behaviours: timers, direction, rest and talk choices | - |
| src/plugPikiKando/aiBore.cpp:710 | exec | GAMEPLAY | Pikmin idle behaviours: timers, direction, rest and talk choices | - |
| src/plugPikiKando/aiBore.cpp:715 | exec | GAMEPLAY | Pikmin idle behaviours: timers, direction, rest and talk choices | - |
| src/plugPikiKando/aiBou.cpp:171 | climb | GAMEPLAY | Pikmin pole-climb speed jitter | - |
| src/plugPikiKando/aiBreakWall.cpp:51 | init | GAMEPLAY | Pikmin wall-break work timers and fail counts | - |
| src/plugPikiKando/aiBreakWall.cpp:91 | animationKeyUpdated | GAMEPLAY | Pikmin wall-break work timers and fail counts | - |
| src/plugPikiKando/aiBreakWall.cpp:160 | initBreakWall | GAMEPLAY | Pikmin wall-break work timers and fail counts | - |
| src/plugPikiKando/aiBreakWall.cpp:210 | breakWall | GAMEPLAY | Pikmin wall-break work timers and fail counts | - |
| src/plugPikiKando/aiBridge.cpp:53 | init | GAMEPLAY | Pikmin bridge work: action counter, bridge width offset | - |
| src/plugPikiKando/aiBridge.cpp:457 | newInitGo | GAMEPLAY | Pikmin bridge work: action counter, bridge width offset | - |
| src/plugPikiKando/aiChase.cpp:37 | init | GAMEPLAY | Pikmin chase timer and wander angle | - |
| src/plugPikiKando/aiChase.cpp:80 | exec | GAMEPLAY | Pikmin chase timer and wander angle | - |
| src/plugPikiKando/aiCrowd.cpp:388 | exec | GAMEPLAY | Pikmin in formation: trip chance and loops, impatience rolls | - |
| src/plugPikiKando/aiCrowd.cpp:393 | exec | GAMEPLAY | Pikmin in formation: trip chance and loops, impatience rolls | - |
| src/plugPikiKando/aiCrowd.cpp:397 | exec | GAMEPLAY | Pikmin in formation: trip chance and loops, impatience rolls | - |
| src/plugPikiKando/aiCrowd.cpp:410 | exec | GAMEPLAY | Pikmin in formation: trip chance and loops, impatience rolls | - |
| src/plugPikiKando/aiCrowd.cpp:473 | exec | GAMEPLAY | Pikmin in formation: trip chance and loops, impatience rolls | - |
| src/plugPikiKando/aiCrowd.cpp:508 | exec | GAMEPLAY | Pikmin in formation: trip chance and loops, impatience rolls | - |
| src/plugPikiKando/aiCrowd.cpp:531 | exec | GAMEPLAY | Pikmin in formation: trip chance and loops, impatience rolls | - |
| src/plugPikiKando/aiEnter.cpp:103 | findLeg | GAMEPLAY | Pikmin Onion enter: leg pick, side weight | - |
| src/plugPikiKando/aiEnter.cpp:202 | climb | GAMEPLAY | Pikmin Onion enter: leg pick, side weight | - |
| src/plugPikiKando/aiEscape.cpp:37 | init | GAMEPLAY | Pikmin escape timer and wander angle | - |
| src/plugPikiKando/aiEscape.cpp:83 | exec | GAMEPLAY | Pikmin escape timer and wander angle | - |
| src/plugPikiKando/aiExit.cpp:90 | exec | GAMEPLAY | Pikmin Onion exit side velocity | - |
| src/plugPikiKando/aiFormation.cpp:38 | animationKeyUpdated | GAMEPLAY | Pikmin formation idle timers, offsets, trip chance | - |
| src/plugPikiKando/aiFormation.cpp:51 | animationKeyUpdated | GAMEPLAY | Pikmin formation idle timers, offsets, trip chance | - |
| src/plugPikiKando/aiFormation.cpp:126 | init | GAMEPLAY | Pikmin formation idle timers, offsets, trip chance | - |
| src/plugPikiKando/aiFormation.cpp:193 | exec | GAMEPLAY | Pikmin formation idle timers, offsets, trip chance | - |
| src/plugPikiKando/aiFormation.cpp:201 | exec | GAMEPLAY | Pikmin formation idle timers, offsets, trip chance | - |
| src/plugPikiKando/aiFormation.cpp:213 | exec | GAMEPLAY | Pikmin formation idle timers, offsets, trip chance | - |
| src/plugPikiKando/aiFormation.cpp:216 | exec | GAMEPLAY | Pikmin formation idle timers, offsets, trip chance | - |
| src/plugPikiKando/aiFormation.cpp:244 | exec | GAMEPLAY | Pikmin formation idle timers, offsets, trip chance | - |
| src/plugPikiKando/aiFree.cpp:86 | init | GAMEPLAY | free Pikmin boid timer and roll | - |
| src/plugPikiKando/aiFree.cpp:95 | init | GAMEPLAY | free Pikmin boid timer and roll | - |
| src/plugPikiKando/aiGoto.cpp:44 | init | GAMEPLAY | Pikmin goto timeout and wander angle | - |
| src/plugPikiKando/aiGoto.cpp:108 | exec | GAMEPLAY | Pikmin goto timeout and wander angle | - |
| src/plugPikiKando/aiGuard.cpp:70 | cleanup | UNCLEAR | dead store (unused) on guard cleanup; consumes one sim draw, no consumer | - |
| src/plugPikiKando/aiGuard.cpp:284 | setGoal | GAMEPLAY | Pikmin guard goal roll | - |
| src/plugPikiKando/aiKinoko.cpp:180 | initBoid | GAMEPLAY | Pikmin on Kinoko boid timers and direction | - |
| src/plugPikiKando/aiKinoko.cpp:184 | initBoid | GAMEPLAY | Pikmin on Kinoko boid timers and direction | - |
| src/plugPikiKando/aiKinoko.cpp:188 | initBoid | GAMEPLAY | Pikmin on Kinoko boid timers and direction | - |
| src/plugPikiKando/aiPullout.cpp:59 | init | GAMEPLAY | Pikmin pluck target pick | - |
| src/plugPikiKando/aiPush.cpp:168 | initGo | GAMEPLAY | Pikmin push-object counts and timers | - |
| src/plugPikiKando/aiPush.cpp:177 | initGo | GAMEPLAY | Pikmin push-object counts and timers | - |
| src/plugPikiKando/aiPush.cpp:256 | animationKeyUpdated | GAMEPLAY | Pikmin push-object counts and timers | - |
| src/plugPikiKando/aiPush.cpp:284 | animationKeyUpdated | GAMEPLAY | Pikmin push-object counts and timers | - |
| src/plugPikiKando/aiRandomBoid.cpp:28 | animationKeyUpdated | GAMEPLAY | wandering Pikmin boid AI (directions, timers, state rolls) | - |
| src/plugPikiKando/aiRandomBoid.cpp:29 | animationKeyUpdated | GAMEPLAY | wandering Pikmin boid AI (directions, timers, state rolls) | - |
| src/plugPikiKando/aiRandomBoid.cpp:35 | animationKeyUpdated | GAMEPLAY | wandering Pikmin boid AI (directions, timers, state rolls) | - |
| src/plugPikiKando/aiRandomBoid.cpp:53 | animationKeyUpdated | GAMEPLAY | wandering Pikmin boid AI (directions, timers, state rolls) | - |
| src/plugPikiKando/aiRandomBoid.cpp:89 | init | GAMEPLAY | wandering Pikmin boid AI (directions, timers, state rolls) | - |
| src/plugPikiKando/aiRandomBoid.cpp:90 | init | GAMEPLAY | wandering Pikmin boid AI (directions, timers, state rolls) | - |
| src/plugPikiKando/aiRandomBoid.cpp:115 | exec | GAMEPLAY | wandering Pikmin boid AI (directions, timers, state rolls) | - |
| src/plugPikiKando/aiRandomBoid.cpp:117 | exec | GAMEPLAY | wandering Pikmin boid AI (directions, timers, state rolls) | - |
| src/plugPikiKando/aiRandomBoid.cpp:121 | exec | GAMEPLAY | wandering Pikmin boid AI (directions, timers, state rolls) | - |
| src/plugPikiKando/aiRandomBoid.cpp:122 | exec | GAMEPLAY | wandering Pikmin boid AI (directions, timers, state rolls) | - |
| src/plugPikiKando/aiRandomBoid.cpp:123 | exec | GAMEPLAY | wandering Pikmin boid AI (directions, timers, state rolls) | - |
| src/plugPikiKando/aiRandomBoid.cpp:137 | exec | GAMEPLAY | wandering Pikmin boid AI (directions, timers, state rolls) | - |
| src/plugPikiKando/aiRope.cpp:33 | init | GAMEPLAY | Pikmin rope-climb speed jitter | - |
| src/plugPikiKando/aiShoot.cpp:110 | decideTarget | GAMEPLAY | Pikmin shoot target pick | - |
| src/plugPikiKando/aiStone.cpp:213 | animationKeyUpdated | GAMEPLAY | Pikmin stone work: nectar spawn roll and angle | - |
| src/plugPikiKando/aiStone.cpp:218 | animationKeyUpdated | GAMEPLAY | Pikmin stone work: nectar spawn roll and angle | - |
| src/plugPikiKando/aiTransport.cpp:340 | animationKeyUpdated | GAMEPLAY | Pikmin carry: lift retry counts, goal pick | - |
| src/plugPikiKando/aiTransport.cpp:418 | execJump | GAMEPLAY | Pikmin carry: lift retry counts, goal pick | - |
| src/plugPikiKando/aiTransport.cpp:528 | gotoLiftPos | GAMEPLAY | Pikmin carry: lift retry counts, goal pick | - |
| src/plugPikiKando/aiTransport.cpp:1106 | decideGoal | GAMEPLAY | Pikmin carry: lift retry counts, goal pick | - |
| src/plugPikiKando/aiWatch.cpp:46 | init | GAMEPLAY | Pikmin watch retry timer | - |
| src/plugPikiKando/aiWeed.cpp:206 | animationKeyUpdated | GAMEPLAY | Pikmin grass work: nectar spawn roll and angle | - |
| src/plugPikiKando/aiWeed.cpp:211 | animationKeyUpdated | GAMEPLAY | Pikmin grass work: nectar spawn roll and angle | - |
| src/plugPikiKando/collInfo.cpp:907 | getRandomCollPart | UNCLEAR | shared helper; callers are the head-look (visual) and aiPut.cpp:410 (result unused). Needs a cosmetic twin for the look caller | - |
| src/plugPikiKando/fishItem.cpp:35 | startAI | EFFECT-ONLY | ambient fish school (drawBatten); nothing else reads mFish / mSchoolCentre | moved to cosmetic (fish commit) |
| src/plugPikiKando/fishItem.cpp:36 | startAI | EFFECT-ONLY | ambient fish school (drawBatten); nothing else reads mFish / mSchoolCentre | moved to cosmetic (fish commit) |
| src/plugPikiKando/fishItem.cpp:40 | startAI | EFFECT-ONLY | ambient fish school (drawBatten); nothing else reads mFish / mSchoolCentre | moved to cosmetic (fish commit) |
| src/plugPikiKando/fishItem.cpp:109 | moveFish | EFFECT-ONLY | ambient fish school flocking roll / turn | moved to cosmetic (fish commit) |
| src/plugPikiKando/fishItem.cpp:120 | moveFish | EFFECT-ONLY | ambient fish school flocking roll / turn | moved to cosmetic (fish commit) |
| src/plugPikiKando/formationMgr.cpp:547 | init | GAMEPLAY | formation initial angle and link angles | - |
| src/plugPikiKando/formationMgr.cpp:558 | init | GAMEPLAY | formation initial angle and link angles | - |
| src/plugPikiKando/generator.cpp:1126 | birth | GAMEPLAY | generator spawn colour, count and position | - |
| src/plugPikiKando/generator.cpp:1293 | init | GAMEPLAY | generator spawn colour, count and position | - |
| src/plugPikiKando/generator.cpp:1348 | getPos | GAMEPLAY | generator spawn colour, count and position | - |
| src/plugPikiKando/generator.cpp:1349 | getPos | GAMEPLAY | generator spawn colour, count and position | - |
| src/plugPikiKando/generator.cpp:1352 | getPos | GAMEPLAY | generator spawn colour, count and position | - |
| src/plugPikiKando/goalItem.cpp:169 | playEffect | INACTIVE | #else branch of a PIKI_PC_PORT block; the PC branch already draws from the cosmetic stream | - |
| src/plugPikiKando/goalItem.cpp:413 | exitPiki | GAMEPLAY | Onion: exit leg (spawn position), spawn timer | - |
| src/plugPikiKando/goalItem.cpp:813 | update | GAMEPLAY | Onion: exit leg (spawn position), spawn timer | - |
| src/plugPikiKando/itemAI.cpp:284 | act | GAMEPLAY | grow / wilt / bomb-fuse timer jitter on the item | - |
| src/plugPikiKando/itemAI.cpp:319 | act | GAMEPLAY | grow / wilt / bomb-fuse timer jitter on the item | - |
| src/plugPikiKando/itemAI.cpp:342 | act | GAMEPLAY | grow / wilt / bomb-fuse timer jitter on the item | - |
| src/plugPikiKando/itemAI.cpp:344 | act | GAMEPLAY | grow / wilt / bomb-fuse timer jitter on the item | - |
| src/plugPikiKando/itemAI.cpp:413 | act | GAMEPLAY | grow / wilt / bomb-fuse timer jitter on the item | - |
| src/plugPikiKando/itemAI.cpp:465 | act | GAMEPLAY | grow / wilt / bomb-fuse timer jitter on the item | - |
| src/plugPikiKando/itemAI.cpp:792 | act | GAMEPLAY | bare libc rand(), result discarded: off the sim stream in deterministic mode; in off mode it is the same libc sequence | - |
| src/plugPikiKando/itemAI.cpp:883 | act | EFFECT-ONLY | dead store (unused1) in the firework effect block; no consumer | moved to cosmetic (itemAI commit) |
| src/plugPikiKando/itemAI.cpp:914 | act | GAMEPLAY | seed launch direction (velocity of the new PikiHead) | - |
| src/plugPikiKando/itemGem.cpp:115 | setColorType | GAMEPLAY | gem colour and split angle | - |
| src/plugPikiKando/itemGem.cpp:311 | split | GAMEPLAY | gem colour and split angle | - |
| src/plugPikiKando/navi.cpp:463 | startDamageEffect | EFFECT-ONLY | picks the camera-vibration type (index into vibTypes); feeds only startVibrationEvent | moved to cosmetic (navi commit) |
| src/plugPikiKando/naviDemoState.cpp:69 | exec | UNCLEAR | captain stumble in the demo walk state: stumble loop count drives a state timer | - |
| src/plugPikiKando/naviDemoState.cpp:72 | exec | UNCLEAR | captain stumble in the demo walk state: stumble loop count drives a state timer | - |
| src/plugPikiKando/naviState.cpp:362 | exec | GAMEPLAY | captain state machine (flick stickers, get-up timers, spin, bury) | - |
| src/plugPikiKando/naviState.cpp:485 | procAnimMsg | UNCLEAR | idle anim pick (GWait1 vs GWait2) in the bury state; animation end keys drive the next state | - |
| src/plugPikiKando/naviState.cpp:526 | procAnimMsg | EFFECT-ONLY | KEY_PlayEffect (bury state 2): direction of the SmokeSoil effect only | moved to cosmetic (navi commit) |
| src/plugPikiKando/naviState.cpp:726 | exec | GAMEPLAY | DEVELOP / VERSION_PIKIDEMO debug number-pellet spawn; not in retail builds | - |
| src/plugPikiKando/naviState.cpp:1519 | init | GAMEPLAY | captain state machine (flick stickers, get-up timers, spin, bury) | - |
| src/plugPikiKando/naviState.cpp:1521 | init | GAMEPLAY | captain state machine (flick stickers, get-up timers, spin, bury) | - |
| src/plugPikiKando/naviState.cpp:1571 | procAnimMsg | GAMEPLAY | captain state machine (flick stickers, get-up timers, spin, bury) | - |
| src/plugPikiKando/naviState.cpp:1610 | init | GAMEPLAY | captain state machine (flick stickers, get-up timers, spin, bury) | - |
| src/plugPikiKando/naviState.cpp:1698 | procBounceMsg | GAMEPLAY | captain state machine (flick stickers, get-up timers, spin, bury) | - |
| src/plugPikiKando/omake.cpp:151 | init | GAMEPLAY |  | - |
| src/plugPikiKando/pelletMgr.cpp:694 | getRandomFreeSlotIndex | GAMEPLAY | random free carry slot on a pellet | - |
| src/plugPikiKando/piki.cpp:2015 | startMotion | EFFECT-ONLY | idle head-look target (cursor vs light); mLookatPosPtr is read only by Piki::updateLook (render pose) | not moved: same look chain as viewPiki:343 |
| src/plugPikiKando/piki.cpp:2550 | init | GAMEPLAY | Pikmin birth colour and size, drown-start roll | - |
| src/plugPikiKando/piki.cpp:2564 | init | GAMEPLAY | Pikmin birth colour and size, drown-start roll | - |
| src/plugPikiKando/piki.cpp:2640 | updateLookCreature | EFFECT-ONLY | idle head-look target swap; consumed only by the look selection | not moved: same look chain |
| src/plugPikiKando/piki.cpp:2644 | updateLookCreature | EFFECT-ONLY | idle head-look target swap; consumed only by the look selection | not moved: same look chain |
| src/plugPikiKando/piki.cpp:2881 | realAI | GAMEPLAY | Pikmin birth colour and size, drown-start roll | - |
| src/plugPikiKando/pikiState.cpp:236 | init | GAMEPLAY | Pikmin state machine (timers, directions, velocities, flower loss) | - |
| src/plugPikiKando/pikiState.cpp:564 | init | GAMEPLAY | Pikmin state machine (timers, directions, velocities, flower loss) | - |
| src/plugPikiKando/pikiState.cpp:566 | init | GAMEPLAY | Pikmin state machine (timers, directions, velocities, flower loss) | - |
| src/plugPikiKando/pikiState.cpp:715 | initWalk | GAMEPLAY | Pikmin state machine (timers, directions, velocities, flower loss) | - |
| src/plugPikiKando/pikiState.cpp:719 | initWalk | GAMEPLAY | Pikmin state machine (timers, directions, velocities, flower loss) | - |
| src/plugPikiKando/pikiState.cpp:723 | initWalk | GAMEPLAY | Pikmin state machine (timers, directions, velocities, flower loss) | - |
| src/plugPikiKando/pikiState.cpp:855 | init | GAMEPLAY | Pikmin state machine (timers, directions, velocities, flower loss) | - |
| src/plugPikiKando/pikiState.cpp:878 | exec | GAMEPLAY | Pikmin state machine (timers, directions, velocities, flower loss) | - |
| src/plugPikiKando/pikiState.cpp:879 | exec | GAMEPLAY | Pikmin state machine (timers, directions, velocities, flower loss) | - |
| src/plugPikiKando/pikiState.cpp:912 | init | GAMEPLAY | Pikmin state machine (timers, directions, velocities, flower loss) | - |
| src/plugPikiKando/pikiState.cpp:932 | exec | GAMEPLAY | Pikmin state machine (timers, directions, velocities, flower loss) | - |
| src/plugPikiKando/pikiState.cpp:933 | exec | GAMEPLAY | Pikmin state machine (timers, directions, velocities, flower loss) | - |
| src/plugPikiKando/pikiState.cpp:1075 | init | GAMEPLAY | Pikmin state machine (timers, directions, velocities, flower loss) | - |
| src/plugPikiKando/pikiState.cpp:1076 | init | GAMEPLAY | Pikmin state machine (timers, directions, velocities, flower loss) | - |
| src/plugPikiKando/pikiState.cpp:1077 | init | GAMEPLAY | Pikmin state machine (timers, directions, velocities, flower loss) | - |
| src/plugPikiKando/pikiState.cpp:1135 | procAnimMsg | GAMEPLAY | Pikmin state machine (timers, directions, velocities, flower loss) | - |
| src/plugPikiKando/pikiState.cpp:1144 | procAnimMsg | GAMEPLAY | Pikmin state machine (timers, directions, velocities, flower loss) | - |
| src/plugPikiKando/pikiState.cpp:1211 | init | GAMEPLAY | Pikmin state machine (timers, directions, velocities, flower loss) | - |
| src/plugPikiKando/pikiState.cpp:1212 | init | GAMEPLAY | Pikmin state machine (timers, directions, velocities, flower loss) | - |
| src/plugPikiKando/pikiState.cpp:1271 | procAnimMsg | GAMEPLAY | Pikmin state machine (timers, directions, velocities, flower loss) | - |
| src/plugPikiKando/pikiState.cpp:1280 | procAnimMsg | GAMEPLAY | Pikmin state machine (timers, directions, velocities, flower loss) | - |
| src/plugPikiKando/pikiState.cpp:1372 | procBounceMsg | GAMEPLAY | Pikmin state machine (timers, directions, velocities, flower loss) | - |
| src/plugPikiKando/pikiState.cpp:1573 | procAnimMsg | GAMEPLAY | Pikmin state machine (timers, directions, velocities, flower loss) | - |
| src/plugPikiKando/pikiState.cpp:1623 | procAnimMsg | GAMEPLAY | Pikmin state machine (timers, directions, velocities, flower loss) | - |
| src/plugPikiKando/pikiState.cpp:2025 | exec | GAMEPLAY | Pikmin state machine (timers, directions, velocities, flower loss) | - |
| src/plugPikiKando/pikiState.cpp:2783 | exec | GAMEPLAY | Pikmin state machine (timers, directions, velocities, flower loss) | - |
| src/plugPikiKando/pikiState.cpp:3303 | init | UNCLEAR | emotion state: picks motion plus sound together; motion end drives the state timing | - |
| src/plugPikiKando/pikiState.cpp:3325 | init | UNCLEAR | emotion state: picks motion plus sound together; motion end drives the state timing | - |
| src/plugPikiKando/pikiState.cpp:3356 | init | UNCLEAR | emotion state: picks motion plus sound together; motion end drives the state timing | - |
| src/plugPikiKando/pikiState.cpp:3402 | init | UNCLEAR | emotion state: picks motion plus sound together; motion end drives the state timing | - |
| src/plugPikiKando/pikiState.cpp:3446 | exec | GAMEPLAY | Pikmin state machine (timers, directions, velocities, flower loss) | - |
| src/plugPikiKando/pikidoKill.cpp:65 | doKill | GAMEPLAY | Pikmin leave-seed chance on death | - |
| src/plugPikiKando/pikiheadItem.cpp:136 | startAI | GAMEPLAY | seed physics (air resistance) | - |
| src/plugPikiKando/utkando.cpp:27 | selectRandomly | UNCLEAR | shared weighted-choice helper; callers pick idle / emotion motions (pikiState 3034, 3181, 3339; naviState 1447; aiBore 153, 358, 589), so motion timing may feed state | - |
| src/plugPikiKando/viewPiki.cpp:212 | init | UNCLEAR | random spawn offset written to mSRT.t; probably overwritten by the later Piki::init / birth placement, not proven | - |
| src/plugPikiKando/viewPiki.cpp:314 | update | EFFECT-ONLY | smoke-effect gate; inactive by default (AIPerf::optLevel == 0 only), spawns a utEffect | tried with :343, reverted (never runs by default) |
| src/plugPikiKando/viewPiki.cpp:343 | startHimaLook | EFFECT-ONLY | dead store: _334 is never read | tried, reverted: breaks overlay proof (see notes) |
| src/plugPikiKando/viewPiki.cpp:511 | demoDraw | INACTIVE | #else branch of a PIKI_PC_PORT block; the PC branch already draws from the cosmetic stream | - |
| src/plugPikiKando/weedsItem.cpp:156 | create | GAMEPLAY | pebble and grass placement, random pick for Pikmin AI | - |
| src/plugPikiKando/weedsItem.cpp:157 | create | GAMEPLAY | pebble and grass placement, random pick for Pikmin AI | - |
| src/plugPikiKando/weedsItem.cpp:160 | create | GAMEPLAY | pebble and grass placement, random pick for Pikmin AI | - |
| src/plugPikiKando/weedsItem.cpp:167 | create | EFFECT-ONLY | pebble yaw; mRotationDegrees is read only when drawing (weedsItem.cpp:254) | tried, reverted: moves K (23/12 with fish) |
| src/plugPikiKando/weedsItem.cpp:168 | create | GAMEPLAY | pebble shape index also sets mHealth = shape * 2 + 2 | - |
| src/plugPikiKando/weedsItem.cpp:273 | getRandomPebble | GAMEPLAY | pebble and grass placement, random pick for Pikmin AI | - |
| src/plugPikiKando/weedsItem.cpp:314 | create | GAMEPLAY | pebble and grass placement, random pick for Pikmin AI | - |
| src/plugPikiKando/weedsItem.cpp:315 | create | GAMEPLAY | pebble and grass placement, random pick for Pikmin AI | - |
| src/plugPikiKando/weedsItem.cpp:318 | create | GAMEPLAY | pebble and grass placement, random pick for Pikmin AI | - |
| src/plugPikiKando/weedsItem.cpp:325 | create | EFFECT-ONLY | grass blade yaw; mRotationDegrees is read only when drawing (weedsItem.cpp:375) | tried, reverted: moves K (same change) |
| src/plugPikiKando/weedsItem.cpp:395 | getRandomGrass | GAMEPLAY | pebble and grass placement, random pick for Pikmin AI | - |
| src/plugPikiKando/weedsItem.cpp:453 | startAI | GAMEPLAY | pebble and grass placement, random pick for Pikmin AI | - |
| src/plugPikiKando/weedsItem.cpp:454 | startAI | GAMEPLAY | pebble and grass placement, random pick for Pikmin AI | - |
| src/plugPikiKando/workObject.cpp:869 | update | GAMEPLAY | push-object move timer | - |
| src/plugPikiNakata/nlibsystem.cpp:307 | randomInt | GAMEPLAY | NSystem::randomInt helper; callers are teki waypoint / route picks | - |
| src/plugPikiNakata/nlibsystem.cpp:309 | randomInt | GAMEPLAY | NSystem::randomInt helper; callers are teki waypoint / route picks | - |
| src/plugPikiNakata/nlibsystem.cpp:309 | randomInt | GAMEPLAY | NSystem::randomInt helper; callers are teki waypoint / route picks | - |
| src/plugPikiNakata/taimizinko.cpp:591 | start | GAMEPLAY | Mizinko flight height, amplitude, phase, angular velocity | - |
| src/plugPikiNakata/taimizinko.cpp:592 | start | GAMEPLAY | Mizinko flight height, amplitude, phase, angular velocity | - |
| src/plugPikiNakata/taimizinko.cpp:593 | start | GAMEPLAY | Mizinko flight height, amplitude, phase, angular velocity | - |
| src/plugPikiNakata/taimizinko.cpp:595 | start | GAMEPLAY | Mizinko flight height, amplitude, phase, angular velocity | - |
| src/plugPikiNakata/taimizinko.cpp:643 | start | GAMEPLAY | Mizinko flight height, amplitude, phase, angular velocity | - |
| src/plugPikiNakata/taimizinko.cpp:644 | start | GAMEPLAY | Mizinko flight height, amplitude, phase, angular velocity | - |
| src/plugPikiNakata/taimizinko.cpp:645 | start | GAMEPLAY | Mizinko flight height, amplitude, phase, angular velocity | - |
| src/plugPikiNakata/taimizinko.cpp:647 | start | GAMEPLAY | Mizinko flight height, amplitude, phase, angular velocity | - |
| src/plugPikiNakata/taimotionactions.cpp:218 | start | UNCLEAR | TaiRandomSetAnimationCounterAction: random start frame of a teki animation; anim keys drive state changes | - |
| src/plugPikiNakata/taimoveactions.cpp:297 | makeTargetPosition | GAMEPLAY | teki route pick | - |
| src/plugPikiNakata/tainapkid.cpp:1052 | makeTargetPosition | GAMEPLAY | Napkid waypoint pick, flick chance | - |
| src/plugPikiNakata/tainapkid.cpp:1567 | act | GAMEPLAY | Napkid waypoint pick, flick chance | - |
| src/plugPikiNakata/taiotimoti.cpp:777 | act | GAMEPLAY | Otimoti fail-chance roll | - |
| src/plugPikiNakata/taipalm.cpp:299 | start | GAMEPLAY | Palm enemy pellet colour / timer | - |
| src/plugPikiNakata/taipalm.cpp:596 | resetTimer | GAMEPLAY | Palm enemy pellet colour / timer | - |
| src/plugPikiNakata/taitimeractions.cpp:39 | resetTimer | GAMEPLAY | teki timers (jittered length, chance roll) | - |
| src/plugPikiNakata/taitimeractions.cpp:55 | resetTimer | GAMEPLAY | teki timers (jittered length, chance roll) | - |
| src/plugPikiNakata/taitimeractions.cpp:80 | act | GAMEPLAY | teki timers (jittered length, chance roll) | - |
| src/plugPikiNakata/taiwaitactions.cpp:26 | start | GAMEPLAY | teki wait target angle | - |
| src/plugPikiNakata/taiwaitactions.cpp:26 | start | GAMEPLAY | teki wait target angle | - |
| src/plugPikiNakata/tekibteki.cpp:878 | spawnItems | GAMEPLAY | teki death drops (pellet/nectar chance, count, colour, direction), flick roll | - |
| src/plugPikiNakata/tekibteki.cpp:883 | spawnItems | GAMEPLAY | teki death drops (pellet/nectar chance, count, colour, direction), flick roll | - |
| src/plugPikiNakata/tekibteki.cpp:891 | spawnItems | GAMEPLAY | teki death drops (pellet/nectar chance, count, colour, direction), flick roll | - |
| src/plugPikiNakata/tekibteki.cpp:894 | spawnItems | GAMEPLAY | teki death drops (pellet/nectar chance, count, colour, direction), flick roll | - |
| src/plugPikiNakata/tekibteki.cpp:914 | spawnPellets | GAMEPLAY | teki death drops (pellet/nectar chance, count, colour, direction), flick roll | - |
| src/plugPikiNakata/tekibteki.cpp:914 | spawnPellets | GAMEPLAY | teki death drops (pellet/nectar chance, count, colour, direction), flick roll | - |
| src/plugPikiNakata/tekibteki.cpp:929 | spawnPellets | GAMEPLAY | teki death drops (pellet/nectar chance, count, colour, direction), flick roll | - |
| src/plugPikiNakata/tekibteki.cpp:970 | spawnWaters | GAMEPLAY | teki death drops (pellet/nectar chance, count, colour, direction), flick roll | - |
| src/plugPikiNakata/tekibteki.cpp:970 | spawnWaters | GAMEPLAY | teki death drops (pellet/nectar chance, count, colour, direction), flick roll | - |
| src/plugPikiNakata/tekibteki.cpp:1604 | flickUpper | GAMEPLAY | teki death drops (pellet/nectar chance, count, colour, direction), flick roll | - |
| src/plugPikiNishimura/Boss.cpp:107 | createPellet | GAMEPLAY | boss drop pellet angle / colour, wander target | - |
| src/plugPikiNishimura/Boss.cpp:120 | createPellet | GAMEPLAY | boss drop pellet angle / colour, wander target | - |
| src/plugPikiNishimura/Boss.cpp:242 | makeTargetRandom | GAMEPLAY | boss drop pellet angle / colour, wander target | - |
| src/plugPikiNishimura/Boss.cpp:244 | makeTargetRandom | GAMEPLAY | boss drop pellet angle / colour, wander target | - |
| src/plugPikiNishimura/BossCnd.cpp:38 | satisfy | GAMEPLAY | boss flick condition roll | - |
| src/plugPikiNishimura/KingAi.cpp:473 | pikiStickToKingMouth | GAMEPLAY | King Chappy AI: mouth slot, jump chance | - |
| src/plugPikiNishimura/KingAi.cpp:690 | setAttackPriority | GAMEPLAY | King Chappy AI: mouth slot, jump chance | - |
| src/plugPikiNishimura/KoganeAi.cpp:91 | initAI | GAMEPLAY | Kogane AI: appear/idle timers, target position, dropped pellet colour | - |
| src/plugPikiNishimura/KoganeAi.cpp:301 | setNewTargetPosition | GAMEPLAY | Kogane AI: appear/idle timers, target position, dropped pellet colour | - |
| src/plugPikiNishimura/KoganeAi.cpp:306 | setNewTargetPosition | GAMEPLAY | Kogane AI: appear/idle timers, target position, dropped pellet colour | - |
| src/plugPikiNishimura/KoganeAi.cpp:309 | setNewTargetPosition | GAMEPLAY | Kogane AI: appear/idle timers, target position, dropped pellet colour | - |
| src/plugPikiNishimura/KoganeAi.cpp:330 | setRouteTargetPosition | GAMEPLAY | Kogane AI: appear/idle timers, target position, dropped pellet colour | - |
| src/plugPikiNishimura/KoganeAi.cpp:335 | setRouteTargetPosition | GAMEPLAY | Kogane AI: appear/idle timers, target position, dropped pellet colour | - |
| src/plugPikiNishimura/KoganeAi.cpp:338 | setRouteTargetPosition | GAMEPLAY | Kogane AI: appear/idle timers, target position, dropped pellet colour | - |
| src/plugPikiNishimura/KoganeAi.cpp:387 | birthItemPellet | GAMEPLAY | Kogane AI: appear/idle timers, target position, dropped pellet colour | - |
| src/plugPikiNishimura/KoganeAi.cpp:655 | initStopWalk | GAMEPLAY | Kogane AI: appear/idle timers, target position, dropped pellet colour | - |
| src/plugPikiNishimura/Pom.cpp:71 | setColor | GAMEPLAY | Pom flower colour (decides the pellet / Pikmin colour it yields) | - |
| src/plugPikiNishimura/PomAi.cpp:69 | initAI | GAMEPLAY | Pom AI cycle count, seed spawn angle | - |
| src/plugPikiNishimura/PomAi.cpp:335 | createPikiHead | GAMEPLAY | Pom AI cycle count, seed spawn angle | - |
| src/plugPikiNishimura/SlimeAi.cpp:617 | makeTargetRandom | GAMEPLAY | Slime AI wander target | - |
| src/plugPikiNishimura/SnakeAi.cpp:242 | setAppearPosition01 | GAMEPLAY | Snake AI appear position, mouth slot, fast-appear chance | - |
| src/plugPikiNishimura/SnakeAi.cpp:446 | pikiStickMouth | GAMEPLAY | Snake AI appear position, mouth slot, fast-appear chance | - |
| src/plugPikiNishimura/SnakeAi.cpp:1085 | initAppear | GAMEPLAY | Snake AI appear position, mouth slot, fast-appear chance | - |
| src/plugPikiNishimura/SnakeBody.cpp:351 | createDeadPellet | GAMEPLAY | dead-snake pellet colour | - |
| src/plugPikiNishimura/SpiderAi.cpp:388 | initWalk | GAMEPLAY | Spider AI cycle counts, appear yaw, leg params | - |
| src/plugPikiNishimura/SpiderAi.cpp:404 | initShakeOff | GAMEPLAY | Spider AI cycle counts, appear yaw, leg params | - |
| src/plugPikiNishimura/SpiderAi.cpp:425 | initAppear | GAMEPLAY | Spider AI cycle counts, appear yaw, leg params | - |
| src/plugPikiNishimura/SpiderAi.cpp:438 | initWait | GAMEPLAY | Spider AI cycle counts, appear yaw, leg params | - |
| src/plugPikiNishimura/SpiderAi.cpp:457 | dieState | EFFECT-ONLY | 20% per-tick gate on createSmallSparkEffect(1); the branch only spawns sparks | moved to cosmetic (spider commit) |
| src/plugPikiNishimura/SpiderLeg.cpp:228 | createSmallSparkEffect | EFFECT-ONLY | shuffle picking which leg joints emit EFF_Spider_SmallSparks; legVals read only by setSmallSparkEffect | moved to cosmetic (spider commit) |
| src/plugPikiNishimura/SpiderLeg.cpp:509 | setNextDirAndCent | GAMEPLAY | Spider leg targets, walk position, shake-off chance (sim: leg positions hit Pikmin) | - |
| src/plugPikiNishimura/SpiderLeg.cpp:511 | setNextDirAndCent | GAMEPLAY | Spider leg targets, walk position, shake-off chance (sim: leg positions hit Pikmin) | - |
| src/plugPikiNishimura/SpiderLeg.cpp:513 | setNextDirAndCent | GAMEPLAY | Spider leg targets, walk position, shake-off chance (sim: leg positions hit Pikmin) | - |
| src/plugPikiNishimura/SpiderLeg.cpp:578 | setWalkNewPosition | GAMEPLAY | Spider leg targets, walk position, shake-off chance (sim: leg positions hit Pikmin) | - |
| src/plugPikiNishimura/SpiderLeg.cpp:1041 | stepShakeOffPiki | GAMEPLAY | Spider leg targets, walk position, shake-off chance (sim: leg positions hit Pikmin) | - |
| src/plugPikiYamashita/TAIAattack.cpp:133 | setTargetPosition | GAMEPLAY | enemy attack target offset along a bridge | - |
| src/plugPikiYamashita/TAIAmove.cpp:86 | start | GAMEPLAY | enemy movement target (angle, distance, offsets, direction flip) | - |
| src/plugPikiYamashita/TAIAmove.cpp:213 | start | GAMEPLAY | enemy movement target (angle, distance, offsets, direction flip) | - |
| src/plugPikiYamashita/TAIAmove.cpp:214 | start | GAMEPLAY | enemy movement target (angle, distance, offsets, direction flip) | - |
| src/plugPikiYamashita/TAIAmove.cpp:251 | setTargetPoint | GAMEPLAY | enemy movement target (angle, distance, offsets, direction flip) | - |
| src/plugPikiYamashita/TAIAmove.cpp:358 | setTargetDirection | GAMEPLAY | enemy movement target (angle, distance, offsets, direction flip) | - |
| src/plugPikiYamashita/TAIAmove.cpp:537 | makeTargetPosition | GAMEPLAY | enemy movement target (angle, distance, offsets, direction flip) | - |
| src/plugPikiYamashita/TAIAmove.cpp:537 | makeTargetPosition | GAMEPLAY | enemy movement target (angle, distance, offsets, direction flip) | - |
| src/plugPikiYamashita/TAIAmove.cpp:538 | makeTargetPosition | GAMEPLAY | enemy movement target (angle, distance, offsets, direction flip) | - |
| src/plugPikiYamashita/TAIAmove.cpp:538 | makeTargetPosition | GAMEPLAY | enemy movement target (angle, distance, offsets, direction flip) | - |
| src/plugPikiYamashita/TAIAmove.cpp:880 | setTargetPosition | GAMEPLAY | enemy movement target (angle, distance, offsets, direction flip) | - |
| src/plugPikiYamashita/TAIbeatle.cpp:290 | (anon) | EFFECT-ONLY | createSteamEffect: shuffle picks which 3 of 6 'stmN' parts emit EFF_Beatle_Flick2 (slot index is the loop counter) | moved to cosmetic (beatle commit) |
| src/plugPikiYamashita/TAIbeatle.cpp:291 | (anon) | EFFECT-ONLY | createSteamEffect: shuffle picks which 3 of 6 'stmN' parts emit EFF_Beatle_Flick2 (slot index is the loop counter) | moved to cosmetic (beatle commit) |
| src/plugPikiYamashita/TAIdororo.cpp:395 | killDororoEffect | GAMEPLAY | Dororo wait timer | - |
| src/plugPikiYamashita/TAImar.cpp:39 | (anon) | UNCLEAR | hitCreature force jitter; called from the particle-generator callback (TAIeffectAttack), once per emitter per update, so generator lifetime (effect data) can gate it | - |
| src/plugPikiYamashita/TAImar.cpp:267 | (anon) | GAMEPLAY | Mar attack-trigger roll | - |
| src/plugPikiYamashita/TAImiurin.cpp:183 | (anon) | GAMEPLAY | Miurin AI navi-check chance | - |
| src/plugPikiYamashita/TAIotama.cpp:137 | (anon) | GAMEPLAY | Otama AI timers, rare-state roll | - |
| src/plugPikiYamashita/TAIotama.cpp:181 | (anon) | GAMEPLAY | Otama AI timers, rare-state roll | - |
| src/plugPikiYamashita/TAIotama.cpp:187 | (anon) | GAMEPLAY | Otama AI timers, rare-state roll | - |
| src/plugPikiYamashita/TAItamago.cpp:118 | (anon) | GAMEPLAY | Tamago emergence timer | - |
| src/plugPikiYamashita/drawCMbest.cpp:89 | (anon) | INACTIVE | #else branch of a PIKI_PC_PORT block; the PC branch already draws from the cosmetic stream | - |
| src/plugPikiYamashita/drawCommon.cpp:343 | invoke | INACTIVE | #else branch of a PIKI_PC_PORT block; the PC branch already draws from the cosmetic stream | - |
| src/plugPikiYamashita/drawCommon.cpp:383 | setGoalPos | INACTIVE | #else branch of a PIKI_PC_PORT block; the PC branch already draws from the cosmetic stream | - |
| src/plugPikiYamashita/drawCommon.cpp:396 | setGoalRotate | INACTIVE | #else branch of a PIKI_PC_PORT block; the PC branch already draws from the cosmetic stream | - |
| src/plugPikiYamashita/drawGameOver.cpp:106 | (anon) | INACTIVE | #else branch of a PIKI_PC_PORT block; the PC branch already draws from the cosmetic stream | - |
| src/plugPikiYamashita/drawGameOver.cpp:173 | (anon) | INACTIVE | #else branch of a PIKI_PC_PORT block; the PC branch already draws from the cosmetic stream | - |
| src/plugPikiYamashita/drawWorldMap.cpp:291 | (anon) | INACTIVE | #else branch of a PIKI_PC_PORT block; the PC branch already draws from the cosmetic stream | - |
| src/plugPikiYamashita/drawWorldMap.cpp:309 | (anon) | INACTIVE | #else branch of a PIKI_PC_PORT block; the PC branch already draws from the cosmetic stream | - |
| src/plugPikiYamashita/drawWorldMap.cpp:2380 | (anon) | INACTIVE | #else branch of a PIKI_PC_PORT block; the PC branch already draws from the cosmetic stream | - |
| src/plugPikiYamashita/drawWorldMap.cpp:2381 | (anon) | INACTIVE | #else branch of a PIKI_PC_PORT block; the PC branch already draws from the cosmetic stream | - |

## Header and helper rows

| Site | What | Class | Note |
|---|---|---|---|
| include/system.h:214 | System::getRand / getHalfRand | HELPER | the sim-stream shaper (pc_sim_randf); every gsys->getRand call below goes through it |
| include/NsMath.h:99,116 | NsMathF::getRand / NsMathI::getRand | HELPER | wrappers over gsys->getRand; callers are the Nishimura enemy AI rows |
| include/nlib/System.h:39 | NSystem::random | HELPER | wrapper over system->getRand; callers are NSystem::randomInt, TAIAmove, taiwaitactions, tekibteki |
| include/nlib/Math.h:133-135 | NMathF::rangeRandom / occurred / rateRandom (+ NMathI) | HELPER | wrappers over NSystem::random; callers are the Nakata teki AI rows |
| include/BaseApp.h:40 | BaseApp::rnd | HELPER | pc_sim_randf; no caller (the navi.cpp firefly 'rnd' is a local LCG lambda) |
| include/zen/Math.h:36-56 | zen::Rand / RandShift | HELPER | sim stream; callers are the TAI* and draw* rows |
| include/Actor.h:190 | police-AI candidate pick | GAMEPLAY | picks the target creature from a candidate list |
| include/Omake.h:205 | ClothFader::reset | EFFECT-ONLY | screen-fade cloth grid jitter (2 draws per interior grid point) Action: tried, reverted: moves K (13/10). |

## Already on the cosmetic stream at base

| Site | What | Stream |
|---|---|---|
| src/plugPikiYamashita/particleGenerator.cpp (29 draws) | particle emission/spread/velocity/etc. | cosmetic stream (file-local Rand macro) |
| include/zen/particle.h:443-444 | pmGetArbitUnitVec | cosmetic stream |
| src/plugPikiKando/goalItem.cpp:167 | playEffect (Onion puff leg) | cosmetic stream |
| src/plugPikiKando/viewPiki.cpp:509 | demoDraw damage flash | cosmetic stream |
| src/plugPikiNakata/pcammotionevents.cpp:128-142 | PcamRandomMoveEvent camera shake | cosmetic stream |
| src/plugPikiColin/lightPool.cpp:27 | LightPool focus angle | cosmetic stream |
| src/plugPikiYamashita/drawCommon.cpp:341,381,394 | draw-only UI | cosmetic stream |
| src/plugPikiYamashita/drawWorldMap.cpp:8,289,307,2375-2376 | world map UI | cosmetic stream |
| src/plugPikiYamashita/drawGameOver.cpp:104,171 | game-over UI | cosmetic stream |
| src/plugPikiYamashita/drawCMbest.cpp:87 | best-score UI | cosmetic stream |
| include/zen/MenuPanelMgr.h:79-81 | menu panel drift | cosmetic stream |

## Other generators (not the sim stream)

| Site | What | Status |
|---|---|---|
| src/jaudio/random.c GetRandom_* | JAudio's own LCG (audio engine); never the sim stream | not a sim draw |
| pc_port/audio/pc_audio.cpp:1280 effectRandom; pc_port/dolphin_stubs/audio_stubs.cpp:110 sVoiceRandom | local LCGs for audio variation | not a sim draw |
| src/plugPikiKando/navi.cpp:1922 (pcDrawNightAmbience) | local LCG lambda for the firefly overlay | not a sim draw |
| pc_port/randomizer/pc_randomizer.cpp:29 std::random_device | picks the randomizer seed at run start | outside both streams; seed is an input, not a draw |
| pc_port/launcher/*, prepared_image.h | temp-dir names, launcher UI | outside the game |
| pc_port/netplay/pc_state_hash.cpp:346-347 | PIKMIN_NETPLAY_TEST_PREROLL_RAND test hook | test only |
| src/card/CARDUnlock.c, src/jaudio/dsp_cardunlock.c, src/MSL_C/.../rand.c | srand/rand of the GC card unlock and MSL | not built on PC (MSL uses system libc; card stubs) |
| pc_port/netplay/pc_netplay_det.cpp:189-192 | pc_sim_srand / pc_cosmetic_srand per-day reseed | seeding, not a draw |

## Notes

**Moved (kept).** Captain damage camera-shake pick and bury smoke direction (navi), Spider spark shuffle and chance (spider), Beatle steam shuffle (beatle), the dead firework draw (itemAI), the ambient fish school (fish). None of these run in `throw_release` on the sandbox or Forest of Hope, so K and the hashes were unchanged before #92's GameCube draw-order pin (stand-ins 15 at 30 Hz and 18 at 60, disc 15 / 18; the control hash logs equal the pre-change ones). After #92, K on both trees is 13 / 22; see `standin-k-investigation.md`. That also means they were not exercised at runtime: the claim rests on the code read in the Why column.

**Tried and reverted (K or the proof moved).** Throw_release K at 30 / 60 Hz on the disc tree, base 15 / 18 (pre-#92):

| Change on top of the kept set | K 30 / 60 | Overlay proof |
|---|---|---|
| weeds yaw (:167, :325) + fish | 12 / 23 | holds |
| Omake ClothFader (:205) | 10 / 13 | holds |
| viewPiki :343 and :314 (look dead store) | control 14 / 17, overlay 8 / 20 | broken: the sim rng column differs from tick 96 at 60 Hz (the overlay's tick-96 value is the control's tick-97 value, a one-tick shift) |

K moves for the first two because those draws run every game, so taking them off the sim stream shifts every later sim draw; K is chaotic in the sim sequence. They are still EFFECT-ONLY by the code read, so moving them is a sound change that needs a re-baselined K, not a fix of a bug. The viewPiki result is different and worth a follow-up: removing one dead sim draw per `startHimaLook` made the overlay tree's sim RNG state differ from the control's by one tick of look draws. The look chain (`piki.cpp` startMotion, `updateLookCreature`, `getRandomCollPart`) draws only when `isLooking()` is false or a target is set, and `mLookTimer` is decremented in the render path (`Piki::updateLook`, gated by `aiCullable()` and `pc_render_is_authoritative()`). So whether those sim draws fire can depend on render timing. That is a probable (not confirmed) render-to-sim coupling; it is hidden at base because the draw timing happens to agree between the two trees. A backend-free check: run the control tree twice with `--state-hash-log` under different CPU load and compare. Resolved in [render-sim-coupling.md](render-sim-coupling.md): in deterministic mode `updateLook` runs once per tick, gated by sim state alone, and the hash logs are byte-identical across window size, aspect, render scale, a visible window and CPU load, so the shift is not render timing. In normal play the camera and the window aspect do reach the sim.

**Other leak candidate.** `TAImar.cpp:39` draws on the sim stream inside `hitCreature`, called per emitter update from the particle-generator callback (`TAIeffectAttack.cpp:197`), so generator lifetime from effect data can gate it. `standin-particle-rng.md` omits TAImar from its callback list. Mar is not in the sandbox or Forest of Hope, so the overlay proof cannot show it.

**Off mode.** With deterministic mode off both `pc_sim_rand` and `pc_cosmetic_rand` return `std::rand()` (`pc_sim_rng.cpp`, `draw()`), and the float shapers are the same expression as `System::getRand` and `zen::Rand`. So a draw moved between streams returns the same value at the same position in the libc sequence. `pc_sim_rng_test` now checks all-sim, all-cosmetic and an interleaved order against a plain `rand()` sequence (64 draws, seed 20261028); it passes. The `SpiderLeg` and `SpiderAi` edits inline the `NsMathI/F::getRand` shaping (`x * (n * 0.99999899f)`), so those values are bit-identical too.

**Unmoved UNCLEAR rows.** Emotion and idle motion picks (`selectRandomly`, pikiState 3303-3402, naviState:485, naviDemoState), `taimotionactions.cpp:218`, `viewPiki.cpp:212`, `collInfo.cpp:907`, `aiGuard.cpp:70`, `TAImar.cpp:39`. Motion picks change animation length, and animation keys drive state changes, so they are not provably visual.
