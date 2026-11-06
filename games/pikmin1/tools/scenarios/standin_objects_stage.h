#pragma once

// The second stand-in stage: stages/sandbox_objects.ini (#59, objects gap). The
// first one, stages/sandbox.ini, is a flat floor with a few walls and no
// generators; every existing scenario runs there and its numbers (the
// throw_release calibration K) depend on it staying that way. This stage is for
// scenarios that opt in with PIKMIN_SCENARIO_STAGE=stages/sandbox_objects.ini.
//
// It has a heightfield with a cliff on one side and a default.gen that places,
// from the nectar_assets stand-ins only: a blue Onion beside the cliff, three
// buried sprouts, a one-pellet red pellet and a sleeping Dwarf Bulborb. Nothing
// here is read from the disc.

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace standin {

/// `stages.ini` `file` value of the stage.
constexpr const char* kObjectsStageIni = "stages/sandbox_objects.ini";

/// Where things are, in map units (the cliff face runs along x = kCliffFootX to
/// kCliffTopX, rising kCliffHeight; the captain starts at the origin).
constexpr float kCliffFootX   = 50.0f;
constexpr float kCliffTopX    = 100.0f;
constexpr float kCliffHeight  = 200.0f;
constexpr float kOnionX       = -120.0f;
constexpr float kOnionZ       = -250.0f;

/// The Onion stage, `stages/sandbox_onions.ini` (onion_day3): the same map and
/// .ini text as sandbox_objects, its own generator folder. default.gen places the
/// Onions of a story course the way the disc's do (stage1 default.gen, read
/// 2026-10-28): a red, a yellow and a blue Onion from generators not named `next`
/// (born only while the player owns that colour) and a yellow Onion from a `next`
/// generator (born only while the player does not), at another spot. Nothing else.
constexpr const char* kOnionsStageIni = "stages/sandbox_onions.ini";

/// The play stage, `stages/sandbox_play.ini` (free_play, issues #127 and #129): the same
/// map and .ini text as sandbox_objects, its own generator folder. default.gen is the
/// object stage's sprouts, pellet and Dwarf Bulborb, and one Onion of each colour from a
/// generator not named `next`, so a Pikmin of any colour that carries the pellet finds
/// an Onion to deliver to (ActTransport aborts, as the original does, when it finds
/// none). Such an Onion is born only while the player owns its colour, and booted (route
/// open) only with the boot bit, so free_play owns all three before the stage loads
/// (PcScenario::ownsAllOnions). Appended last in stages.ini.
constexpr const char* kPlayStageIni = "stages/sandbox_play.ini";
constexpr float kRedOnionX        = -120.0f;
constexpr float kRedOnionZ        = -250.0f;
constexpr float kYellowOnionX     = -280.0f; ///< the owned yellow Onion (not `next`)
constexpr float kYellowOnionZ     = -250.0f;
constexpr float kBlueOnionX       = -200.0f; ///< the owned blue Onion (not `next`)
constexpr float kBlueOnionZ       = -400.0f;
constexpr float kNextYellowX      = -300.0f; ///< the yellow Onion still to be found (`next`)
constexpr float kNextYellowZ      = 0.0f;

/// Adds the stage's map, .ini and generators to `tree` (paths relative to assets/, with dataDir/),
/// and the Onion stage's .ini and generators.
bool addObjectsStage(std::map<std::string, std::vector<std::uint8_t>>& tree, std::string& error);

} // namespace standin
