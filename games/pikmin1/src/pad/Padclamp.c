#include "Dolphin/pad.h"
#include "Dolphin/si.h"
#include <math.h>

typedef struct PADClampRegion {
	u8 minTrigger;
	u8 maxTrigger;
	s8 minStick;
	s8 maxStick;
	s8 xyStick;
	s8 minSubstick;
	s8 maxSubstick;
	s8 xySubstick;
} PADClampRegion;

static PADClampRegion ClampRegion = {
	// Triggers
	30,
	180,

	// Left stick
	15,
	72,
#if defined(PIKI_PC_PORT)
	// Quality-of-life choice to match upstream. The original octagon diagonal
	// is 40, about 0.789 of a run. 46 is the smallest corner that keeps every
	// point on this octagon at a full run, so the shape only clips beyond full.
	46,
#else
	40,
#endif

	// Right stick
	15,
#if defined(PIKI_PC_PORT)
	// Quality-of-life choice to match upstream. The original C-stick octagon
	// is 59 / 31, about 0.524 of a swarm on a full cardinal and about 0.394
	// on a full diagonal. The same beyond-full octagon as the main stick
	// (72 / 46) still clips the square corners and stays a full swarm.
	72,
	46,
#else
	59,
	31,
#endif
};

/**
 * @TODO: Documentation
 */
static void ClampStick(s8* px, s8* py, s8 max, s8 xy, s8 min)
{
	int x = *px;
	int y = *py;
	int signX;
	int signY;
	int d;

	if (0 <= x) {
		signX = 1;
	} else {
		signX = -1;
		x     = -x;
	}

	if (0 <= y) {
		signY = 1;
	} else {
		signY = -1;
		y     = -y;
	}

	if (x <= min) {
		x = 0;
	} else {
		x -= min;
	}
	if (y <= min) {
		y = 0;
	} else {
		y -= min;
	}

	if (x == 0 && y == 0) {
		*px = *py = 0;
		return;
	}

	if (xy * y <= xy * x) {
		d = xy * x + (max - xy) * y;
		if (xy * max < d) {
			x = (s8)(xy * max * x / d);
			y = (s8)(xy * max * y / d);
		}
	} else {
		d = xy * y + (max - xy) * x;
		if (xy * max < d) {
			x = (s8)(xy * max * x / d);
			y = (s8)(xy * max * y / d);
		}
	}

	*px = (s8)(signX * x);
	*py = (s8)(signY * y);
}

/**
 * @TODO: Documentation
 * @note UNUSED Size: 000044
 */
static void ClampTrigger(u8* trigger)
{
	if (*trigger <= ClampRegion.minTrigger) {
		*trigger = 0;
	} else {
		if (ClampRegion.maxTrigger < *trigger) {
			*trigger = ClampRegion.maxTrigger;
		}
		*trigger -= ClampRegion.minTrigger;
	}
}

#if defined(PIKI_PC_PORT)
// Quality-of-life choice to match upstream. GameCube PADClamp subtracts its
// dead zone (15) from every axis, so a half tilt (byte 64) falls to about
// 0.662 of a run and equal-axis diagonals 44-58 fall short of a full run.
// Only a stick at rest (both axes within the dead zone) is zeroed. A stick
// that has already left that square keeps its bytes.
static void PcRestGate(s8* px, s8* py, int min)
{
	int x = *px;
	int y = *py;
	if (x < 0) {
		x = -x;
	}
	if (y < 0) {
		y = -y;
	}
	if (x <= min && y <= min) {
		*px = 0;
		*py = 0;
	}
}
#endif

/**
 * @TODO: Documentation
 */
void PADClamp(PADStatus* status)
{
	int i;
	for (i = 0; i < SI_MAX_CHAN; i++, status++) {
		if (status->err != PAD_ERR_NONE) {
			continue;
		}

#if defined(PIKI_PC_PORT)
		PcRestGate(&status->stickX, &status->stickY, ClampRegion.minStick);
		PcRestGate(&status->substickX, &status->substickY, ClampRegion.minSubstick);
		// min is 0: the rest gate above owns the dead zone. Subtracting it
		// here would shrink every axis the way the GameCube routine does.
		ClampStick(&status->stickX, &status->stickY, ClampRegion.maxStick, ClampRegion.xyStick, 0);
		ClampStick(&status->substickX, &status->substickY, ClampRegion.maxSubstick, ClampRegion.xySubstick, 0);
#else
		ClampStick(&status->stickX, &status->stickY, ClampRegion.maxStick, ClampRegion.xyStick, ClampRegion.minStick);
		ClampStick(&status->substickX, &status->substickY, ClampRegion.maxSubstick, ClampRegion.xySubstick,
		           ClampRegion.minSubstick);
#endif
		ClampTrigger(&status->triggerLeft);
		ClampTrigger(&status->triggerRight);
	}
}
