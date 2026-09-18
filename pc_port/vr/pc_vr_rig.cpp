#include "pc_vr_rig.h"

#include <algorithm>

namespace pcvr {

namespace {

constexpr float kPi = 3.14159265359f;

// A focus that jumps further than this in one frame is a teleport (a new
// level, the captain respawning), not movement, and is not smoothed.
constexpr float kFollowSnapDistance = 1500.0f;

} // namespace

void Rig::setMode(PcVrRigMode mode)
{
	if (settings.mode == mode) return;
	settings.mode = mode;
	mFollowValid  = false;
}

void Rig::faceFocus()
{
	// Look the way the captain looks: in third-person that puts the view behind
	// him, and in tabletop it turns the level so he faces away from the player.
	// Either way it is a cut, not a sweep: the follow snaps instead of gliding.
	const Vec3 flat { mFocusForward.x, 0.0f, mFocusForward.z };
	if (length(flat) < 1e-4f) return;
	mWorldYaw    = yawOfDirection(normalize(flat));
	mHasWorldYaw = true;
	mFollowValid = false;
}

void Rig::snapTurn(int direction)
{
	if (direction == 0) return;
	// Positive yaw turns left, so a turn to the right lowers it.
	mWorldYaw    = wrapAngle(mWorldYaw - float(direction) * settings.snapTurnDegrees * kPi / 180.0f);
	mFollowValid = false; // snap the view round the captain; gliding there is what makes turns sickening
}

void Rig::zoom(float factor)
{
	mZoom        = std::clamp(mZoom * factor, 0.25f, 4.0f);
	mFollowValid = false;
}

void Rig::rotateTable(float radians) { mWorldYaw = wrapAngle(mWorldYaw + radians); }

void Rig::dragTable(Vec3 delta) { mTablePos = mTablePos + delta; }

void Rig::scaleTable(float factor) { settings.tabletopScale = std::clamp(settings.tabletopScale * factor, 150.0f, 8000.0f); }

Vec3 Rig::followTarget(const SceneState& scene) const
{
	if (settings.mode == PC_VR_RIG_TABLETOP) return scene.focus;
	// The head at recentre sits behind the captain along the direction the
	// player faces in the world: local +Z is behind, as forward is -Z.
	const Vec3 behind { 0.0f, settings.thirdPersonHeight * mZoom, settings.thirdPersonDistance * mZoom };
	return scene.focus + rotate(yawQuat(mWorldYaw), behind);
}

void Rig::update(const TrackedFrame& tracked, const SceneState& scene, float dt)
{
	if (!tracked.valid) return;

	mFocusForward = scene.focusForward;

	if (mRecentrePending) {
		mRecentrePending    = false;
		const float headYaw = yawOf(tracked.head.q);
		mOrigin             = tracked.head.p;
		mTrackingYaw        = headYaw;
		mTableYaw           = headYaw;
		mTablePos = tracked.head.p + forwardOnGround(headYaw) * settings.tabletopDistance + Vec3 { 0.0f, -settings.tabletopDrop, 0.0f };
		if (!mHasWorldYaw) {
			// Start out facing the way the flat game's camera was, so the first
			// thing seen is what the player was already looking at.
			mWorldYaw    = yawOfDirection(scene.cameraForward);
			mHasWorldYaw = true;
		}
		mFollowValid = false;
	}

	if (settings.mode == PC_VR_RIG_THIRD_PERSON && scene.cutscene) {
		// Put the head where the director put the camera, facing the same way.
		mFocus       = scene.cameraPos;
		mMapYaw      = yawOfDirection(scene.cameraForward) - mTrackingYaw;
		mWasCutscene = true;
		mReady       = true;
		return;
	}
	if (mWasCutscene) {
		mWasCutscene = false;
		mFollowValid = false;
	}

	const Vec3 target = followTarget(scene);
	if (!mFollowValid || length(target - mFocus) > kFollowSnapDistance) {
		mFocus       = target;
		mFollowValid = true;
	} else {
		const float blend = 1.0f - std::exp(-std::max(dt, 0.0f) / std::max(settings.followSeconds, 0.01f));
		mFocus            = mFocus + (target - mFocus) * blend;
	}

	mMapYaw = settings.mode == PC_VR_RIG_TABLETOP ? mWorldYaw - mTableYaw : mWorldYaw - mTrackingYaw;
	mReady  = true;
}

float Rig::scale() const { return settings.mode == PC_VR_RIG_TABLETOP ? settings.tabletopScale : settings.thirdPersonScale; }

Vec3 Rig::trackingToWorld(Vec3 tracking) const
{
	return mFocus + rotate(yawQuat(mMapYaw), tracking - anchorTracking()) * scale();
}

Vec3 Rig::trackingDirectionToWorld(Vec3 direction) const { return rotate(yawQuat(mMapYaw), direction); }

Pose Rig::headInWorld(const Pose& headTracking) const
{
	return { normalize(yawQuat(mMapYaw) * headTracking.q), trackingToWorld(headTracking.p) };
}

// 10 cm and 60 m, whatever the scale. The ratio is what the depth buffer
// cares about, and 1:600 is well inside what 24 bits resolve over the half of
// the range the GameCube projection uses.
float Rig::nearPlane() const { return std::max(1.0f, 0.1f * scale()); }
float Rig::farPlane() const { return std::max(10000.0f, 60.0f * scale()); }

float Rig::fogOffset(const Pose& headTracking) const
{
	if (settings.mode != PC_VR_RIG_TABLETOP) return 0.0f;
	return length(headTracking.p - mTablePos) * scale();
}

Mat4 eyeFromHead(const Pose& headTracking, const Pose& eyeTracking, float scale)
{
	// The rig's rotation and translation cancel between head and eye: both go
	// through the same rigid part, so only their relative pose matters, with
	// the translation in world units.
	const Pose head { headTracking.q, headTracking.p * scale };
	const Pose eye { eyeTracking.q, eyeTracking.p * scale };
	return matrixFromPose(compose(inverse(eye), head));
}

} // namespace pcvr
