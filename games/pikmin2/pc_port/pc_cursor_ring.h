#pragma once
/**
 * @file pc_cursor_ring.h
 * @brief When mouse-cursor mode draws a ground reticle for the whistle.
 *
 * Pikmin 1 draws the cursor mesh every frame, so the whistle target is a ring
 * on the ground. Pikmin 2's cursor is a J3D model (wakka) plus a particle
 * circle. In mouse-cursor mode the stick fed to that circle is zero, and the
 * model is easy to lose, so the port draws its own ring the same way the
 * lock-on ring is drawn. Classic mode keeps the disc cursor. A lock-on target
 * and a cutscene keep the rings off: those already replace the cursor.
 *
 * whistleState is NaviWhistle::WhistleState: 0 idle, 1 blowing, 2 fading.
 */

struct PcCursorRing {
	bool aim;     // small ring at the cursor, the thing the whistle will hit
	bool whistle; // the radius circle while the whistle is blowing or fading
};

inline PcCursorRing pc_cursor_ring(int controlMode, bool locked, bool inMovie, int whistleState)
{
	PcCursorRing ring = {};
	// PC_CONTROL_MOUSE_CURSOR. Classic (0) keeps the GameCube cursor.
	if (controlMode != 1 || locked || inMovie)
		return ring;
	ring.aim     = true;
	ring.whistle = whistleState == 1 || whistleState == 2;
	return ring;
}
