#include "install_note_layout.h"

#include <cstdlib>
#include <sstream>

namespace pikmin {
namespace launcher {

namespace {

std::string messageWithPath(const std::string& displayPath)
{
    return "Your save in " + displayPath + " will be copied here. The original is kept.";
}

std::vector<std::string> splitWords(const std::string& text)
{
    std::vector<std::string> words;
    std::istringstream stream(text);
    std::string word;
    while (stream >> word) words.push_back(word);
    return words;
}

} // namespace

std::string homeDirectoryForDisplayPath()
{
#if defined(_WIN32)
    return {};
#else
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') return home;
    return {};
#endif
}

std::string shortenPathForDisplay(const std::string& path, const std::string& homeDirectory)
{
    if (path.empty() || homeDirectory.empty()) return path;
    std::string home = homeDirectory;
    if (home.back() != '/') home += '/';
    if (path.compare(0, home.size(), home) == 0) return std::string("~/") + path.substr(home.size());
    return path;
}

std::string middleEllipsisPath(const std::string& path, std::size_t maxCharacters)
{
    if (path.size() <= maxCharacters) return path;
    if (maxCharacters <= 3) return "...";
    const std::size_t tail = (maxCharacters - 3) / 2;
    const std::size_t head = maxCharacters - 3 - tail;
    return path.substr(0, head) + "..." + path.substr(path.size() - tail);
}

std::string installSaveMigrationMessage(const std::filesystem::path& legacySaveRoot, const std::string& homeDirectory)
{
    const std::string display = shortenPathForDisplay(legacySaveRoot.u8string(), homeDirectory);
    return messageWithPath(display);
}

TextWrapResult wrapTextToLines(const std::string& text, float maxWidth, int maxLines, const TextWidthMeasure& measure)
{
    TextWrapResult result;
    if (maxLines <= 0 || maxWidth <= 0.0f || text.empty()) {
        result.complete = text.empty();
        return result;
    }

    const std::vector<std::string> words = splitWords(text);
    std::string current;
    std::size_t wordIndex = 0;
    for (; wordIndex < words.size(); ++wordIndex) {
        const std::string& word = words[wordIndex];
        const std::string candidate = current.empty() ? word : current + " " + word;
        if (measure(candidate) <= maxWidth) {
            current = candidate;
            continue;
        }
        if (!current.empty()) {
            result.lines.push_back(current);
            current.clear();
            if (static_cast<int>(result.lines.size()) >= maxLines) break;
            --wordIndex;
            continue;
        }
        result.lines.push_back(word);
        if (static_cast<int>(result.lines.size()) >= maxLines) break;
    }
    if (static_cast<int>(result.lines.size()) < maxLines && !current.empty()) result.lines.push_back(current);

    std::string rendered;
    for (std::size_t i = 0; i < result.lines.size(); ++i) {
        if (i) rendered += ' ';
        rendered += result.lines[i];
    }
    result.complete = wordIndex >= words.size() && rendered == text;
    return result;
}

std::vector<std::string> layoutInstallSaveMigrationNote(const std::filesystem::path& legacySaveRoot, float maxWidth,
                                                        int maxLines, const TextWidthMeasure& measure,
                                                        const std::string& homeDirectory)
{
    if (legacySaveRoot.empty() || maxLines <= 0 || maxWidth <= 0.0f) return {};

    std::string displayPath = shortenPathForDisplay(legacySaveRoot.u8string(), homeDirectory);
    for (int attempt = 0; attempt < 64; ++attempt) {
        const std::string message = messageWithPath(displayPath);
        const TextWrapResult wrapped = wrapTextToLines(message, maxWidth, maxLines, measure);
        if (wrapped.complete) return wrapped.lines;
        if (displayPath.size() <= 8) return wrapped.lines;
        const std::size_t nextLen = displayPath.size() > 12 ? displayPath.size() - 2 : displayPath.size() - 1;
        displayPath = middleEllipsisPath(displayPath, nextLen);
    }
    return { messageWithPath(displayPath) };
}

} // namespace launcher
} // namespace pikmin
