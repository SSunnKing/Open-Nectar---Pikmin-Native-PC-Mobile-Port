/**
 * @file pc_cursor_ring_test.cpp
 * @brief Mouse-cursor mode shows the whistle target. Classic mode does not.
 */

#include "pc_cursor_ring.h"

#include <cstdio>

namespace {

int sFailures = 0;

void expect(int actual, int expected, const char* what)
{
	if (actual != expected) {
		std::printf("FAIL: %s -> got %d, expected %d\n", what, actual, expected);
		++sFailures;
	}
}

} // namespace

int main()
{
	const PcCursorRing idle = pc_cursor_ring(1, false, false, 0);
	expect(idle.aim, 1, "mouse mode shows the aim ring");
	expect(idle.whistle, 0, "an idle whistle has no radius circle");

	const PcCursorRing blowing = pc_cursor_ring(1, false, false, 1);
	expect(blowing.aim, 1, "blowing still shows the aim ring");
	expect(blowing.whistle, 1, "blowing shows the radius circle");

	const PcCursorRing fading = pc_cursor_ring(1, false, false, 2);
	expect(fading.whistle, 1, "the circle stays while it fades");

	const PcCursorRing classic = pc_cursor_ring(0, false, false, 1);
	expect(classic.aim, 0, "classic mode keeps the disc cursor");
	expect(classic.whistle, 0, "classic mode does not add a circle");

	const PcCursorRing locked = pc_cursor_ring(1, true, false, 1);
	expect(locked.aim, 0, "lock-on replaces the aim ring");
	expect(locked.whistle, 0, "lock-on replaces the whistle circle");

	const PcCursorRing movie = pc_cursor_ring(1, false, true, 1);
	expect(movie.aim, 0, "a cutscene hides the aim ring");
	expect(movie.whistle, 0, "a cutscene hides the whistle circle");

	if (sFailures == 0) {
		std::printf("pc_cursor_ring_test: all checks passed\n");
		return 0;
	}
	std::printf("pc_cursor_ring_test: %d failure(s)\n", sFailures);
	return 1;
}
