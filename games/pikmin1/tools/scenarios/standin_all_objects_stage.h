#pragma once

// The third stand-in stage: stages/sandbox_all_objects.ini. A flat floor with one
// of each object that has a stand-in model: the soft gate, six plants, three
// work objects (two bridges and a push box), two bosses and eight enemies, all
// placed by default.gen. scenario_objects_smoke boots it, so a model, collision
// platform, animation table or parameter file that is missing or malformed
// aborts the run. Nothing is read from the disc.

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace standin {

constexpr const char* kAllObjectsStageIni = "stages/sandbox_all_objects.ini";

/// How many enemies default.gen places (the scenario checks them).
constexpr int kAllObjectsEnemies = 8;

bool addAllObjectsStage(std::map<std::string, std::vector<std::uint8_t>>& tree, std::string& error);

} // namespace standin
