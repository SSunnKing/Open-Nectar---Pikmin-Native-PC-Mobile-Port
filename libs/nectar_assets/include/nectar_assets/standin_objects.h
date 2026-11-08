#pragma once

// Stand-ins for everything an object, plant, gate, boss or enemy loads besides
// its model: the per-model animation bundles (.anm) and AnimMgr tables (.bin,
// .key) of issue #66's "other AnimMgrs" gap, the three ship parts naka1, naka4
// and naka7 and the pellet tables (parms/pelMgr.bin, objects/pelAnim.bin), and
// the .gen item and creature generators a sandbox stage can place (the Onion,
// a pellet, a sprout, an enemy).
//
// Provenance (#190): every value here is authored for the fixtures or required by game code.
// An info is a static identity animation on the model's joints, named by this file; its frame
// count, anim-key frames, info-key records (which anim key each KEY_* event sits on) and flags
// follow the recipe in standin_object_profiles.cpp, designed from the key each state machine
// reads. No retail frame counts, key frames, curves, names, event records or geometry are used.
// The Onion boot motions carry one effect event, the route-opening cue the game requires
// (ObjectAnimInfoProfile::effectKeys), at an authored frame. The extra models (naka1/4/7,
// chappy) and both pellet tables are authored fixture values too: every pelMgr.bin config,
// the Main Engine's included, uses this file's own numbers or source defaults.

#include "nectar_assets/mod.h"
#include "nectar_assets/standin_anims.h"
#include "nectar_assets/standin_models.h"
#include "nectar_assets/standin_onion_boot.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace nectar {

// ---------------------------------------------------------------- animation profiles

/// One info of an AnimMgr table: an authored schedule.
struct ObjectAnimInfoProfile {
	int frames = 0; ///< frame count N of the animation payload
	int flags  = 2; ///< 'p00' (AnimInfoFlags; bit 1 = the game sets the speed)
	float speed = 30.0f; ///< 'spd'
	std::vector<int> keys; ///< anim-key frames, already clamped below N (the game clamps them on load)
	std::vector<std::pair<int, int>> infoKeys; ///< (anim-key index, key type): 0 loop start, 1 loop end, 2-5 KEY_Action0-3
	/// (frame, value) of action event keys (type 1, KEY_PlayEffect with that value). Event keys are
	/// otherwise not written; the Onion boot motions carry one because it opens the route way point.
	std::vector<std::pair<int, int>> effectKeys;
};

/// One AnimMgr: its table, its bundle and a joint count used only when its model is not written.
struct ObjectAnimProfile {
	const char* id;         ///< short name, also the prefix of the invented animation names
	const char* tablePath;  ///< relative to assets/, with dataDir/ as in the boot manifest
	const char* bundlePath;
	const char* baseDir;    ///< 'a01': prefix of every bundle entry name
	int joints;             ///< joint count of a flat fan, used only when no stand-in model exists; otherwise the model's
	std::vector<ObjectAnimInfoProfile> infos;
};

/// The 45 AnimMgrs of the boot manifest other than pikis/animMgr.bin, plus the hard
/// gate's bundle (the soft gate's sibling, sharing objects/gate/gate.bin).
const std::vector<ObjectAnimProfile>& objectAnimProfiles();

/// The profiles as animation sets ready to write: invented names, static
/// identity payloads, parents from the matching stand-in model when there is one.
const std::vector<StandInAnimSet>& standInObjectAnims();

/// The generator assertions for an object set: unique names, a joint list that
/// matches the model, anim keys below the frame count (the game clamps them
/// silently otherwise), info keys that point into the anim-key list and are
/// ascending, at most 16 of them.
bool checkObjectAnimSet(const StandInAnimSet& set, std::string& error);

/// Table and bundle of one set (`checkObjectAnimSet` first).
bool writeObjectAnimSet(const StandInAnimSet& set, std::vector<StandInAnimFile>& out, std::string& error);

/// writeObjectAnimSet over standInObjectAnims(). Sets that share a table path
/// (the two gates) write the same bytes twice; the caller's map keeps one.
bool writeStandInObjectAnimations(std::vector<StandInAnimFile>& out, std::string& error);

// ---------------------------------------------------------------- extra models

