#ifndef PC_MENU_TAP_H
#define PC_MENU_TAP_H

// One tap source for Pikmin 2 menus: a touch tap or a left mouse click.
// A screen calls these four functions and does not care which device it was.
// Hover and the hand cursor stay mouse-only (pc_menu_pointer.h).

#if (defined(PIKI_PC_PORT) && PIKI_P2_HOST) || PIKI_PC_TOUCH
#define PIKI_PC_MENU_TAP 1
#else
#define PIKI_PC_MENU_TAP 0
#endif

#if PIKI_PC_MENU_TAP
// Call every update while the screen is up. Hides the game's touch layer and
// stops the mouse click from also becoming pad A.
void pc_menu_claim();
// Takes the pending tap as window-normalised coordinates.
bool pc_menu_take_tap(float* nx, float* ny);
// Whether the tap lies on what `pane` and its children drew, with a margin in
// fractions of the window height. The first call for a pane only starts
// tracking it.
bool pc_menu_pane_hit(const void* pane, float nx, float ny, float margin);
// A button plate for a screen with nothing to click: call every update. The
// plate shows while the mouse is in use and a click presses `padButton`.
// Asking also claims the pointer for the screen.
void pc_menu_plate(unsigned short padButton, const char* label);
// Mouse drag since the last call, window-normalised (left button held).
bool pc_menu_take_drag(float* dx, float* dy);
// Presses a pad button for the next sample, through the device that tapped.
void pc_menu_queue_button(unsigned short button);
// Mouse only: where the pointer rests (window-normalised), false once a key or
// pad button was used. Screens test it with pc_menu_pane_hit.
bool pc_menu_hover(float* nx, float* ny);
// Asks for the hand cursor this update. Only a true request counts, so two
// parts of one screen can each ask without cancelling the other; the request
// lapses a few ticks after the pointer leaves.
void pc_menu_set_hand(bool hand);
// Reads the pending mouse click without consuming it (the mouse only);
// pc_menu_drop_tap consumes it. For a part of a screen that must not steal
// clicks meant for the rest, such as the page arrows.
bool pc_menu_peek_tap(float* nx, float* ny);
void pc_menu_drop_tap();
// Eased scale for a pane the screen already draws. 1 at rest. A click calls press.
float pc_menu_hover_scale(const void* pane, bool hovered, float amount, float dt);
void pc_menu_hover_press(const void* pane);
#endif

#endif
