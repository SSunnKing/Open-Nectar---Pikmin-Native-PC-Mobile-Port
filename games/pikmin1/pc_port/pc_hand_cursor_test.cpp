/**
 * Hand cursor over the pointer menus, without a window and without a disc.
 *
 * Each tick runs in the window layer's order: begin_tick, input events, the pointer sample,
 * the cursor update, then the screen's own claim and hover test. A stub stands in for
 * SDL_SetCursor and counts every call, so a redundant call is a failure.
 */

#include "pc_hand_cursor.h"
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

int sSetCalls = 0;
int sLastSet  = -1;

void stubSetCursor(int shape)
{
	++sSetCalls;
	sLastSet = shape;
}

// Main menu rows, 640x480 graph. The middle row is locked, like the Randomizer row.
const PcMenuClickRect kRows[] = {
	{ 200, 140, 440, 170 },
	{ 200, 180, 440, 210 },
	{ 200, 220, 440, 250 },
};
const int kActive[] = { 1, 0, 1 };
// Another screen with one button elsewhere (a Yes/No prompt, say).
const PcMenuClickRect kOther[] = { { 40, 400, 140, 440 } };

enum Screen { SCREEN_MENU, SCREEN_OTHER, SCREEN_CLAIM_ONLY, SCREEN_NONE, SCREEN_TITLE_ANY };

struct Tick {
	float gx, gy;          // pointer, 640x480 graph; negative means outside the window
	Screen screen;
	bool keyPress   = false;
	bool clickPress = false;
	bool free       = true; // system pointer visible, not captured, window focused
};

void runTick(const Tick& t)
{
	pc_menu_click_begin_tick();
	if (t.keyPress || t.clickPress) pc_menu_click_disarm_hover();
	if (t.clickPress) pc_menu_click_post(t.gx / 640.0f, t.gy / 480.0f);
	if (t.gx < 0.0f) {
		pc_menu_click_clear_pointer();
	} else {
		pc_menu_click_set_pointer(t.gx / 640.0f, t.gy / 480.0f);
	}
	pc_hand_cursor_update(t.free, pc_menu_click_hand_wanted());

	// The screen: claim the pointer, then hover through the armed pointer as the menus do.
	if (t.screen == SCREEN_NONE) return;
	pc_menu_click_claim_pointer_menu();
	if (t.screen == SCREEN_CLAIM_ONLY) return;
	float nx = 0.0f, ny = 0.0f;
	if (!pc_menu_click_read_pointer_normalized(&nx, &ny)) return;
	const float x = nx * 640.0f, y = ny * 480.0f;
	if (t.screen == SCREEN_MENU) {
		pc_menu_click_hover(PC_MENU_CLICK_MAIN, x, y, kRows, kActive, 3, kPcMenuClickMargin);
	} else if (t.screen == SCREEN_OTHER) {
		pc_menu_click_hover(PC_MENU_CLICK_MAIN, x, y, kOther, nullptr, 1, kPcMenuClickMargin);
	} else if (t.screen == SCREEN_TITLE_ANY) {
		pc_menu_click_hover(PC_MENU_CLICK_NONE, x, y, kRows, nullptr, 3, kPcMenuClickMargin);
	}
}

void start()
{
	pc_menu_click_reset();
	pc_hand_cursor_init(stubSetCursor);
	sSetCalls = 0;
	sLastSet  = -1;
}

// Rows 0 and 2 are active. These points sit on row 0, the locked row 1, and empty space.
constexpr float kRowX = 320.0f, kRow0Y = 155.0f, kRow1Y = 195.0f, kRow2Y = 235.0f;
constexpr float kEmptyX = 600.0f, kEmptyY = 60.0f;

void testHandFollowsHover()
{
	start();
	runTick({ kEmptyX, kEmptyY, SCREEN_MENU }); // first sample is not motion
	runTick({ kEmptyX + 1, kEmptyY, SCREEN_MENU });
	runTick({ kEmptyX + 2, kEmptyY, SCREEN_MENU });
	check(sSetCalls == 0, "empty space never sets a cursor (the arrow is already showing)");

	runTick({ kRowX, kRow0Y, SCREEN_MENU }); // hover test runs this tick
	check(sSetCalls == 0, "the hand waits for the tick that found the row to end");
	runTick({ kRowX + 1, kRow0Y, SCREEN_MENU });
	check(sSetCalls == 1 && sLastSet == PC_HAND_CURSOR_HAND, "over an active row: one call, the hand");

	for (int i = 0; i < 20; i++) runTick({ kRowX + 2.0f + i, kRow0Y, SCREEN_MENU });
	check(sSetCalls == 1, "moving within the row makes no further calls");

	runTick({ kRowX, kRow2Y, SCREEN_MENU });
	runTick({ kRowX + 1, kRow2Y, SCREEN_MENU });
	check(sSetCalls == 1, "row to row keeps the hand with no call");

	runTick({ kEmptyX, kEmptyY, SCREEN_MENU });
	runTick({ kEmptyX + 1, kEmptyY, SCREEN_MENU });
	check(sSetCalls == 2 && sLastSet == PC_HAND_CURSOR_ARROW, "off the rows: one call, the arrow");
	runTick({ kEmptyX + 2, kEmptyY, SCREEN_MENU });
	runTick({ kEmptyX + 3, kEmptyY, SCREEN_MENU });
	check(sSetCalls == 2, "staying off the rows makes no further calls");
}