/// Models beyond the 45 of standin_models.h: ufoparts/naka1, naka4 and naka7
/// (parms/pelMgr.bin names them) and the enemy models of the sandbox.
const std::vector<StandInModelSpec>& standInObjectModelSpecs();

/// buildStandInModel over standInObjectModelSpecs().
bool writeStandInObjectModels(std::vector<StandInModelFile>& out, std::string& error);

// ---------------------------------------------------------------- pellet tables

/// The Main Engine ('ust5') config, authored: the fewest and most Pikmin that carry it.
constexpr int kStandInEngineCarryMin = 12;
constexpr int kStandInEngineCarryMax = 24;
/// The ship joint the collected engine is drawn on (PlayerState::renderParts): any joint of the
/// stand-in ship other than the ones UfoShapeObject and UfoItem animate or read by number.
constexpr int kStandInEngineRepairJoint = 1;

/// parms/pelMgr.bin (the pellet configs, PelletMgr::readConfigs) and
/// objects/pelAnim.bin (the shape list, PelletMgr::readAnimInfos).
bool writeStandInPelletTables(std::vector<StandInAnimFile>& out, std::string& error);

// ---------------------------------------------------------------- .gen object generators

/// One generator of a .gen file, as GeneratorMgr::read loads it (generator.cpp:727):
/// name, position, an object part (what it makes), an area part (a point) and a
/// type part (one thing, facing `faceDegrees`). Only what a sandbox needs.
struct ObjectGenSpec {
	enum Kind {
		Item,   ///< 'item': `name` is the object type name ("goal" the Onion, "pikihead", "bomb gen"...), `itemParam` its 'p00' (an Onion's colour: 0 blue, 1 red, 2 yellow)
		Pellet, ///< 'pelt': `name` is the 4-letter pellet id of parms/pelMgr.bin ("pr01")
		Piki,   ///< 'piki': `pikiState` 0 a buried sprout, 1 a free Pikmin, 2 one in the party; `pikiColour` 0-2
		Teki,   ///< 'teki': `tekiType` is the TEKI_* index (3 is the Dwarf Bulborb, chappy)
		Plant,  ///< 'plnt': `plantType` is the PLANT_* index (0 clover ... 11 hae)
		Work,   ///< 'work' (version v0.3): `name` is the object ("bridge test", "move stone"), `shape` its shape ("bridge 4", "bridge 13", "stone 10"); a move stone also takes `pos`-relative destination `stoneTo`
		Boss,   ///< 'boss' (version 2): `bossType` is the GENBOSS_* index (1 snake, 8 mizu)
	};
	Kind kind = Item;
	std::string generatorName = "    "; ///< four characters: an Onion the player does not own yet is only born by a generator named "next" (GenObjectItem::birth)
	std::string memo;                   ///< up to 31 characters
	Vec3 pos;
	int carryOver   = 0;
	std::string name;
	int itemParam   = 0;
	/// Item only, for the gates (BuildingItem): 'p01' (days) and 'p02' (hours), which GenObjectItem::birth turns into the
	/// gate's health, hours + days * 24 (genItem.cpp:241). -1 leaves the parameter out (the game then reads 0).
	int itemDays    = -1;
	int itemHours   = -1;
	int pikiState   = 0;
	int pikiColour  = 0;
	int tekiType    = 3;
	int faceDegrees = 0;
	int plantType   = 0;
	std::string shape;
	Vec3 stoneTo;
	/// Work only: with stonePushers >= 0 the object part carries 'p02' (Pikmin needed to move a move stone) and
	/// 'p03' (its speed); otherwise no parameters, so the game keeps its defaults (10 and 30, workObject.cpp).
	int stonePushers = -1;
	float stoneSpeed = 30.0f;
	int bossType = 1;
};

/// The bytes of one generator, as a Generator::raw for writeGen.
std::vector<std::uint8_t> writeObjectGen(const ObjectGenSpec& spec);

/// A whole .gen file (nectar::writeGen over the generators), the captain at the origin.
std::vector<std::uint8_t> writeObjectGenFile(const std::vector<ObjectGenSpec>& specs);

// ---------------------------------------------------------------- everything

/// Every file this header writes (animations, extra models, pellet tables), each
/// with its path relative to assets/. The generator (#68) puts them in the tree.
bool writeStandInObjectFiles(std::vector<StandInAnimFile>& out, std::string& error);

} // namespace nectar
