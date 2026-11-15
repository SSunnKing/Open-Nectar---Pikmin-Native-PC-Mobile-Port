#include "pc_file_pointer.h"

// The draw puts "<" at (cx-92, cy-14) in a 20x28 box and "L" at (cx-92, cy+14)
// in a 10x14 box. ">" is at (cx+76, cy-14), "R" at (cx+78, cy+14). The box
// below covers both glyphs of one side and the same 8px the other menus use.

PcMenuClickRect pc_file_day_arrow_left(int cx, int cy)
{
	const PcMenuClickRect rect = { cx - 100, cy - 22, cx - 64, cy + 36 };
	return rect;
}

PcMenuClickRect pc_file_day_arrow_right(int cx, int cy)
{
	const PcMenuClickRect rect = { cx + 68, cy - 22, cx + 96, cy + 36 };
	return rect;
}

static bool inside(const PcMenuClickRect& r, float x, float y)
{
	return x >= float(r.minX) && x < float(r.maxX) && y >= float(r.minY) && y < float(r.maxY);
}

int pc_file_day_arrow_hit(float x, float y, int cx, int cy, bool leftOn, bool rightOn)
{
	if (leftOn && inside(pc_file_day_arrow_left(cx, cy), x, y)) return -1;
	if (rightOn && inside(pc_file_day_arrow_right(cx, cy), x, y)) return 1;
	return 0;
}

PcFileCopyPointer pc_file_copy_pointer(int sourceSlot, int currentTarget, int hitTag, bool click)
{
	PcFileCopyPointer result = { PC_FILE_COPY_NONE, -1 };
	if (hitTag < 0 || hitTag > 2 || hitTag == sourceSlot) return result;
	result.slot = hitTag;
	if (click) {
		result.action = PC_FILE_COPY_CONFIRM;
		return result;
	}
	if (hitTag != currentTarget) result.action = PC_FILE_COPY_MOVE;
	return result;
}
