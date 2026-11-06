// Stand-in asset generator for the disc-free scenario tests (issue #68, under
// #62 and #59). It calls every M2 writer in libs/nectar_assets and writes the
// complete stand-in tree to <out>/assets/dataDir/..., so `nectar --scenario`
// boots the sandbox stage with no disc data:
//
//   nectar_standin_generator <out> [--manifest <boot-manifest.txt>]
//
// <out> is the folder a scenario run uses as PIKMIN_SCENARIO_DATA_DIR (CTest
// passes <build>/scenario-data). The tree omits assets/.pikmin-assets: only the
// launcher reads it (#62).
//
// Pass criteria owned here (#68): the tree is built twice in-process and the two
// hashes must match (run-to-run identity on one machine; no cross-OS identity is
// claimed), every writer error is fatal, and with --manifest every path of the
// disc boot manifest (#64) is either in the tree or on the allowlist below with
// its reason.
//
// Fidelity (#62): nothing here is read from disc data. Numbers that only the
// disc has are recorded where they are used, in the sibling stand-in lists and
// below.

#include "nectar_assets/archive.h"
#include "nectar_assets/bundle.h"
#include "nectar_assets/cinema.h"
#include "nectar_assets/gen.h"
#include "nectar_assets/mod_writer.h"
#include "nectar_assets/parms.h"
#include "nectar_assets/stages_ini.h"
#include "nectar_assets/standin_anims.h"
#include "nectar_assets/standin_audio.h"
#include "nectar_assets/standin_models.h"
#include "nectar_assets/standin_objects.h"
#include "nectar_assets/standin_sandbox.h"
#include "nectar_assets/standin_terrain.h"
#include "nectar_assets/standin_textures.h"
#include "nectar_assets/standin_ui.h"
#include "nectar_mapcore/compile.h"
#include "nectar_mapcore/map.h"
#include "standin_all_objects_stage.h"
#include "standin_map_dressing.h"
#include "standin_objects_stage.h"
#include "standin_showcase_stage.h"
#include "standin_practice_stage.h"
#include "standin_water_stage.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// The game's BARC table for pikiseq.arc (src/jaudio/pikiseq.c, compiled into
// this executable). Its size is 0x20 plus 0x20 per entry; the entry count is
// the header's big-endian u32 at 0x0C.
extern "C" std::uint8_t HEAD_pikiseq[];

