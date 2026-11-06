#pragma once

// The tutorial stand-in stage: stages/practice.ini, the Impact Site's own file
// name. The stand-in stages.ini already lists it first with `id 0`
// (standin_generator.cpp, stagesIni), so the game takes it as STAGE_Practice at
// stage index 0: PlayerState::isTutorial() is true on a new game's Day 1
// (playerState.cpp, isTutorial: stage id Practice, not challenge mode,
// mIsTutorialMode, which PlayerState::initCourse keeps only at stage index 0).
// That is what the tutorial-only milestones need (coverage/milestones.md,
// section 0, gap G1). The three sandbox stages are not touched.
//
// A flat floor with a route grid. What is placed, and from which file, follows
// the disc's stages/practice/ (read once, read-only, 2026-10-28; names and
// numbers only, nothing copied):
//   default.gen  the red Onion (item "goal", colour 1, facing 100), the blue and
//                yellow Onions (colours 0 and 2, facing 180 and 320; their
//                generators are not named "next", so GenObjectItem::birth makes
//                them only once the player owns that colour: not on a new game),
//                and the ship (item "ufo", facing 256);
//   init.gen     the Main Engine (pellet "ust5") and the push box ("move stone",
//                shape "stone 10", 10 Pikmin, speed 50, pushed 270 units);
//   0.gen        the Day 1 file: one red 1-pellet ("pr01").
// The positions are this file's own (the disc's sit on its own terrain and
// spread over 2,000 units): the red Onion, the pellet and the box on one side,
// the engine and the ship on the other, each far enough from the others that no
// trigger radius (parms/demo.bin) reaches two of them, and all of them outside
// the 3 by 3 route grid (x and z -400, 0, 400; see the .cpp), so no route line
// runs through an Onion, the ship or the box. The captain starts at the origin
// (writeGen).

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace standin {

constexpr const char* kPracticeStageIni = "stages/practice.ini";

/// Positions in map units (x, z); the captain starts at the origin.
constexpr float kPracticeRedOnionX   = -600.0f;
constexpr float kPracticeRedOnionZ   = -300.0f;
constexpr float kPracticeUfoX        = 650.0f;
constexpr float kPracticeUfoZ        = 0.0f;
constexpr float kPracticeEngineX     = 600.0f;
constexpr float kPracticeEngineZ     = -600.0f;
constexpr float kPracticeBoxX        = -600.0f;
constexpr float kPracticeBoxZ        = 600.0f;
/// The disc's box, unrotated, moves 270 units towards -z (init.gen: z 987.2 to 717.2); this one does the same.
constexpr float kPracticeBoxPush     = 270.0f;
constexpr float kPracticePelletX     = -600.0f;
constexpr float kPracticePelletZ     = 100.0f;

bool addPracticeStage(std::map<std::string, std::vector<std::uint8_t>>& tree, std::string& error);

} // namespace standin
