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
// Presses a pad button for the next sample, through the device that tapped.
void pc_menu_queue_button(unsigned short button);
// Mouse only: where the pointer rests (window-normalised), false once a key or
// pad button was used. Screens test it with pc_menu_pane_hit.
bool pc_menu_hover(float* nx, float* ny);
// Asks for the hand cursor this update; call every update with true or false.
void pc_menu_set_hand(bool hand);
#endif

#endif
