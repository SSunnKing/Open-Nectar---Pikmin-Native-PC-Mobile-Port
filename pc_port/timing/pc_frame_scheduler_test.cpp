#include "pc_frame_scheduler.h"

#include <cmath>
#include <cstdio>
#include <initializer_list>

static int failures = 0;

static void check(bool condition, const char* message)
{
	if (!condition) {
		std::fprintf(stderr, "FAIL: %s\n", message);
		++failures;
	}
}

static std::uint64_t simulate(double presentHz, int clamp, double seconds, bool jitter = false)
{
	PcFrameScheduler scheduler;
	double now = 0.0;
	scheduler.reset(now, clamp);
	const int presents = static_cast<int>(std::ceil(presentHz * seconds));
	for (int i = 0; i < presents; ++i) {
		double step = 1.0 / presentHz;
		if (jitter) step += (i & 1) ? 0.002 : -0.002;
		now = std::min(seconds, now + step);
		scheduler.advance(now, clamp);
		if (now >= seconds) break;
	}
	if (now < seconds) scheduler.advance(seconds, clamp);
	return scheduler.totalTicks();
}

int main()
{
	const double rates[] = { 50.0, 59.94, 60.0, 75.0, 90.0, 120.0, 144.0, 165.0, 240.0 };
	for (double rate : rates) {
		check(simulate(rate, 1, 600.0) == 36000, "60 Hz ticks must not depend on presentation");
		check(simulate(rate, 2, 300.0) == 9000, "30 Hz ticks must not depend on presentation");
	}
	check(simulate(144.0, 1, 600.0, true) == 36000, "alternating jitter must not drift");

	PcFrameScheduler slow(4, 0.5);
	slow.reset(0.0, 1);
	auto result = slow.advance(0.25, 1);
	check(result.logicalTicks == 4 && result.discardedTicks == 11, "250 ms stall must have bounded catch-up");
	result = slow.advance(5.25, 1);
	check(result.logicalTicks == 0, "long suspension must create no catch-up ticks");

	PcFrameScheduler change;
	change.reset(0.0, 1);
	change.advance(1.0 / 60.0, 1);
	result = change.advance(1.0 / 60.0, 2);
	check(result.logicalTicks == 0 && std::abs(result.fixedDelta - 1.0 / 30.0) < 1e-12,
	      "clamp change must reset debt and fixed delta");
	result = change.advance(1.0 / 20.0, 2);
	check(result.logicalTicks == 1, "new clamp deadline must produce one tick");


    for (int clamp : {0, 1, 2}) {
        PcFrameScheduler fast;
        fast.reset(0.0, clamp, 2.0);
        double simulated = 0.0;
        for (int i = 1; i <= 2400; ++i) {
            auto tick = fast.advance(i / 240.0, clamp, 2.0);
            for (int n = 0; n < tick.logicalTicks; ++n) simulated += tick.fixedDelta;
        }
        check(std::abs(simulated - 20.0) < 1e-8, "2x consumes twenty simulated seconds in ten wall seconds");
    }
    PcFrameScheduler toggle;
    toggle.reset(0, 2);
    toggle.advance(.01, 2);
    auto doubled = toggle.advance(.01, 2, 2);
    check(doubled.logicalTicks == 0 && std::abs(doubled.nextDeadline - (.01 + 1.0/60)) < 1e-12,
          "speed change clears fractional debt and uses wall-speed deadline");
    auto suspended = toggle.advance(2.0, 2, 2);
    check(suspended.logicalTicks == 0, "2x suspension does not create catch-up debt");
    auto normal = toggle.advance(2.01, 2, 1);
    check(normal.logicalTicks == 0, "release resets debt");
    PcFrameScheduler overloaded;
    overloaded.reset(0, 2, 2);
    auto bounded = overloaded.advance(.25, 2, 2);
    check(bounded.logicalTicks == 4 && bounded.discardedTicks == 11,
          "2x catch-up remains bounded");
	std::printf("PcFrameScheduler: %s\n", failures ? "FAILED" : "all tests passed");
	return failures ? 1 : 0;
}
