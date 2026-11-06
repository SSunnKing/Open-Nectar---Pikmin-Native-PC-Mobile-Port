#ifndef PC_WALL_SIDES_H
#define PC_WALL_SIDES_H

/**
 * @brief Whether a breakable wall's model is submitted this frame.
 *
 * @param offCamera The wall's bounding sphere is outside the camera that is drawing.
 * @param completed BuildingItem::isCompleted(). True once the last stage has broken.
 * @param motionSpeed ItemCreature::mMotionSpeed. SluiceAI::WaitInit calls
 *        stopMotion() when the sink animation finishes, which stores 0.
 *
 * gt_soft.mod and gt_hard.mod sink the centre of the wall. The side pieces
 * stay in the last pose, and GameCube draws that pose, so the sides remain
 * after the wall is destroyed. A save has to park that same pose. Speed 0
 * and isCompleted() only decide that the mesh is submitted.
 *
 * The port used to skip the draw when @p completed was true and @p motionSpeed
 * was 0. That removed the sides of every destroyed wall (upstream issue #266).
 * An off-camera wall is still not drawn.
 *
 * The buried centre still writes depth. Sun shadows and ambient occlusion read
 * that depth and can darken water above a destroyed wall. Both effects are off
 * by default. The model stays submitted so the sides remain.
 *
 * Drawing the finished pose is not enough on a later day. Live destruction
 * freezes the playback end-key time and sets the animator key index to -1
 * (PaniAnimator::finishAnimation). BuildingItem::doLoad used to seek one frame
 * before the last anim key and leave the key index at 0. That is a different
 * sample, and a later animate(0) still walks info keys. doRestore also read
 * that key span from motion 0, the clip startAI had just started.
 */

/**
 * @brief End key index PaniAnimator::startMotion plays a oneshot through.
 *
 * startAnim is called with end key 8, then clamped to the last anim key.
 * A restored wall has to freeze that same key. The final key in the file can
 * sit past it.
 */
constexpr int PC_WALL_MOTION_END_KEY = 8;

inline int pc_wall_playback_end_key_index(int animKeyCount)
{
	if (animKeyCount <= 0) {
		return 0;
	}
	const int last = animKeyCount - 1;
	return PC_WALL_MOTION_END_KEY < last ? PC_WALL_MOTION_END_KEY : last;
}

/**
 * @brief Pose to rebuild when a wall is born again from a save.
 *
 * Intact and partial walls keep GameCube's startMotion(mCurrStage), which
 * holds the start of that clip. A destroyed wall holds the playback end key
 * of clip (mNumStages - 1) and is marked finished, matching the sink that
 * just played out.
 */
struct PcWallRestoredPose {
	int motionId;
	bool holdAtEnd;
	bool markFinished;
	bool waypointOpen;
};

inline PcWallRestoredPose pc_wall_restored_pose(int currStage, int numStages)
{
	PcWallRestoredPose pose;
	pose.motionId     = 0;
	pose.holdAtEnd    = false;
	pose.markFinished = false;
	pose.waypointOpen = false;
	if (numStages < 1) {
		return pose;
	}
	if (currStage < 0) {
		currStage = 0;
	}
	if (currStage >= numStages) {
		pose.motionId     = numStages - 1;
		pose.holdAtEnd    = true;
		pose.markFinished = true;
		pose.waypointOpen = true;
		return pose;
	}
	pose.motionId = currStage;
	return pose;
}

inline bool pc_wall_model_should_draw(bool offCamera, bool completed, float motionSpeed)
{
	// completed and motionSpeed are the inputs the old skip used. The finished
	// pose is drawn; only the camera cull removes the model.
	(void)completed;
	(void)motionSpeed;
	return !offCamera;
}

#endif // PC_WALL_SIDES_H
