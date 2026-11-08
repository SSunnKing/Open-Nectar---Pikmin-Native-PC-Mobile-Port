#include "settings_row_layout.h"
#include <cassert>
#include <initializer_list>

int main()
{
    using namespace pikmin::launcher;
    for (float scale : {1.0f, 2.0f}) {
        const auto narrow = settingsRowLayout(326 * scale, 190 * scale, 300 * scale);
        assert(narrow.stacked);
        assert(narrow.controlsX >= 0);
        const auto wide = settingsRowLayout(966 * scale, 190 * scale, 300 * scale);
        assert(!wide.stacked);
        assert(wide.controlsX >= 202 * scale);
        assert(!settingsButtonsFit(326 * scale, 240 * scale));
        assert(settingsButtonsFit(966 * scale, 240 * scale));
    }
    assert(settingsButtonsFit(492, 240));
    assert(!settingsButtonsFit(491, 240));
    assert(!settingsRowLayout(502, 190, 300).stacked);
    assert(settingsRowLayout(501, 190, 300).stacked);
}
