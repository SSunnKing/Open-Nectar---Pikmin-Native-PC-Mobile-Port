#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace pikmin {
namespace launcher {

using TextWidthMeasure = std::function<float(const std::string&)>;

std::string homeDirectoryForDisplayPath();

/// Replaces a leading home directory with `~/` when possible.
std::string shortenPathForDisplay(const std::string& path, const std::string& homeDirectory);

std::string installSaveMigrationMessage(const std::filesystem::path& legacySaveRoot,
                                        const std::string& homeDirectory = homeDirectoryForDisplayPath());

struct TextWrapResult {
    std::vector<std::string> lines;
    bool complete = false;
};

TextWrapResult wrapTextToLines(const std::string& text, float maxWidth, int maxLines, const TextWidthMeasure& measure);

/// At most `maxLines` lines; shortens the embedded path (middle ellipsis) until the full sentence fits.
std::vector<std::string> layoutInstallSaveMigrationNote(const std::filesystem::path& legacySaveRoot, float maxWidth,
                                                        int maxLines, const TextWidthMeasure& measure,
                                                        const std::string& homeDirectory = homeDirectoryForDisplayPath());

std::string middleEllipsisPath(const std::string& path, std::size_t maxCharacters);

} // namespace launcher
} // namespace pikmin
