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
 * after the wall is destroyed. The same draw is what a save restores: a
 * finished wall is parked on the last frame with motion stopped.
 *
 * The port used to skip the draw when @p completed was true and @p motionSpeed
 * was 0. That removed the sides of every destroyed wall (upstream issue #266).
 * An off-camera wall is still not drawn.
 *
 * The buried centre still writes depth. Sun shadows and ambient occlusion read
 * that depth and can darken water above a destroyed wall. Both effects are off
 * by default. The model stays submitted so the sides remain.
 */
inline bool pc_wall_model_should_draw(bool offCamera, bool completed, float motionSpeed)
{
	// completed and motionSpeed are the inputs the old skip used. The finished
	// pose is drawn; only the camera cull removes the model.
	(void)completed;
	(void)motionSpeed;
	return !offCamera;
}

#endif // PC_WALL_SIDES_H
