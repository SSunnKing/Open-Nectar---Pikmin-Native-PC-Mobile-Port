/**
 * @file pc_pad_clamp_test.cpp
 * @brief Checks GameCube PADClamp on already-scaled stick bytes.
 *
 * No window and no controller. On PC the dead zone only zeros a stick at
 * rest (both axes within 15) and does not subtract 15 from a stick that is
 * already moving. The octagon is 72 cardinal / 46 diagonal for the main
 * stick and the C-stick, which clips square corners beyond a full run or
 * full swarm and no further. The original GameCube values are a diagonal
 * corner of 40 (about 0.789 of a run) and a C-stick octagon of 59 / 31
 * (about 0.524 / 0.394 of a swarm). Triggers stay on the GameCube window,
 * 30..180 then shifted by 30.
 *
 * The run and swarm checks then apply the game's own stick math to those
 * bytes: Controller divides by 74, Navi::makeVelocity caps each main-stick
 * axis at ±1, and Navi::reviseController bins the angle (p35 = 10 degrees),
 * snaps to a full run at p48 = 0.85 and drops a neutral stick below p43 = 0.1.
 * Those are the NaviMgr.h defaults. Issue #15 read p48 = 0.85 from the disc's
 * naviMgr.bin as well. makeCStick uses the same revise step and does not cap
 * the C-stick axes first; swarm strength is the formula in Navi::makeCStick.
 */

#include "Dolphin/pad.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

int sFailures = 0;

void expect(int actual, int expected, const char* what)
{
	if (actual != expected) {
		std::printf("FAIL: %s -> got %d, expected %d\n", what, actual, expected);
		++sFailures;
	}
}

void expectNear(float actual, float expected, float tol, const char* what)
{
	if (!(actual >= expected - tol && actual <= expected + tol)) {
		std::printf("FAIL: %s -> got %.6f, expected %.6f\n", what, actual, expected);
		++sFailures;
	}
}

void fillDisconnected(PADStatus pads[4])
{
	std::memset(pads, 0, sizeof(PADStatus) * 4);
	for (int i = 0; i < 4; ++i) {
		pads[i].err = PAD_ERR_NO_CONTROLLER;
	}
}

struct StickCase {
	const char* name;
	s8 sx, sy, cx, cy;
	s8 esx, esy, ecx, ecy;
	u8 tl, tr, etl, etr;
};

void checkStick(const StickCase& c)
{
	PADStatus pads[4];
	fillDisconnected(pads);
	pads[0].err          = PAD_ERR_NONE;
	pads[0].button       = PAD_BUTTON_A;
	pads[0].stickX       = c.sx;
	pads[0].stickY       = c.sy;
	pads[0].substickX    = c.cx;
	pads[0].substickY    = c.cy;
	pads[0].triggerLeft  = c.tl;
	pads[0].triggerRight = c.tr;
	pads[0].analogA      = 9;
	pads[0].analogB      = 8;

	// A disconnected channel must keep whatever bytes it already holds.
	pads[1].stickX      = 127;
	pads[1].stickY      = -127;
	pads[1].substickX   = 127;
	pads[1].substickY   = 127;
	pads[1].triggerLeft = 255;
	pads[1].triggerRight = 255;
	pads[1].button      = PAD_BUTTON_B;

	PADClamp(pads);

	expect(pads[0].stickX, c.esx, c.name);
	expect(pads[0].stickY, c.esy, c.name);
	expect(pads[0].substickX, c.ecx, c.name);
	expect(pads[0].substickY, c.ecy, c.name);
	expect(pads[0].triggerLeft, c.etl, c.name);
	expect(pads[0].triggerRight, c.etr, c.name);
	expect(pads[0].button, PAD_BUTTON_A, c.name);
	expect(pads[0].analogA, 9, c.name);
	expect(pads[0].analogB, 8, c.name);
	expect(pads[0].err, PAD_ERR_NONE, c.name);

	expect(pads[1].stickX, 127, "disconnected stick X");
	expect(pads[1].stickY, -127, "disconnected stick Y");
	expect(pads[1].substickX, 127, "disconnected C-stick X");
	expect(pads[1].substickY, 127, "disconnected C-stick Y");
	expect(pads[1].triggerLeft, 255, "disconnected trigger L");
	expect(pads[1].triggerRight, 255, "disconnected trigger R");
	expect(pads[1].button, PAD_BUTTON_B, "disconnected buttons");
	expect(pads[1].err, PAD_ERR_NO_CONTROLLER, "disconnected err");
}

// Mirror of Controller::get*Stick / Navi::reviseController. Kept here so the
// host test does not link the game. The constants are the decomp defaults
// cited in the file header.
float roundAng(float x)
{
	if (x < 0.0f) {
		x += TAU;
	}
	if (x >= TAU) {
		x -= TAU;
	}
	return x;
}

