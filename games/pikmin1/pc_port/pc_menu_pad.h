#ifndef PC_MENU_PAD_H
#define PC_MENU_PAD_H

#include <SDL2/SDL.h>
#include <cstdint>

#ifndef PC_BIND_MOUSE_BASE
#define PC_BIND_MOUSE_BASE SDL_NUM_SCANCODES
#endif

// GameCube pad bits. Kept here so the translator can be tested without the
// rest of the window layer. pc_window.cpp static_asserts these against pad.h.
enum {
	PC_MENU_PAD_LEFT  = 0x0001,
	PC_MENU_PAD_RIGHT = 0x0002,
	PC_MENU_PAD_DOWN  = 0x0004,
	PC_MENU_PAD_UP    = 0x0008,
	PC_MENU_PAD_A     = 0x0100,
	PC_MENU_PAD_B     = 0x0200,
	PC_MENU_PAD_START = 0x1000,
};

// Keys the menu translator reads. Tests fill this directly.
struct PcMenuPadKeys {
	bool enter;
	bool kpEnter;
	bool space;
	bool escape;
	bool backspace;
	bool shiftL;
	bool shiftR;
	bool left, right, up, down;
	bool keyA, keyD, keyW, keyS;
	bool clickLeft;
	bool clickRight;
};

// menuMode false leaves the pad alone (free play). menuMode true makes Enter,
// Space and left click accept, Escape, Backspace and right click cancel, and
// arrows and WASD both move the cursor. Shift never cancels.
// nonShiftB is a non-Shift keyboard binding for B that is held right now.
// bindStart / bindStart2 are the Start bindings, so an accept key bound to
// Start does not also open leave or close a pause menu.
void pc_menu_pad_apply(const PcMenuPadKeys& keys, bool menuMode, bool nonShiftB,
                       int bindStart, int bindStart2,
                       std::uint16_t& button, std::int8_t& stickX, std::int8_t& stickY);

// Text windows (tutorial notes, Olimar's log entries, the Voyage Log, result
// messages) read B as "show the whole page" and "next page", as on the
// GameCube. An open window notes the time on each frame it updates, and a
// Shift binding for B counts as B while that note is this recent. Pause, the
// world map and every other menu still never take Shift as B.
constexpr std::uint32_t PC_MENU_PAD_TEXT_WINDOW_MS = 100;
bool pc_menu_pad_text_window_live(bool noted, std::uint32_t notedMs, std::uint32_t nowMs);

// Whether a held keyboard binding for B produces B in menu mode. Shift only
// does while a text window is live. Invalid bindings never do.
bool pc_menu_pad_b_binding_counts(int bind, bool textWindowLive);

// A click that went down and up before the sample is absent from the held
// mask and present only in the press mask. Either one is a click.
bool pc_menu_pad_click(std::uint32_t held, std::uint32_t pressed, int sdlButton);

// A finger is also delivered as a mouse event with this id. Menu clicks and
// mouse-button bindings ignore it. A physical mouse keeps its own id.
bool pc_menu_pad_mouse_is_touch(std::uint32_t which);

// Fold one mouse-button event into the real-mouse masks. Touch events are
// ignored. pressed keeps every real press until the caller takes it, including
// one that was released before the sample. held is the buttons still down.
void pc_menu_pad_note_mouse_button(std::uint32_t& held, std::uint32_t& pressed,
                                   std::uint32_t which, int sdlButton, bool down);

// 1 while a modal is reading the pad. Free play, the gameplay HUD and photo
// mode return 0. Defined by the game, not by the pure translator.
int pc_menu_mode_active(void);

#endif
