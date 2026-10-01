// Host test for the co-op formation (C-stick swarm) sound arbiter. No audio
// engine and no game data: a small model of Jac_Orima_Formation stands in for
// the engine, so the thing counted is what the real one would be told.
#include "audio/pc_formation_arbiter.h"

#include <cmath>
#include <cstdio>

namespace {

int checks = 0;
int failures = 0;

#define CHECK(condition) do { \
    ++checks; \
    if (!(condition)) { \
        std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        ++failures; \
    } \
} while (0)

// What both engines do with a request: bound each axis, take the length, and
// start the voice on the first non-zero request / stop it on the first zero one
// (JAudio piki_player.c Jac_Orima_Formation, audio_stubs.cpp the same).
struct EngineModel {
    bool active = false;
    int starts = 0;
    int stops = 0;
    int lastX = 0;
    int lastMag = 0;
    int calls = 0;

    void formation(int x, int y) {
        ++calls;
        x = x < -127 ? -127 : (x > 127 ? 127 : x);
        y = y < -127 ? -127 : (y > 127 ? 127 : y);
        if (y < 0) y = -y;
        const int mag = static_cast<int>(std::sqrt(static_cast<float>(x * x + y * y)));
        lastX = x;
        lastMag = mag;
        if (x == 0 && mag == 0) {
            if (active) {
                active = false;
                ++stops;
            }
        } else if (!active) {
            active = true;
            ++starts;
        }
    }
};

// SeMgr::playNaviSound, as a function of the arbiter.
void navi_call(PcFormationArbiter& arb, EngineModel& eng, int captain, int x, int y, unsigned frame) {
    const PcFormationStick s = arb.submit(captain, x, y, frame);
    eng.formation(s.x, s.y);
}

// The way it worked before: every captain writes the engine directly.
void navi_call_unarbitrated(EngineModel& eng, int x, int y) { eng.formation(x, y); }

// Navi.cpp scales the c-stick by 74.
constexpr int kPush = 74;

void test_single_captain_is_a_pass_through() {
    PcFormationArbiter arb;
    // The arbiter hands back exactly what was submitted, including values the
    // engine will bound itself, so one captain is byte-for-byte as before.
    const int sticks[][2] = { { 0, 0 }, { 74, 0 }, { -74, 52 }, { 0, -74 }, { 200, -300 }, { 3, 4 }, { 0, 0 } };
    unsigned frame = 0;
    EngineModel direct, viaArb;
    for (const auto& s : sticks) {
        const PcFormationStick out = arb.submit(0, s[0], s[1], ++frame);
        CHECK(out.x == s[0]);
        CHECK(out.y == s[1]);
        direct.formation(s[0], s[1]);
        viaArb.formation(out.x, out.y);
        CHECK(direct.active == viaArb.active);
        CHECK(direct.lastX == viaArb.lastX && direct.lastMag == viaArb.lastMag);
    }
    CHECK(direct.starts == viaArb.starts && direct.stops == viaArb.stops);
    // And the same for the second captain's slot on its own.
    PcFormationArbiter arb2;
    const PcFormationStick out = arb2.submit(1, -40, 9, 5);
    CHECK(out.x == -40 && out.y == 9);
}

void test_one_swarming_one_idle_does_not_flap() {
    // Before: captain 0 swarms, captain 1 idle, 600 frames. The old path
    // starts and stops the voice on every call.
    EngineModel before;
    for (int f = 1; f <= 600; ++f) {
        navi_call_unarbitrated(before, kPush, kPush);
        navi_call_unarbitrated(before, 0, 0);
    }
    CHECK(before.starts >= 600 && before.stops >= 599);

    // After: one start, no stop, and the engine ends up playing.
    PcFormationArbiter arb;
    EngineModel after;
    for (unsigned f = 1; f <= 600; ++f) {
        navi_call(arb, after, 0, 0, kPush, f);
        navi_call(arb, after, 1, 0, 0, f);
    }
    CHECK(after.starts == 1);
    CHECK(after.stops == 0);
    CHECK(after.active);
    CHECK(after.lastMag == kPush);
}

