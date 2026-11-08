#pragma once

// Stand-in models and parameter files for the objects that had animations but no
// model (standin_objects.h): plants, bridges and the push box, the soft gate, two
// bosses, seven enemies, the collision platforms their models name, the enemy
// parameter files (tekipara/*.bin) and the boss parameter files, so a generator
// can place any of them without the game aborting at a missing file.
//
// Synthetic fixtures use authored simple geometry and source-required loader
// names. Parameter files preserve source constructor defaults. Missing positional
// enemy parameter files intentionally retain the per-type constructors.

#include "nectar_assets/mod.h"
#include "nectar_assets/standin_anims.h"
#include "nectar_assets/standin_models.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace nectar {

/// One collision part of a model, as written in its collinfo text. `depth` nests
/// it under the previous part of depth - 1. Type 1 is a sphere (radius, centre),
/// type 2 a platform: `platform` is the .mod with the collision, loaded by path.
struct WorldPart {
	int depth, joint;
	const char* id;
	const char* code;
	int type;
	float radius;
	Vec3 centre;
	const char* platform;
};

struct WorldModelProfile {
	const char* path; ///< relative to assets/, with dataDir/
	std::vector<int> parents;
	std::vector<WorldPart> parts;
};

const std::vector<WorldModelProfile>& worldModelProfiles();

/// The platform .mod files the profiles name (the gate's two are the sandbox gate's).
const std::vector<std::string>& worldPlatformPaths();

struct TekiParaProfile {
	const char* name; ///< tekipara/<name>.bin
	std::vector<std::uint32_t> words; ///< the whole file, big-endian words
};
const std::vector<TekiParaProfile>& tekiParaProfiles();

struct BossParmProfile {
	const char* path;
	/// Tagged lists (Parameters::write): (three-letter id, one 32-bit value).
	std::vector<std::vector<std::pair<const char*, std::uint32_t>>> lists;
};
const std::vector<BossParmProfile>& bossParmProfiles();

/// The model specs of worldModelProfiles(): colour boxes on independently authored joints,
/// collinfo text from the parts.
std::vector<StandInModelSpec> worldModelSpecs();

/// The collinfo text of a part list (ObjCollInfo::loadini grammar).
std::string worldCollInfo(const std::vector<WorldPart>& parts);

/// Platform models, gt_soft.anm, tekipara and boss parameter files.
bool writeStandInWorldFiles(std::vector<StandInAnimFile>& out, std::string& error);

} // namespace nectar
