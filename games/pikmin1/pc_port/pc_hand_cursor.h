#ifndef PC_HAND_CURSOR_H
#define PC_HAND_CURSOR_H

// System pointer shape over the pointer menus: a hand over anything a click activates, the
// arrow everywhere else, like the launcher's hand cursor and CSS cursor: pointer.
//
// This file holds the decision only. The window layer gives it a setter that calls
// SDL_SetCursor, so tests run it with a stub and no window. The setter runs only when the
// shape changes.

enum PcHandCursorShape {
	PC_HAND_CURSOR_ARROW = 0,
	PC_HAND_CURSOR_HAND  = 1,
};

typedef void (*PcHandCursorSetter)(int shape);

// Starts at the arrow, which is what the window shows before anything is set.
void pc_hand_cursor_init(PcHandCursorSetter setter);

// Once per logical tick. pointerFree: the system pointer is shown, not captured or in
// relative mode, and the window has focus. wantHand: pc_menu_click_hand_wanted(). The hand
// shows only when both hold. When the pointer is not free the arrow goes back once and
// nothing else is set, so the game's own hiding and capture are left alone.
void pc_hand_cursor_update(bool pointerFree, bool wantHand);

// Back to the arrow now, such as on focus loss, a gameplay change, or shutdown. No call
// when the arrow is already showing.
void pc_hand_cursor_reset(void);

int pc_hand_cursor_shape(void);

#endif
