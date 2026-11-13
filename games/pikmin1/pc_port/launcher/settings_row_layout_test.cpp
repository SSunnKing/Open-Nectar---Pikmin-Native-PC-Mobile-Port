#include "settings_row_layout.h"

#include <cstdlib>
#include <initializer_list>
#include <iostream>

static void check(bool value, const char* message)
{
    if (!value) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

int main()
{
    using namespace pikmin::launcher;
    for (float scale : {1.0f, 2.0f}) {
        const auto narrow = settingsRowLayout(326 * scale, 190 * scale, 300 * scale);
        check(narrow.stacked, "narrow row is stacked");
        check(narrow.controlsX >= 0, "narrow controls stay on screen");
        const auto wide = settingsRowLayout(966 * scale, 190 * scale, 300 * scale);
        check(!wide.stacked, "wide row is not stacked");
        check(wide.controlsX >= 202 * scale, "wide controls keep their indent");
        check(!settingsButtonsFit(326 * scale, 240 * scale), "narrow window cannot fit both buttons");
        check(settingsButtonsFit(966 * scale, 240 * scale), "wide window fits both buttons");
    }
    check(settingsButtonsFit(492, 240), "buttons fit at 492");
    check(!settingsButtonsFit(491, 240), "buttons do not fit at 491");
    check(!settingsRowLayout(502, 190, 300).stacked, "502 is wide enough not to stack");
    check(settingsRowLayout(501, 190, 300).stacked, "501 stacks");
    return 0;
}
