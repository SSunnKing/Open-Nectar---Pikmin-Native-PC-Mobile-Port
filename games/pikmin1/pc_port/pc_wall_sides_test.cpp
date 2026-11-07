/**
 * @file pc_wall_sides_test.cpp
 * @brief Locks breakable-wall drawing and the pose a later day rebuilds.
 *
 * Upstream issue #266: after a wall is destroyed, its side pieces disappear.
 * GameCube keeps drawing the finished pose of gt_soft.mod / gt_hard.mod. The
 * centre of that pose is under the ground; the sides are not. The port skipped
 * the whole shape once the wall was completed and its motion had stopped.
 *
 * Same-day play stops on the playback end key with the animator finished.
 * A later visit used to seek one frame earlier and leave the animator running,
 * so the rebuilt wall was not that pose.
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

/// The seek BuildingItem::doLoad used for a destroyed wall before this fix.
float oldDestroyedFrame(const int* keyFrames, int keyCount)
{
	return float(keyFrames[keyCount - 1]) - 1.0f;
}

// Info-key loop, cut down from PaniAnimator::checkConstantKey. type 0 is the
// loop start, type 1 is the loop end. animKeyIndex selects a playback key.
struct LoopKey {
	int animKeyIndex;
	int type;
};

struct ParkedAnim {
	float counter;
	int keyIndex; // -1 once finishAnimation has run
	int previousKey;
	int endKeyIndex;
};

float keyTime(const int* keyFrames, int index)
{
	return index >= 0 ? float(keyFrames[index]) : 0.0f;
}

/// Walk info keys the way PaniAnimator::checkConstantKeys does. Returning here
/// does not skip the caller's end-of-clip check.
void walkLoopKeys(ParkedAnim& anim, const int* keyFrames, const LoopKey* loops, int loopCount)
{
	for (; anim.keyIndex < loopCount; ++anim.keyIndex) {
		const float time = keyTime(keyFrames, loops[anim.keyIndex].animKeyIndex);
		if (anim.counter < time) {
			return;
		}
		if (loops[anim.keyIndex].type == 0) {
			anim.previousKey = anim.keyIndex;
		} else if (loops[anim.keyIndex].type == 1) {
			const float loopStart = keyTime(keyFrames, loops[anim.previousKey].animKeyIndex);
			anim.counter -= (time - loopStart);
			anim.keyIndex = anim.previousKey;
		}
	}
}

/// One PaniAnimator::animate step. A finished clip (keyIndex < 0) stays put.
void tickParked(ParkedAnim& anim, const int* keyFrames, const LoopKey* loops, int loopCount, float speed)
{
	if (anim.keyIndex < 0) {
		return;
	}
	const float startTime = keyTime(keyFrames, 0);
	const float endTime   = keyTime(keyFrames, anim.endKeyIndex);
	if (startTime > endTime) {
		return;
	}
	anim.counter += speed;
	if (loops && loopCount > 0) {
		walkLoopKeys(anim, keyFrames, loops, loopCount);
	}
	if (anim.counter >= endTime) {
		anim.counter  = endTime;
		anim.keyIndex = -1;
	}
}

} // namespace

int main()
{
	// The same-day regression. WaitInit stops the motion after the sink.
	// Submitting the mesh is required, and it is not the whole restore.
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

	// Intact and partial holds are the start of the saved stage clip.
	const PcWallRestoredPose intact = pc_wall_restored_pose(0, 3);
	check(intact.motionId == 0, "an intact wall stays on motion 0");
	check(!intact.holdAtEnd && !intact.markFinished && !intact.waypointOpen, "an intact wall stays at the start of its clip, waypoint closed");

	const PcWallRestoredPose partial = pc_wall_restored_pose(1, 3);
	check(partial.motionId == 1, "a partly broken wall holds motion 1");
	check(!partial.holdAtEnd && !partial.markFinished && !partial.waypointOpen, "a partial wall stays at the start of that clip, waypoint closed");

	const PcWallRestoredPose twoDown = pc_wall_restored_pose(2, 3);
	check(twoDown.motionId == 2 && !twoDown.holdAtEnd && !twoDown.waypointOpen, "two stages down still has one stage left");

	// Destroyed on a later day: the clip the sink actually finished, at the
	// playback end key, marked finished, waypoint open.
	const PcWallRestoredPose down = pc_wall_restored_pose(3, 3);
	check(down.motionId == 2, "a destroyed 3-stage wall rebuilds motion 2");
	check(down.motionId != 0, "a destroyed wall does not keep the idle motion startAI parked");
	check(down.holdAtEnd && down.markFinished && down.waypointOpen, "a destroyed wall freezes the finished pose and opens the waypoint");

	const PcWallRestoredPose oneStage = pc_wall_restored_pose(1, 1);
	check(oneStage.motionId == 0 && oneStage.holdAtEnd && oneStage.markFinished && oneStage.waypointOpen,
	      "a destroyed 1-stage wall freezes the end of motion 0");

	const PcWallRestoredPose over = pc_wall_restored_pose(4, 2);
	check(over.motionId == 1 && over.holdAtEnd && over.markFinished, "a stage past the end still uses the last clip");

	// Two playback keys. Live completion stores the end key (40). The old
	// day-reload seek stored 39 and left the animator unfinished.
	const int shortKeys[] = { 0, 40 };
	const int shortEnd    = pc_wall_playback_end_key_index(2);
	check(shortEnd == 1, "a 2-key clip plays through its last key");
	check(float(shortKeys[shortEnd]) == 40.0f, "live completion freezes time 40");
	check(oldDestroyedFrame(shortKeys, 2) == 39.0f, "the old reload seek stopped one frame earlier");
	check(float(shortKeys[shortEnd]) != oldDestroyedFrame(shortKeys, 2), "the rebuilt pose is the live end time");

	// More than nine keys: startMotion stops at key 8, not the file's last key.
	const int longKeys[] = { 0, 15, 30, 45, 60, 75, 90, 105, 120, 135, 150, 165 };
	const int longEnd    = pc_wall_playback_end_key_index(12);
	check(longEnd == 8, "a long clip stops at playback key 8");
	check(longKeys[longEnd] == 120, "live completion freezes key 8's time");
	check(oldDestroyedFrame(longKeys, 12) == 164.0f, "the old reload seek used the file's last key");
	check(longKeys[longEnd] != int(oldDestroyedFrame(longKeys, 12)), "key 8 is not the old last-key-minus-one seek");

	// A loop-end info key sits on the playback end. Seeking that time without
	// finishAnimation rewinds to the loop start. The finished key index does not.
	const LoopKey loop[] = { { 0, 0 }, { 1, 1 } };
	ParkedAnim live;
	live.counter     = 0.0f;
	live.keyIndex    = 0;
	live.previousKey = 0;
	live.endKeyIndex = shortEnd;
	tickParked(live, shortKeys, nullptr, 0, 40.0f);
	check(live.keyIndex < 0 && live.counter == 40.0f, "playing the sink to the end marks it finished at time 40");
	tickParked(live, shortKeys, loop, 2, 0.0f);
	check(live.counter == 40.0f && live.keyIndex < 0, "a later stopped tick leaves the finished sink where it ended");

	ParkedAnim reloaded;
	reloaded.counter     = float(shortKeys[shortEnd]);
	reloaded.keyIndex    = down.markFinished ? -1 : 0;
	reloaded.previousKey = 0;
	reloaded.endKeyIndex = shortEnd;
	tickParked(reloaded, shortKeys, loop, 2, 0.0f);
	check(reloaded.counter == live.counter && reloaded.keyIndex == live.keyIndex,
	      "a destroyed wall rebuilt on a later day matches the live finished sink");

	ParkedAnim unfinishedEnd;
	unfinishedEnd.counter     = float(shortKeys[shortEnd]);
	unfinishedEnd.keyIndex    = 0;
	unfinishedEnd.previousKey = 0;
	unfinishedEnd.endKeyIndex = shortEnd;
	tickParked(unfinishedEnd, shortKeys, loop, 2, 0.0f);
	check(unfinishedEnd.counter == 0.0f, "the end time without finishAnimation rewinds through the loop");
	check(unfinishedEnd.counter != live.counter, "that rewind is not the pose the sink left on screen");

	if (sFailures == 0) {
		std::printf("pc_wall_sides_test: all checks passed\n");
		return 0;
	}
	std::printf("pc_wall_sides_test: %d failure(s)\n", sFailures);
	return 1;
}