float reviseMagnitude(float x, float z)
{
	constexpr float kFullRun  = 0.85f; // p48, mClampStickToMaxThreshold
	constexpr float kNeutral  = 0.1f;  // p43, mNeutralStickThreshold
	constexpr float kShakeDeg = 10.0f; // p35, mShakePreventionAngle

	float newMag         = sqrtf(x * x + z * z);
	float theta          = roundAng(atan2f(x, z));
	const float binWidth = (PI / 180.0f) * kShakeDeg;
	const float binned   = binWidth * static_cast<int>((theta + binWidth * 0.5f) / binWidth);
	float boost          = binned - static_cast<int>(binned / QUARTER_PI) * QUARTER_PI;
	theta                = sinf(QUARTER_PI - boost);
	boost                = sinf(boost) + theta;
	const float len      = sinf(QUARTER_PI) / boost;
	newMag *= 1.0f / len;
	if (newMag >= kFullRun) {
		newMag = 1.0f;
	}
	if (newMag < kNeutral) {
		newMag = 0.0f;
	}
	return newMag;
}

float capAxis(float v)
{
	if (v >= 1.0f) {
		return 1.0f;
	}
	if (v <= -1.0f) {
		return -1.0f;
	}
	return v;
}

void clampAxes(s8& sx, s8& sy, s8& cx, s8& cy)
{
	PADStatus pads[4];
	fillDisconnected(pads);
	pads[0].err       = PAD_ERR_NONE;
	pads[0].stickX    = sx;
	pads[0].stickY    = sy;
	pads[0].substickX = cx;
	pads[0].substickY = cy;
	PADClamp(pads);
	sx = pads[0].stickX;
	sy = pads[0].stickY;
	cx = pads[0].substickX;
	cy = pads[0].substickY;
}

// Main stick, including makeVelocity's per-axis cap at ±1.
float mainRun(s8 sx, s8 sy, bool clamp)
{
	s8 cx = 0;
	s8 cy = 0;
	if (clamp) {
		clampAxes(sx, sy, cx, cy);
	}
	return reviseMagnitude(capAxis(sx / 74.0f), capAxis(-sy / 74.0f));
}

// C-stick swarm strength from Navi::makeCStick. No per-axis cap.
float swarmStrength(s8 cx, s8 cy, bool clamp)
{
	s8 sx = 0;
	s8 sy = 0;
	if (clamp) {
		clampAxes(sx, sy, cx, cy);
	}
	const float mag      = reviseMagnitude(cx / 74.0f, -cy / 74.0f);
	const float strength = (mag - 0.05f) / (1.0f - 0.05f);
	if (strength >= 0.9f) {
		return 1.0f;
	}
	return 0.6f * (strength / 0.9f);
}

} // namespace

