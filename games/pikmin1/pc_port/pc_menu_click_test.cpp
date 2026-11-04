/**
 * Title-screen and main-menu clicks, without a window and without a disc.
 *
 * The rectangles stand in for the panes those screens already draw. A click
 * inside one activates that button. A click on any other screen does not.
 * Nothing here reads the control mode: Classic stays the default.
 */

#include "pc_menu_click.h"

#include <cmath>
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

const PcMenuClickRect kRoot = { 0, 0, 640, 480 };

// Two prompts on the title screen. Both mean Press Start: that screen has one action.
const PcMenuClickRect kTitleButtons[] = {
	{ 220, 360, 420, 400 },
	{ 250, 410, 390, 440 },
};

// Main menu rows, top to bottom. The middle row is inactive (a locked option).
const PcMenuClickRect kMainButtons[] = {
	{ 200, 140, 440, 170 },
	{ 200, 180, 440, 210 },
	{ 200, 220, 440, 250 },
};
const int kMainActive[] = { 1, 0, 1 };

bool near(float got, float want) { return std::fabs(got - want) < 0.5f; }

void menuScale(int targetW, int targetH, int graphW, int graphH, float* scaleX, float* scaleY, float* offsetX, float* offsetY)
{
	const float scale = std::fmin(float(targetW) / float(graphW), float(targetH) / float(graphH));
	*scaleX           = scale;
	*scaleY           = scale;
	*offsetX          = (float(targetW) - float(graphW) * scale) * 0.5f;
	*offsetY          = (float(targetH) - float(graphH) * scale) * 0.5f;
}

PcMenuClickFrame frameFor(int drawableW, int drawableH, float renderedAspect, int graphW, int graphH)
{
	PcMenuClickFrame frame {};
	frame.drawableW = drawableW;
	frame.drawableH = drawableH;
	frame.graphW    = graphW;
	frame.graphH    = graphH;
	pc_menu_click_output_area(drawableW, drawableH, renderedAspect, &frame.outX, &frame.outY, &frame.outW, &frame.outH);
	frame.targetW = float(frame.outW);
	frame.targetH = float(frame.outH);
	menuScale(frame.outW, frame.outH, graphW, graphH, &frame.scaleX, &frame.scaleY, &frame.offsetX, &frame.offsetY);
	return frame;
}

void testOutputArea()
{
	int x = 0;
	int y = 0;
	int w = 0;
	int h = 0;
	const float wide = 16.0f / 9.0f;
	pc_menu_click_output_area(1920, 1080, wide, &x, &y, &w, &h);
	check(x == 0 && y == 0 && w == 1920 && h == 1080, "16:9 window matches a 16:9 picture");

	pc_menu_click_output_area(1920, 1200, wide, &x, &y, &w, &h);
	check(x == 0 && w == 1920 && h == 1080 && y == 60, "a taller window letterboxes a 16:9 picture");

	pc_menu_click_output_area(1920, 1080, 4.0f / 3.0f, &x, &y, &w, &h);
	check(y == 0 && h == 1080 && w == 1440 && x == 240, "a wide window pillarboxes a 4:3 picture");

	pc_menu_click_output_area(1280, 720, wide, &x, &y, &w, &h);
	check(x == 0 && y == 0 && w == 1280 && h == 720, "a smaller 16:9 window still fills");
}

