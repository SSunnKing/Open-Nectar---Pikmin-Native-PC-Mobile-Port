#include "launcher_sidebar_layout.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

using namespace pikmin::launcher;

static void check(bool value, const char* message)
{
    if (!value) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

static bool near(float a, float b) { return std::fabs(a - b) < 0.01f; }

// The list must end above "Reset all" for every count and height: either it fits
// (content <= available) or it says it needs to scroll.
static void checkFits(int count, float available)
{
    const SidebarMetrics m;
    const SidebarLayout l = fitSidebar(count, available);
    const float end = float(count - 1) * l.step + l.height;
    check(near(end, l.content), "content matches step/height");
    check(l.height >= m.minHeight - 0.01f, "buttons never shorter than the minimum");
    check(l.height <= m.preferredHeight + 0.01f, "buttons never taller than preferred");
    check(l.step - l.height >= m.minGap - 0.01f, "gap never below the minimum");
    if (!l.scroll) check(l.content <= available + 0.01f, "non-scrolling list fits");
    else check(sidebarContent(count, m.minHeight, m.minGap) > available, "scroll only when the minimum does not fit");
}

int main()
{
    // Plenty of room: preferred 40 px buttons every 50 px, as before.
    SidebarLayout l = fitSidebar(8, 600.0f);
    check(!l.scroll && near(l.height, 40.0f) && near(l.step, 50.0f), "roomy layout unchanged");

    // Default 1180x780 window, Pikmin 1 (6 groups + Texture Packs + HD Models).
    // The old fixed layout needed 390 px and HD Models landed under Reset all.
    l = fitSidebar(8, 380.0f);
    check(!l.scroll && near(l.height, 40.0f) && l.step < 50.0f && l.step - l.height >= 8.0f, "a little short: gap shrinks first");
    l = fitSidebar(8, 360.0f);
    check(!l.scroll && l.height < 40.0f && near(l.step - l.height, 8.0f) && near(l.content, 360.0f),
          "780 px: buttons shrink once the gap reaches its comfortable minimum");

    // Minimum 960x700 window: smallest buttons, gap squeezed, still fits.
    l = fitSidebar(8, 280.0f);
    check(!l.scroll && near(l.height, 30.0f) && l.step - l.height < 8.0f && l.content <= 280.01f, "700 px: still fits");

    // One more page (e.g. a Saves page) at the minimum height: falls back to scrolling.
    l = fitSidebar(12, 288.0f);
    check(l.scroll && near(l.height, 30.0f) && l.content > 288.0f, "too many pages scroll");

    // Degenerate inputs.
    l = fitSidebar(0, 100.0f);
    check(!l.scroll && near(l.content, 0.0f), "empty list");
    l = fitSidebar(3, -50.0f);
    check(l.scroll, "negative space scrolls");
    l = fitSidebar(1, 34.0f);
    check(!l.scroll && near(l.height, 34.0f), "single short button");

    // Scrolling list: the selected page is brought into view with the smallest move.
    l = fitSidebar(8, 174.0f); // 960x560: step 35, height 30, content 275
    check(l.scroll && near(l.content, 275.0f), "560 px scrolls");
    check(near(sidebarScrollToShow(7, l, 174.0f, 0.0f), 101.0f), "last page scrolled to the bottom");
    check(near(sidebarScrollToShow(5, l, 174.0f, 0.0f), 31.0f), "hidden page scrolled just into view");
    check(near(sidebarScrollToShow(2, l, 174.0f, 40.0f), 40.0f), "visible page does not move the list");
    check(near(sidebarScrollToShow(0, l, 174.0f, 101.0f), 0.0f), "page above scrolls up to its top");
    check(near(sidebarScrollToShow(1, l, 174.0f, 40.0f), 35.0f), "partly hidden page above is shown whole");
    check(near(sidebarScrollToShow(7, l, 20.0f, 0.0f), 245.0f), "view shorter than a button shows its top");
    check(near(sidebarScrollToShow(-1, l, 174.0f, 500.0f), 101.0f), "no selection only clamps");

    for (int count = 1; count <= 16; ++count)
        for (float available = 0.0f; available <= 800.0f; available += 7.0f) checkFits(count, available);

    std::cout << "launcher_sidebar_layout_test passed\n";
    return 0;
}