int main()
{
	// Rest is both axes within 15. A stick that has left that square is not shifted.
	const StickCase cases[] = {
	    {"neutral", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	    {"stick dead zone", 15, -15, 15, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	    {"just outside dead zone", 16, 0, 0, 16, 16, 0, 0, 16, 0, 0, 0, 0},
	    {"inside octagon", 30, 30, 20, 0, 30, 30, 20, 0, 0, 0, 0, 0},
	    // Cardinal cap is maxStick 72. Inputs already past a full run clip to it.
	    {"stick cardinal cap", 127, 0, 0, 0, 72, 0, 0, 0, 0, 0, 0, 0},
	    {"stick cardinal cap negative", -127, 0, 0, 0, -72, 0, 0, 0, 0, 0, 0, 0},
	    // s8's lowest value. Negating it in an int is 128, then the cap is 72.
	    {"stick raw -128", -128, 0, 0, 0, -72, 0, 0, 0, 0, 0, 0, 0},
	    {"stick at cap edge", 72, 0, 0, 0, 72, 0, 0, 0, 0, 0, 0, 0},
	    {"stick one above cap", 73, 0, 0, 0, 72, 0, 0, 0, 0, 0, 0, 0},
	    // Equal axes outside the corner clip to 46. The GameCube corner is 40.
	    {"stick diagonal", 127, 127, 0, 0, 46, 46, 0, 0, 0, 0, 0, 0},
	    {"stick diagonal signs", -127, 127, 0, 0, -46, 46, 0, 0, 0, 0, 0, 0},
	    // A circular stick's full diagonal is about 90 on each axis, not 127.
	    {"stick analog diagonal", 90, 90, 0, 0, 46, 46, 0, 0, 0, 0, 0, 0},
	    // First octagon branch: (90,20) -> (63,14) with corner 46, no dead-zone subtract.
	    {"stick octagon x-major", 90, 20, 0, 0, 63, 14, 0, 0, 0, 0, 0, 0},
	    // Second octagon branch: (20,127) -> (10,66).
	    {"stick octagon y-major", 20, 127, 0, 0, 10, 66, 0, 0, 0, 0, 0, 0},
	    // C-stick uses the same beyond-full octagon, 72 / 46.
	    {"c-stick cardinal", 0, 0, 127, 0, 0, 0, 72, 0, 0, 0, 0, 0},
	    {"c-stick diagonal", 0, 0, -127, -127, 0, 0, -46, -46, 0, 0, 0, 0},
	    // Triggers: <= 30 becomes 0; above that, cap at 180 then subtract 30.
	    {"trigger dead zone", 0, 0, 0, 0, 0, 0, 0, 0, 30, 31, 0, 1},
	    {"trigger cap", 0, 0, 0, 0, 0, 0, 0, 0, 255, 180, 150, 150},
	    {"trigger one past cap", 0, 0, 0, 0, 0, 0, 0, 0, 181, 179, 150, 149},
	};

	for (const StickCase& c : cases) {
		checkStick(c);
	}

	// Channel 3 is clamped too. The routine walks every SI channel.
	{
		PADStatus pads[4];
		fillDisconnected(pads);
		pads[3].err    = PAD_ERR_NONE;
		pads[3].stickX = 127;
		pads[3].stickY = 127;
		PADClamp(pads);
		expect(pads[3].stickX, 46, "channel 3 diagonal X");
		expect(pads[3].stickY, 46, "channel 3 diagonal Y");
		expect(pads[0].err, PAD_ERR_NO_CONTROLLER, "channel 0 still disconnected");
	}

	// A channel that is not PAD_ERR_NONE is left untouched, not only the
	// "no controller" code.
	{
		PADStatus pads[4];
		fillDisconnected(pads);
		pads[0].err          = PAD_ERR_NOT_READY;
		pads[0].stickX       = 127;
		pads[0].stickY       = -128;
		pads[0].triggerLeft  = 255;
		pads[0].triggerRight = 180;
		PADClamp(pads);
		expect(pads[0].stickX, 127, "not ready stick X");
		expect(pads[0].stickY, -128, "not ready stick Y");
		expect(pads[0].triggerLeft, 255, "not ready trigger L");
		expect(pads[0].triggerRight, 180, "not ready trigger R");
		expect(pads[0].err, PAD_ERR_NOT_READY, "not ready err");
	}

	// Run fraction and swarm strength, from the clamped bytes. The unclamped
	// column is what the no-op stub used to feed the same math.
	expectNear(mainRun(127, 0, true), 1.0f, 0.0f, "clamped full cardinal is a full run");
	expectNear(mainRun(0, 127, true), 1.0f, 0.0f, "clamped full up is a full run");
	expectNear(mainRun(-127, 0, true), 1.0f, 0.0f, "clamped full left is a full run");
	expectNear(mainRun(127, 0, false), 1.0f, 0.0f, "unclamped full cardinal is a full run");
	expectNear(mainRun(127, 127, false), 1.0f, 0.0f, "unclamped keyboard diagonal is a full run");
	expectNear(mainRun(127, 127, true), 1.0f, 0.0f, "clamped keyboard diagonal is a full run");
	expectNear(mainRun(-127, 127, true), 1.0f, 0.0f, "clamped keyboard diagonal, other quadrant");
	expectNear(mainRun(90, 90, true), 1.0f, 0.0f, "clamped full stick diagonal is a full run");
	expectNear(mainRun(-90, -90, true), 1.0f, 0.0f, "clamped full stick diagonal, other quadrant");
	// Shallowest full-rim point with corner 46: about 38 degrees, bytes (100, 78).
	expectNear(mainRun(100, 78, true), 1.0f, 0.0f, "clamped full stick off exact diagonal is a full run");
	// 64 is half of the positive byte range: SDL 16384 / 256.
	expectNear(mainRun(64, 0, false), 1.0f, 0.0f, "unclamped half tilt is a full run");
	expectNear(mainRun(64, 0, true), mainRun(64, 0, false), 0.0f, "clamped half tilt matches upstream");
	// Equal axes 44 through 58 were the band a subtracted dead zone dropped
	// below a full run. Each one must keep upstream's run.
	for (int byte = 44; byte <= 58; ++byte) {
		char what[64];
		std::snprintf(what, sizeof(what), "equal axes %d match upstream", byte);
		const s8 b = static_cast<s8>(byte);
		expectNear(mainRun(b, b, true), mainRun(b, b, false), 0.0f, what);
		expectNear(mainRun(b, b, true), 1.0f, 0.0f, what);
	}
	expectNear(swarmStrength(127, 0, false), 1.0f, 0.0f, "unclamped full C-stick is full swarm");
	expectNear(swarmStrength(127, 0, true), swarmStrength(127, 0, false), 0.0f, "clamped full C-stick matches upstream");
	expectNear(swarmStrength(-127, -127, false), 1.0f, 0.0f, "unclamped diagonal C-stick is full swarm");
	expectNear(swarmStrength(-127, -127, true), swarmStrength(-127, -127, false), 0.0f,
	           "clamped diagonal C-stick matches upstream");

	if (sFailures == 0) {
		std::printf("pc_pad_clamp_test: all checks passed\n");
		return 0;
	}
	std::printf("pc_pad_clamp_test: %d failure(s)\n", sFailures);
	return 1;
}
