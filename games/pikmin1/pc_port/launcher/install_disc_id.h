#pragma once

#include <filesystem>
#include <fstream>
#include <string>

namespace pikmin::launcher {

// Pikmin 2 ships as two builds: Europe (GPVP01, pikmin2_pc) and USA (GPVE01,
// pikmin2_pc-usa). The installer copies the one matching the disc.
inline constexpr const char* kPikmin2PalDiscId = "GPVP01";
inline constexpr const char* kPikmin2UsaDiscId = "GPVE01";

// Region of an installed Pikmin 2: the first line of assets/.pikmin2-assets,
// which the installer writes from the disc header. A missing or unknown
// marker means the default build, Europe.
inline std::string installedPikmin2DiscId(const std::filesystem::path& gameDirectory)
{
    std::ifstream in(gameDirectory / "assets" / ".pikmin2-assets");
    std::string line;
    if (!in || !std::getline(in, line)) return kPikmin2PalDiscId;
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
    return line == kPikmin2UsaDiscId ? kPikmin2UsaDiscId : kPikmin2PalDiscId;
}

// Disc id for game `gameIndex` (0 Pikmin, 1 Pikmin 2) when the launcher
// state has none: Pikmin 2 reads its install's marker (#323), and with no
// install both games fall back to USA.
inline std::string discIdOrInstalled(int gameIndex, const std::string& discId, const std::string& directory)
{
    if (!discId.empty()) return discId;
    if (gameIndex == 1 && !directory.empty()) return installedPikmin2DiscId(std::filesystem::path(directory));
    return gameIndex == 0 ? "GPIE01" : kPikmin2UsaDiscId;
}

} // namespace pikmin::launcher