void testLockedRowShowsNoHand()
{
	start();
	runTick({ kEmptyX, kEmptyY, SCREEN_MENU });
	for (int i = 0; i < 6; i++) runTick({ kRowX + i, kRow1Y, SCREEN_MENU });
	check(sSetCalls == 0 && pc_hand_cursor_shape() == PC_HAND_CURSOR_ARROW, "a locked row keeps the arrow");

	// From the hand on row 0 onto the locked row: back to the arrow.
	runTick({ kRowX, kRow0Y, SCREEN_MENU });
	runTick({ kRowX + 1, kRow0Y, SCREEN_MENU });
	check(pc_hand_cursor_shape() == PC_HAND_CURSOR_HAND, "row 0 shows the hand");
	runTick({ kRowX, kRow1Y, SCREEN_MENU });
	runTick({ kRowX + 1, kRow1Y, SCREEN_MENU });
	check(pc_hand_cursor_shape() == PC_HAND_CURSOR_ARROW && sSetCalls == 2, "onto the locked row: the arrow");
}

void handOnRow0()
{
	runTick({ kEmptyX, kEmptyY, SCREEN_MENU });
	runTick({ kRowX, kRow0Y, SCREEN_MENU });
	runTick({ kRowX + 1, kRow0Y, SCREEN_MENU });
}

void testScreenChangeResets()
{
	// Pointer moving while the next screen shows: that screen's own test decides.
	start();
	handOnRow0();
	check(pc_hand_cursor_shape() == PC_HAND_CURSOR_HAND, "hand before the screen change");
	runTick({ kRowX + 2, kRow0Y, SCREEN_OTHER });
	runTick({ kRowX + 3, kRow0Y, SCREEN_OTHER });
	check(pc_hand_cursor_shape() == PC_HAND_CURSOR_ARROW && sSetCalls == 2,
	      "the new screen has nothing under the pointer: arrow");

	// A fade or gameplay, where no screen claims the pointer: the arrow, even with a still pointer.
	start();
	handOnRow0();
	runTick({ kRowX + 1, kRow0Y, SCREEN_NONE });
	runTick({ kRowX + 1, kRow0Y, SCREEN_NONE });
	runTick({ kRowX + 1, kRow0Y, SCREEN_NONE });
	runTick({ kRowX + 1, kRow0Y, SCREEN_NONE });
	check(pc_hand_cursor_shape() == PC_HAND_CURSOR_ARROW && sSetCalls == 2, "no claim: arrow");
	// Back on the menu: its own hover test brings the hand back over the row.
	runTick({ kRowX + 1, kRow0Y, SCREEN_MENU });
	runTick({ kRowX + 1, kRow0Y, SCREEN_MENU });
	check(pc_hand_cursor_shape() == PC_HAND_CURSOR_HAND && sSetCalls == 3, "back on the menu over a row: the hand");

	// After a click the hover waits for motion. A screen reached by that click, with no
	// claim on the way, starts from the arrow and keeps it until the pointer moves.
	start();
	handOnRow0();
	runTick({ kRowX + 1, kRow0Y, SCREEN_MENU, false, true });
	for (int i = 0; i < 4; i++) runTick({ kRowX + 1, kRow0Y, SCREEN_NONE });
	for (int i = 0; i < 3; i++) runTick({ kRowX + 1, kRow0Y, SCREEN_OTHER });
	check(pc_hand_cursor_shape() == PC_HAND_CURSOR_ARROW && sSetCalls == 2, "after a click and a fade: arrow");

	// Any other screen id is not a pointer menu: the hover test ignores it, no hand.
	start();
	runTick({ kEmptyX, kEmptyY, SCREEN_TITLE_ANY });
	for (int i = 0; i < 4; i++) runTick({ kRowX + i, kRow0Y, SCREEN_TITLE_ANY });
	check(sSetCalls == 0, "a non-menu screen id shows no hand");
}

