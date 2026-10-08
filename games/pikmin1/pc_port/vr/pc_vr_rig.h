#ifndef PC_VR_RIG_H
#define PC_VR_RIG_H

// Where the player's tracked space sits in the game world.
//
// Tracking space is the headset's: metres, the runtime's stage or local
// origin. The world is the game's units (the captain is about 30 tall). The
// rig is the similarity between them,
//
//     world = focus + scale * rotate(turn, tracking - origin)
//
// with the terms chosen per mode:
//
//   third-person  origin is the head at recentre time, so that head position
//                 lands `distance` behind and `height` above the captain, who
//                 is the focus. Turning is snap turns of the world yaw about
//                 the captain; physically walking moves the view as usual.
//
//   tabletop      origin is the table: a point in front of and below the head
//                 at recentre time. The captain stands at its centre, and the
//                 level is shrunk around him by `tabletopScale`. The table can
//                 be dragged, turned and resized.
//
//   first-person  origin is the head at recentre again, landing on the
//                 captain's eyes, at a scale that makes the player about his
//                 size. The view rides him closely and turns in snap turns; his
//                 own model is left undrawn.
//
//   cutscene      (not tabletop) the cutscene camera is the focus, with no
//                 offset, so a head at the recentre position sees exactly what
//                 the director framed.
//
// On top of the mode sit three adjustments that apply to all of them:
//
//   lean back     `turn` is a yaw, with a pitch about the player's left-right
//                 axis (as of the recentre) inside it. The world tips up
//                 towards a player who is reclined, or who would rather look
//                 ahead than down at the captain or the table.
//
//   camera        forward and height move the player off the anchor by a fixed
//                 amount, in metres, cutscenes included.
//
//   tracking      the head the world is drawn from can keep its position fixed
//                 (3DoF), or its rotation too, instead of following the headset.
//
// The eye transforms never need the rig: the offset of an eye from the head is
// the same in both spaces once scaled, so the per-eye matrices come from the
// tracking poses alone (see eyeFromHead).

#include "pc_vr.h"
#include "pc_vr_math.h"

namespace pcvr {

/// How much of the headset's motion reaches the view.
enum class Tracking {
	SixDof,   // position and rotation
	ThreeDof, // rotation only: the head stays where it was at recentre
	Fixed,    // neither: the view is locked to the headset like a screen
};

struct RigSettings {
	PcVrRigMode mode              = PC_VR_RIG_THIRD_PERSON;
	float thirdPersonScale        = 100.0f; // world units per metre
	float thirdPersonDistance     = 300.0f; // world units behind the captain
	float thirdPersonHeight       = 220.0f; // world units above the captain
	float tabletopScale           = 1000.0f;
	float tabletopDistance        = 0.55f; // metres in front of the head
	float tabletopDrop            = 0.45f; // metres below the head
	float firstPersonScale        = 15.0f; // world units per metre: the player is about the captain's size
	float firstPersonEyeHeight    = 20.0f; // world units from the captain's feet to his eyes
	float firstPersonFollowSeconds = 0.08f; // tighter than third-person: the view is his head
	float snapTurnDegrees         = 30.0f;
	float followSeconds           = 0.25f; // captain-follow smoothing
	float leanBackDegrees         = 0.0f;  // tips the world up towards the player
	float cameraForward           = 0.0f;  // metres the player sits forward of the anchor
	float cameraHeight            = 0.0f;  // metres the player sits above it
	Tracking tracking             = Tracking::SixDof;
};

struct TrackedFrame {
	bool valid = false;
	Pose head;
};

struct SceneState {
	Vec3 cameraPos;
	Vec3 cameraForward { 0.0f, 0.0f, -1.0f };
	Vec3 focus;
	Vec3 focusForward { 0.0f, 0.0f, -1.0f };
	bool cutscene = false;
};

class Rig {
public:
	RigSettings settings;

	/// Advances follow smoothing and resolves a pending recentre. Once per frame,
	/// before any mapping below is read.
	void update(const TrackedFrame& tracked, const SceneState& scene, float dt);

	/// Request that the next update re-anchors on the head (and, in tabletop,
	/// puts the table back in front of it).
	void requestRecentre() { mRecentrePending = true; }

	/// Forget the world yaw too, so the next update takes it from the game camera.
	void reset()
	{
		mHasWorldYaw     = false;
		mRecentrePending = true;
		mFollowValid     = false;
	}

	void setMode(PcVrRigMode mode);
	/// Third-person, tabletop, first-person, and round again.
	void toggleMode();

	/// Swings the view round behind the captain, looking the way he looks --
	/// what the flat game's attention camera does. In tabletop it turns the
	/// level instead, so he faces away from the player.
	void faceFocus();

	/// Third-person: turn the world by one snap step (-1 left, +1 right).
	void snapTurn(int direction);
	/// Third-person: pull the view in (<1) or out (>1).
	void zoom(float factor);
	/// Tabletop: spin the level on the table.
	void rotateTable(float radians);
	/// Tabletop: move the table by a tracking-space delta.
	void dragTable(Vec3 delta);
	/// Tabletop: grow (>1) or shrink (<1) the miniature.
	void scaleTable(float factor);

	bool ready() const { return mReady; }

	/// The last update followed a cutscene's camera rather than the captain.
	bool inCutscene() const { return mWasCutscene; }

	float scale() const;
	Vec3 trackingToWorld(Vec3 tracking) const;
	Vec3 trackingDirectionToWorld(Vec3 direction) const;
	Pose headInWorld(const Pose& headTracking) const;

	/// The head the world is drawn from, in tracking space: the headset's own
	/// pose with whatever the tracking mode holds still put back where it was
	/// at recentre. Everything else located in tracking space (the hands) has
	/// to go through the same change to stay where the player sees it.
	Pose viewHead(const Pose& headTracking) const;

	/// Tracking position the rig anchors to: the head at recentre, or the
	/// table, less the camera offset.
	Vec3 anchorTracking() const;

	/// Near and far planes in world units for this scale.
	float nearPlane() const;
	float farPlane() const;

	/// World distance from the head to the anchor: 0 in third-person, where the
	/// head is the anchor.
	float fogOffset(const Pose& headTracking) const;

private:
	Vec3 followTarget(const SceneState& scene) const;
	float anchorYaw() const { return settings.mode == PC_VR_RIG_TABLETOP ? mTableYaw : mTrackingYaw; }
	Quat mapRotation(float worldYaw) const;

	bool mReady           = false;
	bool mRecentrePending = true;
	bool mHasWorldYaw     = false;
	bool mFollowValid     = false;
	bool mWasCutscene     = false;

	Vec3 mOrigin;           // tracking: head at recentre (third-person)
	float mTrackingYaw = 0; // tracking: head yaw at recentre
	Vec3 mTablePos;         // tracking: table centre
	float mTableYaw   = 0;  // tracking: table facing
	float mWorldYaw   = 0;  // world yaw the player faces
	float mZoom       = 1;  // third-person distance multiplier

	Vec3 mFocus;        // world point the anchor maps to (smoothed)
	Quat mMapRotation;  // tracking -> world: the yaw between them, around the lean
	Vec3 mFocusForward { 0.0f, 0.0f, -1.0f }; // the captain's facing, as of the last update
};

/// The rigid transform from the head's view space to an eye's, with the head
/// and eye poses in tracking space and translations scaled to world units.
Mat4 eyeFromHead(const Pose& headTracking, const Pose& eyeTracking, float scale);

} // namespace pcvr

#endif // PC_VR_RIG_H
