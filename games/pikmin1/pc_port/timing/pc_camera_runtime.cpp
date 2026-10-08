#include "pc_camera_runtime.h"

#include "Camera.h"
#include "pc_camera_snapshot.h"
#include "pc_render_phase.h"

#include <algorithm>
#include <vector>

namespace {
PcCameraSnapshotStore sSnapshots;
bool sEnabled = false;

struct CameraBackup {
	Camera* camera;
	Camera state;
};

std::vector<CameraBackup> sBackups;

PcCameraState readState(const Camera& camera)
{
	PcCameraState state {};
	state.position[0] = camera.mPosition.x;
	state.position[1] = camera.mPosition.y;
	state.position[2] = camera.mPosition.z;
	state.focus[0] = camera.mFocus.x;
	state.focus[1] = camera.mFocus.y;
	state.focus[2] = camera.mFocus.z;
	// Row 1 is the up basis actually used by makeLookat, including camera bank.
	state.up[0] = camera.mLookAtMtx.mMtx[1][0];
	state.up[1] = camera.mLookAtMtx.mMtx[1][1];
	state.up[2] = camera.mLookAtMtx.mMtx[1][2];
	state.fov = camera.mFov;
	state.aspect = camera.mAspectRatio;
	state.nearPlane = camera.mNear;
	state.farPlane = camera.mFar;
	state.blurAlpha = camera.mBlurAlpha;
	return state;
}

void applyState(Camera& camera, const PcCameraState& state)
{
	camera.mPosition.set(state.position[0], state.position[1], state.position[2]);
	camera.mFocus.set(state.focus[0], state.focus[1], state.focus[2]);
	camera.mFov = state.fov;
	camera.mAspectRatio = state.aspect;
	camera.mNear = state.nearPlane;
	camera.mFar = state.farPlane;
	camera.mBlurAlpha = state.blurAlpha;
	Vector3f up(state.up[0], state.up[1], state.up[2]);
	camera.calcLookAt(camera.mPosition, camera.mFocus, &up);
	camera.update(camera.mAspectRatio, camera.mFov, camera.mNear, camera.mFar);
}
}

void pc_camera_set_enabled(bool enabled)
{
	if (sEnabled == enabled) return;
	pc_camera_restore_frame();
	sEnabled = enabled;
	sSnapshots.clear();
}

bool pc_camera_is_enabled()
{
	return sEnabled;
}

void pc_camera_begin_authoritative_tick()
{
	if (!sEnabled) return;
	pc_camera_restore_frame();
	sSnapshots.beginTick();
}

void pc_camera_prepare_presentation(double alpha)
{
	if (!sEnabled) return;
	sSnapshots.preparePresentation(alpha);
}

void pc_camera_on_set(Camera* camera)
{
	if (!sEnabled || !camera) return;
	if (pc_render_is_authoritative()) {
		sSnapshots.capture(camera, readState(*camera));
		return;
	}

	const auto existing = std::find_if(sBackups.begin(), sBackups.end(),
	                                   [camera](const CameraBackup& backup) { return backup.camera == camera; });
	if (existing != sBackups.end()) return;

	PcCameraState state {};
	if (!sSnapshots.resolve(camera, state)) return;
	sBackups.push_back(CameraBackup { camera, *camera });
	applyState(*camera, state);
}

void pc_camera_restore_frame()
{
	for (auto backup = sBackups.rbegin(); backup != sBackups.rend(); ++backup) {
		*backup->camera = backup->state;
	}
	sBackups.clear();
}

void pc_camera_synchronize()
{
	if (sEnabled) sSnapshots.synchronize();
}

void pc_camera_clear()
{
	pc_camera_restore_frame();
	sSnapshots.clear();
}