void test_update_order_does_not_matter() {
    // Captain 1 swarming, captain 0 idle, called in either order.
    for (int order = 0; order < 2; ++order) {
        PcFormationArbiter arb;
        EngineModel eng;
        for (unsigned f = 1; f <= 300; ++f) {
            if (order == 0) {
                navi_call(arb, eng, 0, 0, 0, f);
                navi_call(arb, eng, 1, kPush, 0, f);
            } else {
                navi_call(arb, eng, 1, kPush, 0, f);
                navi_call(arb, eng, 0, 0, 0, f);
            }
        }
        CHECK(eng.starts == 1 && eng.stops == 0 && eng.active);
        CHECK(eng.lastX == kPush);
    }
}

void test_both_swarming_picks_the_larger_deflection() {
    PcFormationArbiter arb;
    for (unsigned f = 1; f <= 20; ++f) {
        const PcFormationStick a = arb.submit(0, 30, 10, f);
        const PcFormationStick b = arb.submit(1, -60, 20, f);
        (void)a;
        CHECK(b.x == -60 && b.y == 20);
        // Captain 0 calling again after captain 1 sees the same winner.
        const PcFormationStick a2 = arb.submit(0, 30, 10, f + 1);
        CHECK(a2.x == -60 && a2.y == 20);
        (void)arb.submit(1, -60, 20, f + 1);
    }

    // Ordering is by length, not by one axis, and the engine's +-127 bound
    // applies to both when comparing.
    PcFormationArbiter arb2;
    arb2.submit(0, 0, -74, 1);                      // length 74
    PcFormationStick w = arb2.submit(1, 50, 50, 1); // length ~70.7
    CHECK(w.x == 0 && w.y == -74);
    arb2.submit(0, 500, 0, 2);                      // bounded to 127
    w = arb2.submit(1, 127, 5, 2);                  // ~127.1
    CHECK(w.x == 127 && w.y == 5);

    // An exact tie goes to the lower captain index, whichever called last.
    for (int order = 0; order < 2; ++order) {
        PcFormationArbiter arb3;
        PcFormationStick out{};
        for (unsigned f = 1; f <= 3; ++f) {
            if (order == 0) {
                arb3.submit(0, 40, 0, f);
                out = arb3.submit(1, 0, 40, f);
            } else {
                arb3.submit(1, 0, 40, f);
                out = arb3.submit(0, 40, 0, f);
            }
            CHECK(out.x == 40 && out.y == 0);
        }
    }
}

void test_both_idle_stops_once() {
    PcFormationArbiter arb;
    EngineModel eng;
    // Both swarm, both let go, then keep calling (0, 0) every frame.
    for (unsigned f = 1; f <= 30; ++f) {
        navi_call(arb, eng, 0, kPush, 0, f);
        navi_call(arb, eng, 1, 0, kPush, f);
    }
    CHECK(eng.active && eng.starts == 1);
    for (unsigned f = 31; f <= 330; ++f) {
        navi_call(arb, eng, 0, 0, 0, f);
        navi_call(arb, eng, 1, 0, 0, f);
    }
    CHECK(!eng.active);
    CHECK(eng.starts == 1 && eng.stops == 1);
}

void test_a_swarm_hands_over() {
    // Captain 0 swarms, captain 1 joins, captain 0 lets go: the voice plays
    // straight through when the two overlap.
    {
        PcFormationArbiter arb;
        EngineModel eng;
        for (unsigned f = 1; f <= 100; ++f) {
            navi_call(arb, eng, 0, f <= 60 ? kPush : 0, 0, f);
            navi_call(arb, eng, 1, f > 40 ? kPush : 0, 0, f);
        }
        CHECK(eng.starts == 1 && eng.stops == 0 && eng.active);
    }
    // With no overlap at all, the captain updating first in the handover frame
    // still sees the other one's previous stick, so the voice restarts once in
    // that frame. That is a single restart, not a per-frame fight.
    {
        PcFormationArbiter arb;
        EngineModel eng;
        for (unsigned f = 1; f <= 100; ++f) {
            navi_call(arb, eng, 0, f <= 50 ? kPush : 0, 0, f);
            navi_call(arb, eng, 1, f > 50 ? kPush : 0, 0, f);
        }
        CHECK(eng.starts == 2 && eng.stops == 1 && eng.active);
    }
}

