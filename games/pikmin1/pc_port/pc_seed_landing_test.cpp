/**
 * @file pc_seed_landing_test.cpp
 * @brief Locks the sprout-landing decisions without a map or a window.
 *
 * BuryInit used to drop every sprout onto getMinY, including at the GameCube
 * 30 Hz step. BuryExec pushes a sprout off a steep face (upstream issue #65).
 * Those are separate choices: the drop follows the 60/120 step, and the push
 * stays at every frame rate.
 */

#include "pc_seed_landing.h"

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

/// The gameplay clamp in newPikiGame.cpp, and System::pcTicksPer30Hz().
int ticksForFpsMode(int fpsMode)
{
	int frameClamp = 2;
	if (fpsMode == 1) {
		frameClamp = 1;
	} else if (fpsMode == 2) {
		frameClamp = 0;
	}
	return frameClamp == 0 ? 4 : (frameClamp == 1 ? 2 : 1);
}

} // namespace

int main()
{
	check(ticksForFpsMode(0) == 1, "30 FPS is one 30 Hz tick");
	check(ticksForFpsMode(1) == 2, "60 FPS is two 30 Hz ticks");
	check(ticksForFpsMode(2) == 4, "120 FPS is four 30 Hz ticks");

	check(!pc_seed_landing_snaps_to_ground(ticksForFpsMode(0)), "30 FPS keeps the bounce");
	check(pc_seed_landing_snaps_to_ground(ticksForFpsMode(1)), "60 FPS drops a sprout still in the air");
	check(pc_seed_landing_snaps_to_ground(ticksForFpsMode(2)), "120 FPS drops a sprout still in the air");
	check(!pc_seed_landing_snaps_to_ground(1), "a single tick does not snap");
	check(pc_seed_landing_snaps_to_ground(3), "any step smaller than 30 Hz snaps");

	// A floor, and the 60-degree boundary, stay planted.
	check(!pc_seed_landing_pushes_off_steep_face(1.0f), "a flat floor stays planted");
	check(!pc_seed_landing_pushes_off_steep_face(0.6f), "a face recorded as ground can still be a floor");
	check(!pc_seed_landing_pushes_off_steep_face(0.5f), "the 0.5 boundary stays planted");

	// Steeper than that is the wall-edge case: the face is a wall even though
	// the collision normal was high enough to store the triangle as ground.
	check(pc_seed_landing_pushes_off_steep_face(0.49f), "just under 0.5 is pushed off");
	check(pc_seed_landing_pushes_off_steep_face(0.0f), "a vertical face is pushed off");
	check(pc_seed_landing_pushes_off_steep_face(-0.2f), "a downward face is pushed off");

	if (sFailures == 0) {
		std::printf("pc_seed_landing_test: all checks passed\n");
		return 0;
	}
	std::printf("pc_seed_landing_test: %d failure(s)\n", sFailures);
	return 1;
}