void testMapCenters()
{
	const int graphW = int(lroundf(480.0f * (16.0f / 9.0f)));
	check(graphW == 853, "wide menu graph width");
	const PcMenuClickFrame hd  = frameFor(1920, 1080, 16.0f / 9.0f, graphW, 480);
	const PcMenuClickFrame hd2 = frameFor(1280, 720, 16.0f / 9.0f, graphW, 480);
	float x                     = 0.0f;
	float y                     = 0.0f;
	float x2                    = 0.0f;
	float y2                    = 0.0f;
	check(pc_menu_click_map_to_graph(hd, 0.5f, 0.5f, &x, &y), "1080p center converts");
	check(pc_menu_click_map_to_graph(hd2, 0.5f, 0.5f, &x2, &y2), "720p center converts");
	check(near(x, 426.5f) && near(y, 240.0f), "1080p center is the wide graph center");
	check(near(x2, x) && near(y2, y), "720p and 1080p centers land on the same graph point");

	const PcMenuClickFrame pillar = frameFor(1920, 1080, 16.0f / 9.0f, 640, 480);
	check(pc_menu_click_map_to_graph(pillar, 0.5f, 0.5f, &x, &y), "4:3 menu center converts");
	check(near(x, 320.0f) && near(y, 240.0f), "window center is the 4:3 menu center");
	check(pc_menu_click_map_to_graph(pillar, 10.0f / 1920.0f, 0.5f, &x, &y), "left bar converts");
	check(x < 0.0f, "a click in the side bar is outside the 4:3 menu");

	const PcMenuClickFrame tall = frameFor(1080, 1920, 16.0f / 9.0f, graphW, 480);
	check(tall.outY > 0 && tall.outH < 1920, "a portrait window bars a 16:9 picture");
	const float bandCenter = (float(tall.outY) + float(tall.outH) * 0.5f) / 1920.0f;
	check(pc_menu_click_map_to_graph(tall, 0.5f, bandCenter, &x, &y), "portrait band center converts");
	check(near(x, 426.5f) && near(y, 240.0f), "the visible band center is the graph center");
	check(pc_menu_click_map_to_graph(tall, 0.5f, 0.01f, &x, &y), "portrait bar converts");
	check(y < 0.0f || y > 480.0f, "a click in the portrait bar is outside the graph");

	PcMenuClickFrame bad = hd;
	bad.drawableW        = 0;
	check(!pc_menu_click_map_to_graph(bad, 0.5f, 0.5f, &x, &y), "a zero window does not convert");
}

void testDisabled()
{
	pc_menu_click_reset();
	pc_menu_click_set_enabled(true);
	pc_menu_click_set_pointer(0.2f, 0.2f);
	pc_menu_click_set_pointer(0.3f, 0.3f);
	pc_menu_click_post(0.3f, 0.3f);
	pc_menu_click_claim_pointer_menu();

	pc_menu_click_set_enabled(false);
	float nx = 0.0f;
	float ny = 0.0f;
	check(!pc_menu_click_pending(&nx, &ny), "disabling drops a pending click");
	check(!pc_menu_click_read_pointer_normalized(&nx, &ny), "disabling drops the highlight pointer");
	check(!pc_menu_click_pointer_menu_active(), "disabled menus do not claim the pointer");
	pc_menu_click_take_activity();
	pc_menu_click_post(0.5f, 0.5f);
	check(!pc_menu_click_take(&nx, &ny), "a click while disabled is ignored");
	pc_menu_click_set_pointer(0.6f, 0.6f);
	pc_menu_click_set_pointer(0.7f, 0.7f);
	check(!pc_menu_click_read_pointer_normalized(&nx, &ny), "a move while disabled does not highlight");
	check(!pc_menu_click_take_activity(), "pointer input while disabled is not activity");
	pc_menu_click_claim_pointer_menu();
	check(!pc_menu_click_pointer_menu_active(), "a claim while disabled does not hold");
	pc_menu_click_post_back();
	check(!pc_menu_click_take_back(), "right click back is suppressed while disabled");

	pc_menu_click_set_enabled(true);
	pc_menu_click_post(0.4f, 0.4f);
	check(pc_menu_click_take(&nx, &ny) && near(nx, 0.4f), "clicks work again once enabled");
	pc_menu_click_post_back();
	check(pc_menu_click_take_back(), "right click back works again once enabled");
}

void testPendingPeek()
{
	pc_menu_click_reset();
	pc_menu_click_begin_tick();
	pc_menu_click_post(0.25f, 0.75f);
	float nx = 0.0f;
	float ny = 0.0f;
	check(pc_menu_click_pending(&nx, &ny) && near(nx, 0.25f) && near(ny, 0.75f), "pending peeks without consuming");
	check(pc_menu_click_pending(&nx, &ny), "pending still holds after peek");
	check(pc_menu_click_take(&nx, &ny), "take consumes after pending");
	check(!pc_menu_click_pending(nullptr, nullptr), "pending is empty after take");
}

} // namespace