namespace {

using Bytes = std::vector<std::uint8_t>;
/// Path relative to assets/ (so it starts with "dataDir/", as in the boot manifest) -> bytes.
using Tree = std::map<std::string, Bytes>;

// ── The sandbox stage ────────────────────────────────────────────────────────
// stages.ini `file` value, the stage .ini, its generator folder (the game cuts
// the path at its first '.', gameCoreSection.cpp initStage) and the map model.
constexpr const char* kSandboxIni    = "stages/sandbox.ini";
constexpr const char* kSandboxGenDir = "stages/sandbox/";
constexpr const char* kSandboxMod    = "courses/sandbox/sandbox.mod";
// In-memory only: what compileMap reads through its GameFileReader.
constexpr const char* kSourceMod  = "courses/sandbox/source.mod";
constexpr const char* kSourceIni  = "stages/sandbox_base.ini";

// compileMap with grid=false always builds meshes on materials 0 (walls), 14
// (floor), 15, 16 and 24 (water layers) and 23 (pond bed), and the game reads
// mMaterialList[poly->mIndex] without a bounds check (shapeBase.cpp), so the
// source .mod has at least 25 materials.
constexpr int kSourceMaterials = 25;

bool fail(std::string& error, const std::string& what)
{
	error = what;
	return false;
}

Bytes textBytes(const std::string& s) { return Bytes(s.begin(), s.end()); }

/// The map-source .mod: tables only. The floor (14) and the walls (0) carry the
/// stand-in grass and sandstone textures (standin_terrain.h); the rest are flat colours.
bool sandboxSourceMod(Bytes& out, std::string& error)
{
	nectar::Model m;
	for (int i = 0; i < kSourceMaterials; i++) {
		nectar::ModMaterial mat;
		mat.flags   = nectar::MOD_MAT_OPAQUE;
		mat.texAttr = -1;
		mat.colour  = { 110, 150, 90, 255 }; // grass green
		if (i == 1) mat.colour = { 90, 130, 70, 255 }; // darker grass
		m.materials.push_back(mat);
	}
	nectar::addStandInTerrainTextures(m);
	if (!nectar::writeModTables(m, out, error)) return fail(error, "sandbox source .mod: " + error);
	return true;
}

/// The stage .ini base that compileMap's buildIni rewrites: it only needs a
/// map_file line. createMapObjects (newPikiGame.cpp) acts on map_file,
/// day_multiply and an optional dayMgr block; with no dayMgr block DayMgr keeps
/// TimeSetting's default white parallel lights. day_multiply 1.0 is the value of
/// the disc's stages/stage1.ini (Forest of Hope, the disc-mode scenario stage),
/// so the world clock runs at the same rate as the disc-mode runs.
std::string sandboxBaseIni()
{
	return "# Sandbox stage for the disc-free scenario tests (#68), written by nectar_standin_generator.\n"
	       "map_file\t\tcourses/sandbox/sandbox.mod\n"
	       "day_multiply\t1.0\n";
}

/// A flat floor (compileMap with grid=false, no walls), the gate and default.gen
/// (standin_sandbox.h).
bool sandboxMap(Tree& tree, std::string& error)
{
	Bytes sourceMod;
	if (!sandboxSourceMod(sourceMod, error)) return false;
	const std::string baseIni = sandboxBaseIni();

	nectar::MapDoc doc;
	doc.name      = "sandbox";
	doc.symmetric = false;
	doc.minX      = -1200;
	doc.maxX      = 1200;
	doc.minZ      = -1200;
	doc.maxZ      = 1200;
	doc.sourceMod = kSourceMod;
	doc.baseStage = kSourceIni;
	doc.terrain.border.source = kSourceMod;

	nectar::MapData data;
	if (!nectar::expandMap(doc, data, error, /*validate=*/false)) return fail(error, "sandbox map: " + error);

	// Serves the in-memory source files and refuses every other path, so an
	// accidental disc read fails loudly.
	const nectar::GameFileReader read = [&](const std::string& path, Bytes& out) {
		if (path == kSourceMod) {
			out = sourceMod;
			return true;
		}
		if (path == kSourceIni) {
			out = textBytes(baseIni);
			return true;
		}
		std::fprintf(stderr, "[standin] compileMap asked for %s, which the generator does not serve\n", path.c_str());
		return false;
	};
	// Draw-only dressing (standin_map_dressing.h): rock faces along the four walls
	// and a soil path from the captain's start through the gate.
	standin::MapDressing dressing;
	standin::DressPath path;
	path.points = { { 60.0f, 260.0f }, { 20.0f, 120.0f }, { -10.0f, 0.0f }, { -25.0f, -120.0f }, { -31.5f, -207.0f }, { -55.0f, -330.0f }, { -20.0f, -470.0f }, { 50.0f, -600.0f } };
	dressing.paths.push_back(path);
	nectar::CompiledMap compiled;
	if (!standin::compileDressedMap(data, read, kSandboxMod, dressing, compiled, error)) return fail(error, "sandbox map: " + error);

	tree[std::string("dataDir/") + kSandboxMod] = compiled.mod;
	tree[std::string("dataDir/") + kSandboxIni] = compiled.ini;
	// The captain at the origin and the gate in his way (standin_sandbox.h); day,
	// init and plants files are omitted because scenarios spawn their own Pikmin
	// (#61).
	std::vector<nectar::SandboxFile> files;
	nectar::SandboxFile gen;
	if (!nectar::writeSandboxGateFiles(files, error) || !nectar::writeSandboxDefaultGen(kSandboxGenDir, gen, error)) return fail(error, "sandbox: " + error);
	files.push_back(std::move(gen));
	for (nectar::SandboxFile& f : files) tree[f.path] = std::move(f.bytes);
	return true;
}

// ── stages/stages.ini ────────────────────────────────────────────────────────
// Five story entries with ids 0-4 (GameSetupSection walks the first STAGE_COUNT
// list nodes without a null check, #64), then the sandbox as the sixth. The
// sandbox has no `id` line, so it is STAGE_TESTMAP (5): not the tutorial (the
// clock runs), no zone banner, music falls back to stage 0. `hidden` keeps it out
// of the in-game map select. Only `file` must match. Of the story stages only
// the first, stages/practice.ini (id 0, index 0: the tutorial), has stand-in
// files (standin_practice_stage.h); the other four are never opened.
bool stagesIni(Tree& tree, std::string& error)
{
	std::vector<nectar::StagesEntry> entries;
	const char* const story[] = { "stages/practice.ini", "stages/stage1.ini", "stages/stage2.ini", "stages/stage3.ini", "stages/last.ini" };
	for (int id = 0; id < 5; id++) {
		nectar::StagesEntry e;
		e.visible = true;
		e.name    = "story" + std::to_string(id);
		e.id      = id;
		e.file    = story[id];
		entries.push_back(e);
	}
	nectar::StagesEntry sandbox;
	sandbox.visible = false; // `other`: readMapList treats anything but `visible` as hidden
	sandbox.name    = "sandbox";
	sandbox.file    = kSandboxIni;
	entries.push_back(sandbox);
	nectar::StagesEntry objects; // the opt-in object sandbox (standin_objects_stage.h), also hidden, also STAGE_TESTMAP
	objects.visible = false;
	objects.name    = "sandbox_objects";
	objects.file    = standin::kObjectsStageIni;
	entries.push_back(objects);
	nectar::StagesEntry all; // every-object stage (standin_all_objects_stage.h)
	all.visible = false;
	all.name    = "sandbox_all_objects";
	all.file    = standin::kAllObjectsStageIni;
	entries.push_back(all);
	nectar::StagesEntry onions; // the Onion stage of onion_day3 (standin_objects_stage.h), last so no other index moves
	onions.visible = false;
	onions.name    = "sandbox_onions";
	onions.file    = standin::kOnionsStageIni;
	entries.push_back(onions);
	nectar::StagesEntry water;
	water.visible = false;
	water.name    = "sandbox_water";
	water.file    = standin::kWaterStageIni;
	entries.push_back(water);
	nectar::StagesEntry showcase;
	showcase.visible = false;
	showcase.name    = "sandbox_showcase";
	showcase.file    = standin::kShowcaseStageIni;
	entries.push_back(showcase);
	nectar::StagesEntry play; // the free_play stage (standin_objects_stage.h), last so no other index moves
	play.visible = false;
	play.name    = "sandbox_play";
	play.file    = standin::kPlayStageIni;
	entries.push_back(play);

	std::string text;
	if (!nectar::writeStagesIni(entries, text, error)) return fail(error, "stages.ini: " + error);
	tree["dataDir/stages/stages.ini"] = textBytes(text);
	return true;
}

// ── Parameter files ─────────────────────────────────────────────────────────
// Every value not listed keeps its code default. gamePrms.bin must exist (it is
// the scenario runner's data gate) and stays empty. naviMgr.bin and pikiMgr.bin
// hold two lists each (CreatureProp, then NaviProp or PikiProp).
//
// throw_release touches how the captain and his Pikmin move, collide, aim and
// throw, and how far a grab reaches, so those IDs carry the disc's values. With
// the code defaults the captain grabs straight from 80 units (K = 1, the
// ThrowWait key frame) where the disc walks the Pikmin to the hand first (K = 13
// at 30 Hz). Only the disc has these numbers: they were read once from the
// VERSION_GPIE01_01 install's parms/naviMgr.bin, pikiMgr.bin, aiconst.bin and
// cunit.bin (read-only, decoded with the Parameters::read layout, 2026-10-27),
// and each differs from the code default in NaviMgr.h, PikiMgr.h,
// CreatureProp.h, AIConstant.h or CPlate.h (noted per line).
// The other differing captain and game IDs (whistle, punch) are left at the
// code defaults because no v1 scenario touches them. pikiMgr.bin is the
// exception: since 2026-11-04 it carries every ID the disc file sets, with the
// disc's value (standin-piki-parms.md). Every ID of the three disc files was
// diffed against these and the code defaults on 2026-10-28 (verification-log.md,
// item 4); the ones left at code defaults are:
//  - gamePrms.bin (stays empty): p00 daySpeed (disc 27, code 20) is overwritten
//    on PC from the day-length setting (27 by default) in GameCoreSection's
//    constructor, so the file value never reaches the clock; t00 startHour,
//    t01 endHour, p06 nightTimer and x99 equal the code defaults; p01-p04, s01,
//    s03 only blend the lighting; p05 nightWarning (disc 18, code 16) is the
//    Hurry Up message, which day30_clock passes either way (it starts Day 30 at
//    18:48, after both). x98 is the unused title string.
//  - naviMgr.bin: p01-p03 whistle, p29/p30 escape animation frames, p31/p33
//    pressed and push timing, p42 pluck loop count, p49 impatience time, p51
//    punch damage, p57-p59 Kinoko flick and bury escape, p61 pluck zoom. No v1
//    scenario whistles, runs into the escape speed, is pressed, pushes, plucks,
//    punches or is buried.
//
// CreatureProp: the captain's effective code defaults are 1.0, 0.5, 0.5, 0.2,
// 0.3 (the NaviProp constructor, navi.cpp, overrides s00, s02 and s03 of the
// CreatureProp 0.5, 0.5, 0.25, 0.1, 0.3) and the Pikmin's 0.1, 0.5, 0.25, 0.1,
// 0.3 (PikiProp overrides s00). The disc differs in the captain's s03 and the
// Pikmin's s02 and s03; the lists below carry all five disc values.
//
// With these values the walk to the hand and the grab run identically on both
// trees: the same target speeds (240 beyond 2 x grab range, then 120), the
// hand-over at 15 units, then ThrowWait's KEY_Action0 one frame later. K is set
// by how far away the Pikmin is when the scenario presses A, and that comes
// from the stage and the sim RNG, not from these files. These values stay the
// disc's; the stand-in K comes from the sandbox gate the walk runs into
// (standin_sandbox.h). Details are in standin-k-investigation.md.
bool parmFiles(Tree& tree, std::string& error)
{
	using nectar::parmFloat;
	using nectar::parmInt;
	struct File {
		const char* path;
		std::vector<std::vector<nectar::ParmEntry>> lists;
	};
	const File files[] = {
		{ "dataDir/parms/gamePrms.bin", { {} } },
		// AIConstant (aiConstants.cpp). Every ID that differs from the code default carries the
		// disc value (read-only, 2026-10-28); the full table is in standin-aiconst.md. The other
		// 17 IDs equal the code default.
		{ "dataDir/parms/aiconst.bin",
		  { {
		      parmFloat("p00", 560.0f), // mGravity (code 550): shapes the throw's flight
		      parmInt("p01", 0),        // _34, Pikmin auto-attack (code 1)
		      parmInt("p08", 0),        // _A4, return after attack (code 1)
		      parmInt("p10", 0),        // _C4, attack once = 1, until death = 0 (code 1)
		      parmInt("p13", 0),        // mDoPluckWithCursor (code 1)
		      parmInt("p19", 5),        // _154, UFO level 2 part count (code 6)
		  } } },
		// CPlate (cPlate.cpp): where the squad stands behind the captain.
		// DemoParms (gameDemo.cpp): the trigger radii and timers of the tutorial's
		// demo flags. All five differ from the code default (150, 100, 6, 30, 150)
		// except p03, so the file carries the disc's (parms/demo.bin, read-only,
		// 2026-10-28). Only the tutorial scenarios reach them: the red Onion, seed and
		// engine checks of Navi::demoCheck run on stage Practice (or in the tutorial)
		// only, and p03 (the DEMOFLAG_Unk9 timer, onion_unlock) is the code default.
		{ "dataDir/parms/demo.bin",
		  { {
		      parmFloat("p00", 60.0f), // mOnionBootTriggerRadius (code 150): DiscoverRedOnyon
		      parmFloat("p01", 25.0f), // mSeedDemoTriggerRadius (code 100): ApproachSeed
		      parmFloat("p02", 40.0f), // mSeedDemoWaitTime (code 6): the NoPikminTimeout timer
		      parmFloat("p03", 30.0f), // _30, the DEMOFLAG_Unk9 timer (code 30)
		      parmFloat("p04", 40.0f), // mDemoTriggerRadius (code 150): ApproachEngine, part discovery
		  } } },
		{ "dataDir/parms/cunit.bin",
		  { {
		      parmFloat("p00", 17.5f), // mStartOffset (code 30)
		      parmFloat("p01", 90.0f), // mLengthLimit (code 150)
		      parmFloat("p02", 7.0f),  // mMaxPosSize (code 12)
		  } } },
		{ "dataDir/parms/naviMgr.bin",
		  {
		      // CreatureProp, disc values (effective code defaults 1.0, 0.5, 0.5, 0.2, 0.3: only s03 differs).
		      { parmFloat("s00", 1.0f), parmFloat("s01", 0.5f), parmFloat("s02", 0.5f), parmFloat("s03", 0.1f), parmFloat("s04", 0.3f) },
		      {
		          parmFloat("p00", 25.0f),   // mActionRadius (code 120)
		          parmFloat("p04", 160.0f),  // mMoveSpeed (code 170)
		          parmFloat("p56", 160.0f),  // mRunSpeed (code 210)
		          parmFloat("p38", 1.2f),    // mDisplayScale (code 1)
		          parmFloat("p10", 140.0f),  // mThrowMaxDistance (code 600)
		          parmFloat("p11", 140.0f),  // mThrowMinDistance (code 150)
		          parmFloat("p24", 75.0f),   // mThrowMaxHeight (code 100)
		          parmFloat("p25", 75.0f),   // mThrowMinHeight (code 80)
		          parmFloat("p54", 115.0f),  // mYellowThrowHeight (code 200)
		          parmFloat("p37", 15.0f),   // mPluckGrabRange (code 80): beyond it the Pikmin walks to the hand first
		          parmFloat("p39", 15.0f),   // mPikiWaitRange (code 30)
		          parmFloat("p40", 40.0f),   // mPikiFormationChangeRange (code 60)
		          parmFloat("p21", 8.0f),    // mBodyCollisionRadius (code 15)
		          parmFloat("p41", 8.5f),    // mGroundCollisionRadius (code 16)
		          parmFloat("p22", 1.0f),    // mInverseMass (code 4)
		          parmFloat("p23", 0.03f),   // aiming rotation speed (code 0.04)
		          parmFloat("p15", 8.0f),    // mWalkStartSpeed (code 35)
		          parmFloat("p16", 20.0f),   // mRunStartSpeed (code 70)
		          parmFloat("p17", 95.0f),   // mEscapeStartSpeed (code 160)
		          parmFloat("p18", 60.0f),   // mWalkAnimMinFrames (code 30)
		          parmFloat("p19", 90.0f),   // mWalkAnimMaxFrames (code 60)
		          parmFloat("p27", 40.0f),   // mRunAnimMinFrames (code 30)
		          parmFloat("p44", 0.65f),   // mCursorMoveStickThreshold (code 0.75)
		          parmFloat("p45", 0.0f),    // mCursorMinRadius (code 50)
		          parmFloat("p46", 100.0f),  // mCursorMaxRadius (code 300)
		          parmFloat("p47", 300.0f),  // mCursorMoveSpeed (code 200)
		          parmFloat("p48", 0.85f),   // mCursorMoveDelayTime (code 2)
		      },
		  } },
		{ "dataDir/parms/pikiMgr.bin",
		  {
		      // CreatureProp, disc values (effective code defaults 0.1, 0.5, 0.25, 0.1, 0.3: s02 and s03 differ).
		      { parmFloat("s00", 0.1f), parmFloat("s01", 0.5f), parmFloat("s02", 0.8f), parmFloat("s03", 0.14f), parmFloat("s04", 0.3f) },
		      // PikiProp::Parms: all 77 IDs the disc file sets, in the disc file's order, with the disc
		      // values (parms/pikiMgr.bin, read-only, 2026-11-04). The stand-in file is byte-identical to
		      // the disc's. The full table, and what each differing ID changes, is in standin-piki-parms.md.
		      {
		          parmFloat("p00", 10.0f),    // mMinMoveSpeed (code 84.0)
		          parmFloat("p01", 120.0f),   // mMaxLeafMoveSpeed (code 162.5)
		          parmFloat("p54", 170.0f),   // mMaxFlowerMoveSpeed (code 220.0)
		          parmFloat("p65", 140.0f),   // mMaxBudMoveSpeed (code 160.0)
		          parmFloat("p38", 1.0f),     // mPikiDisplayScale, equals the code default
		          parmFloat("p03", 30.0f),    // mPikiMaxHealth (code 100.0)
		          parmFloat("p10", 10.0f),    // mSkinnyPikiHealth (code 150.0)
		          parmFloat("p11", 10.0f),    // mFatPikiHealth (code 200.0)
		          parmFloat("p04", 10.0f),    // mBlueAttackPower, equals the code default
		          parmFloat("p12", 15.0f),    // mRedAttackPower, equals the code default
		          parmFloat("p13", 10.0f),    // mYellowAttackPower, equals the code default
		          parmInt("p05", 1),          // mBudEnergyRequirement, equals the code default
		          parmInt("p06", 1),          // mFlowerEnergyRequirement (code 2)
		          parmFloat("p07", 0.8f),     // mPluckStrength0, equals the code default
		          parmFloat("p08", 1.5f),     // mPluckStrength1, equals the code default
		          parmFloat("p09", 1.2f),     // mPluckStrength2, equals the code default
		          parmFloat("p14", 2.5f),     // mPressDeathFactor (code 5.0)
		          parmFloat("p15", 1.0f),     // mNormalPikiPressFactor (code 3.0)
		          parmFloat("p16", 0.9f),     // mWildPikiPressFactor (code 1.0)
		          parmFloat("p37", 35.0f),    // mFatPikiPressFactor (code 40.0)
		          parmFloat("p17", 5.0f),     // mStepSpeedThreshold, equals the code default
		          parmFloat("p18", 8.0f),     // mWalkSpeedThreshold (code 35.0)
		          parmFloat("p19", 20.0f),    // mRunSpeedThreshold (code 70.0)
		          parmFloat("p20", 95.0f),    // mEscapeSpeedThreshold (code 160.0)
		          parmFloat("p21", 30.0f),    // mMinWalkAnimSpeed, equals the code default
		          parmFloat("p22", 50.0f),    // mMaxWalkAnimSpeed (code 60.0)
		          parmFloat("p23", 40.0f),    // mMinRunAnimSpeed (code 30.0)
		          parmFloat("p24", 60.0f),    // mMaxRunAnimSpeed, equals the code default
		          parmFloat("p25", 60.0f),    // mEscapeAnimSpeed (code 30.0)
		          parmFloat("p26", 100.0f),   // mUnusedAnimSpeed (code 60.0)
		          parmFloat("p27", 3.0f),     // mMinPikiSize (code 6.0)
		          parmFloat("p34", 5.0f),     // mMaxPikiSize (code 10.0)
		          parmFloat("p28", 100.0f),   // mInvMass, equals the code default
		          parmFloat("p39", 8.5f),     // mCollisionRadius (code 8.0)
		          parmFloat("p29", 600.0f),   // mFormationSlipRange, equals the code default
		          parmFloat("p30", 1000.0f),  // mFormationBreakRange, equals the code default
		          parmFloat("p31", 10.0f),    // mMaxLostChildTime, equals the code default
		          parmFloat("p32", 50.0f),    // mUnusedWhiteFreeDistance (code 200.0)
		          parmFloat("p33", 250.0f),   // mUnusedGrayFreeDistance (code 800.0)
		          parmFloat("p35", 2.0f),     // mUnusedDynWallPushTime (code 0.6)
		          parmFloat("p36", 2.0f),     // mUnusedStaticWallPushTime (code 0.6)
		          parmFloat("p40", 120.0f),   // mDefaultFreeBoidTime (code 15.0)
		          parmFloat("p41", 70.0f),    // mIdleAttackSearchRange (code 100.0)
		          parmFloat("p42", 55.0f),    // mIdleWorkSearchRange (code 100.0)
		          parmFloat("p43", 3.0f),     // mMaxFlickKnockDownTime (code 0.7)
		          parmFloat("p74", 1.0f),     // mMinFlickKnockDownTime (code 0.7)
		          parmFloat("p44", 2.0f),     // mUnusedLeafBudTime (code 10.0)
		          parmFloat("p45", 2.0f),     // mUnusedBudFlowerTime (code 10.0)
		          parmFloat("p46", 10.0f),    // mUnusedFlowerSeedTime, equals the code default
		          parmFloat("p47", 10.0f),    // mUnusedSeedLeafTime, equals the code default
		          // Flower Pikmin fall at this fraction of gravity after the peak of a throw. With the
		          // code default the descent takes 1.75x (30 Hz) to 2.2x (60 Hz) as long as on the disc.
		          parmFloat("p48", 0.5f),     // mFlowerGravityScale (code 0.01)
		          parmFloat("p49", 0.1f),     // mSeedUpTime (code 1.0)
		          parmFloat("p50", 5.0f),     // mPluckWaitTime (code 1.0)
		          parmFloat("p51", 120.0f),   // mGrowUpTime (code 4.0)
		          parmFloat("p52", 180.0f),   // mWiltTime (code 4.0)
		          parmFloat("p53", 60.0f),    // mRebirthSeedTime (code 1.0)
		          parmFloat("p55", 2.0f),     // mPanicTime (code 3.0)
		          parmFloat("p56", 0.8f),     // mPikiLeaveSeedChance, equals the code default
		          parmFloat("p57", 2.0f),     // mBombSetFuseTime, equals the code default
		          parmFloat("p58", 50.0f),    // mBombExplodeRadius, equals the code default
		          parmFloat("p59", 50.0f),    // mBombDamageBomb (code 200.0)
		          parmFloat("p75", 20.0f),    // mBombDamageNavi (code 10.0)
		          parmFloat("p76", 120.0f),   // mBombDamageEnemy, equals the code default
		          parmFloat("p77", 765.0f),   // mBombDamagePiki, equals the code default
		          parmFloat("p60", 300.0f),   // mSunsetSafetyRange (code 200.0)
		          parmFloat("p61", 250.0f),   // mPostWorkJoinPartyRange, equals the code default
		          parmInt("p62", 10),         // mMinStartDrownFrames (code 30)
		          parmInt("p63", 10),         // mRandStartDrownFrames, equals the code default
		          parmFloat("p64", 120.0f),   // mUnusedPushTime (code 5.0)
		          parmFloat("p66", 170.0f),   // mBombTargetSearchRange (code 300.0)
		          parmFloat("p67", 100.0f),   // mBombThrowMinDistance (code 110.0)
		          parmFloat("p68", 130.0f),   // mBombThrowMaxDistance (code 160.0)
		          parmFloat("p69", 1.5f),     // mBombThrowTimeoutTime, equals the code default
		          parmFloat("p70", 150.0f),   // mBombThrowSpeed, equals the code default
		          parmFloat("p71", 150.0f),   // mBombWarnPikiRange (code 300.0)
		          parmFloat("p72", 0.65f),    // mBombAimTime, equals the code default
		          parmFloat("p73", 50.0f),    // mBombSetDistance, equals the code default
		      },
		  } },
	};
	for (const File& f : files) {
		nectar::ParmFile pf;
		pf.lists = f.lists;
		Bytes bytes;
		if (!nectar::writeParmFile(pf, bytes, error)) return fail(error, std::string(f.path) + ": " + error);
		tree[f.path] = bytes;
	}
	return true;
}

// ── Archive pairs ───────────────────────────────────────────────────────────
// The 18 pairs the USA build parses (arambundleList in gameSetup.cpp plus
// blo_eng from preloadLanguage). On PC files are read loose and the entries are
// never consumed, so an empty index and a 0-byte blob are enough; a missing .arc
// aborts with "Missing archive".
bool archives(Tree& tree, std::string& error)
{
	const char* const names[] = { "blo_eng", "tekis",   "bosses", "tekipara", "tekikey", "plants",   "ufopartsbin", "bridges", "gates",
		                          "bomb",    "rope",    "water",  "fl_water", "pelletsbin", "pikihead", "effshapes",   "weeds",   "goal" };
	for (const char* n : names) {
		nectar::ArchivePair pair;
		if (!nectar::writeArchivePair({}, pair, error)) return fail(error, std::string(n) + ": " + error);
		tree[std::string("dataDir/archives/") + n + ".dir"] = pair.dir;
		tree[std::string("dataDir/archives/") + n + ".arc"] = pair.arc;
	}
	return true;
}

// ── Models, animations, textures, screens and particles (#65 #66 #67 #72) ──
bool siblingStandIns(Tree& tree, std::string& error)
{
	std::vector<nectar::StandInModelFile> models;
	if (!nectar::writeStandInModels(models, error)) return fail(error, "stand-in models: " + error);
	for (auto& f : models) tree[f.path] = std::move(f.bytes);

	std::vector<nectar::StandInAnimFile> anims;
	if (!nectar::writeStandInAnimations(anims, error)) return fail(error, "stand-in animations: " + error);
	for (auto& f : anims) tree[f.path] = std::move(f.bytes);

	// Object, ship-part, plant, gate, boss and enemy animations, the extra models
	// and the pellet tables (standin_objects.h).
	std::vector<nectar::StandInAnimFile> objects;
	if (!nectar::writeStandInObjectFiles(objects, error)) return fail(error, "stand-in objects: " + error);
	for (auto& f : objects) tree[f.path] = std::move(f.bytes);

	// Optional textures too: the game tolerates their absence, but the disc boot
	// opens them, so writing them keeps the open log close to the disc's.
	std::vector<nectar::StandInFile> textures;
	if (!nectar::standInTextureFiles(textures, error)) return fail(error, "stand-in textures: " + error);
	for (auto& f : textures) tree["dataDir/" + f.path] = std::move(f.bytes);

	for (const nectar::StandinLayout& l : nectar::standinBloLayouts()) {
		Bytes bytes;
		if (!nectar::writeBlo(l.panes, bytes, error)) return fail(error, l.file + ": " + error);
		tree["dataDir/screen/eng_blo/" + l.file] = std::move(bytes);
	}

	// effects/pcr/effects.bun: one inert type-0 entry per particle name, under its
	// full path, which is what particleLoader::load looks up.
	std::vector<nectar::BundleEntry> pcr;
	const Bytes inert = nectar::inertPcrPayload();
	for (const std::string& name : nectar::standinPcrNames()) pcr.push_back({ nectar::BUNDLE_BINARY, name, inert });
	// The 2D effects of the file-select and results screens (EffectMgr2D).
	for (const std::string& name : nectar::standinPcr2dNames()) pcr.push_back({ nectar::BUNDLE_BINARY, name, inert });
	Bytes bun;
	if (!nectar::writeBundle(pcr, bun, error)) return fail(error, "effects/pcr/effects.bun: " + error);
	tree["dataDir/effects/pcr/effects.bun"] = std::move(bun);
	return true;
}

// ── Cinemas (standin-cinema.md) ────────────────────────────────────────────
bool standInCinemas(Tree& tree, std::string& error)
{
	std::vector<nectar::StandInCinemaFile> files;
	if (!nectar::writeStandInCinemas(files, error)) return fail(error, "stand-in cinemas: " + error);
	for (const auto& f : files) tree[f.path] = textBytes(f.text);
	tree["dataDir/cinemas/opening.ini"] = textBytes("# Stand-in title opening (titles.cpp reads dayMgr optionally)\n");
	return true;
}

// ── SndData for the native JAudio engine (standin-audio.md) ─────────────────
// Only the PIKMIN_NATIVE_JAUDIO=ON build reads it; the legacy facade (OFF) loads
// pikiseq.arc only after Banks/pikibank.bx, which is not written.
bool audioStandIns(Tree& tree, std::string& error)
{
	const std::uint8_t* h     = HEAD_pikiseq;
	const std::size_t entries = std::size_t(h[12]) << 24 | std::size_t(h[13]) << 16 | std::size_t(h[14]) << 8 | h[15];
	std::vector<nectar::StandInFile> files;
	if (!nectar::standInSndDataFiles(h, 0x20 + 0x20 * entries, files, error)) return fail(error, "stand-in SndData: " + error);
	for (auto& f : files) tree["dataDir/" + f.path] = std::move(f.bytes);
	return true;
}

bool buildTree(Tree& tree, std::string& error)
{
	tree.clear();
	return parmFiles(tree, error) && archives(tree, error) && stagesIni(tree, error) && sandboxMap(tree, error) && siblingStandIns(tree, error)
	    && standInCinemas(tree, error) && audioStandIns(tree, error) && standin::addObjectsStage(tree, error) && standin::addAllObjectsStage(tree, error)
	    && standin::addWaterStage(tree, error) && standin::addShowcaseStage(tree, error)
	    && standin::addPracticeStage(tree, error);
}

/// FNV-1a 64 over every path and its bytes, in path order.
std::uint64_t hashTree(const Tree& tree)
{
	std::uint64_t h = 1469598103934665603ull;
	auto mix        = [&](const std::uint8_t* p, std::size_t n) {
        for (std::size_t i = 0; i < n; i++) {
            h ^= p[i];
            h *= 1099511628211ull;
        }
	};
	for (const auto& [path, bytes] : tree) {
		mix(reinterpret_cast<const std::uint8_t*>(path.data()), path.size() + 1);
		const std::uint64_t n = bytes.size();
		mix(reinterpret_cast<const std::uint8_t*>(&n), sizeof(n));
		mix(bytes.data(), bytes.size());
	}
	return h;
}

// ── Manifest check ───────────────────────────────────────────────────────────
// Paths the disc boot manifest (#64, Forest of Hope on disc) lists that the
// stand-in tree leaves out on purpose. A prefix covers every path under it.
struct Allowed {
	const char* prefix;
	const char* why;
	const char* suffix = nullptr; ///< when set, only paths that also end with it
};
const Allowed kManifestAllowlist[] = {
	// What the Forest of Hope loads and the sandbox (no generators) does not.
	{ "dataDir/courses/stage1/", "the disc-mode stage's map; stand-ins boot the sandbox map" },
	{ "dataDir/stages/stage1", "the disc-mode stage's .ini and generators; stand-ins boot stages/sandbox.ini" },
	// Every model, platform, animation table and parameter file of the objects,
	// plants, gates, bosses and enemies in the manifest is written by
	// standin_objects.cpp and standin_world.cpp (and the sandbox gate), so none
	// is allowlisted: objects/{tekis,bosses,plants,bridge,gate,hinderrock}/ and
	// tekikeys/ and tekipara/ are all in the tree.
	// Silent when missing: CoreNode::load and the null-checked opens keep the
	// code defaults (see standin-open-allowlist.txt for each).
	{ "dataDir/parms/sound.bin", "sound constants, skipped when missing" },
	{ "dataDir/parms/key.bin", "key config, skipped when missing; the throw key stays A" },
	{ "dataDir/camera/camepara.bin", "camera parameters, skipped when missing" },
	{ "dataDir/ghost/record.gst", "ghost replay, null-checked" },
	// The disc's naka1 table names three animations its bundle lacks, so the
	// game opens loose .dck files that do not exist there either (FAILED in the
	// manifest). The stand-in bundle has all five entries, so nothing is opened.
	{ "dataDir/objects/ufoparts/mnaka1/", "loose animations the disc's naka1 bundle lacks (they fail on the disc too)" },
};

int checkManifest(const Tree& tree, const std::string& manifestPath)
{
	std::ifstream in(manifestPath);
	if (!in) {
		std::fprintf(stderr, "[standin] cannot read %s\n", manifestPath.c_str());
		return 1;
	}
	int missing = 0, allowed = 0, present = 0;
	std::set<std::string> listed;
	std::string line;
	while (std::getline(in, line)) {
		if (line.empty() || line[0] == '#') continue;
		const std::string path = line.substr(0, line.find('\t'));
		listed.insert(path);
		if (tree.count(path)) {
			present++;
			continue;
		}
		const Allowed* hit = nullptr;
		for (const Allowed& a : kManifestAllowlist) {
			if (path.rfind(a.prefix, 0) != 0) continue;
			if (a.suffix && (path.size() < std::strlen(a.suffix) || path.compare(path.size() - std::strlen(a.suffix), std::string::npos, a.suffix) != 0)) continue;
			hit = &a;
		}
		if (hit) {
			allowed++;
			continue;
		}
		std::fprintf(stderr, "[standin] manifest path not in the tree and not allowlisted: %s\n", path.c_str());
		missing++;
	}
	int extra = 0;
	for (const auto& kv : tree) extra += listed.count(kv.first) ? 0 : 1;
	std::printf("[standin] manifest: %d in the tree, %d allowlisted, %d missing; %d tree files the disc boot does not open (sandbox stage, stand-in parameter files)\n", present, allowed, missing, extra);
	return missing == 0 ? 0 : 1;
}

bool writeTree(const Tree& tree, const fs::path& out, std::string& error)
{
	std::error_code ec;
	// Only the generated dataDir is replaced, so a stale file from an older
	// generator never survives into a run.
	fs::remove_all(out / "assets" / "dataDir", ec);
	for (const auto& [path, bytes] : tree) {
		const fs::path file = out / "assets" / path;
		fs::create_directories(file.parent_path(), ec);
		std::ofstream f(file, std::ios::binary | std::ios::trunc);
		if (!f) return fail(error, "cannot write " + file.string());
		f.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
		if (!f) return fail(error, "cannot write " + file.string());
	}
	return true;
}

} // namespace

