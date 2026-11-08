#pragma once

// Flat art showcase stage: stages/sandbox_showcase.ini. No cliff; props sit near the
// captain spawn for the standin_showcase scenario camera and 640x480 captures.

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace standin {

constexpr const char* kShowcaseStageIni = "stages/sandbox_showcase.ini";

// The captain spawns 50 units in front (+z) of the ship generator, at (12, 0, 84).
// The scenario camera looks along -z (-x on the left of the frame) and moves the
// ship to the back right (kShowcaseShipX/Z in the scenario). Onion back left,
// pellet between the onion and the captain, Pikmin in a row to his right.
constexpr float kShowcaseRedOnionX = -8.0f;
constexpr float kShowcaseRedOnionZ = 30.0f;
constexpr float kShowcasePelletX   = 2.0f;
constexpr float kShowcasePelletZ   = 70.0f;
constexpr float kShowcaseUfoX      = 12.0f;
constexpr float kShowcaseUfoZ      = 34.0f;

bool addShowcaseStage(std::map<std::string, std::vector<std::uint8_t>>& tree, std::string& error);

} // namespace standin
