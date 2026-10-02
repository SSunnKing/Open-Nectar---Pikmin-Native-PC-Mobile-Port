// The camera convention here is derived from NCamera::makeCamera and
// NPolar3f::output, not guessed. If it is wrong the controls point the wrong
// way, which is slow and annoying to diagnose with a stage loaded -- so it is
// pinned down here instead.
#include "pc_photo_mode.h"

#include <cmath>
#include <cstdio>

namespace {
int failures = 0;

void check(bool ok, const char* what)
{
	if (!ok) {
		std::printf("FAIL: %s\n", what);
		failures++;
	}
}

bool near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

const float kPi = 3.14159265358979323846f;
}

int main()
{
	float x = 0.0f, y = 0.0f, z = 0.0f;

	// Level, facing zero azimuth. NPolar3f::output puts azimuth zero along +Z,
	// and that vector runs from the target to the camera, so the camera looks
	// down -Z.
	pc_photo_mode_forward_vector(0.0f, 0.0f, &x, &y, &z);
	check(near(x, 0.0f) && near(y, 0.0f) && near(z, -1.0f), "yaw 0 looks down -Z");

	// A quarter turn of azimuth swings the look direction to -X.
	pc_photo_mode_forward_vector(0.0f, kPi * 0.5f, &x, &y, &z);
	check(near(x, -1.0f) && near(y, 0.0f) && near(z, 0.0f), "yaw +90 looks down -X");

	// Pitch is the inclination less a quarter turn, so a positive pitch looks up.
	pc_photo_mode_forward_vector(kPi * 0.5f, 0.0f, &x, &y, &z);
	check(near(y, 1.0f), "positive pitch looks up");
	pc_photo_mode_forward_vector(-kPi * 0.5f, 0.0f, &x, &y, &z);
	check(near(y, -1.0f), "negative pitch looks down");

	// Always unit length, or movement speed would vary with where you look.
	for (int i = 0; i < 16; i++) {
		const float pitch = -1.2f + 0.15f * i;
		const float yaw   = -3.0f + 0.4f * i;
		pc_photo_mode_forward_vector(pitch, yaw, &x, &y, &z);
		check(near(std::sqrt(x * x + y * y + z * z), 1.0f), "forward is unit length");
		pc_photo_mode_right_vector(yaw, &x, &y, &z);
		check(near(std::sqrt(x * x + y * y + z * z), 1.0f), "right is unit length");
		check(near(y, 0.0f), "right stays level");
	}

	// Right must be perpendicular to the flattened forward, and on the correct
	// side: strafing right from a level camera has to move right on screen.
	for (int i = 0; i < 8; i++) {
		const float yaw = -3.0f + 0.8f * i;
		float fx, fy, fz, rx, ry, rz;
		pc_photo_mode_forward_vector(0.0f, yaw, &fx, &fy, &fz);
		pc_photo_mode_right_vector(yaw, &rx, &ry, &rz);
		check(near(fx * rx + fz * rz, 0.0f), "right is perpendicular to forward");
		// forward x right should point down (-Y) for a right-handed frame with
		// Y up; the cross product's Y term is what says which side we are on.
		const float crossY = fz * rx - fx * rz;
		check(crossY < 0.0f, "right is on the right, not the left");
	}

	// Entering photo mode reads the live camera's direction and turns it back
	// into angles. If that round trip is not exact the view snaps the moment
	// you press the key, which reads as a bug even though nothing moved.
	for (int i = 0; i < 24; i++) {
		const float pitch = -1.4f + 0.12f * i;
		const float yaw   = -3.0f + 0.26f * i;
		float fx, fy, fz;
		pc_photo_mode_forward_vector(pitch, yaw, &fx, &fy, &fz);
		float backPitch = 0.0f, backYaw = 0.0f;
		pc_photo_mode_angles_from_forward(fx, fy, fz, &backPitch, &backYaw);
		float rx, ry, rz;
		pc_photo_mode_forward_vector(backPitch, backYaw, &rx, &ry, &rz);
		// Compare the directions rather than the angles: yaw wraps, and two
		// different angles can name the same heading.
		check(near(rx, fx) && near(ry, fy) && near(rz, fz),
		      "angles survive the round trip through a direction");
	}

	// A direction that is not unit length must still work: it arrives as the
	// difference between two camera points, at whatever scale those happen to be.
	{
		float p = 0.0f, y2 = 0.0f;
		pc_photo_mode_angles_from_forward(0.0f, 0.0f, -250.0f, &p, &y2);
		check(near(p, 0.0f) && near(y2, 0.0f), "unnormalised input is handled");
		pc_photo_mode_angles_from_forward(0.0f, 0.0f, 0.0f, &p, &y2);
		check(near(p, 0.0f) && near(y2, 0.0f), "a zero direction does not produce NaN");
	}

	if (failures == 0) std::printf("pc_photo_mode_test: all checks passed\n");
	return failures == 0 ? 0 : 1;
}
