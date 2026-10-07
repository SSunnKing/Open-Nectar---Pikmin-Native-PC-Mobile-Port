#include "pc_mtx_interp.h"

#include <cmath>
#include <cstdio>

namespace {
int failures;
const float kPi = 3.14159265358979f;

void check(bool ok, const char* message)
{
	if (!ok) {
		std::fprintf(stderr, "FAIL: %s\n", message);
		++failures;
	}
}

bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

// R_y(deg) * diag(s) + t
void makeY(float m[3][4], float deg, float s, float tx, float ty, float tz)
{
	const float r = deg * kPi / 180.0f, c = std::cos(r), si = std::sin(r);
	const float rot[3][3] = { { c, 0.0f, si }, { 0.0f, 1.0f, 0.0f }, { -si, 0.0f, c } };
	for (int row = 0; row < 3; row++) {
		for (int col = 0; col < 3; col++) {
			m[row][col] = rot[row][col] * s;
		}
	}
	m[0][3] = tx;
	m[1][3] = ty;
	m[2][3] = tz;
}
} // namespace

int main()
{
	float a[3][4], b[3][4], out[3][4], expect[3][4];

	// 359 -> 1 degrees must pass through 0, not 180.
	makeY(a, 359.0f, 1.0f, 0.0f, 0.0f, 0.0f);
	makeY(b, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f);
	check(pc_mtx34_interp(a, b, 0.5f, out), "wraparound decomposes");
	check(near(out[0][0], 1.0f) && near(out[0][2], 0.0f) && near(out[2][0], 0.0f), "359->1 takes the short way to 0");

	// Translation and scale linear and independent of the rotation.
	makeY(a, 0.0f, 1.0f, 0.0f, 10.0f, 0.0f);
	makeY(b, 90.0f, 3.0f, 100.0f, 30.0f, -50.0f);
	check(pc_mtx34_interp(a, b, 0.5f, out), "rotation+scale decomposes");
	makeY(expect, 45.0f, 2.0f, 50.0f, 20.0f, -25.0f);
	bool same = true;
	for (int row = 0; row < 3; row++) {
		for (int col = 0; col < 4; col++) {
			same = same && near(out[row][col], expect[row][col], 1e-3f);
		}
	}
	check(same, "half way = 45 degrees, scale 2, midpoint translation");

	// End points reproduce the samples exactly enough to draw a tick as is.
	check(pc_mtx34_interp(a, b, 1.0f, out), "t=1 decomposes");
	same = true;
	for (int row = 0; row < 3; row++) {
		for (int col = 0; col < 4; col++) {
			same = same && near(out[row][col], b[row][col], 1e-4f);
		}
	}
	check(same, "t=1 gives the second sample");

	// A mirrored joint on both sides still interpolates; on one side only not.
	makeY(a, 10.0f, 1.0f, 0.0f, 0.0f, 0.0f);
	makeY(b, 20.0f, 1.0f, 0.0f, 0.0f, 0.0f);
	for (int row = 0; row < 3; row++) {
		a[row][0] = -a[row][0];
		b[row][0] = -b[row][0];
	}
	check(pc_mtx34_interp(a, b, 0.5f, out), "mirrored on both sides decomposes");
	makeY(b, 20.0f, 1.0f, 0.0f, 0.0f, 0.0f);
	check(!pc_mtx34_interp(a, b, 0.5f, out), "mirrored on one side is refused");

	// Zero scale (a hidden joint) is refused rather than producing NaN.
	makeY(a, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
	check(!pc_mtx34_interp(a, b, 0.5f, out), "zero scale is refused");

	// Camera cut detection.
	makeY(a, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f);
	makeY(b, 90.0f, 1.0f, 0.0f, 0.0f, 0.0f);
	check(near(pc_mtx34_rotation_delta(a, b), kPi / 2.0f, 1e-3f), "rotation delta 90 degrees");

	if (failures == 0) {
		std::puts("Matrix interpolation tests passed");
	}
	return failures == 0 ? 0 : 1;
}
