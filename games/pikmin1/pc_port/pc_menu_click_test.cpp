/**
 * Title-screen and main-menu clicks, without a window and without a disc.
 *
 * The rectangles stand in for the panes those screens already draw. A click
 * inside one activates that button. A click on any other screen does not.
 * Nothing here reads the control mode: Classic stays the default.
 */

#include "pc_menu_click.h"

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

} // namespace

int main()
{
	// A full-screen backdrop is not a button. A short strip and a small pane are.
	const PcMenuClickRect backdrop = { 0, 0, 640, 480 };
	const PcMenuClickRect strip    = { 0, 360, 640, 400 };
	const PcMenuClickRect empty    = { 10, 10, 10, 40 };
	check(!pc_menu_click_is_button_rect(backdrop, kRoot), "full-screen pane is not a button");
	check(pc_menu_click_is_button_rect(strip, kRoot), "full-width short pane is a button");
	check(pc_menu_click_is_button_rect(kTitleButtons[0], kRoot), "small pane is a button");
	check(!pc_menu_click_is_button_rect(empty, kRoot), "empty pane is not a button");

	// Title screen: either button presses Start. The gap between them does not.
	const int titleActive[] = { 1, 1 };
	PcMenuClickResult titleA
	    = pc_menu_click_resolve(PC_MENU_CLICK_TITLE, 300.0f, 380.0f, kTitleButtons, titleActive, 2, kPcMenuClickMargin);
	check(titleA.action == PC_MENU_CLICK_PRESS_START && titleA.index == 0, "title button 0 presses Start");
	PcMenuClickResult titleB
	    = pc_menu_click_resolve(PC_MENU_CLICK_TITLE, 300.0f, 420.0f, kTitleButtons, titleActive, 2, kPcMenuClickMargin);
	check(titleB.action == PC_MENU_CLICK_PRESS_START && titleB.index == 1, "title button 1 presses Start");
	PcMenuClickResult titleMiss
	    = pc_menu_click_resolve(PC_MENU_CLICK_TITLE, 10.0f, 10.0f, kTitleButtons, titleActive, 2, kPcMenuClickMargin);
	check(titleMiss.action == PC_MENU_CLICK_IGNORE && titleMiss.index == -1, "title background does not press Start");

	// The 8px margin still counts. One pixel past it does not.
	PcMenuClickResult marginHit
	    = pc_menu_click_resolve(PC_MENU_CLICK_TITLE, 212.0f, 380.0f, kTitleButtons, titleActive, 2, kPcMenuClickMargin);
	check(marginHit.action == PC_MENU_CLICK_PRESS_START, "click within the margin hits the title button");
	PcMenuClickResult marginMiss
	    = pc_menu_click_resolve(PC_MENU_CLICK_TITLE, 211.0f, 380.0f, kTitleButtons, titleActive, 2, kPcMenuClickMargin);
	check(marginMiss.action == PC_MENU_CLICK_IGNORE, "click past the margin misses the title button");

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

	// Overlap uses the first row, matching the menu's existing touch scan.
	const PcMenuClickRect overlap[] = { { 0, 0, 100, 100 }, { 0, 0, 100, 100 } };
	const int overlapActive[]       = { 1, 1 };
	const int first = pc_menu_click_hit(50.0f, 50.0f, overlap, overlapActive, 2, 0.0f);
	check(first == 0, "overlapping buttons keep the first match");

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

	if (sFailures == 0) {
		std::printf("pc_menu_click_test: all checks passed\n");
		return 0;
	}
	std::printf("pc_menu_click_test: %d failure(s)\n", sFailures);
	return 1;
}