int main()
{
	testOutputArea();
	testMapCenters();
	testDisabled();
	testPendingPeek();
	// A full-screen backdrop is not a button. A short strip and a small pane are.
	const PcMenuClickRect backdrop = { 0, 0, 640, 480 };
	const PcMenuClickRect strip    = { 0, 360, 640, 400 };
	const PcMenuClickRect empty    = { 10, 10, 10, 40 };
	check(!pc_menu_click_is_button_rect(backdrop, kRoot), "full-screen pane is not a button");
	check(pc_menu_click_is_button_rect(strip, kRoot), "full-width short pane is a button");
	check(pc_menu_click_is_button_rect(kTitleButtons[0], kRoot), "small pane is a button");
	check(!pc_menu_click_is_button_rect(empty, kRoot), "empty pane is not a button");

	// Title screen: any click in the window presses Start (Classic and Mouse Cursor).
	const int titleActive[] = { 1, 1 };
	PcMenuClickResult titleA
	    = pc_menu_click_resolve(PC_MENU_CLICK_TITLE, 300.0f, 380.0f, kTitleButtons, titleActive, 2, kPcMenuClickMargin);
	check(titleA.action == PC_MENU_CLICK_PRESS_START, "title button 0 presses Start");
	PcMenuClickResult titleB
	    = pc_menu_click_resolve(PC_MENU_CLICK_TITLE, 300.0f, 420.0f, kTitleButtons, titleActive, 2, kPcMenuClickMargin);
	check(titleB.action == PC_MENU_CLICK_PRESS_START, "title button 1 presses Start");
	PcMenuClickResult titleMiss
	    = pc_menu_click_resolve(PC_MENU_CLICK_TITLE, 10.0f, 10.0f, kTitleButtons, titleActive, 2, kPcMenuClickMargin);
	check(titleMiss.action == PC_MENU_CLICK_PRESS_START, "title background also presses Start");

	// Main menu: each active row activates itself. The locked row does not.
	PcMenuClickResult main0
	    = pc_menu_click_resolve(PC_MENU_CLICK_MAIN, 300.0f, 150.0f, kMainButtons, kMainActive, 3, kPcMenuClickMargin);
	check(main0.action == PC_MENU_CLICK_ACTIVATE && main0.index == 0, "main menu row 0 activates");
	PcMenuClickResult mainLocked
	    = pc_menu_click_resolve(PC_MENU_CLICK_MAIN, 300.0f, 190.0f, kMainButtons, kMainActive, 3, kPcMenuClickMargin);
	check(mainLocked.action == PC_MENU_CLICK_IGNORE, "inactive main menu row does not activate");
	PcMenuClickResult main2
	    = pc_menu_click_resolve(PC_MENU_CLICK_MAIN, 300.0f, 230.0f, kMainButtons, kMainActive, 3, kPcMenuClickMargin);
	check(main2.action == PC_MENU_CLICK_ACTIVATE && main2.index == 2, "main menu row 2 activates");
	PcMenuClickResult mainMiss
	    = pc_menu_click_resolve(PC_MENU_CLICK_MAIN, 10.0f, 10.0f, kMainButtons, kMainActive, 3, kPcMenuClickMargin);
	check(mainMiss.action == PC_MENU_CLICK_IGNORE, "main menu background does not activate a row");

	// Identical rects tie, and the first one keeps the point.
	const PcMenuClickRect overlap[] = { { 0, 0, 100, 100 }, { 0, 0, 100, 100 } };
	const int overlapActive[]       = { 1, 1 };
	const int first = pc_menu_click_hit(50.0f, 50.0f, overlap, overlapActive, 2, 0.0f);
	check(first == 0, "identical overlapping buttons keep the first match");

	// Rows whose panes overlap split at the midline between their centres. A locked row
	// that is nearest takes no click, so its active neighbour does not get it.
	const PcMenuClickRect tall[] = { { 0, 0, 100, 36 }, { 0, 30, 100, 66 } };
	const int tallActive[]       = { 1, 1 };
	const int tallLocked[]       = { 1, 0 };
	check(pc_menu_click_hit(50.0f, 31.0f, tall, tallActive, 2, 0.0f) == 0, "a point above the midline takes the upper row");
	check(pc_menu_click_hit(50.0f, 34.0f, tall, tallActive, 2, 0.0f) == 1, "a point below the midline takes the lower row");
	check(pc_menu_click_hit(50.0f, 34.0f, tall, tallLocked, 2, 0.0f) == -1, "a locked row nearest the point takes no click");

	// Pause, file select, and the rest are not these two screens.
	PcMenuClickResult other
	    = pc_menu_click_resolve(PC_MENU_CLICK_NONE, 300.0f, 150.0f, kMainButtons, kMainActive, 3, kPcMenuClickMargin);
	check(other.action == PC_MENU_CLICK_IGNORE, "another screen ignores a click inside a button rect");

	// Window center on a wide virtual menu lands on a centered row.
	float gx = 0.0f;
	float gy = 0.0f;
	check(pc_menu_click_window_to_graph(1920, 1080, 960, 540, 1920, 480, &gx, &gy), "window click converts");
	check(gx == 960.0f && gy == 240.0f, "center click maps to the center of the menu graph");
	const PcMenuClickRect centered[] = { { 760, 220, 1160, 260 } };
	const int centeredActive[]       = { 1 };
	PcMenuClickResult centeredHit
	    = pc_menu_click_resolve(PC_MENU_CLICK_MAIN, gx, gy, centered, centeredActive, 1, kPcMenuClickMargin);
	check(centeredHit.action == PC_MENU_CLICK_ACTIVATE && centeredHit.index == 0,
	      "a centered main-menu row receives the center click");
	check(!pc_menu_click_window_to_graph(0, 1080, 0, 0, 1920, 480, &gx, &gy), "a zero window size does not convert");

	// The click is delivered once, on the sample that saw it or the next one,
	// and then it is gone. A second take does not activate another button.
	pc_menu_click_reset();
	pc_menu_click_begin_tick();
	pc_menu_click_post(0.5f, 0.5f);
	float nx = 0.0f;
	float ny = 0.0f;
	check(pc_menu_click_take(&nx, &ny) && nx == 0.5f && ny == 0.5f, "the menu receives the posted click");
	check(!pc_menu_click_take(&nx, &ny), "the same click does not activate a second time");

	// Posted on one sample and still there for the next. The sample after that
	// drops it if nobody took it.
	pc_menu_click_reset();
	pc_menu_click_begin_tick();
	pc_menu_click_post(0.25f, 0.75f);
	pc_menu_click_begin_tick();
	float graphX = 0.0f;
	float graphY = 0.0f;
	check(pc_menu_click_take_graph(640, 480, &graphX, &graphY), "a click survives until the next sample");
	check(graphX == 160.0f && graphY == 360.0f, "the surviving click scales into the menu graph");

	pc_menu_click_reset();
	pc_menu_click_begin_tick();
	pc_menu_click_post(0.5f, 0.5f);
	pc_menu_click_begin_tick();
	pc_menu_click_begin_tick();
	check(!pc_menu_click_take(&nx, &ny), "an untaken click is dropped after the next sample");

	pc_menu_click_reset();
	pc_menu_click_begin_tick();
	pc_menu_click_post(0.5f, 0.5f);
	check(!pc_menu_click_take_graph(0, 480, &graphX, &graphY), "a bad menu size does not consume a click");
	check(pc_menu_click_take(&nx, &ny) && nx == 0.5f, "the click is still pending after a bad menu size");

	// Moving the pointer across buttons changes the highlight and does not activate.
	// A later click anywhere on Press Start still proceeds. Main menu empty space does not.
	const int titleActiveFollow[] = { 1, 1 };
	PcMenuClickResult titleHover0
	    = pc_menu_click_hover(PC_MENU_CLICK_TITLE, 300.0f, 380.0f, kTitleButtons, titleActiveFollow, 2, kPcMenuClickMargin);
	check(titleHover0.action == PC_MENU_CLICK_HIGHLIGHT && titleHover0.index == 0,
	      "title pointer over button 0 highlights it");
	check(titleHover0.action != PC_MENU_CLICK_PRESS_START, "title hover does not press Start");
	PcMenuClickResult titleHover1
	    = pc_menu_click_hover(PC_MENU_CLICK_TITLE, 300.0f, 420.0f, kTitleButtons, titleActiveFollow, 2, kPcMenuClickMargin);
	check(titleHover1.action == PC_MENU_CLICK_HIGHLIGHT && titleHover1.index == 1,
	      "title pointer moving to button 1 changes the highlight");
	check(titleHover1.action != PC_MENU_CLICK_PRESS_START, "the moved title highlight still does not press Start");
	PcMenuClickResult titleHoverGap
	    = pc_menu_click_hover(PC_MENU_CLICK_TITLE, 10.0f, 10.0f, kTitleButtons, titleActiveFollow, 2, kPcMenuClickMargin);
	check(titleHoverGap.action == PC_MENU_CLICK_IGNORE && titleHoverGap.index == -1,
	      "title pointer on empty space highlights no button");
	PcMenuClickResult titleClick1
	    = pc_menu_click_resolve(PC_MENU_CLICK_TITLE, 300.0f, 420.0f, kTitleButtons, titleActiveFollow, 2, kPcMenuClickMargin);
	check(titleClick1.action == PC_MENU_CLICK_PRESS_START, "a title click over a button presses Start");
	PcMenuClickResult titleClickGap
	    = pc_menu_click_resolve(PC_MENU_CLICK_TITLE, 10.0f, 10.0f, kTitleButtons, titleActiveFollow, 2, kPcMenuClickMargin);
	check(titleClickGap.action == PC_MENU_CLICK_PRESS_START, "a title click on empty space presses Start");

	int mainHighlight = -1;
	PcMenuClickResult mainHover0
	    = pc_menu_click_hover(PC_MENU_CLICK_MAIN, 300.0f, 150.0f, kMainButtons, kMainActive, 3, kPcMenuClickMargin);
	check(mainHover0.action == PC_MENU_CLICK_HIGHLIGHT && mainHover0.index == 0, "main menu pointer highlights row 0");
	check(mainHover0.action != PC_MENU_CLICK_ACTIVATE, "main menu hover does not activate row 0");
	mainHighlight = mainHover0.index;
	PcMenuClickResult mainHover2
	    = pc_menu_click_hover(PC_MENU_CLICK_MAIN, 300.0f, 230.0f, kMainButtons, kMainActive, 3, kPcMenuClickMargin);
	check(mainHover2.action == PC_MENU_CLICK_HIGHLIGHT && mainHover2.index == 2, "main menu pointer highlights row 2");
	check(mainHover2.index != mainHighlight, "moving across main-menu rows changes the highlight");
	check(mainHover2.action != PC_MENU_CLICK_ACTIVATE, "the moved main-menu highlight does not activate");
	mainHighlight = mainHover2.index;
	PcMenuClickResult mainHoverLocked
	    = pc_menu_click_hover(PC_MENU_CLICK_MAIN, 300.0f, 190.0f, kMainButtons, kMainActive, 3, kPcMenuClickMargin);
	check(mainHoverLocked.action == PC_MENU_CLICK_IGNORE, "an inactive main-menu row is not highlighted");
	PcMenuClickResult mainHoverGap
	    = pc_menu_click_hover(PC_MENU_CLICK_MAIN, 10.0f, 10.0f, kMainButtons, kMainActive, 3, kPcMenuClickMargin);
	check(mainHoverGap.action == PC_MENU_CLICK_IGNORE && mainHoverGap.index == -1,
	      "main menu pointer on empty space highlights no row");
	PcMenuClickResult mainClick2
	    = pc_menu_click_resolve(PC_MENU_CLICK_MAIN, 300.0f, 230.0f, kMainButtons, kMainActive, 3, kPcMenuClickMargin);
	check(mainClick2.action == PC_MENU_CLICK_ACTIVATE && mainClick2.index == mainHighlight,
	      "a click activates the highlighted main-menu row");
	PcMenuClickResult mainClickGap
	    = pc_menu_click_resolve(PC_MENU_CLICK_MAIN, 10.0f, 10.0f, kMainButtons, kMainActive, 3, kPcMenuClickMargin);
	check(mainClickGap.action == PC_MENU_CLICK_IGNORE, "a main-menu click on empty space does not activate");
	PcMenuClickResult otherHover
	    = pc_menu_click_hover(PC_MENU_CLICK_NONE, 300.0f, 150.0f, kMainButtons, kMainActive, 3, kPcMenuClickMargin);
	check(otherHover.action == PC_MENU_CLICK_IGNORE, "another screen does not highlight from the pointer");

	// A pointer move is not a click, and a later move replaces the position.
	pc_menu_click_reset();
	pc_menu_click_set_pointer(0.2f, 0.25f);
	check(!pc_menu_click_take(&nx, &ny), "moving the pointer does not post a click");
	pc_menu_click_set_pointer(0.75f, 0.5f);
	float movedX = 0.0f;
	float movedY = 0.0f;
	check(pc_menu_click_read_pointer_graph(640, 480, &movedX, &movedY), "the menu can read the pointer");
	check(movedX == 480.0f && movedY == 240.0f, "a later pointer move replaces the highlight position");
	float normX = 0.0f;
	float normY = 0.0f;
	check(pc_menu_click_read_pointer_normalized(&normX, &normY) && normX == 0.75f && normY == 0.5f,
	      "the normalized pointer is the window position, unscaled");
	pc_menu_click_clear_pointer();
	check(!pc_menu_click_read_pointer_graph(640, 480, &movedX, &movedY), "clearing the pointer leaves nothing to highlight");

	// The pointer highlights only while the mouse drives. Motion gives it the menus.
	// A key press takes them back, and a pointer that rests does not take them again.
	pc_menu_click_reset();
	pc_menu_click_set_pointer(0.1f, 0.1f);
	check(!pc_menu_click_read_pointer_graph(640, 480, &movedX, &movedY), "the first pointer sample does not drive the menu");
	pc_menu_click_set_pointer(0.46875f, 0.3125f);
	check(pc_menu_click_read_pointer_graph(640, 480, &movedX, &movedY), "pointer motion gives the menus to the mouse");
	pc_menu_click_disarm_hover();
	check(!pc_menu_click_read_pointer_graph(640, 480, &movedX, &movedY), "a key press takes the menus back from the pointer");
	pc_menu_click_set_pointer(0.46875f, 0.3125f);
	check(!pc_menu_click_read_pointer_graph(640, 480, &movedX, &movedY), "a resting pointer does not take the menus back");

	// The pointer rests on row 0 and the keyboard moves the highlight to row 2. The
	// highlight stays on row 2. Moving the pointer again gives the mouse the menu back.
	int rowSelect = -1;
	pc_menu_click_reset();
	pc_menu_click_set_pointer(0.1f, 0.1f);
	pc_menu_click_set_pointer(0.46875f, 0.3125f);
	for (int frame = 0; frame < 2; frame++) {
		if (pc_menu_click_read_pointer_graph(640, 480, &movedX, &movedY)) {
			const PcMenuClickResult hover
			    = pc_menu_click_hover(PC_MENU_CLICK_MAIN, movedX, movedY, kMainButtons, kMainActive, 3, kPcMenuClickMargin);
			if (hover.action == PC_MENU_CLICK_HIGHLIGHT) {
				rowSelect = hover.index;
			}
		}
	}
	check(rowSelect == 0, "a pointer moved onto row 0 highlights it");
	pc_menu_click_disarm_hover();
	rowSelect = 2;
	for (int frame = 0; frame < 3; frame++) {
		pc_menu_click_set_pointer(0.46875f, 0.3125f);
		if (pc_menu_click_read_pointer_graph(640, 480, &movedX, &movedY)) {
			const PcMenuClickResult hover
			    = pc_menu_click_hover(PC_MENU_CLICK_MAIN, movedX, movedY, kMainButtons, kMainActive, 3, kPcMenuClickMargin);
			if (hover.action == PC_MENU_CLICK_HIGHLIGHT) {
				rowSelect = hover.index;
			}
		}
	}
	check(rowSelect == 2, "a pointer resting on row 0 does not pull the keyboard's highlight back");
	pc_menu_click_set_pointer(0.46875f, 0.32f);
	if (pc_menu_click_read_pointer_graph(640, 480, &movedX, &movedY)) {
		const PcMenuClickResult hover
		    = pc_menu_click_hover(PC_MENU_CLICK_MAIN, movedX, movedY, kMainButtons, kMainActive, 3, kPcMenuClickMargin);
		if (hover.action == PC_MENU_CLICK_HIGHLIGHT) {
			rowSelect = hover.index;
		}
	}
	check(rowSelect == 0, "moving the pointer again gives the highlight back to the mouse");

	// A click disarms the highlight as well. It comes back only once the pointer moves.
	pc_menu_click_reset();
	pc_menu_click_set_pointer(0.1f, 0.1f);
	pc_menu_click_set_pointer(0.46875f, 0.3125f);
	check(pc_menu_click_read_pointer_graph(640, 480, &movedX, &movedY), "motion arms the highlight before a click");
	pc_menu_click_disarm_hover();
	check(!pc_menu_click_read_pointer_graph(640, 480, &movedX, &movedY), "a click disarms the highlight");
	pc_menu_click_set_pointer(0.46875f, 0.3125f);
	check(!pc_menu_click_read_pointer_graph(640, 480, &movedX, &movedY),
	      "a pointer resting after a click does not re-arm the highlight");
	pc_menu_click_set_pointer(0.46875f, 0.32f);
	check(pc_menu_click_read_pointer_graph(640, 480, &movedX, &movedY), "moving the pointer again re-arms the highlight");

	// Motion and clicks each count as activity once, for the title screen's idle timer.
	pc_menu_click_reset();
	check(!pc_menu_click_take_activity(), "no pointer means no activity");
	pc_menu_click_set_pointer(0.1f, 0.1f);
	check(!pc_menu_click_take_activity(), "the first pointer sample is not activity");
	pc_menu_click_set_pointer(0.2f, 0.2f);
	check(pc_menu_click_take_activity(), "pointer motion counts as activity");
	check(!pc_menu_click_take_activity(), "pointer activity is taken once");
	pc_menu_click_post(0.5f, 0.5f);
	check(pc_menu_click_take_activity(), "a click counts as activity");
	check(!pc_menu_click_take_activity(), "a click is taken once");
	pc_menu_click_reset();

	// The title and main menu hold the pointer while they keep claiming it. The claim lapses
	// once the menu stops renewing it for a few ticks.
	check(!pc_menu_click_pointer_menu_active(), "no claim means no pointer menu");
	pc_menu_click_claim_pointer_menu();
	check(pc_menu_click_pointer_menu_active(), "a claim holds the pointer menu");
	pc_menu_click_begin_tick();
	check(pc_menu_click_pointer_menu_active(), "a claim still holds one tick later");
	pc_menu_click_begin_tick();
	pc_menu_click_begin_tick();
	check(!pc_menu_click_pointer_menu_active(), "a claim lapses when the menu stops renewing it");
	pc_menu_click_reset();

	// A right click backs out of the VS lobby once. An untaken one is dropped after the next sample.
	pc_menu_click_post_back();
	check(pc_menu_click_take_back(), "a right click backs out once");
	check(!pc_menu_click_take_back(), "a right click is taken once");
	pc_menu_click_post_back();
	pc_menu_click_begin_tick();
	pc_menu_click_begin_tick();
	check(!pc_menu_click_take_back(), "an untaken right click is dropped after the next sample");
	pc_menu_click_reset();

	// Click targets: a tagged pane's window rectangle is tested on the next click, per screen kind.
	pc_menu_click_targets_begin(0);
	pc_menu_click_targets_add(7, PcMenuClickRect{ 100, 100, 200, 200 });
	pc_menu_click_targets_end();
	int targetTag = -1;
	check(pc_menu_click_targets_hit(0, 150.0f, 150.0f, kPcMenuClickMargin, &targetTag) && targetTag == 7,
	      "a click inside a recorded target returns its tag");
	check(!pc_menu_click_targets_hit(0, 400.0f, 400.0f, kPcMenuClickMargin, &targetTag),
	      "a click outside every target misses");
	check(!pc_menu_click_targets_hit(1, 150.0f, 150.0f, kPcMenuClickMargin, &targetTag),
	      "another screen kind has no targets");
	pc_menu_click_targets_begin(0);
	pc_menu_click_targets_end();
	check(!pc_menu_click_targets_hit(0, 150.0f, 150.0f, kPcMenuClickMargin, &targetTag),
	      "a screen that draws no targets ends with an empty set");
	pc_menu_click_targets_add(9, PcMenuClickRect{ 0, 0, 10, 10 });
	check(!pc_menu_click_targets_collecting(), "targets added outside a collection are not collecting");
	check(!pc_menu_click_targets_hit(0, 5.0f, 5.0f, kPcMenuClickMargin, &targetTag),
	      "targets added outside a collection are dropped");

	if (sFailures == 0) {
		std::printf("pc_menu_click_test: all checks passed\n");
		return 0;
	}
	std::printf("pc_menu_click_test: %d failure(s)\n", sFailures);
	return 1;
}
