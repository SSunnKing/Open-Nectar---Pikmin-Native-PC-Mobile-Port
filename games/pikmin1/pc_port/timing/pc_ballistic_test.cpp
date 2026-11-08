#include "pc_ballistic.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

int main()
{
	int failures = 0;
	auto check = [&](bool ok, const char* what) { if (!ok) { std::printf("FAIL %s\n", what); ++failures; } };
	check(!PcBallisticClock::enabled(0), "paused time does not consume a reference kick");
	check(!PcBallisticClock::enabled(PcBallisticClock::step), "original 30Hz path bypasses the adapter");
	// The two Creature passes give y += (v - 2*g*H)*H, v -= g*H.
	// This is the observed GC throw, rather than the continuous parabola.
	for (int rate : {30, 60, 120, 75}) {
		PcBallisticClock clock;
		float y = 0, v = 290, peak = 0;
		float time = 0;
		while (time < 0.5f - 0.000001f) {
			float left = std::min(1.0f / rate, 0.5f - time);
			time += left;
			while (left > 0.000001f) {
				const float dt = clock.slice(left);
				if (clock.boundary()) v -= 560 * PcBallisticClock::step;
				y += (v - 560 * PcBallisticClock::step) * dt;
				peak = std::max(peak, y);
				clock.advance(dt);
				left -= dt;
			}
		}
		check(std::fabs(peak - 61.288889f) < 0.0002f, "dual-pass sampled peak is the GC discrete maximum");
		check(std::fabs(y - 61.0f) < 0.0003f, "dual-pass position at 0.5s");
		check(std::fabs(v - 10.0f) < 0.0003f, "persistent velocity receives exactly fifteen kicks");
	}
	// An impulse inside a segment changes velocity without consuming another kick.
	PcBallisticClock c;
	check(c.boundary(), "new flight begins a reference interval");
	c.advance(PcBallisticClock::step / 4);
	check(!c.boundary(), "mid-interval impulse preserves consumed phase");
	check(std::fabs(c.slice(1.0f) - PcBallisticClock::step * 0.75f) < 0.000001f, "split at remaining boundary");
	c.advance(c.slice(1.0f));
	check(c.boundary(), "split ends exactly on next reference interval");
	c.advance(PcBallisticClock::step / 2);
	c.reset();
	check(c.boundary(), "reused actor starts a fresh flight");
	std::printf("pc_ballistic_test: %d failures\n", failures);
	return failures != 0;
}
