#include "launcher_scroll.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

using namespace pikmin::launcher;

static void check(bool value, const char* message)
{
    if (!value) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

static bool near(float a, float b, float eps = 0.01f) { return std::fabs(a - b) < eps; }

// Runs frames at 60 fps with no further wheel input until the scroll settles.
static float settle(SmoothScroll& s, float pos, float maxScroll, int* frames = nullptr)
{
    int n = 0;
    for (; n < 600; ++n) {
        const float next = smoothScrollStep(s, pos, maxScroll, 0.0f, 51.0f, 1.0f / 60.0f);
        if (next == pos) break;
        pos = next;
    }
    if (frames) *frames = n;
    return pos;
}

int main()
{
    const float row = 51.0f, dt = 1.0f / 60.0f;

    // One notch down moves exactly one row, eased over a few frames.
    {
        SmoothScroll s;
        float pos = smoothScrollStep(s, 0.0f, 1000.0f, -1.0f, row, dt);
        check(pos > 0.0f && pos < row, "first frame moves part of the way (eased, not a jump)");
        int frames = 0;
        pos = settle(s, pos, 1000.0f, &frames);
        check(near(pos, row), "one notch = one row");
        check(frames > 2 && frames < 30, "ease lasts a short moment (< 0.5 s)");
    }
    // One notch up from a row back to the top; three notches = three rows.
    {
        SmoothScroll s;
        float pos = settle(s, smoothScrollStep(s, 0.0f, 1000.0f, -3.0f, row, dt), 1000.0f);
        check(near(pos, 3.0f * row), "three notches = three rows");
        pos = settle(s, smoothScrollStep(s, pos, 1000.0f, 1.0f, row, dt), 1000.0f);
        check(near(pos, 2.0f * row), "one notch up = one row up");
    }
    // Notches during the ease accumulate on the target instead of restarting from the current spot.
    {
        SmoothScroll s;
        float pos = smoothScrollStep(s, 0.0f, 1000.0f, -1.0f, row, dt);
        pos = smoothScrollStep(s, pos, 1000.0f, -1.0f, row, dt);
        pos = settle(s, pos, 1000.0f);
        check(near(pos, 2.0f * row), "quick notches add up");
    }
    // Trackpad fractions scroll proportionally.
    {
        SmoothScroll s;
        float pos = 0.0f;
        for (int i = 0; i < 4; ++i) pos = smoothScrollStep(s, pos, 1000.0f, -0.25f, row, dt);
        pos = settle(s, pos, 1000.0f);
        check(near(pos, row), "four quarter steps = one row");
    }
    // Clamped to the content.
    {
        SmoothScroll s;
        float pos = settle(s, smoothScrollStep(s, 0.0f, 80.0f, -5.0f, row, dt), 80.0f);
        check(near(pos, 80.0f), "never past the bottom");
        pos = settle(s, smoothScrollStep(s, pos, 80.0f, 5.0f, row, dt), 80.0f);
        check(near(pos, 0.0f), "never past the top");
        SmoothScroll none;
        check(near(smoothScrollStep(none, 0.0f, 0.0f, -1.0f, row, dt), 0.0f), "nothing to scroll");
    }
    // Scrollbar drag (external change) resyncs the target: the next notch starts from there.
    {
        SmoothScroll s;
        float pos = settle(s, smoothScrollStep(s, 0.0f, 1000.0f, -1.0f, row, dt), 1000.0f);
        pos = 400.0f; // user dragged the scrollbar
        check(near(smoothScrollStep(s, pos, 1000.0f, 0.0f, row, dt), 400.0f), "no drift back after a drag");
        pos = settle(s, smoothScrollStep(s, pos, 1000.0f, -1.0f, row, dt), 1000.0f);
        check(near(pos, 400.0f + row), "notch after a drag moves one row from the dragged spot");
    }
    // Content shrinking below the scroll position clamps the target too.
    {
        SmoothScroll s;
        float pos = settle(s, smoothScrollStep(s, 0.0f, 1000.0f, -4.0f, row, dt), 1000.0f);
        pos = settle(s, std::min(pos, 60.0f), 60.0f);
        check(near(pos, 60.0f), "follows a shorter list");
    }
    // A long frame (dt spike) does not overshoot.
    {
        SmoothScroll s;
        const float pos = smoothScrollStep(s, 0.0f, 1000.0f, -1.0f, row, 1.0f);
        check(near(pos, row), "large dt lands on the target without overshooting");
    }

    std::cout << "launcher_scroll_test passed\n";
    return 0;
}
