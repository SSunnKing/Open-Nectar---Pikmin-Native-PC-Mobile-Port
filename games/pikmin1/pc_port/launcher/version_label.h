#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace pikmin::launcher {

// The version label next to the logo (#327), drawn in the CPU to look like
// the logo's lettering: glossy orange letters with a darker edge and the
// same soft halo. Pure pixels, so it can be tested without a window.
struct VersionLabelImage {
    int width = 0, height = 0;
    std::vector<unsigned char> rgba; // straight alpha, rows top to bottom
    // Ink box of the letters inside the image (the rest is outline and halo).
    float inkTop = 0.0f, inkBottom = 0.0f, inkLeft = 0.0f, inkRight = 0.0f;
};

// Renders `text` with the TrueType font `ttf` so the digits are `inkHeight`
// pixels tall (physical pixels: pass the size times the display scale).
// false if the font can't be read or the text has nothing to draw.
bool renderVersionLabel(const unsigned char* ttf, std::size_t ttfSize, const std::string& text, float inkHeight,
                        VersionLabelImage& out);

} // namespace pikmin::launcher
