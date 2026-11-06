/**
 * @file pc_wall_sides_test.cpp
 * @brief Locks the breakable-wall draw decision without a disc or a window.
 *
 * Upstream issue #266: after a wall is destroyed, its side pieces disappear.
 * GameCube keeps drawing the finished pose of gt_soft.mod / gt_hard.mod. The
 * centre of that pose is under the ground; the sides are not. The port skipped
 * the whole shape once the wall was completed and its motion had stopped.
 */

#include "pc_wall_sides.h"

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

/// The skip BuildingItem::refresh used before issue #266.
bool skippedFinishedPose(bool completed, float motionSpeed)
{
	return completed && motionSpeed == 0.0f;
}

} // namespace

int main()
{
	// The regression. WaitInit stops the motion after the sink, and a loaded
	// destroyed wall is restored the same way: completed, speed 0, on camera.
	check(skippedFinishedPose(true, 0.0f), "the old skip matches a finished, stopped wall");
	check(pc_wall_model_should_draw(false, true, 0.0f), "a finished wall with motion stopped still draws");

	// The sink itself is still in progress. ChangeInit runs it at speed 30,
	// and isCompleted() is already true for that last stage.
	check(!skippedFinishedPose(true, 30.0f), "the old skip left the sinking animation on screen");
	check(pc_wall_model_should_draw(false, true, 30.0f), "a wall still sinking draws");

	// Intact walls, including the idle pose startAI parks with stopMotion().
	check(pc_wall_model_should_draw(false, false, 0.0f), "an intact stopped wall draws");
	check(pc_wall_model_should_draw(false, false, 30.0f), "an intact wall playing its hit animation draws");

	// Camera cull is unchanged, including for a wall that is already down.
	check(!pc_wall_model_should_draw(true, false, 0.0f), "an off-camera intact wall does not draw");
	check(!pc_wall_model_should_draw(true, true, 0.0f), "an off-camera finished wall does not draw");
	check(!pc_wall_model_should_draw(true, true, 30.0f), "an off-camera sinking wall does not draw");

	if (sFailures == 0) {
		std::printf("pc_wall_sides_test: all checks passed\n");
		return 0;
	}
	std::printf("pc_wall_sides_test: %d failure(s)\n", sFailures);
	return 1;
}