void test_a_captain_that_stops_calling_goes_stale() {
    PcFormationArbiter arb;
    EngineModel eng;
    // Captain 0 is swarming when it dies: its calls stop at frame 40.
    for (unsigned f = 1; f <= 40; ++f) {
        navi_call(arb, eng, 0, kPush, 0, f);
        navi_call(arb, eng, 1, 0, 0, f);
    }
    CHECK(eng.active);
    // Captain 1 keeps calling (0, 0). The held stick must not outlive the
    // staleness window.
    for (unsigned f = 41; f <= 40 + PC_FORMATION_STALE_FRAMES; ++f) navi_call(arb, eng, 1, 0, 0, f);
    CHECK(eng.active); // still held inside the window
    for (unsigned f = 41 + PC_FORMATION_STALE_FRAMES; f <= 200; ++f) navi_call(arb, eng, 1, 0, 0, f);
    CHECK(!eng.active);
    CHECK(eng.starts == 1 && eng.stops == 1);

    // Without the staleness rule the held stick would never release.
    PcFormationArbiter arb2;
    arb2.submit(0, kPush, 0, 1);
    PcFormationStick s = arb2.submit(1, 0, 0, 2);
    CHECK(s.x == kPush); // still live: one frame old
    s = arb2.submit(1, 0, 0, 1 + PC_FORMATION_STALE_FRAMES);
    CHECK(s.x == kPush); // exactly at the limit
    s = arb2.submit(1, 0, 0, 1 + PC_FORMATION_STALE_FRAMES + 1);
    CHECK(s.x == 0 && s.y == 0);

    // The stale captain comes back to life and counts again.
    s = arb2.submit(0, 0, kPush, 100);
    CHECK(s.y == kPush);
}

void test_frame_counter_wrap() {
    PcFormationArbiter arb;
    const unsigned top = 0xFFFFFFFFu;
    arb.submit(0, kPush, 0, top);
    PcFormationStick s = arb.submit(1, 0, 0, 0); // wrapped, one frame later
    CHECK(s.x == kPush);
    s = arb.submit(1, 0, 0, 10);
    CHECK(s.x == 0);
}

void test_a_direct_stop_still_stops() {
    PcFormationArbiter arb;
    EngineModel eng;
    for (unsigned f = 1; f <= 10; ++f) {
        navi_call(arb, eng, 0, kPush, 0, f);
        navi_call(arb, eng, 1, 0, 0, f);
    }
    CHECK(eng.active);
    // Jac_Orima_Formation(0, 0) straight to the engine (verysimple.cpp,
    // pikidemo.c at the end of a demo): the arbiter is not involved.
    eng.formation(0, 0);
    CHECK(!eng.active && eng.stops == 1);
    // It forwards on every call rather than on change, so the next navi call
    // simply asks for the sound again if the stick is still pushed...
    navi_call(arb, eng, 0, kPush, 0, 11);
    CHECK(eng.active && eng.starts == 2);
    // ...and a stop is not undone by an idle captain's call: the arbiter has
    // no memory of whether the engine is on.
    eng.formation(0, 0);
    navi_call(arb, eng, 0, 0, 0, 12);
    navi_call(arb, eng, 1, 0, 0, 12);
    CHECK(!eng.active);
}

void test_out_of_range_captain_is_forwarded() {
    PcFormationArbiter arb;
    PcFormationStick s = arb.submit(-1, 12, 34, 1);
    CHECK(s.x == 12 && s.y == 34);
    s = arb.submit(PC_FORMATION_MAX_CAPTAINS, -5, 6, 1);
    CHECK(s.x == -5 && s.y == 6);
    // It does not take over a slot.
    s = arb.submit(0, 0, 0, 1);
    CHECK(s.x == 0 && s.y == 0);
}

void test_reset_forgets_everyone() {
    PcFormationArbiter arb;
    arb.submit(0, kPush, 0, 5);
    arb.reset();
    const PcFormationStick s = arb.submit(1, 0, 0, 5);
    CHECK(s.x == 0 && s.y == 0);
}

} // namespace

int main() {
    test_single_captain_is_a_pass_through();
    test_one_swarming_one_idle_does_not_flap();
    test_update_order_does_not_matter();
    test_both_swarming_picks_the_larger_deflection();
    test_both_idle_stops_once();
    test_a_swarm_hands_over();
    test_a_captain_that_stops_calling_goes_stale();
    test_frame_counter_wrap();
    test_a_direct_stop_still_stops();
    test_out_of_range_captain_is_forwarded();
    test_reset_forgets_everyone();
    std::printf("pc_formation_arbiter_test: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
