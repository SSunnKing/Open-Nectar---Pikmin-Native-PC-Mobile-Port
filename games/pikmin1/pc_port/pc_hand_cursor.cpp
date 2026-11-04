#include "pc_hand_cursor.h"

static PcHandCursorSetter sSetter = nullptr;
static int sShape                 = PC_HAND_CURSOR_ARROW;

static void pc_hand_cursor_apply(int shape)
{
	if (shape == sShape) {
		return;
	}
	sShape = shape;
	if (sSetter) {
		sSetter(shape);
	}
}

void pc_hand_cursor_init(PcHandCursorSetter setter)
{
	sSetter = setter;
	sShape  = PC_HAND_CURSOR_ARROW;
}

void pc_hand_cursor_update(bool pointerFree, bool wantHand)
{
	pc_hand_cursor_apply(pointerFree && wantHand ? PC_HAND_CURSOR_HAND : PC_HAND_CURSOR_ARROW);
}

void pc_hand_cursor_reset(void)
{
	pc_hand_cursor_apply(PC_HAND_CURSOR_ARROW);
}

int pc_hand_cursor_shape(void)
{
	return sShape;
}
