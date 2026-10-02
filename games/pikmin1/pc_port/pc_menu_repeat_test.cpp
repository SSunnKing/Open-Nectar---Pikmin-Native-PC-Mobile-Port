/**
 * @file pc_menu_repeat_test.cpp
 * @brief Checks menu input repeat, without a controller and without waiting.
 *
 * Written after a player reported the settings menu "accepting dozens of
 * inputs a second", so that nudging a stick lapped the whole list. The
 * controller path read raw state, which is down on every frame it is held; a
 * measurement with a virtual pad confirmed sixty reads in sixty frames.
 *
 * The clock is a parameter, so a held input can be walked through several
 * seconds of frames in an instant.
 */

#include "pc_menu_repeat.h"

#include <cstdio>

namespace {

int sFailures = 0;

void check(bool condition, const char* what)
{
	if (!condition) {
		std::printf("FAIL: %s\n", what);
		++sFailures;
	}
}

/// Holds an input for @p frames at 60 fps and counts how often it acts.
int countOverFrames(int slot, int frames, std::uint32_t startMs)
{
	int acted = 0;
	for (int f = 0; f < frames; f++) {
		if (pc_menu_edge(true, slot, startMs + std::uint32_t(f * 1000 / 60))) {
			++acted;
		}
	}
	return acted;
}

} // namespace

int main()
{
	// A press acts once, however many frames it is held within the delay.
	pc_menu_edge_reset();
	check(pc_menu_edge(true, 0, 1000), "a press acts immediately");
	for (int f = 1; f < 20; f++) {
		check(!pc_menu_edge(true, 0, 1000 + std::uint32_t(f * 16)),
		      "holding does not act again before the delay");
	}

	// Releasing and pressing again acts again.
	pc_menu_edge_reset();
	check(pc_menu_edge(true, 0, 0), "first press acts");
	check(!pc_menu_edge(false, 0, 16), "release does not act");
	check(pc_menu_edge(true, 0, 32), "the next press acts");

	// The reported failure: one second of holding must not lap a menu. At 60
	// fps the old code acted 60 times; here it is the first press plus repeats
	// after the delay.
	pc_menu_edge_reset();
	const int inOneSecond = countOverFrames(0, 60, 0);
	std::printf("  held one second at 60 fps: acts %d times (raw state acted 60)\n", inOneSecond);
	check(inOneSecond >= 2, "holding does eventually repeat");
	check(inOneSecond <= 8, "holding does not run away");

	// Three seconds, to confirm the repeat is steady rather than accelerating.
	pc_menu_edge_reset();
	const int inThree = countOverFrames(0, 180, 0);
	std::printf("  held three seconds: acts %d times\n", inThree);
	check(inThree > inOneSecond, "a longer hold keeps repeating");
	check(inThree <= 30, "the repeat stays controllable over time");

	// A six-row menu must not be lapped by a one-second hold.
	check(inOneSecond < 6, "one second of holding cannot lap a six-row menu");

	// Slots are independent: holding one direction must not consume another.
	pc_menu_edge_reset();
	check(pc_menu_edge(true, 0, 0), "slot 0 acts");
	check(pc_menu_edge(true, 1, 0), "slot 1 acts independently");
	check(!pc_menu_edge(true, 0, 16), "slot 0 stays held");
	check(pc_menu_edge(true, 2, 16), "slot 2 still acts");

	// Out-of-range slots are refused rather than corrupting the table.
	check(!pc_menu_edge(true, -1, 0), "negative slot refused");
	check(!pc_menu_edge(true, 99, 0), "slot past the end refused");

	if (sFailures == 0) {
		std::printf("pc_menu_repeat_test: all checks passed\n");
		return 0;
	}
	std::printf("pc_menu_repeat_test: %d failure(s)\n", sFailures);
	return 1;
}
