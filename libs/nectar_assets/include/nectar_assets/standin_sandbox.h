#pragma once

// The scenario sandbox stage's own content besides its map (#68, under #62 and
// #59): a gate in the captain's way, and the default.gen that places it.
//
// The gate is the game's own 'sluice bomb' item (a BuildingItem, the
// reinforced wall that only bombs break), born from an item generator in
// stages/sandbox/default.gen. Its model and collision are ours: a panel and
// two posts made of boxes. It reproduces the mechanism the disc's
// throw_release run shows on the Forest of Hope (standin-k-investigation.md):
// the captain's walk away ends against the wall, the squad catches up, the
// Pikmin in formation touch its 'gate' platform and switch to BreakwallMode
// (piki.cpp), their attacks bounce off (InteractAttack::actItem returns false
// for a bomb gate), and after 3 to 5 failed hits each gives up, Sad, and
// comes back through PIKISTATE_Emotion (aiBreakWall.cpp).
//
// Fidelity (#62): no disc geometry, texture or animation is used. The item
// type, the collinfo grammar, the joint the panel hangs from (1) and the file
// paths the item loads (objects/gate/gt_hard.mod, which names its platform
// .mod files) are the game's. Where the gate stands relative to the captain
// is chosen to give the walk the shape the disc trace shows, and is noted in
// standin_sandbox.cpp.

#include "nectar_assets/mod.h"

#include <cstdint>
#include <string>
#include <vector>

namespace nectar {

struct SandboxFile {
	std::string path; ///< relative to assets/, starting with dataDir/ (as in the boot manifest)
	std::vector<std::uint8_t> bytes;
};

/// Where the sandbox gate stands: the generator position (the captain starts at
/// the origin and the throw_release walk goes to -z) and its turn about y in
/// degrees (GenTypeOne rotation).
struct SandboxGatePlacement {
	float x = 0.0f, z = 0.0f;
	int rotY = 0;
};
SandboxGatePlacement sandboxGatePlacement();

/// The gate model (gt_hard.mod, the shape ItemMgr loads for 'sluice bomb'):
/// joint 0 holds the two posts, joint 1 (its child) the panel, and the
/// collinfo text names the two platform files.
Model buildSandboxGateModel();
/// The platform shape of the panel ('gate' part) or of the posts ('side').
Model buildSandboxGatePlatform(bool panel);

/// Files under dataDir/objects/gate/ that the gate loads.
bool writeSandboxGateFiles(std::vector<SandboxFile>& out, std::string& error);

/// stages/<genDir>default.gen: the captain at the origin facing 0, and the gate
/// generator. `genDir` is the stage's generator folder relative to dataDir,
/// with its trailing slash (e.g. "stages/sandbox/").
bool writeSandboxDefaultGen(const std::string& genDir, SandboxFile& out, std::string& error);

} // namespace nectar
