#include "pc_camera_snapshot.h"

#include <cmath>
#include <cstdio>
#include <limits>

static int failures = 0;

static void check(bool condition, const char* message)
{
	if (!condition) {
		std::fprintf(stderr, "FAIL: %s\n", message);
		++failures;
	}
}

static PcCameraState cameraState(float x)
{
	return { { x, 2.0f, 3.0f }, { x + 10.0f, 5.0f, 6.0f }, { 0.0f, 2.0f, 0.0f },
		     60.0f + x * 0.1f, 16.0f / 9.0f, 1.0f, 5000.0f, x };
}

int main()
{
	PcCameraSnapshotStore store;
	int gameplayCamera = 0;
	int movieCamera = 0;
	PcCameraState output {};

	store.beginTick();
	check(store.capture(&gameplayCamera, cameraState(0.0f)), "valid gameplay camera must be captured");
	store.beginTick();
	check(store.capture(&movieCamera, cameraState(100.0f)), "new movie camera must be captured");
	check(store.capture(&gameplayCamera, cameraState(20.0f)), "gameplay camera order may change");
	store.preparePresentation(0.25);
	check(store.resolve(&gameplayCamera, output) && std::fabs(output.position[0] - 5.0f) < 1e-5f,
	      "camera position must interpolate by stable camera identity");
	check(std::fabs(output.fov - 60.5f) < 1e-5f && std::fabs(output.blurAlpha - 5.0f) < 1e-5f,
	      "projection and blur fields must interpolate");
	check(std::fabs(output.up[1] - 1.0f) < 1e-5f, "interpolated up vector must be normalized");
	check(store.resolve(&movieCamera, output) && std::fabs(output.position[0] - 100.0f) < 1e-5f,
	      "newly selected camera must use current state without cross-camera blend");

	store.beginTick();
	store.capture(&gameplayCamera, cameraState(1000.0f), true);
	store.preparePresentation(0.1);
	check(store.resolve(&gameplayCamera, output) && std::fabs(output.position[0] - 1000.0f) < 1e-5f,
	      "camera cut must suppress interpolation");

	PcCameraState invalid = cameraState(0.0f);
	invalid.fov = std::numeric_limits<float>::quiet_NaN();
	check(!store.capture(&movieCamera, invalid), "non-finite camera state must be rejected");
	invalid = cameraState(0.0f);
	invalid.farPlane = invalid.nearPlane;
	check(!store.capture(&movieCamera, invalid), "invalid clip range must be rejected");

	store.clear();
	check(store.currentSize() == 0 && store.previousSize() == 0, "clear must remove camera generations");
	std::printf("PcCameraSnapshotStore: %s\n", failures ? "FAILED" : "all tests passed");
	return failures ? 1 : 0;
}
