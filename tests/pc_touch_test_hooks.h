// Observation hooks shared by pc_touch_test.cpp and pc_touch_test_stubs.cpp.
// The stubs stand in for the renderer, window, settings and gyro code that
// pc_touch.cpp calls, so the touch layer can run on a host with no GPU, window
// or disc. They record what the touch layer asked for.
#pragma once

struct TouchTestHooks {
	// Drawable size reported to the touch layer (the "screen").
	int drawableW = 1920;
	int drawableH = 1080;

	// Mod switches the layer reads to decide which bubbles exist.
	int lockOn = 0, charge = 0, firstPerson = 0, gyro = 0, bomb = 0, p2Selection = 0;

	// What the touch layer sent to the rest of the game.
	float zoomTotal = 0.0f;       // pc_window_add_touch_zoom
	float cameraDragTotal = 0.0f; // pc_window_add_camera_drag
	float cursorDx = 0.0f, cursorDy = 0.0f;
	int settingsToggles = 0;
	int gyroRecenters = 0;
	int lockOnPresses = 0, chargePresses = 0, firstPersonPresses = 0, bombPresses = 0;
	int overlayDraws = 0; // must stay 0: the tests never draw

	void resetCounters()
	{
		zoomTotal = cameraDragTotal = cursorDx = cursorDy = 0.0f;
		settingsToggles = gyroRecenters = 0;
		lockOnPresses = chargePresses = firstPersonPresses = bombPresses = 0;
	}
};

TouchTestHooks& touch_test_hooks();