int main(int argc, char** argv)
{
	std::string out, manifest;
	for (int i = 1; i < argc; i++) {
		const std::string a = argv[i];
		if (a == "--manifest" && i + 1 < argc) {
			manifest = argv[++i];
		} else if (out.empty() && !a.empty() && a[0] != '-') {
			out = a;
		} else {
			out.clear();
			break;
		}
	}
	if (out.empty()) {
		std::fprintf(stderr, "usage: nectar_standin_generator <out> [--manifest <boot-manifest.txt>]\n");
		return 2;
	}

	std::string error;
	Tree first, second;
	if (!buildTree(first, error) || !buildTree(second, error)) {
		std::fprintf(stderr, "[standin] %s\n", error.c_str());
		return 1;
	}
	const std::uint64_t h1 = hashTree(first), h2 = hashTree(second);
	if (h1 != h2 || first != second) {
		std::fprintf(stderr, "[standin] two builds of the tree differ (%016llx vs %016llx)\n", (unsigned long long)h1, (unsigned long long)h2);
		return 1;
	}
	std::size_t bytes = 0;
	for (const auto& kv : first) bytes += kv.second.size();
	std::printf("[standin] %zu files, %zu bytes, hash %016llx (two builds identical)\n", first.size(), bytes, (unsigned long long)h1);

	if (!manifest.empty() && checkManifest(first, manifest) != 0) return 1;

	if (!writeTree(first, out, error)) {
		std::fprintf(stderr, "[standin] %s\n", error.c_str());
		return 1;
	}
	std::printf("[standin] wrote %s/assets/dataDir\n", out.c_str());
	return 0;
}
