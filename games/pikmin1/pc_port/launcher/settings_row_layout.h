#pragma once

namespace pikmin::launcher {

struct SettingsRowLayout {
    bool stacked;
    float controlsX;
};

// Decide once using the widest name so every row has the same pitch.
constexpr SettingsRowLayout settingsRowLayout(float width, float nameWidth, float controlsWidth)
{
    return {nameWidth + 12.0f + controlsWidth > width, width - controlsWidth};
}

constexpr bool settingsButtonsFit(float width, float buttonWidth)
{
    return 2.0f * buttonWidth + 12.0f <= width;
}

} // namespace pikmin::launcher
