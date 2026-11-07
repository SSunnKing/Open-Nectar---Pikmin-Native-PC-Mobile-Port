#pragma once
#include <algorithm>
#include <string>

namespace pikmin::launcher {
// Keep UTF-8 boundaries intact even when a filename has no word breaks.
inline void popToastCharacter(std::string& text) {
    if (text.empty()) return;
    size_t at = text.size() - 1;
    while (at > 0 && (static_cast<unsigned char>(text[at]) & 0xc0) == 0x80) --at;
    text.resize(at);
}
template <typename Measure>
std::string fitToast(std::string text, float maxWidth, Measure width) {
    for (char& c : text) if (c == '\n' || c == '\r' || c == '\t') c = ' ';
    if (maxWidth <= 0) return {};
    if (width(text) <= maxWidth) return text;
    std::string first = text;
    while (!first.empty() && width(first) > maxWidth) popToastCharacter(first);
    const auto word = first.find_last_of(' ');
    if (word != std::string::npos && word > 0) first.resize(word);
    if (first.empty()) return {};
    std::string second = text.substr(first.size());
    while (!second.empty() && second.front() == ' ') second.erase(0, 1);
    if (width(second) > maxWidth) {
        const std::string dots = width("...") <= maxWidth ? "..." : "";
        while (!second.empty() && width(second + dots) > maxWidth) popToastCharacter(second);
        second += dots;
    }
    return first + '\n' + second;
}
} // namespace pikmin::launcher