void testClickAndKeysHold()
{
	start();
	handOnRow0();
	// A click disarms the hover. The pointer has not moved, so the hand stays, as in a browser.
	runTick({ kRowX + 1, kRow0Y, SCREEN_MENU, false, true });
	runTick({ kRowX + 1, kRow0Y, SCREEN_MENU });
	runTick({ kRowX + 1, kRow0Y, SCREEN_MENU });
	check(pc_hand_cursor_shape() == PC_HAND_CURSOR_HAND && sSetCalls == 1, "a click on a row keeps the hand");
	// A key press does the same.
	runTick({ kRowX + 1, kRow0Y, SCREEN_MENU, true });
	runTick({ kRowX + 1, kRow0Y, SCREEN_MENU });
	check(pc_hand_cursor_shape() == PC_HAND_CURSOR_HAND && sSetCalls == 1, "a key press keeps the hand");
	// The next motion asks again.
	runTick({ kEmptyX, kEmptyY, SCREEN_MENU });
	runTick({ kEmptyX + 1, kEmptyY, SCREEN_MENU });
	check(pc_hand_cursor_shape() == PC_HAND_CURSOR_ARROW && sSetCalls == 2, "motion after the click re-tests");

	// A screen that claims but runs no test this tick (an opening animation) holds the shape.
	start();
	handOnRow0();
	runTick({ kRowX + 2, kRow0Y, SCREEN_CLAIM_ONLY });
	runTick({ kRowX + 3, kRow0Y, SCREEN_CLAIM_ONLY });
	check(pc_hand_cursor_shape() == PC_HAND_CURSOR_HAND && sSetCalls == 1, "a tick with no test holds the hand");
}

void testPointerNotFree()
{
	// Captured, hidden, or unfocused: the arrow goes back once, then nothing is set.
	start();
	handOnRow0();
	for (int i = 0; i < 5; i++) {
		Tick t { kRowX + 2.0f + i, kRow0Y, SCREEN_MENU };
		t.free = false;
		runTick(t);
	}
	check(sSetCalls == 2 && sLastSet == PC_HAND_CURSOR_ARROW, "not free: one arrow call, then none");
	runTick({ kRowX + 8, kRow0Y, SCREEN_MENU });
	check(sSetCalls == 3 && sLastSet == PC_HAND_CURSOR_HAND, "free again over the row: the hand");

	// Focus loss and gameplay changes reset at once, and only when the hand shows.
	pc_hand_cursor_reset();
	check(sSetCalls == 4 && sLastSet == PC_HAND_CURSOR_ARROW, "reset: the arrow");
	pc_hand_cursor_reset();
	check(sSetCalls == 4, "a second reset makes no call");

	// The pointer leaves the window.
	start();
	handOnRow0();
	runTick({ -1.0f, -1.0f, SCREEN_MENU });
	check(pc_hand_cursor_shape() == PC_HAND_CURSOR_ARROW && sSetCalls == 2, "pointer out of the window: arrow");

	// Pointer menus off (deterministic netplay day).
	start();
	handOnRow0();
	pc_menu_click_set_enabled(false);
	runTick({ kRowX + 2, kRow0Y, SCREEN_MENU });
	check(pc_hand_cursor_shape() == PC_HAND_CURSOR_ARROW && sSetCalls == 2, "pointer menus off: arrow");
	pc_menu_click_set_enabled(true);
}

void testProjectedTargets()
{
	// Data-file cards go through targets_hit_row; that test asks for the hand too.
	start();
	const int kKind = 3;
	pc_menu_click_targets_begin(kKind);
	pc_menu_click_targets_add(0, { 100, 200, 200, 300 });
	pc_menu_click_targets_add(1, { 300, 200, 400, 300 });
	pc_menu_click_targets_end();
	auto tick = [&](float x, float y) {
		pc_menu_click_begin_tick();
		pc_menu_click_set_pointer(x / 1000.0f, y / 1000.0f);
		pc_hand_cursor_update(true, pc_menu_click_hand_wanted());
		pc_menu_click_claim_pointer_menu();
		float nx = 0.0f, ny = 0.0f;
		int tag = -1;
		if (pc_menu_click_read_pointer_normalized(&nx, &ny)) {
			pc_menu_click_targets_hit_row(kKind, nx * 1000.0f, ny * 1000.0f, 0, 16.0f, &tag);
		}
	};
	tick(900, 900);
	tick(901, 900);
	tick(902, 900);
	check(sSetCalls == 0, "outside every card cell: arrow");
	tick(150, 250);
	tick(151, 250);
	check(sSetCalls == 1 && sLastSet == PC_HAND_CURSOR_HAND, "over a card cell: hand");
	tick(250, 250); // the midpoint gap between cards belongs to a cell
	tick(251, 250);
	check(sSetCalls == 1, "between cards stays on a cell: no call");
	tick(900, 900);
	tick(901, 900);
	check(sSetCalls == 2 && sLastSet == PC_HAND_CURSOR_ARROW, "off the cards: arrow");
}

} // namespace

int main()
{
	testHandFollowsHover();
	testLockedRowShowsNoHand();
	testScreenChangeResets();
	testClickAndKeysHold();
	testPointerNotFree();
	testProjectedTargets();
	if (sFailures == 0) {
		std::printf("pc_hand_cursor_test: all passed\n");
		return 0;
	}
	std::printf("pc_hand_cursor_test: %d failure(s)\n", sFailures);
	return 1;
}
