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
// Independently authored flat floor, route grid and object placement. The
// tutorial code needs a red Onion, a deliverable Main Engine, a ship and a
// pushable obstruction. Default generator names preserve ownership gating;
// init.gen is one-shot and 0.gen supplies the Day 1 pellet exercise.

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
/// Authored push corridor, long enough to exercise sustained work.
constexpr float kPracticeBoxPush     = 240.0f;
constexpr float kPracticePelletX     = -600.0f;
constexpr float kPracticePelletZ     = 100.0f;

bool addPracticeStage(std::map<std::string, std::vector<std::uint8_t>>& tree, std::string& error);

} // namespace standin
