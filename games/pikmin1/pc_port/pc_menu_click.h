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

// First active rect that contains the point, expanded by margin. -1 on a miss.
// active may be null, which treats every rect as active. Inactive rects are skipped.
int pc_menu_click_hit(float x, float y, const PcMenuClickRect* rects, const int* active, int count, float margin);

// Title: any hit presses Start. Main menu: a hit activates that item.
// Any other screen ignores the point, including when it lands inside a rect.
PcMenuClickResult pc_menu_click_resolve(int screen, float x, float y, const PcMenuClickRect* rects, const int* active,
                                        int count, float margin);

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
