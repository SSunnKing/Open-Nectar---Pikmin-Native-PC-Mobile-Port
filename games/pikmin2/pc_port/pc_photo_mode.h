#ifndef PC_PHOTO_MODE_H
#define PC_PHOTO_MODE_H

/*
 * Photo mode: freeze the world, then fly the camera anywhere.
 *
 * The freeze reuses the game's own gates. gameflow.mPauseAll stops every
 * manager -- pikiMgr, tekiMgr, pelletMgr, plantMgr, workObjectMgr, bossMgr --
 * and all of their postUpdates. It does not stop the captain or the day clock,
 * which hang off gameflow.mIsUIOverlayActive instead, so photo mode sets both
 * and the world stands completely still.
 *
 * cameraMgr->update() runs regardless of either flag, so the pose is written
 * over the top of it once it has run rather than by suppressing it. That keeps
 * the game's own camera state coherent for when photo mode ends.
 *
 * The pose maths lives here, away from the game headers, so it can be tested
 * without a stage loaded.
 */

#ifdef __cplusplus
extern "C" {
#endif

/// True while the world is frozen and the camera is under manual control.
int pc_photo_mode_active(void);

/// Edge-detected toggle key. Call once per frame.
int pc_photo_mode_poll_toggle(void);
/// Toggle from elsewhere (touch button): consumed by the next poll.
void pc_photo_mode_request_toggle(void);
/// Touch input, same axes as the keys: move (stick, -1..1, y forward) is
/// held state set every frame; look deltas (fraction of screen height) are
/// accumulated by drags and consumed by the next update.
void pc_photo_mode_set_touch_move(float x, float y);
void pc_photo_mode_add_touch_look(float dx, float dy);

/// Seeds the free camera from the pose the game camera currently holds.
void pc_photo_mode_enter(float posX, float posY, float posZ,
                         float pitch, float yaw);

void pc_photo_mode_exit(void);

/**
 * @brief Advances the free camera and returns the pose to write to the camera.
 *
 * Angles follow the game's own convention, taken from NCamera::makeCamera and
 * NPolar3f::output: rotation.x is the polar inclination less a quarter turn,
 * rotation.y is the azimuth, and the camera looks down the negative of the
 * vector those two describe.
 */
void pc_photo_mode_update(float dt,
                          float* posX, float* posY, float* posZ,
                          float* pitch, float* yaw, float* roll);

/**
 * @brief The direction the camera looks, for a given pitch and yaw.
 *
 * Public so a test can pin the convention down: getting it wrong points the
 * controls backwards, and that is tedious to diagnose in-game.
 */
void pc_photo_mode_forward_vector(float pitch, float yaw,
                                  float* outX, float* outY, float* outZ);

/// The camera's right-hand direction on the horizontal plane.
void pc_photo_mode_right_vector(float yaw, float* outX, float* outY, float* outZ);

/**
 * @brief The inverse of pc_photo_mode_forward_vector.
 *
 * Used on entry, to carry on from wherever the gameplay camera was looking
 * rather than snapping to some arbitrary heading.
 */
void pc_photo_mode_angles_from_forward(float fx, float fy, float fz,
                                       float* outPitch, float* outYaw);

#ifdef __cplusplus
}
#endif

#endif // PC_PHOTO_MODE_H
