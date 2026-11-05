/**
 * @file pc_menu_pad_test.cpp
 * @brief Menu keys versus free-play keys on the virtual GameCube pad.
 *
 * Menus accept with Enter, Space and left click, and cancel with Escape,
 * Backspace and right click. Shift does not cancel. Free play keeps Enter as
 * Start and a Shift binding as B. No window is opened: the translator takes
 * the keys directly.
 */

#include "pc_menu_pad.h"

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

PcMenuPadKeys keysNone()
{
	PcMenuPadKeys keys = {};
	return keys;
}

void apply(const PcMenuPadKeys& keys, bool menuMode, bool nonShiftB, int bindStart, int bindStart2,
           std::uint16_t& button, std::int8_t& stickX, std::int8_t& stickY)
{
	pc_menu_pad_apply(keys, menuMode, nonShiftB, bindStart, bindStart2, button, stickX, stickY);
}

} // namespace

int main()
{
	const int startKey = SDL_SCANCODE_RETURN;

	// Enter accepts. It does not also press Start, or the world map would
	// open its leave dialog and a pause menu would close.
	{
		PcMenuPadKeys keys = keysNone();
		keys.enter = true;
		std::uint16_t button = PC_MENU_PAD_START;
		std::int8_t sx = 0, sy = 0;
		apply(keys, true, false, startKey, 0, button, sx, sy);
		expect((button & PC_MENU_PAD_A) != 0, 1, "menu Enter sets A");
		expect((button & PC_MENU_PAD_START) != 0, 0, "menu Enter clears Start");
		expect(sx, 0, "menu Enter leaves the stick centred");
	}

	// Numpad Enter and Space are the same accept key.
	{
		PcMenuPadKeys keys = keysNone();
		keys.kpEnter = true;
		std::uint16_t button = PC_MENU_PAD_START;
		std::int8_t sx = 0, sy = 0;
		apply(keys, true, false, SDL_SCANCODE_KP_ENTER, 0, button, sx, sy);
		expect((button & PC_MENU_PAD_A) != 0, 1, "menu numpad Enter sets A");
		expect((button & PC_MENU_PAD_START) != 0, 0, "menu numpad Enter clears Start");
	}
	{
		PcMenuPadKeys keys = keysNone();
		keys.space = true;
		std::uint16_t button = 0;
		std::int8_t sx = 0, sy = 0;
		apply(keys, true, false, startKey, 0, button, sx, sy);
		expect((button & PC_MENU_PAD_A) != 0, 1, "menu Space sets A");
		expect((button & PC_MENU_PAD_START) != 0, 0, "menu Space is not Start");
	}

	// Classic mode drops the mouse binding, so the click arrives on its own.
	// It still accepts, and it does not press Start.
	{
		PcMenuPadKeys keys = keysNone();
		keys.clickLeft = true;
		std::uint16_t button = 0;
		std::int8_t sx = 0, sy = 0;
		apply(keys, true, false, startKey, 0, button, sx, sy);
		expect((button & PC_MENU_PAD_A) != 0, 1, "menu left click sets A");
		expect((button & PC_MENU_PAD_START) != 0, 0, "menu left click is not Start");
	}

	// A click shorter than a frame is gone from the held buttons and lives
	// only in the press mask. It still accepts, and a right-button press does
	// not.
	{
		const std::uint32_t leftEdge = SDL_BUTTON(SDL_BUTTON_LEFT);
		const std::uint32_t rightEdge = SDL_BUTTON(SDL_BUTTON_RIGHT);
		expect(pc_menu_pad_click(0, leftEdge, SDL_BUTTON_LEFT), 1, "short left click counts");
		expect(pc_menu_pad_click(leftEdge, 0, SDL_BUTTON_LEFT), 1, "held left click counts");
		expect(pc_menu_pad_click(0, 0, SDL_BUTTON_LEFT), 0, "no left click");
		expect(pc_menu_pad_click(0, rightEdge, SDL_BUTTON_RIGHT), 1, "short right click counts");
		expect(pc_menu_pad_click(0, rightEdge, SDL_BUTTON_LEFT), 0, "right click is not left");

		PcMenuPadKeys keys = keysNone();
		keys.clickLeft = pc_menu_pad_click(0, leftEdge, SDL_BUTTON_LEFT);
		keys.clickRight = pc_menu_pad_click(0, rightEdge, SDL_BUTTON_RIGHT);
		std::uint16_t button = 0;
		std::int8_t sx = 0, sy = 0;
		apply(keys, true, false, startKey, 0, button, sx, sy);
		expect((button & PC_MENU_PAD_A) != 0, 1, "short left click sets A");
		expect((button & PC_MENU_PAD_B) != 0, 1, "short right click sets B");
		expect((button & PC_MENU_PAD_START) != 0, 0, "short left click is not Start");
	}

	// A finger is delivered again as a mouse event. That event must not become
	// a menu click. A physical mouse, including one released before the sample,
	// still does.
	{
		std::uint32_t held = 0, pressed = 0;
		pc_menu_pad_note_mouse_button(held, pressed, SDL_TOUCH_MOUSEID, SDL_BUTTON_LEFT, true);
		pc_menu_pad_note_mouse_button(held, pressed, SDL_TOUCH_MOUSEID, SDL_BUTTON_RIGHT, true);
		expect(pc_menu_pad_mouse_is_touch(SDL_TOUCH_MOUSEID), 1, "touch mouse id is touch");
		expect(pc_menu_pad_click(held, pressed, SDL_BUTTON_LEFT), 0, "finger is not a left click");
		expect(pc_menu_pad_click(held, pressed, SDL_BUTTON_RIGHT), 0, "finger is not a right click");

		pc_menu_pad_note_mouse_button(held, pressed, 0, SDL_BUTTON_LEFT, true);
		expect(pc_menu_pad_click(held, pressed, SDL_BUTTON_LEFT), 1, "real mouse down is a left click");
		pc_menu_pad_note_mouse_button(held, pressed, 0, SDL_BUTTON_LEFT, false);
		expect(held == 0, 1, "real mouse up releases the button");
		expect(pc_menu_pad_click(held, pressed, SDL_BUTTON_LEFT), 1, "short real click stays pressed");

		pc_menu_pad_note_mouse_button(held, pressed, 3, SDL_BUTTON_RIGHT, true);
		expect(pc_menu_pad_click(held, pressed, SDL_BUTTON_RIGHT), 1, "physical mouse id 3 is a right click");
		expect(pc_menu_pad_mouse_is_touch(3), 0, "physical mouse id is not touch");
	}

	// A saved Shift binding still arrives as B from the keyboard layer. Menu
	// mode takes it back off. Either Shift is the same.
	{
		PcMenuPadKeys keys = keysNone();
		keys.shiftL = true;
		keys.shiftR = true;
		std::uint16_t button = PC_MENU_PAD_B;
		std::int8_t sx = 0, sy = 0;
		apply(keys, true, false, startKey, 0, button, sx, sy);
		expect((button & PC_MENU_PAD_B) != 0, 0, "menu Shift does not set B");
	}

	// Escape, Backspace and right click cancel. A non-Shift B binding does too.
	{
		PcMenuPadKeys keys = keysNone();
		keys.escape = true;
		std::uint16_t button = 0;
		std::int8_t sx = 0, sy = 0;
		apply(keys, true, false, startKey, 0, button, sx, sy);
		expect((button & PC_MENU_PAD_B) != 0, 1, "menu Escape sets B");
		expect((button & PC_MENU_PAD_A) != 0, 0, "menu Escape is not A");
	}
	{
		PcMenuPadKeys keys = keysNone();
		keys.backspace = true;
		std::uint16_t button = 0;
		std::int8_t sx = 0, sy = 0;
		apply(keys, true, false, startKey, 0, button, sx, sy);
		expect((button & PC_MENU_PAD_B) != 0, 1, "menu Backspace sets B");
	}
	{
		PcMenuPadKeys keys = keysNone();
		keys.clickRight = true;
		std::uint16_t button = 0;
		std::int8_t sx = 0, sy = 0;
		apply(keys, true, false, startKey, 0, button, sx, sy);
		expect((button & PC_MENU_PAD_B) != 0, 1, "menu right click sets B");
	}
	{
		PcMenuPadKeys keys = keysNone();
		std::uint16_t button = 0;
		std::int8_t sx = 0, sy = 0;
		apply(keys, true, true, startKey, 0, button, sx, sy);
		expect((button & PC_MENU_PAD_B) != 0, 1, "menu non-Shift B binding sets B");
	}

	// Arrows and WASD both move the cursor, on the stick and the d-pad.
	{
		PcMenuPadKeys keys = keysNone();
		keys.left = true;
		std::uint16_t button = 0;
		std::int8_t sx = 0, sy = 0;
		apply(keys, true, false, startKey, 0, button, sx, sy);
		expect((button & PC_MENU_PAD_LEFT) != 0, 1, "menu Left sets the d-pad");
		expect((button & PC_MENU_PAD_RIGHT) != 0, 0, "menu Left clears Right");
		expect(sx, -127, "menu Left deflects the stick");
		expect(sy, 0, "menu Left leaves the vertical stick centred");
	}
	{
		PcMenuPadKeys keys = keysNone();
		keys.keyD = true;
		keys.keyW = true;
		std::uint16_t button = 0;
		std::int8_t sx = 0, sy = 0;
		apply(keys, true, false, startKey, 0, button, sx, sy);
		expect((button & PC_MENU_PAD_RIGHT) != 0, 1, "menu D sets the d-pad");
		expect((button & PC_MENU_PAD_UP) != 0, 1, "menu W sets the d-pad");
		expect(sx, 127, "menu D deflects the stick");
		expect(sy, 127, "menu W deflects the stick");
	}
	{
		PcMenuPadKeys keys = keysNone();
		keys.down = true;
		std::uint16_t button = 0;
		std::int8_t sx = 0, sy = 0;
		apply(keys, true, false, startKey, 0, button, sx, sy);
		expect((button & PC_MENU_PAD_DOWN) != 0, 1, "menu Down sets the d-pad");
		expect((button & PC_MENU_PAD_UP) != 0, 0, "menu Down leaves Up clear");
		expect(sy, -127, "menu Down deflects the stick");
	}
	{
		PcMenuPadKeys keys = keysNone();
		std::uint16_t button = 0;
		std::int8_t sx = -40, sy = 0;
		apply(keys, true, false, startKey, 0, button, sx, sy);
		expect((button & PC_MENU_PAD_LEFT) != 0, 1, "menu stick left sets the d-pad");
		expect(sx, -127, "menu stick left reaches full deflection");
	}

	// A Start binding that is not an accept key still pauses.
	{
		PcMenuPadKeys keys = keysNone();
		keys.enter = true;
		std::uint16_t button = PC_MENU_PAD_START;
		std::int8_t sx = 0, sy = 0;
		apply(keys, true, false, SDL_SCANCODE_P, 0, button, sx, sy);
		expect((button & PC_MENU_PAD_A) != 0, 1, "rebound Start still accepts Enter");
		expect((button & PC_MENU_PAD_START) != 0, 1, "rebound Start stays Start");
	}

	// Free play leaves the pad the keyboard layer already filled.
	{
		PcMenuPadKeys keys = keysNone();
		keys.enter = true;
		keys.shiftL = true;
		keys.left = true;
		std::uint16_t button = PC_MENU_PAD_START | PC_MENU_PAD_B;
		std::int8_t sx = 40, sy = -20;
		apply(keys, false, false, startKey, 0, button, sx, sy);
		expect(button, PC_MENU_PAD_START | PC_MENU_PAD_B, "gameplay Enter stays Start and Shift stays B");
		expect((button & PC_MENU_PAD_A) != 0, 0, "gameplay Enter does not set A");
		expect(sx, 40, "gameplay leaves the horizontal stick");
		expect(sy, -20, "gameplay leaves the vertical stick");
	}

	// Shift bound to B is not B on a menu, but a text window still takes it to
	// show the whole page and turn it.
	{
		expect(pc_menu_pad_b_binding_counts(SDL_SCANCODE_LSHIFT, false), 0, "menu Left Shift B is not B");
		expect(pc_menu_pad_b_binding_counts(SDL_SCANCODE_RSHIFT, false), 0, "menu Right Shift B is not B");
		expect(pc_menu_pad_b_binding_counts(SDL_SCANCODE_LSHIFT, true), 1, "text window Left Shift B is B");
		expect(pc_menu_pad_b_binding_counts(SDL_SCANCODE_RSHIFT, true), 1, "text window Right Shift B is B");
		expect(pc_menu_pad_b_binding_counts(SDL_SCANCODE_LCTRL, false), 1, "menu Ctrl B is B");
		expect(pc_menu_pad_b_binding_counts(0, true), 0, "unbound B is never B");
		expect(pc_menu_pad_b_binding_counts(SDL_NUM_SCANCODES + SDL_BUTTON_RIGHT, true), 0,
		       "a mouse binding is not a held key");

		expect(pc_menu_pad_text_window_live(false, 0, 0), 0, "no text window yet");
		expect(pc_menu_pad_text_window_live(true, 1000, 1016), 1, "text window updated last frame");
		expect(pc_menu_pad_text_window_live(true, 1000, 1000 + PC_MENU_PAD_TEXT_WINDOW_MS), 1,
		       "text window at the edge of the window");
		expect(pc_menu_pad_text_window_live(true, 1000, 1001 + PC_MENU_PAD_TEXT_WINDOW_MS), 0,
		       "closed text window no longer takes Shift");
		expect(pc_menu_pad_text_window_live(true, 0xFFFFFFF0u, 0x10u), 1, "tick wrap stays live");

		// The B the translator sees is whatever the binding check says.
		PcMenuPadKeys keys = keysNone();
		keys.shiftL = true;
		std::uint16_t button = PC_MENU_PAD_B;
		std::int8_t sx = 0, sy = 0;
		apply(keys, true, pc_menu_pad_b_binding_counts(SDL_SCANCODE_LSHIFT, true), startKey, 0, button, sx, sy);
		expect((button & PC_MENU_PAD_B) != 0, 1, "text window Shift reaches the pad as B");
		button = PC_MENU_PAD_B;
		apply(keys, true, pc_menu_pad_b_binding_counts(SDL_SCANCODE_LSHIFT, false), startKey, 0, button, sx, sy);
		expect((button & PC_MENU_PAD_B) != 0, 0, "menu Shift still does not cancel");
	}

	if (sFailures == 0) {
		std::printf("pc_menu_pad_test: all checks passed\n");
		return 0;
	}
	std::printf("pc_menu_pad_test: %d failure(s)\n", sFailures);
	return 1;
}
