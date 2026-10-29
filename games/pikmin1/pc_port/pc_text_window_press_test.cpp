/**
 * @file pc_text_window_press_test.cpp
 * @brief Space, left click and a Shift binding for B show a typing page, then
 * turn it on the next press.
 *
 * The keys go through the menu translator, as in a text window, and the window
 * sees fresh presses only, as Controller::keyClick does. Shift reaches that
 * translator only while the text window is live, which is the same check
 * pc_window.cpp uses.
 */

#include "pc_menu_pad.h"
#include "pc_text_window_press.h"

#include <cstdio>

namespace {

int sFailures = 0;

void expect(int actual, int expected, const char* what)
{
	if (actual != expected) {
		std::printf("FAIL: %s -> got %d, expected %d\n", what, actual, expected);
		++sFailures;
	}
}

// One text window fed one game tick at a time.
struct Window {
	bool pageShown         = false;
	int page               = 0;
	std::uint16_t prev     = 0;
	bool textWindowLive    = true;
	int bBind              = 0;
	bool bBindHeld         = false;

	PcTextWindowPress tick(const PcMenuPadKeys& keys)
	{
		std::uint16_t button = 0;
		std::int8_t sx = 0;
		std::int8_t sy = 0;
		const bool boundB = pc_menu_pad_b_binding_counts(bBind, textWindowLive) && bBindHeld;
		pc_menu_pad_apply(keys, true, boundB, SDL_SCANCODE_RETURN, 0, button, sx, sy);
		const std::uint16_t fresh = button & ~prev;
		prev = button;
		const PcTextWindowPress press
		    = pc_text_window_press(pageShown, (fresh & PC_MENU_PAD_A) != 0, (fresh & PC_MENU_PAD_B) != 0);
		if (press == PC_TEXT_PRESS_SHOW_PAGE) {
			pageShown = true;
		} else if (press == PC_TEXT_PRESS_NEXT_PAGE) {
			++page;
			pageShown = false;
		}
		return press;
	}
};

void testRules()
{
	expect(pc_text_window_press(false, false, false), PC_TEXT_PRESS_NONE, "no press while typing");
	expect(pc_text_window_press(true, false, false), PC_TEXT_PRESS_NONE, "no press on a shown page");
	expect(pc_text_window_press(false, true, false), PC_TEXT_PRESS_SHOW_PAGE, "A while typing shows the page");
	expect(pc_text_window_press(false, false, true), PC_TEXT_PRESS_SHOW_PAGE, "B while typing shows the page");
	expect(pc_text_window_press(true, true, false), PC_TEXT_PRESS_NEXT_PAGE, "A on a shown page turns it");
	expect(pc_text_window_press(true, false, true), PC_TEXT_PRESS_NEXT_PAGE, "B on a shown page turns it");
	expect(pc_text_window_press(false, true, true), PC_TEXT_PRESS_SHOW_PAGE, "A and B together only show the page");
}

void testHeldKey(PcMenuPadKeys down, const char* name, int bBind = 0)
{
	Window w;
	w.bBind = bBind;
	w.bBindHeld = bBind != 0;
	PcMenuPadKeys up = {};
	char what[160];
	std::snprintf(what, sizeof what, "%s pressed while typing shows the page", name);
	expect(w.tick(down), PC_TEXT_PRESS_SHOW_PAGE, what);
	std::snprintf(what, sizeof what, "%s held does not also turn the page", name);
	for (int i = 0; i < 30; ++i) {
		expect(w.tick(down), PC_TEXT_PRESS_NONE, what);
	}
	expect(w.page, 0, what);
	w.bBindHeld = false;
	w.tick(up);
	w.bBindHeld = bBind != 0;
	std::snprintf(what, sizeof what, "%s pressed again turns the page", name);
	expect(w.tick(down), PC_TEXT_PRESS_NEXT_PAGE, what);
	expect(w.page, 1, what);
	std::snprintf(what, sizeof what, "%s held on the new page does nothing", name);
	expect(w.tick(down), PC_TEXT_PRESS_NONE, what);
	expect(w.pageShown ? 1 : 0, 0, what);
	w.bBindHeld = false;
	w.tick(up);
	w.bBindHeld = bBind != 0;
	std::snprintf(what, sizeof what, "%s pressed on the new page shows it", name);
	expect(w.tick(down), PC_TEXT_PRESS_SHOW_PAGE, what);
	expect(w.page, 1, what);
}

void testShiftIgnoredOnMenu()
{
	// Pause and the world map are menu mode with no live text window.
	Window w;
	w.textWindowLive = false;
	w.bBind          = SDL_SCANCODE_LSHIFT;
	w.bBindHeld      = true;
	PcMenuPadKeys shift = {};
	shift.shiftL = true;
	expect(w.tick(shift), PC_TEXT_PRESS_NONE, "Shift on a menu does not show a page");
	w.pageShown = true;
	expect(w.tick(shift), PC_TEXT_PRESS_NONE, "Shift on a menu does not turn a page");
}

void testShownPageTurnsOnFirstPress()
{
	// The page finished typing on its own: the first press turns it.
	Window w;
	w.pageShown = true;
	PcMenuPadKeys space = {};
	space.space = true;
	expect(w.tick(space), PC_TEXT_PRESS_NEXT_PAGE, "Space on a fully typed page turns it");
	expect(w.page, 1, "one page turned");
}

} // namespace

int main()
{
	testRules();
	PcMenuPadKeys space = {};
	space.space = true;
	testHeldKey(space, "Space");
	PcMenuPadKeys click = {};
	click.clickLeft = true;
	testHeldKey(click, "Left click");
	PcMenuPadKeys enter = {};
	enter.enter = true;
	testHeldKey(enter, "Enter");
	PcMenuPadKeys shiftL = {};
	shiftL.shiftL = true;
	testHeldKey(shiftL, "Left Shift bound to B", SDL_SCANCODE_LSHIFT);
	PcMenuPadKeys shiftR = {};
	shiftR.shiftR = true;
	testHeldKey(shiftR, "Right Shift bound to B", SDL_SCANCODE_RSHIFT);
	testShiftIgnoredOnMenu();
	testShownPageTurnsOnFirstPress();
	if (sFailures) {
		std::printf("%d failure(s)\n", sFailures);
		return 1;
	}
	std::printf("pc_text_window_press_test: OK\n");
	return 0;
}
