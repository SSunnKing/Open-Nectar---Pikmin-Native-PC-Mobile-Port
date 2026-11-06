#include "nectar_assets/standin_objects.h"

// Structure-only measurements of the disc's AnimMgr tables and bundles, for the
// 46 object, ship-part, plant, gate, boss and enemy bundles of the boot manifest
// (everything but pikis/animMgr.bin, which standin_anims.cpp owns) except the two
// gates, whose table and bundles standin_sandbox.cpp and standin_world.cpp write. The rules
// for what may be read from the disc are in standin_objects.h.
//
// Per info: { frame count N, 'p00' flags, 'spd', anim-key frames, info keys }.
// An info key is { index into the anim-key list, key type } with the types of
// AnimKeyRecord: 0 loop start, 1 loop end, 2 KEY_Action0, 3 KEY_Action1.
// Anim keys are the disc's, clamped below N exactly as AnimInfo::checkAnimData
// does when the table loads. Dropped on purpose: names, event-key records (their
// counts per info are 0 to 19: sound and effect cues, which no scenario checks)
// and the curves. Where two infos used one animation on the disc (a bundle entry
// shared by name), each stand-in info has its own entry; the frame count is the
// one of the entry the disc's info resolved to, and the three naka1 infos whose
// entries the disc bundle lacks (it opens loose .dck files that do not exist and
// falls back to a dummy) use the last anim key plus one.

