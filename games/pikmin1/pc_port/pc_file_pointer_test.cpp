/**
 * File-select day chevrons and copy-destination clicks, without a window.
 *
 * The rectangles are the ones ogScrFileSelectMgr draws beside the highlighted
 * card. A grey chevron (no further day) is not a hit. A copy click on the
 * source card does nothing; a click on another card confirms that destination.
 */

#include "pc_file_pointer.h"

#include <cstdio>

namespace {

int sFailures = 0;

void check(bool condition, const char* what)
{
	if (!condition) {
		std::printf("FAIL: %s\n", what);
		++sFailures;
	}
}

void testDayArrows()
{
	const int cx = 320;
	const int cy = 240;
	const PcMenuClickRect left  = pc_file_day_arrow_left(cx, cy);
	const PcMenuClickRect right = pc_file_day_arrow_right(cx, cy);

	// The drawn "<" and "L" sit inside the left box, ">" and "R" inside the right.
	check(pc_file_day_arrow_hit(float(cx - 92), float(cy - 14), cx, cy, true, true) == -1, "chevron <");
	check(pc_file_day_arrow_hit(float(cx - 92), float(cy + 14), cx, cy, true, true) == -1, "letter L");
	check(pc_file_day_arrow_hit(float(cx + 76), float(cy - 14), cx, cy, true, true) == 1, "chevron >");
	check(pc_file_day_arrow_hit(float(cx + 78), float(cy + 14), cx, cy, true, true) == 1, "letter R");

	// The card between the chevrons is not an arrow.
	check(pc_file_day_arrow_hit(float(cx), float(cy), cx, cy, true, true) == 0, "card centre is not an arrow");
	check(left.maxX <= right.minX, "the two chevrons do not overlap");

	// A grey chevron ignores the click. The live one still works.
	check(pc_file_day_arrow_hit(float(cx - 92), float(cy - 14), cx, cy, false, true) == 0, "grey left");
	check(pc_file_day_arrow_hit(float(cx + 76), float(cy - 14), cx, cy, true, false) == 0, "grey right");
	check(pc_file_day_arrow_hit(float(cx + 76), float(cy - 14), cx, cy, false, true) == 1, "live right beside a grey left");

	// Half-open edge: the max corner belongs to neither box.
	check(pc_file_day_arrow_hit(float(left.maxX), float(left.maxY), cx, cy, true, true) == 0, "left max edge is outside");
}

void testCopyDestination()
{
	// Copying file 0. The highlight starts on file 1.
	const PcFileCopyPointer hoverOther = pc_file_copy_pointer(0, 1, 2, false);
	check(hoverOther.action == PC_FILE_COPY_MOVE && hoverOther.slot == 2, "hover moves to the other card");

	const PcFileCopyPointer hoverSame = pc_file_copy_pointer(0, 1, 1, false);
	check(hoverSame.action == PC_FILE_COPY_NONE, "hover on the current destination stays");

	const PcFileCopyPointer hoverSource = pc_file_copy_pointer(0, 1, 0, false);
	check(hoverSource.action == PC_FILE_COPY_NONE && hoverSource.slot == -1, "hover on the source is not a destination");

	const PcFileCopyPointer clickSource = pc_file_copy_pointer(0, 1, 0, true);
	check(clickSource.action == PC_FILE_COPY_NONE, "click on the source does not confirm");

	const PcFileCopyPointer clickDest = pc_file_copy_pointer(0, 1, 2, true);
	check(clickDest.action == PC_FILE_COPY_CONFIRM && clickDest.slot == 2, "click on a destination confirms it");

	const PcFileCopyPointer clickCurrent = pc_file_copy_pointer(0, 1, 1, true);
	check(clickCurrent.action == PC_FILE_COPY_CONFIRM && clickCurrent.slot == 1, "click on the highlighted destination confirms");

	const PcFileCopyPointer miss = pc_file_copy_pointer(0, 1, -1, true);
	check(miss.action == PC_FILE_COPY_NONE && miss.slot == -1, "a click on empty space does nothing");

	const PcFileCopyPointer bad = pc_file_copy_pointer(1, 0, 7, true);
	check(bad.action == PC_FILE_COPY_NONE, "a tag outside the three cards is a miss");
}

} // namespace

int main()
{
	testDayArrows();
	testCopyDestination();
	if (sFailures) {
		std::printf("%d failed\n", sFailures);
		return 1;
	}
	std::printf("pc_file_pointer_test: all passed\n");
	return 0;
}
