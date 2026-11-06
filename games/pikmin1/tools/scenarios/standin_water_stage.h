#pragma once

// The water sandbox stage: stages/sandbox_water.ini (#93). A flat map with one
// pond (nectar_mapcore DocPond / kMapCodeWater triangles). Opt in with
// PIKMIN_SCENARIO_STAGE=stages/sandbox_water.ini. Appended to stages.ini so
// existing scenario hash logs stay unchanged.

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace standin {

constexpr const char* kWaterStageIni = "stages/sandbox_water.ini";

/// Pond centre and radii in map units (see addWaterStage).
constexpr float kPondCx = 180.0f;
constexpr float kPondCz = 0.0f;
constexpr float kPondRx = 100.0f;
constexpr float kPondRz = 100.0f;
constexpr float kPondDepth = 35.0f;
constexpr float kPondSurface = -4.0f;

/// A point known to be over water (for placing or throwing Pikmin).
constexpr float kWaterX = kPondCx;
constexpr float kWaterZ = kPondCz;

/// Dry ground west of the pond (captain starts near the origin).
constexpr float kShoreX = 40.0f;
constexpr float kShoreZ = 0.0f;

bool addWaterStage(std::map<std::string, std::vector<std::uint8_t>>& tree, std::string& error);

} // namespace standin