namespace nectar {

const std::vector<ObjectAnimProfile>& objectAnimProfiles()
{
	static const std::vector<ObjectAnimProfile> list = {
	// bomb: 1 infos, 1 joints
	{ "bomb", "dataDir/objects/bomb/anims.bin", "dataDir/objects/bomb/bomb.anm", "objects/bomb/motion", 1,
	  {
	      { 5, 2, 30.0f, { 0, 4 }, {} },
	  } },
	// fl_water: 2 infos, 4 joints
	{ "fl_water", "dataDir/objects/fl_water/anims.bin", "dataDir/objects/fl_water/fl_water.anm", "objects/fl_water/motion", 4,
	  {
	      { 37, 5, 30.0f, { 0, 19, 23, 30, 36 }, { { 1, 0 }, { 2, 1 }, { 3, 2 } } },
	      { 13, 5, 30.0f, { 0, 8 }, {} },
	  } },
	// goal: 13 infos, 13 joints
	{ "goal", "dataDir/objects/goal/anims.bin", "dataDir/objects/goal/goal.anm", "objects/goal/motion", 13,
	  {
	      { 60, 1, 30.0f, { 0, 20, 30, 40, 58 }, { { 1, 0 }, { 3, 1 }, { 2, 2 } } },
	      { 40, 1, 30.0f, { 0, 38 }, { { 0, 0 }, { 1, 1 } } },
	      { 50, 1, 30.0f, { 0, 48 }, {} },
	      { 100, 2, 30.0f, { 0, 60, 99 }, { { 1, 2 } } }, // blue boot: our own short motion (the disc's has 360 frames, key 240)
	      // red boot: the disc's five effect events too (frame, value), in the disc's order. GoalItem::animationKeyUpdated
	      // (boot motions 3-5): 0 and 1 and 3 are particles, 4 starts the colour fade, 2 opens the Onion's route way point,
	      // which the boot closed (GoalItem::startAI): without it no Pikmin carries a pellet to a booted red Onion. The blue
	      // and yellow boots keep none (no scenario boots them and carries; onion_skip's skip opens the point itself).
	      { 472, 2, 30.0f, { 0, 270, 471 }, { { 1, 2 } }, { { 31, 0 }, { 153, 1 }, { 221, 2 }, { 227, 3 }, { 32, 4 } } },
	      { 360, 2, 30.0f, { 0, 240, 359 }, { { 1, 2 } } },
	      { 399, 2, 30.0f, { 0, 398 }, {} },
	      { 399, 2, 30.0f, { 0, 398 }, {} },
	      { 399, 2, 30.0f, { 0, 398 }, {} },
	      { 280, 1, 30.0f, { 0, 279 }, {} },
	      { 280, 1, 30.0f, { 0, 279 }, {} },
	      { 280, 1, 30.0f, { 0, 279 }, {} },
	      { 40, 1, 30.0f, { 0, 39 }, { { 0, 0 }, { 1, 1 } } },
	  } },
	// pikihead: 7 infos, 4 joints
	{ "pikihead", "dataDir/objects/pikihead/anims.bin", "dataDir/objects/pikihead/pikihead.anm", "objects/pikihead/motion", 4,
	  {
	      { 10, 5, 30.0f, { 0, 9 }, { { 0, 0 }, { 1, 1 } } },
	      { 30, 5, 30.0f, { 0, 10, 29 }, { { 1, 0 }, { 2, 1 } } },
	      { 60, 5, 30.0f, { 0, 59 }, {} },
	      { 20, 5, 30.0f, { 0, 19 }, { { 0, 0 }, { 1, 1 } } },
	      { 30, 5, 30.0f, { 0, 20, 29 }, { { 1, 2 } } },
	      { 120, 5, 30.0f, { 0, 119 }, {} },
	      { 10, 5, 30.0f, { 0, 9 }, {} },
	  } },
	// water: 4 infos, 1 joints
	{ "water", "dataDir/objects/water/anims.bin", "dataDir/objects/water/water.anm", "objects/water/motion", 1,
	  {
	      { 40, 5, 30.0f, { 0, 39 }, {} },
	      { 60, 5, 30.0f, { 0, 50 }, {} },
	      { 41, 5, 30.0f, { 0, 40 }, {} },
	      { 50, 5, 30.0f, { 0, 49 }, {} },
	  } },
	// ufo0705: 17 infos, 57 joints
	{ "ufo0705", "dataDir/objects/ufo/ufo0705.bin", "dataDir/objects/ufo/ufo0705.anm", "objects/ufo/motion", 57,
	  {
	      { 95, 2, 30.0f, { 0, 14, 94 }, { { 1, 2 } } },
	      { 65, 2, 30.0f, { 0, 14, 64 }, { { 1, 2 } } },
	      { 95, 2, 30.0f, { 0, 94 }, {} },
	      { 120, 2, 30.0f, { 0, 119 }, {} },
	      { 40, 2, 30.0f, { 0, 39 }, { { 0, 0 }, { 1, 1 } } },
	      { 120, 2, 30.0f, { 0, 119 }, {} },
	      { 120, 2, 30.0f, { 0, 90, 119 }, { { 1, 0 }, { 2, 1 } } },
	      { 120, 2, 30.0f, { 0, 119 }, {} },
	      { 120, 2, 30.0f, { 0, 119 }, {} },
	      { 120, 2, 30.0f, { 0, 100, 119 }, { { 1, 0 }, { 2, 1 } } },
	      { 120, 2, 30.0f, { 0, 119 }, {} },
	      { 399, 1, 30.0f, { 0, 398 }, {} },
	      { 399, 1, 30.0f, { 0, 398 }, {} },
	      { 399, 1, 30.0f, { 0, 398 }, {} },
	      { 399, 1, 30.0f, { 0, 398 }, {} },
	      { 399, 1, 30.0f, { 0, 398 }, {} },
	      { 29, 2, 30.0f, { 0, 14, 28 }, { { 1, 2 } } },
	  } },
	// white1: 1 infos, 3 joints
	{ "white1", "dataDir/objects/pellets/white1.bin", "dataDir/objects/pellets/white1.anm", "objects/pellets/motion1", 3,
	  {
	      { 41, 2, 30.0f, { 0, 10, 30, 40 }, { { 1, 0 }, { 2, 1 } } },
	  } },
	// white2: 1 infos, 3 joints
	{ "white2", "dataDir/objects/pellets/white2.bin", "dataDir/objects/pellets/white2.anm", "objects/pellets/motion2", 3,
	  {
	      { 41, 2, 30.0f, { 0, 10, 30, 40 }, { { 1, 0 }, { 2, 1 } } },
	  } },
	// white3: 1 infos, 3 joints
	{ "white3", "dataDir/objects/pellets/white3.bin", "dataDir/objects/pellets/white3.anm", "objects/pellets/motion3", 3,
	  {
	      { 41, 2, 30.0f, { 0, 10, 30, 40 }, { { 1, 0 }, { 2, 1 } } },
	  } },
	// white4: 1 infos, 3 joints
	{ "white4", "dataDir/objects/pellets/white4.bin", "dataDir/objects/pellets/white4.anm", "objects/pellets/motion4", 3,
	  {
	      { 41, 2, 30.0f, { 0, 10, 30, 40 }, { { 1, 0 }, { 2, 1 } } },
	  } },
	// soto1: 5 infos, 11 joints
	{ "soto1", "dataDir/objects/ufoparts/soto1.bin", "dataDir/objects/ufoparts/soto1.anm", "objects/ufoparts/msoto1", 11,
	  {
	      { 40, 2, 30.0f, { 0, 10, 30, 39 }, { { 1, 0 }, { 2, 1 } } },
	      { 60, 2, 30.0f, { 0, 59 }, {} },
	      { 40, 2, 30.0f, { 0, 39 }, { { 0, 0 }, { 1, 1 } } },
	      { 80, 2, 30.0f, { 0, 79 }, { { 0, 0 }, { 1, 1 } } },
	      { 120, 2, 30.0f, { 0, 116, 119 }, { { 1, 0 }, { 2, 1 } } },
	  } },
	// soto2: 5 infos, 6 joints
	{ "soto2", "dataDir/objects/ufoparts/soto2.bin", "dataDir/objects/ufoparts/soto2.anm", "objects/ufoparts/msoto2", 6,
	  {
	      { 40, 2, 30.0f, { 0, 10, 30, 39 }, { { 1, 0 }, { 2, 1 } } },
	      { 60, 2, 30.0f, { 0, 59 }, {} },
	      { 100, 2, 30.0f, { 0, 99 }, { { 0, 0 }, { 1, 1 } } },
	      { 100, 2, 30.0f, { 0, 99 }, { { 0, 0 }, { 1, 1 } } },
	      { 120, 2, 30.0f, { 0, 70, 119 }, { { 1, 0 }, { 2, 1 } } },
	  } },
	// soto3: 5 infos, 1 joints
	{ "soto3", "dataDir/objects/ufoparts/soto3.bin", "dataDir/objects/ufoparts/soto3.anm", "objects/ufoparts/msoto3", 1,
	  {
	      { 40, 2, 30.0f, { 0, 10, 30, 39 }, { { 1, 0 }, { 2, 1 } } },
	      { 60, 2, 30.0f, { 0, 59 }, {} },
	      { 40, 2, 30.0f, { 0, 39 }, { { 0, 0 }, { 1, 1 } } },
	      { 10, 2, 30.0f, { 0, 9 }, { { 0, 0 }, { 1, 1 } } },
	      { 40, 2, 30.0f, { 0, 39 }, {} },
	  } },
	// soto4: 5 infos, 1 joints
	{ "soto4", "dataDir/objects/ufoparts/soto4.bin", "dataDir/objects/ufoparts/soto4.anm", "objects/ufoparts/msoto4", 1,
	  {
	      { 40, 2, 30.0f, { 0, 10, 30, 39 }, { { 1, 0 }, { 2, 1 } } },
	      { 60, 2, 30.0f, { 0, 59 }, {} },
	      { 40, 2, 30.0f, { 0, 39 }, { { 0, 0 }, { 1, 1 } } },
	      { 40, 2, 30.0f, { 0, 39 }, { { 0, 0 }, { 1, 1 } } },
	      { 40, 2, 30.0f, { 0, 39 }, {} },
	  } },
	// soto5: 5 infos, 20 joints
	{ "soto5", "dataDir/objects/ufoparts/soto5.bin", "dataDir/objects/ufoparts/soto5.anm", "objects/ufoparts/msoto5", 20,
	  {
	      { 40, 2, 30.0f, { 0, 10, 30, 39 }, { { 1, 0 }, { 2, 1 } } },
	      { 60, 2, 30.0f, { 0, 59 }, {} },
	      { 100, 2, 30.0f, { 0, 99 }, { { 0, 0 }, { 1, 1 } } },
	      { 10, 2, 30.0f, { 0, 9 }, { { 0, 0 }, { 1, 1 } } },
	      { 10, 2, 30.0f, { 0, 9, 9 }, { { 1, 0 }, { 2, 1 } } },
	  } },
	// fuzoku1: 5 infos, 5 joints
	{ "fuzoku1", "dataDir/objects/ufoparts/fuzoku1.bin", "dataDir/objects/ufoparts/fuzoku1.anm", "objects/ufoparts/mfuzoku1", 5,
	  {
	      { 40, 2, 30.0f, { 0, 10, 30, 39 }, { { 1, 0 }, { 2, 1 } } },
	      { 60, 2, 30.0f, { 0, 59 }, {} },
	      { 240, 2, 30.0f, { 0, 239 }, { { 0, 0 }, { 1, 1 } } },
	      { 80, 2, 30.0f, { 0, 79 }, { { 0, 0 }, { 1, 1 } } },
	      { 120, 2, 30.0f, { 0, 80, 119 }, { { 1, 0 }, { 2, 1 } } },
	  } },
	// fuzoku2: 5 infos, 3 joints
	{ "fuzoku2", "dataDir/objects/ufoparts/fuzoku2.bin", "dataDir/objects/ufoparts/fuzoku2.anm", "objects/ufoparts/mfuzoku2", 3,
	  {
	      { 40, 2, 30.0f, { 0, 10, 30, 39 }, { { 1, 0 }, { 2, 1 } } },
	      { 60, 2, 30.0f, { 0, 59 }, {} },
	      { 100, 2, 30.0f, { 0, 99 }, { { 0, 0 }, { 1, 1 } } },
	      { 10, 2, 30.0f, { 0, 9 }, { { 0, 0 }, { 1, 1 } } },
	      { 120, 2, 30.0f, { 0, 118, 119 }, { { 1, 0 }, { 2, 1 } } },
	  } },
	// fuzoku3: 5 infos, 3 joints
	{ "fuzoku3", "dataDir/objects/ufoparts/fuzoku3.bin", "dataDir/objects/ufoparts/fuzoku3.anm", "objects/ufoparts/mfuzoku3", 3,
	  {
	      { 40, 2, 30.0f, { 0, 10, 30, 39 }, { { 1, 0 }, { 2, 1 } } },
	      { 60, 2, 30.0f, { 0, 59 }, {} },
	      { 100, 2, 30.0f, { 0, 99 }, { { 0, 0 }, { 1, 1 } } },
	      { 10, 2, 30.0f, { 0, 9 }, { { 0, 0 }, { 1, 1 } } },
	      { 120, 2, 30.0f, { 0, 118, 119 }, { { 1, 0 }, { 2, 1 } } },
	  } },
	// fuzoku4: 5 infos, 1 joints
	{ "fuzoku4", "dataDir/objects/ufoparts/fuzoku4.bin", "dataDir/objects/ufoparts/fuzoku4.anm", "objects/ufoparts/mfuzoku4", 1,
	  {
	      { 40, 2, 30.0f, { 0, 10, 30, 39 }, { { 1, 0 }, { 2, 1 } } },
	      { 60, 2, 30.0f, { 0, 59 }, {} },
	      { 10, 2, 30.0f, { 0, 9 }, { { 0, 0 }, { 1, 1 } } },
	      { 10, 2, 30.0f, { 0, 9 }, { { 0, 0 }, { 1, 1 } } },
	      { 10, 2, 30.0f, { 0, 9 }, {} },
	  } },
	// fuzoku5: 5 infos, 1 joints
	{ "fuzoku5", "dataDir/objects/ufoparts/fuzoku5.bin", "dataDir/objects/ufoparts/fuzoku5.anm", "objects/ufoparts/mfuzoku5", 1,
	  {
	      { 40, 2, 30.0f, { 0, 10, 30, 39 }, { { 1, 0 }, { 2, 1 } } },
	      { 60, 2, 30.0f, { 0, 59 }, {} },
	      { 40, 2, 30.0f, { 0, 39 }, { { 0, 0 }, { 1, 1 } } },
	      { 40, 2, 30.0f, { 0, 39 }, { { 0, 0 }, { 1, 1 } } },
	      { 40, 2, 30.0f, { 0, 39 }, {} },
	  } },
	// fuzoku6: 5 infos, 2 joints
	{ "fuzoku6", "dataDir/objects/ufoparts/fuzoku6.bin", "dataDir/objects/ufoparts/fuzoku6.anm", "objects/ufoparts/mfuzoku6", 2,
	  {
	      { 40, 2, 30.0f, { 0, 10, 30, 39 }, { { 1, 0 }, { 2, 1 } } },
	      { 60, 2, 30.0f, { 0, 59 }, {} },
	      { 300, 2, 30.0f, { 0, 299 }, { { 0, 0 }, { 1, 1 } } },
	      { 20, 2, 30.0f, { 0, 19 }, { { 0, 0 }, { 1, 1 } } },
	      { 40, 2, 30.0f, { 0, 39 }, {} },
	  } },
	// fuzoku7: 5 infos, 1 joints
	{ "fuzoku7", "dataDir/objects/ufoparts/fuzoku7.bin", "dataDir/objects/ufoparts/fuzoku7.anm", "objects/ufoparts/mfuzoku7", 1,
	  {
	      { 40, 2, 30.0f, { 0, 10, 30, 39 }, { { 1, 0 }, { 2, 1 } } },
	      { 60, 2, 30.0f, { 0, 59 }, {} },
	      { 10, 2, 30.0f, { 0, 9 }, { { 0, 0 }, { 1, 1 } } },
	      { 10, 2, 30.0f, { 0, 9 }, { { 0, 0 }, { 1, 1 } } },
	      { 120, 2, 30.0f, { 0, 70, 119 }, { { 1, 0 }, { 2, 1 } } },
	  } },
	// fuzoku8: 5 infos, 1 joints
	{ "fuzoku8", "dataDir/objects/ufoparts/fuzoku8.bin", "dataDir/objects/ufoparts/fuzoku8.anm", "objects/ufoparts/mfuzoku8", 1,
	  {
	      { 40, 2, 30.0f, { 0, 10, 30, 39 }, { { 1, 0 }, { 2, 1 } } },
	      { 60, 2, 30.0f, { 0, 59 }, {} },
	      { 10, 2, 30.0f, { 0, 9 }, { { 0, 0 }, { 1, 1 } } },
	      { 10, 2, 30.0f, { 0, 9 }, { { 0, 0 }, { 1, 1 } } },
	      { 120, 2, 30.0f, { 0, 70, 119 }, { { 1, 0 }, { 2, 1 } } },
	  } },
	// fuzoku9: 5 infos, 2 joints
	{ "fuzoku9", "dataDir/objects/ufoparts/fuzoku9.bin", "dataDir/objects/ufoparts/fuzoku9.anm", "objects/ufoparts/mfuzoku9", 2,
	  {
	      { 40, 2, 30.0f, { 0, 10, 30, 39 }, { { 1, 0 }, { 2, 1 } } },
	      { 60, 2, 30.0f, { 0, 59 }, {} },
	      { 10, 2, 30.0f, { 0, 9 }, { { 0, 0 }, { 1, 1 } } },
	      { 40, 2, 30.0f, { 0, 39 }, { { 0, 0 }, { 1, 1 } } },
	      { 40, 2, 30.0f, { 0, 39 }, {} },
	  } },
	// fuzoku10: 5 infos, 1 joints
	{ "fuzoku10", "dataDir/objects/ufoparts/fuzoku10.bin", "dataDir/objects/ufoparts/fuzoku10.anm", "objects/ufoparts/mfuzoku10", 1,
	  {
	      { 40, 2, 30.0f, { 0, 10, 30, 39 }, { { 1, 0 }, { 2, 1 } } },
	      { 60, 2, 30.0f, { 0, 59 }, {} },
	      { 40, 2, 30.0f, { 0, 39 }, { { 0, 0 }, { 1, 1 } } },
	      { 40, 2, 30.0f, { 0, 39 }, { { 0, 0 }, { 1, 1 } } },
	      { 40, 2, 30.0f, { 0, 39 }, {} },
	  } },
	// fuzoku11: 5 infos, 1 joints
	{ "fuzoku11", "dataDir/objects/ufoparts/fuzoku11.bin", "dataDir/objects/ufoparts/fuzoku11.anm", "objects/ufoparts/mfuzoku11", 1,
	  {
	      { 40, 2, 30.0f, { 0, 10, 30, 39 }, { { 1, 0 }, { 2, 1 } } },
	      { 60, 2, 30.0f, { 0, 59 }, {} },
	      { 40, 2, 30.0f, { 0, 39 }, { { 0, 0 }, { 1, 1 } } },
	      { 40, 2, 30.0f, { 0, 39 }, { { 0, 0 }, { 1, 1 } } },
	      { 40, 2, 30.0f, { 0, 39 }, {} },
	  } },
	// naka1: 5 infos, 1 joints
	{ "naka1", "dataDir/objects/ufoparts/naka1.bin", "dataDir/objects/ufoparts/naka1.anm", "objects/ufoparts/mnaka1", 1,
	  {
	      { 40, 2, 30.0f, { 0, 10, 30, 39 }, { { 1, 0 }, { 2, 1 } } },
	      { 10, 2, 30.0f, { 0, 9 }, {} },
	      { 10, 2, 30.0f, { 0, 9 }, { { 0, 0 }, { 1, 1 } } },
	      { 10, 2, 30.0f, { 0, 9 }, { { 0, 0 }, { 1, 1 } } },
	      { 20, 2, 30.0f, { 0, 19 }, {} },
	  } },
	// naka4: 4 infos, 1 joints
	{ "naka4", "dataDir/objects/ufoparts/naka4.bin", "dataDir/objects/ufoparts/naka4.anm", "objects/ufoparts/mnaka4", 1,
	  {
	      { 40, 5, 30.0f, { 0, 10, 30, 39 }, { { 1, 0 }, { 2, 1 } } },
	      { 10, 5, 30.0f, { 0, 9 }, {} },
	      { 10, 5, 30.0f, { 0, 9 }, {} },
	      { 10, 5, 30.0f, { 0, 9 }, { { 0, 0 }, { 1, 1 } } },
	  } },
	// naka7: 4 infos, 1 joints
	{ "naka7", "dataDir/objects/ufoparts/naka7.bin", "dataDir/objects/ufoparts/naka7.anm", "objects/ufoparts/mnaka7", 1,
	  {
	      { 40, 2, 30.0f, { 0, 10, 30, 39 }, { { 1, 0 }, { 2, 1 } } },
	      { 10, 5, 30.0f, { 0, 9 }, {} },
	      { 10, 5, 30.0f, { 0, 9 }, {} },
	      { 10, 2, 30.0f, { 0, 9 }, { { 0, 0 }, { 1, 1 } } },
	  } },
	// chidome: 1 infos, 2 joints
	{ "chidome", "dataDir/objects/plants/chidome.bin", "dataDir/objects/plants/chidome.anm", "objects/plants/chidome", 2,
	  {
	      { 50, 2, 30.0f, { 0, 49 }, {} },
	  } },
	// clover: 1 infos, 3 joints
	{ "clover", "dataDir/objects/plants/clover.bin", "dataDir/objects/plants/clover.anm", "objects/plants/clover", 3,
	  {
	      { 50, 2, 30.0f, { 0, 49 }, {} },
	  } },
	// hae: 1 infos, 16 joints
	{ "hae", "dataDir/objects/plants/hae.bin", "dataDir/objects/plants/hae.anm", "objects/plants/hae", 16,
	  {
	      { 200, 2, 30.0f, { 0, 199 }, {} },
	  } },
	// hutaba: 1 infos, 3 joints
	{ "hutaba", "dataDir/objects/plants/hutaba.bin", "dataDir/objects/plants/hutaba.anm", "objects/plants/hutaba", 3,
	  {
	      { 50, 2, 30.0f, { 0, 49 }, {} },
	  } },
	// ine: 1 infos, 3 joints
	{ "ine", "dataDir/objects/plants/ine.bin", "dataDir/objects/plants/ine.anm", "objects/plants/ine", 3,
	  {
	      { 50, 2, 30.0f, { 0, 49 }, {} },
	  } },
	// tanpopo: 1 infos, 3 joints
	{ "tanpopo", "dataDir/objects/plants/tanpopo.bin", "dataDir/objects/plants/tanpopo.anm", "objects/plants/tanpopo", 3,
	  {
	      { 50, 2, 30.0f, { 0, 49 }, {} },
	  } },
	// mizu: 11 infos, 1 joints
	{ "mizu", "dataDir/bosses/mizu/anims.bin", "dataDir/bosses/mizu/mizu.anm", "bosses/mizu/motion", 1,
	  {
	      { 2, 5, 30.0f, { 0, 1 }, {} },
	      { 2, 5, 30.0f, { 0, 1 }, {} },
	      { 2, 5, 30.0f, { 0, 1 }, { { 0, 0 }, { 1, 1 } } },
	      { 2, 5, 30.0f, { 0, 1 }, {} },
	      { 2, 5, 30.0f, { 0, 1 }, {} },
	      { 2, 5, 30.0f, { 0, 1 }, {} },
	      { 2, 5, 30.0f, { 0, 1 }, {} },
	      { 2, 5, 30.0f, { 0, 1 }, {} },
	      { 2, 5, 30.0f, { 0, 1 }, {} },
	      { 2, 5, 30.0f, { 0, 1 }, {} },
	      { 2, 5, 30.0f, { 0, 1 }, { { 0, 0 }, { 1, 1 } } },
	  } },
	// snake: 15 infos, 15 joints
	{ "snake", "dataDir/bosses/snake/anims.bin", "dataDir/bosses/snake/snake.anm", "bosses/snake/motion", 15,
	  {
	      { 150, 5, 30.0f, { 0, 67, 75, 149 }, { { 1, 2 }, { 2, 3 } } },
	      { 150, 5, 30.0f, { 0, 149 }, {} },
	      { 50, 5, 30.0f, { 0, 49 }, { { 0, 0 }, { 1, 1 } } },
	      { 50, 5, 30.0f, { 0, 49 }, {} },
	      { 70, 5, 30.0f, { 0, 35, 69 }, { { 1, 2 } } },
	      { 20, 5, 30.0f, { 0, 19 }, { { 0, 0 }, { 1, 1 } } },
	      { 55, 6, 30.0f, { 0, 54 }, {} },
	      { 165, 5, 30.0f, { 0, 116, 164 }, { { 1, 2 } } },
	      { 35, 5, 30.0f, { 0, 34 }, {} },
	      { 35, 5, 30.0f, { 0, 15, 34 }, { { 1, 2 } } },
	      { 80, 6, 30.0f, { 0, 32, 36, 49, 79 }, { { 1, 0 }, { 2, 2 }, { 3, 1 } } },
	      { 80, 6, 30.0f, { 0, 32, 35, 49, 79 }, { { 1, 0 }, { 2, 2 }, { 3, 1 } } },
	      { 80, 6, 30.0f, { 0, 32, 37, 49, 79 }, { { 1, 0 }, { 2, 2 }, { 3, 1 } } },
	      { 75, 6, 30.0f, { 0, 28, 33, 49, 74 }, { { 1, 0 }, { 2, 2 }, { 3, 1 } } },
	      { 75, 6, 30.0f, { 0, 28, 33, 49, 74 }, { { 1, 0 }, { 2, 2 }, { 3, 1 } } },
	  } },
	// beatle: 15 infos, 19 joints
	{ "beatle", "dataDir/tekikeys/beatle.key", "dataDir/tekis/beatle/beatle.anm", "tekis/beatle/motion", 19,
	  {
	      { 100, 5, 30.0f, { 0, 79, 99 }, { { 1, 2 } } },
	      { 45, 5, 30.0f, { 0, 44 }, {} },
	      { 50, 5, 30.0f, { 0, 44 }, { { 0, 0 }, { 1, 1 } } },
	      { 45, 5, 30.0f, { 0, 44 }, {} },
	      { 45, 5, 30.0f, { 0, 44 }, {} },
	      { 45, 5, 30.0f, { 0, 44 }, {} },
	      { 45, 5, 30.0f, { 0, 12, 33, 44 }, { { 1, 0 }, { 2, 1 } } },
	      { 45, 5, 30.0f, { 0, 44 }, {} },
	      { 93, 5, 30.0f, { 0, 31, 36, 37, 38, 47, 51, 92 }, { { 1, 2 }, { 2, 0 }, { 3, 3 }, { 4, 1 }, { 5, 5 }, { 6, 4 } } },
	      { 140, 5, 30.0f, { 0, 53, 60, 71, 95, 96, 128, 139 }, { { 1, 3 }, { 2, 5 }, { 3, 0 }, { 4, 1 }, { 5, 4 }, { 6, 2 } } },
	      { 25, 5, 30.0f, { 0, 24 }, { { 0, 0 }, { 1, 1 } } },
	      { 25, 5, 30.0f, { 0, 24 }, { { 0, 0 }, { 1, 1 } } },
	      { 35, 5, 30.0f, { 0, 14, 34 }, { { 1, 2 } } },
	      { 45, 5, 30.0f, { 0, 44 }, {} },
	      { 41, 5, 30.0f, { 0, 9, 29, 40 }, { { 1, 0 }, { 2, 1 } } },
	  } },
	// chappy: 15 infos, 17 joints
	{ "chappy", "dataDir/tekikeys/chappy.key", "dataDir/tekis/chappy/chappy.anm", "tekis/chappy/motion", 17,
	  {
	      { 90, 5, 30.0f, { 0, 89 }, {} },
	      { 75, 5, 30.0f, { 0, 74 }, {} },
	      { 75, 5, 30.0f, { 0, 10, 60, 74 }, { { 1, 0 }, { 2, 1 } } },
	      { 75, 5, 30.0f, { 0, 74 }, {} },
	      { 25, 5, 30.0f, { 0, 8, 20, 24 }, { { 1, 0 }, { 2, 1 } } },
	      { 25, 5, 30.0f, { 0, 24 }, {} },
	      { 55, 6, 30.0f, { 0, 10, 40, 54 }, { { 1, 0 }, { 2, 1 } } },
	      { 75, 6, 30.0f, { 0, 74 }, {} },
	      { 90, 5, 30.0f, { 0, 10, 84, 89 }, { { 1, 2 }, { 2, 3 } } },
	      { 80, 5, 30.0f, { 0, 32, 79 }, { { 1, 2 } } },
	      { 106, 5, 30.0f, { 0, 105 }, {} },
	      { 45, 5, 30.0f, { 0, 25, 44 }, { { 1, 2 } } },
	      { 25, 5, 30.0f, { 0, 24 }, {} },
	      { 25, 5, 30.0f, { 0, 24 }, {} },
	      { 40, 5, 30.0f, { 0, 10, 30, 39 }, { { 1, 0 }, { 2, 1 } } },
	  } },
	// iwagon: 15 infos, 8 joints
	{ "iwagon", "dataDir/tekikeys/iwagon.key", "dataDir/tekis/iwagon/iwagon.anm", "tekis/iwagon/motion", 8,
	  {
	      { 70, 5, 30.0f, { 0, 69 }, {} },
	      { 40, 5, 30.0f, { 0, 39 }, {} },
	      { 40, 5, 30.0f, { 0, 39 }, {} },
	      { 40, 5, 30.0f, { 0, 39 }, {} },
	      { 40, 5, 30.0f, { 0, 39 }, {} },
	      { 40, 5, 30.0f, { 0, 39 }, {} },
	      { 40, 6, 6.0f, { 0, 39 }, { { 0, 0 }, { 1, 1 } } },
	      { 40, 5, 30.0f, { 0, 39 }, {} },
	      { 40, 5, 30.0f, { 0, 39 }, {} },
	      { 40, 5, 30.0f, { 0, 39 }, {} },
	      { 40, 5, 30.0f, { 0, 39 }, {} },
	      { 40, 5, 30.0f, { 0, 39 }, {} },
	      { 40, 5, 30.0f, { 0, 39 }, {} },
	      { 40, 5, 30.0f, { 0, 39 }, {} },
	      { 40, 5, 30.0f, { 0, 39 }, {} },
	  } },
	// kabekuiA: 15 infos, 6 joints
	{ "kabekuiA", "dataDir/tekikeys/kabekuiA.key", "dataDir/tekis/kabekuiA/kabekuiA.anm", "tekis/kabekuiA/motion", 6,
	  {
	      { 100, 5, 30.0f, { 0, 17, 99 }, { { 1, 2 } } },
	      { 80, 5, 30.0f, { 0, 1, 79 }, { { 1, 2 } } },
	      { 20, 5, 30.0f, { 0, 19 }, { { 0, 0 }, { 1, 1 } } },
	      { 20, 5, 30.0f, { 0, 19 }, {} },
	      { 60, 5, 30.0f, { 0, 1, 59 }, { { 1, 2 } } },
	      { 40, 5, 30.0f, { 0, 11, 39 }, { { 1, 2 } } },
	      { 20, 5, 30.0f, { 0, 19 }, { { 0, 0 }, { 1, 1 } } },
	      { 20, 5, 30.0f, { 0, 19, 19, 19, 19 }, { { 1, 2 }, { 2, 0 }, { 3, 1 } } },
	      { 20, 5, 30.0f, { 0, 1, 7, 16, 19 }, { { 0, 0 }, { 1, 2 }, { 2, 3 }, { 3, 4 }, { 4, 1 } } },
	      { 20, 5, 30.0f, { 0, 19 }, {} },
	      { 20, 5, 30.0f, { 0, 19 }, {} },
	      { 20, 5, 30.0f, { 0, 19 }, {} },
	      { 20, 5, 30.0f, { 0, 19 }, {} },
	      { 20, 5, 30.0f, { 0, 19 }, {} },
	      { 39, 5, 30.0f, { 0, 10, 30, 38 }, { { 1, 0 }, { 2, 1 } } },
	  } },
	// kabekuiB: 15 infos, 6 joints
	{ "kabekuiB", "dataDir/tekikeys/kabekuiB.key", "dataDir/tekis/kabekuiB/kabekuiB.anm", "tekis/kabekuiB/motion", 6,
	  {
	      { 100, 5, 30.0f, { 0, 17, 99 }, { { 1, 2 } } },
	      { 80, 5, 30.0f, { 0, 1, 79 }, { { 1, 2 } } },
	      { 20, 5, 30.0f, { 0, 19 }, {} },
	      { 20, 5, 30.0f, { 0, 19 }, {} },
	      { 60, 5, 30.0f, { 0, 1, 59 }, { { 1, 2 } } },
	      { 40, 5, 30.0f, { 0, 11, 39 }, { { 1, 2 } } },
	      { 20, 5, 30.0f, { 0, 19 }, { { 0, 0 }, { 1, 1 } } },
	      { 20, 5, 30.0f, { 0, 19, 19, 19, 19 }, { { 1, 2 }, { 2, 0 }, { 3, 1 } } },
	      { 20, 5, 30.0f, { 0, 1, 7, 16, 19 }, { { 0, 0 }, { 1, 2 }, { 2, 3 }, { 3, 4 }, { 4, 1 } } },
	      { 20, 5, 30.0f, { 0, 19 }, {} },
	      { 30, 5, 30.0f, { 0, 7, 14, 29 }, { { 1, 2 }, { 2, 3 } } },
	      { 65, 5, 30.0f, { 0, 51, 64 }, { { 1, 2 } } },
	      { 20, 5, 30.0f, { 0, 19 }, {} },
	      { 20, 5, 30.0f, { 0, 19 }, {} },
	      { 39, 5, 30.0f, { 0, 10, 30, 38 }, { { 1, 0 }, { 2, 1 } } },
	  } },
	// otama: 15 infos, 8 joints
	{ "otama", "dataDir/tekikeys/otama.key", "dataDir/tekis/otama/otama.anm", "tekis/otama/motion", 8,
	  {
	      { 65, 5, 30.0f, { 0, 64 }, {} },
	      { 20, 5, 30.0f, { 0, 19 }, {} },
	      { 30, 5, 30.0f, { 0, 4, 24, 29 }, { { 1, 0 }, { 2, 1 } } },
	      { 20, 5, 30.0f, { 0, 19 }, {} },
	      { 20, 5, 30.0f, { 0, 5, 19 }, { { 1, 2 } } },
	      { 20, 5, 30.0f, { 0, 19 }, {} },
	      { 21, 5, 30.0f, { 0, 4, 14, 19 }, { { 1, 0 }, { 2, 1 } } },
	      { 20, 5, 30.0f, { 0, 19 }, {} },
	      { 20, 5, 30.0f, { 0, 19 }, {} },
	      { 20, 5, 30.0f, { 0, 19 }, {} },
	      { 20, 5, 30.0f, { 0, 19 }, {} },
	      { 20, 5, 30.0f, { 0, 19 }, {} },
	      { 20, 5, 30.0f, { 0, 19 }, {} },
	      { 20, 5, 30.0f, { 0, 19 }, {} },
	      { 39, 5, 30.0f, { 0, 10, 30, 38 }, { { 1, 0 }, { 2, 1 } } },
	  } },
	// palm: 15 infos, 5 joints
	{ "palm", "dataDir/tekikeys/palm.key", "dataDir/tekis/palm/palm.anm", "tekis/palm/motion", 5,
	  {
	      { 20, 5, 30.0f, { 0, 19 }, {} },
	      { 20, 5, 30.0f, { 0, 19 }, {} },
	      { 30, 5, 30.0f, { 0, 29 }, { { 0, 0 }, { 1, 1 } } },
	      { 20, 5, 30.0f, { 0, 19 }, {} },
	      { 20, 5, 30.0f, { 0, 19 }, {} },
	      { 30, 5, 30.0f, { 0, 29 }, { { 0, 0 }, { 1, 1 } } },
	      { 20, 5, 30.0f, { 0, 19 }, {} },
	      { 20, 5, 30.0f, { 0, 19 }, {} },
	      { 30, 5, 30.0f, { 0, 29 }, { { 0, 0 }, { 1, 1 } } },
	      { 30, 5, 30.0f, { 0, 29 }, {} },
	      { 30, 5, 30.0f, { 0, 29 }, {} },
	      { 30, 5, 30.0f, { 0, 29 }, {} },
	      { 30, 5, 30.0f, { 0, 29 }, {} },
	      { 30, 5, 30.0f, { 0, 29 }, {} },
	      { 30, 5, 30.0f, { 0, 29 }, {} },
	  } },
	// swallow: 15 infos, 29 joints
	{ "swallow", "dataDir/tekikeys/swallow.key", "dataDir/tekis/swallow/swallow.anm", "tekis/swallow/motion", 29,
	  {
	      { 60, 5, 30.0f, { 0, 59 }, {} },
	      { 15, 5, 30.0f, { 0, 14 }, {} },
	      { 25, 5, 30.0f, { 0, 24 }, {} },
	      { 25, 5, 30.0f, { 0, 24 }, {} },
	      { 25, 5, 30.0f, { 0, 8, 20, 24 }, { { 1, 0 }, { 2, 1 } } },
	      { 50, 5, 30.0f, { 0, 10, 20, 49 }, { { 1, 2 }, { 2, 3 } } },
	      { 40, 6, 30.0f, { 0, 39 }, {} },
	      { 40, 6, 40.0f, { 0, 5, 35, 39 }, { { 1, 0 }, { 2, 1 } } },
	      { 40, 5, 30.0f, { 0, 10, 38, 39 }, { { 1, 2 }, { 2, 3 } } },
	      { 180, 5, 30.0f, { 0, 70, 130, 179 }, { { 1, 0 }, { 2, 1 } } },
	      { 80, 5, 30.0f, { 0, 30, 79 }, { { 1, 2 } } },
	      { 50, 5, 30.0f, { 0, 49 }, {} },
	      { 50, 5, 30.0f, { 0, 49 }, {} },
	      { 50, 5, 30.0f, { 0, 49 }, {} },
	      { 40, 6, 30.0f, { 0, 10, 30, 39 }, { { 1, 0 }, { 2, 1 } } },
	  } },
	};
	return list;
}

} // namespace nectar
