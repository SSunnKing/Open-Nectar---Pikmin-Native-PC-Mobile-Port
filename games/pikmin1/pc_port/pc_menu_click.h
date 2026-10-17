#ifndef PC_MENU_CLICK_H
#define PC_MENU_CLICK_H

// Left-click hit testing for the Pikmin 1 title screen and main menu.
//
// The decision function does not read the control mode. Classic mode ignores
// mouse buttons as game input; these menus still take a click.
//
// Coordinates are the menu graph: the same space as P2DPane::getGlobalBounds()
// after the menu is drawn. A window click is stored normalized (0..1) and
// scaled by the virtual menu size when a menu reads it.

enum PcMenuClickScreen {
	PC_MENU_CLICK_NONE  = 0, // any other screen: a click does not activate a button
	PC_MENU_CLICK_TITLE = 1, // Press Start
	PC_MENU_CLICK_MAIN  = 2, // Start / Co-op / VS / Options / ...
};

enum PcMenuClickAction {
	PC_MENU_CLICK_IGNORE      = 0,
	PC_MENU_CLICK_PRESS_START = 1,
	PC_MENU_CLICK_ACTIVATE    = 2,
	PC_MENU_CLICK_HIGHLIGHT   = 3, // pointer is over this button; do not activate
};

struct PcMenuClickRect {
	int minX;
	int minY;
	int maxX;
	int maxY;
};

struct PcMenuClickResult {
	int action; // PcMenuClickAction
	int index;  // button that was hit, or -1
};

// Same slack the settings and touch menus already use around a row.
constexpr float kPcMenuClickMargin = 8.0f;

// A full-screen backdrop covers the root on both axes. A button does not.
bool pc_menu_click_is_button_rect(PcMenuClickRect pane, PcMenuClickRect root);

// Nearest rect that contains the point, judged first without the margin and then with it.
// Rows may overlap, and the nearest centre wins. -1 on a miss, and also when the nearest
// row is inactive (a locked row must not hand its click to a neighbour). active may be
// null, which treats every rect as active.
int pc_menu_click_hit(float x, float y, const PcMenuClickRect* rects, const int* active, int count, float margin);

// Pointer moved, no click. A hit highlights that button and does not activate it.
// Empty space and any other screen leave the result as ignore, index -1.
PcMenuClickResult pc_menu_click_hover(int screen, float x, float y, const PcMenuClickRect* rects, const int* active,
                                      int count, float margin);

// Title: a click on a button presses Start. Main menu: a click activates that item.
// Empty space does not activate. Any other screen ignores the point.
PcMenuClickResult pc_menu_click_resolve(int screen, float x, float y, const PcMenuClickRect* rects, const int* active,
                                        int count, float margin);

// Latest pointer position, normalized to the window. Moving it does not post a click.
void pc_menu_click_set_pointer(float nx, float ny);
void pc_menu_click_clear_pointer(void);
// Reads the pointer only while the highlight is armed (see below). False when no
// pointer is over the window, or after a key, controller input, or mouse button
// until the pointer moves again.
bool pc_menu_click_read_pointer_graph(int virtW, int virtH, float* x, float* y);

// Pointer motion arms the highlight that follows the pointer. A key press, a controller
// input, or a mouse button disarms it until the pointer moves again. A pointer resting
// on a button, such as after a click on Press Start, does not take the highlight back.
void pc_menu_click_disarm_hover(void);

// True once after pointer motion or a click, so the title screen can count the mouse as
// input for its idle timeout. Taking it clears it.
bool pc_menu_click_take_activity(void);

// The title screen and its main menu claim the pointer each frame they are shown. While the
// claim holds, a left click is their click, not A, so empty space does not press Start.
void pc_menu_click_claim_pointer_menu(void);
bool pc_menu_click_pointer_menu_active(void);

// A right click backs out of the VS lobby. One pending press, taken once. It lapses like a
// click nobody takes.
void pc_menu_click_post_back(void);
bool pc_menu_click_take_back(void);

// One pending click. begin_tick() runs on the logical input sample (PADRead),
// not on the extra poll at the end of the frame, so a click seen between those
// two polls is still available to the next menu update. A click nobody takes
// is dropped on the sample after that.
void pc_menu_click_reset(void);
void pc_menu_click_begin_tick(void);
void pc_menu_click_post(float nx, float ny);
void pc_menu_click_clear(void);
bool pc_menu_click_take(float* nx, float* ny);
bool pc_menu_click_take_graph(int virtW, int virtH, float* x, float* y);

// Window pixels to the menu graph. Returns false when a size is not positive.
bool pc_menu_click_window_to_graph(int winW, int winH, int x, int y, int virtW, int virtH, float* gx, float* gy);

#endif
