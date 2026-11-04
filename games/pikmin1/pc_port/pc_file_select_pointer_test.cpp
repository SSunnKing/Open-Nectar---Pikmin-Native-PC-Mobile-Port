/**
 * Data-file select mouse cells, without a window and without a disc.
 *
 * The three cards are laid out as drawn on Kyle's window (bubble centres and
 * numbers measured from a screenshot, in the 640x480 menu graph), then mapped
 * to drawable pixels through the present letterbox and menu scale, as the
 * projected click targets are. Each card's cell must cover its bubble and
 * number, touch its neighbour at the midpoint with no dead gap, and stay off
 * the title and the New Ship's Log bar.
 */

#include "pc_menu_click.h"

#include <cmath>
#include <cstdio>

namespace {

int sFailures = 0;

void check(bool condition, const char* what, const char* frame)
{
	if (!condition) {
		std::printf("FAIL [%s]: %s\n", frame, what);
		++sFailures;
	}
}

struct Card {
	float cx, cy, r;   // bubble, 640x480 graph
	float numX, numY;  // slot number
};

// Slot 1 is selected (scale 1.0); slots 2 and 3 are at 0.7.
const Card kCards[3] = {
	{ 135.5f, 241.0f, 56.0f, 187.0f, 316.0f },
	{ 322.5f, 224.0f, 37.0f, 349.0f, 278.0f },
	{ 482.0f, 224.0f, 37.0f, 531.5f, 278.0f },
};
// New Ship's Log bar and the Choose a Data File title, 640x480 graph.
const float kBar[4]   = { 23.5f, 406.0f, 624.5f, 455.0f };
const float kTitle[4] = { 30.0f, 30.0f, 400.0f, 90.0f };
// Kyle's screenshot: the selection only moved to slot 2 here.
const float kKyleSwitchX = 269.0f;

int wideGraphW(int w, int h)
{
	const int v = int(lroundf(480.0f * float(w) / float(h)));
	return v < 640 ? 640 : v;
}

PcMenuClickFrame frameFor(int drawableW, int drawableH, float renderScale)
{
	const int graphW = wideGraphW(drawableW, drawableH);
	PcMenuClickFrame f {};
	f.drawableW = drawableW;
	f.drawableH = drawableH;
	f.graphW    = graphW;
	f.graphH    = 480;
	pc_menu_click_output_area(drawableW, drawableH, float(drawableW) / float(drawableH), &f.outX, &f.outY, &f.outW,
	                          &f.outH);
	f.targetW         = std::floor(float(f.outW) * renderScale);
	f.targetH         = std::floor(float(f.outH) * renderScale);
	const float scale = std::fmin(f.targetW / float(graphW), f.targetH / float(480));
	f.scaleX          = scale;
	f.scaleY          = scale;
	f.offsetX         = (f.targetW - float(graphW) * scale) * 0.5f;
	f.offsetY         = (f.targetH - float(480) * scale) * 0.5f;
	return f;
}

// A 640-wide layout point (centred, as pc_gfx_menu_shift_center draws it) to drawable pixels.
void toDrawable(const PcMenuClickFrame& f, float gx, float gy, float* px, float* py)
{
	gx += (float(f.graphW) - 640.0f) * 0.5f;
	const float rtX   = f.offsetX + gx * f.scaleX;
	const float rtYUp = f.offsetY + (float(f.graphH) - gy) * f.scaleY;
	*px               = float(f.outX) + rtX / f.targetW * float(f.outW);
	*py               = float(f.outY) + (1.0f - rtYUp / f.targetH) * float(f.outH);
}

PcMenuClickRect rectDrawable(const PcMenuClickFrame& f, float x0, float y0, float x1, float y1)
{
	float ax, ay, bx, by;
	toDrawable(f, x0, y0, &ax, &ay);
	toDrawable(f, x1, y1, &bx, &by);
	return PcMenuClickRect { int(std::floor(ax)), int(std::floor(ay)), int(std::ceil(bx)), int(std::ceil(by)) };
}

bool overlaps(PcMenuClickRect a, PcMenuClickRect b)
{
	return a.minX < b.maxX && b.minX < a.maxX && a.minY < b.maxY && b.minY < a.maxY;
}

int cellsContaining(const PcMenuClickRect* cells, int n, float x, float y)
{
	int count = 0;
	for (int i = 0; i < n; i++) {
		if (x >= cells[i].minX && x < cells[i].maxX && y >= cells[i].minY && y < cells[i].maxY) ++count;
	}
	return count;
}

void testFrame(const char* name, int drawableW, int drawableH, float renderScale)
{
	const PcMenuClickFrame f = frameFor(drawableW, drawableH, renderScale);

	// The targets the slots record: each card's NEW bubble and its empty bubble (same tag).
	PcMenuClickRect rects[6];
	int tags[6];
	for (int i = 0; i < 3; i++) {
		const Card& c   = kCards[i];
		rects[2 * i]    = rectDrawable(f, c.cx - c.r, c.cy - c.r, c.cx + c.r, c.cy + c.r);
		rects[2 * i + 1] = rectDrawable(f, c.cx - c.r * 0.8f, c.cy - c.r * 0.8f, c.cx + c.r * 0.8f, c.cy + c.r * 0.8f);
		tags[2 * i]      = i;
		tags[2 * i + 1]  = i;
	}
	// Recorded in draw order 2, 0, 1 to show the order does not matter.
	const int order[6] = { 4, 5, 0, 1, 2, 3 };
	PcMenuClickRect shuffled[6];
	int shuffledTags[6];
	for (int i = 0; i < 6; i++) {
		shuffled[i]     = rects[order[i]];
		shuffledTags[i] = tags[order[i]];
	}

	PcMenuClickRect cells[PC_MENU_CLICK_TARGET_MAX];
	int cellTags[PC_MENU_CLICK_TARGET_MAX];
	const int n = pc_menu_click_row_cells(shuffled, shuffledTags, 6, cells, cellTags, PC_MENU_CLICK_TARGET_MAX);
	check(n == 3, "three cards make three cells", name);
	if (n != 3) return;
	check(cellTags[0] == 0 && cellTags[1] == 1 && cellTags[2] == 2, "cells run left to right", name);

	float px, py;
	for (int i = 0; i < 3; i++) {
		toDrawable(f, kCards[i].cx, kCards[i].cy, &px, &py);
		check(pc_menu_click_row_pick(px, py, cells, cellTags, n, -1, 0.0f) == i, "a bubble centre picks its slot", name);
		toDrawable(f, kCards[i].numX, kCards[i].numY, &px, &py);
		check(pc_menu_click_row_pick(px, py, cells, cellTags, n, -1, 0.0f) == i, "a slot number picks its slot", name);
		// Every corner of the bubble is inside its own cell.
		const PcMenuClickRect b = rects[2 * i];
		check(b.minX >= cells[i].minX && b.maxX <= cells[i].maxX && b.minY >= cells[i].minY && b.maxY <= cells[i].maxY,
		      "a cell surrounds its whole bubble", name);
	}

	// Edge to edge: neighbours share the boundary, halfway between their bubbles.
	for (int i = 0; i + 1 < 3; i++) {
		check(cells[i].maxX == cells[i + 1].minX, "neighbouring cells touch with no gap", name);
		float a, b, y;
		toDrawable(f, kCards[i].cx, 0, &a, &y);
		toDrawable(f, kCards[i + 1].cx, 0, &b, &y);
		check(std::fabs(float(cells[i].maxX) - 0.5f * (a + b)) <= 2.0f, "the boundary is the midpoint of the bubbles",
		      name);
	}
	for (int i = 0; i < 3; i++) {
		for (int j = i + 1; j < 3; j++) check(!overlaps(cells[i], cells[j]), "cells do not overlap", name);
		check(!overlaps(cells[i], rectDrawable(f, kBar[0], kBar[1], kBar[2], kBar[3])),
		      "a cell stays off the New Ship's Log bar", name);
		check(!overlaps(cells[i], rectDrawable(f, kTitle[0], kTitle[1], kTitle[2], kTitle[3])),
		      "a cell stays off the title", name);
	}

	// Sweep left to right through the bubbles and the numbers: one slot at every pixel.
	const float sweepY[3] = { 224.0f, 241.0f, 278.0f };
	for (float gy : sweepY) {
		float y;
		toDrawable(f, 0, gy, &px, &y);
		bool always = true;
		int last    = -1;
		bool ordered = true;
		for (int x = cells[0].minX; x < cells[2].maxX; x++) {
			const int pick = pc_menu_click_row_pick(float(x), y, cells, cellTags, n, -1, 0.0f);
			if (pick < 0 || cellsContaining(cells, n, float(x), y) != 1) always = false;
			if (pick < last) ordered = false;
			if (pick >= 0) last = pick;
		}
		check(always, "a left-to-right sweep is over exactly one slot at every pixel", name);
		check(ordered && last == 2, "the sweep visits slots 1, 2, 3 in order", name);
	}

	// Kyle's report: between slot 1 and the point where it finally switched, the old
	// bubble-plus-16px box missed, so the selection stayed on slot 1. The cell switches
	// at the midpoint instead.
	const float midX = 0.5f * (kCards[0].cx + kCards[1].cx);
	toDrawable(f, midX + 10.0f, kCards[1].cy, &px, &py);
	check(midX + 10.0f < kKyleSwitchX, "the test point is left of where Kyle saw the switch", name);
	check(pc_menu_click_hit(px, py, rects, nullptr, 6, 16.0f) < 0, "the old box test missed in the gap", name);
	check(pc_menu_click_row_pick(px, py, cells, cellTags, n, 0, 16.0f * float(drawableW) / 1920.0f) == 1,
	      "past the midpoint the pointer selects slot 2", name);
	toDrawable(f, midX - 10.0f, kCards[1].cy, &px, &py);
	check(pc_menu_click_row_pick(px, py, cells, cellTags, n, -1, 0.0f) == 0, "before the midpoint it stays on slot 1",
	      name);

	// The selected card holds a few pixels past the shared edge; any other card does not.
	py = float(cells[1].minY + cells[1].maxY) * 0.5f;
	px = float(cells[1].minX) - 4.0f;
	check(pc_menu_click_row_pick(px, py, cells, cellTags, n, 1, 16.0f) == 1, "the selected card holds past its edge",
	      name);
	check(pc_menu_click_row_pick(px, py, cells, cellTags, n, 2, 16.0f) == 0, "an unselected card does not", name);

	// Empty space above and on the bar is nobody's.
	toDrawable(f, 320.0f, 60.0f, &px, &py);
	check(pc_menu_click_row_pick(px, py, cells, cellTags, n, 0, 16.0f) < 0, "the title selects nothing", name);
	toDrawable(f, 320.0f, 430.0f, &px, &py);
	check(pc_menu_click_row_pick(px, py, cells, cellTags, n, 0, 16.0f) < 0, "the Ship's Log bar selects nothing",
	      name);
}

void testCommitted()
{
	pc_menu_click_targets_begin(0);
	pc_menu_click_targets_add(0, PcMenuClickRect { 100, 100, 200, 200 });
	pc_menu_click_targets_add(1, PcMenuClickRect { 400, 120, 460, 180 });
	pc_menu_click_targets_end();
	int tag = -1;
	check(pc_menu_click_targets_hit_row(0, 290.0f, 150.0f, -1, 0.0f, &tag) && tag == 1,
	      "committed targets split at the midpoint (right)", "committed");
	check(pc_menu_click_targets_hit_row(0, 280.0f, 150.0f, -1, 0.0f, &tag) && tag == 0,
	      "committed targets split at the midpoint (left)", "committed");
	check(!pc_menu_click_targets_hit_row(1, 150.0f, 150.0f, -1, 0.0f, &tag), "another kind has no cells",
	      "committed");
	pc_menu_click_reset();
}

} // namespace

int main()
{
	testFrame("Kyle 962x587 window @2x", 1924, 1174, 1.0f);
	testFrame("Kyle window @1x", 962, 587, 1.0f);
	testFrame("2560x1080 ultrawide", 2560, 1080, 1.0f);
	testFrame("1440x900 HiDPI @2x", 2880, 1800, 1.0f);
	testFrame("1024x768 4:3", 1024, 768, 1.0f);
	testFrame("half render scale", 1924, 1174, 0.5f);
	testCommitted();
	if (sFailures == 0) {
		std::printf("pc_file_select_pointer_test: all checks passed\n");
		return 0;
	}
	std::printf("pc_file_select_pointer_test: %d failures\n", sFailures);
	return 1;
}
