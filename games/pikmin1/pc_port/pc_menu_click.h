#ifndef PC_MENU_CLICK_H
#define PC_MENU_CLICK_H

// Left-click hit testing for the Pikmin 1 title screen and main menu.
//
// The decision function does not read the control mode. Classic mode ignores
// mouse buttons as game input; these menus still take a click.
//
// Coordinates are the menu graph: the same space as P2DPane::getGlobalBounds()
// after the menu is drawn. A window click is stored normalized (0..1) and
// mapped through the present letterbox and menu scale when a menu reads it.

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

// Present blit rectangle, then the menu scale that maps graph pixels onto the
// render target. scale/offset are in graph pixels for that menu's projection.
struct PcMenuClickFrame {
	int drawableW;
	int drawableH;
	int outX;
	int outY;
	int outW;
	int outH;
	float scaleX;
	float scaleY;
	float offsetX;
	float offsetY;
	float targetW;
	float targetH;
	int graphW;
	int graphH;
};

// Same slack the settings and touch menus already use around a row.
constexpr float kPcMenuClickMargin = 8.0f;

void pc_menu_click_output_area(int drawableW, int drawableH, float renderedAspect, int* outX, int* outY, int* outW,
                               int* outH);

bool pc_menu_click_map_to_graph(const PcMenuClickFrame& frame, float nx, float ny, float* x, float* y);

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

// Title: any click presses Start (the whole window). Main menu: a click activates
// that item; empty space does not. Any other screen ignores the point.
PcMenuClickResult pc_menu_click_resolve(int screen, float x, float y, const PcMenuClickRect* rects, const int* active,
                                        int count, float margin);

// Latest pointer position, normalized to the window. Moving it does not post a click.
void pc_menu_click_set_pointer(float nx, float ny);
void pc_menu_click_clear_pointer(void);
// Reads the pointer only while the highlight is armed (see below). False when no
// pointer is over the window, or after a key, controller input, or mouse button
// until the pointer moves again.
bool pc_menu_click_read_pointer_normalized(float* nx, float* ny);
// Legacy scale without letterbox; prefer pc_gfx_menu_pointer_to_graph at menu sites.
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
// click nobody takes. Suppressed while pointer menus are disabled (deterministic netplay day).
void pc_menu_click_post_back(void);
bool pc_menu_click_take_back(void);

// One pending click. begin_tick() runs on the logical input sample (PADRead),
// not on the extra poll at the end of the frame, so a click seen between those
// two polls is still available to the next menu update. A click nobody takes
// is dropped on the sample after that. pending() does not consume; take() does.
void pc_menu_click_reset(void);
void pc_menu_click_begin_tick(void);
void pc_menu_click_post(float nx, float ny);
void pc_menu_click_clear(void);
bool pc_menu_click_pending(float* nx, float* ny);
bool pc_menu_click_take(float* nx, float* ny);
bool pc_menu_click_take_graph(int virtW, int virtH, float* x, float* y);

// Off during a deterministic netplay day: no pointer clicks, highlight, claim, or back.
void pc_menu_click_set_enabled(bool enabled);

// Hand cursor. Every hover test (pc_menu_click_hover, and targets_hit / targets_hit_row for
// projected targets) records whether the pointer is over an active target. A locked row is a
// miss, so it never asks for the hand. begin_tick() latches the tick that ended:
// - pointer armed and some test ran: the hand follows that tick's result;
// - pointer disarmed (a click, key or pad press), or no test ran: the last state holds until
//   the pointer moves, as a browser keeps its cursor;
// - no pointer over the window, no menu claim (gameplay, another screen), or disabled: off.
// True while the system pointer should be a hand. pc_hand_cursor applies it.
bool pc_menu_click_hand_wanted(void);

// Window pixels to the menu graph. Returns false when a size is not positive.
bool pc_menu_click_window_to_graph(int winW, int winH, int x, int y, int virtW, int virtH, float* gx, float* gy);

// Click targets for panes that a perspective or rotated graph moves. A screen collects its
// targets while it draws: each tagged pane adds its drawn window rectangle (drawable pixels)
// between begin and end. The next click tests the last complete set of that kind. A set
// that is not ended is not kept, and a screen that draws none ends with an empty set.
enum {
	PC_MENU_CLICK_TARGET_KINDS = 8,
	PC_MENU_CLICK_TARGET_MAX   = 32,
};
void pc_menu_click_targets_begin(int kind);
bool pc_menu_click_targets_collecting(void);
void pc_menu_click_targets_add(int tag, PcMenuClickRect windowRect);
void pc_menu_click_targets_end(void);
// Nearest committed target of this kind under the point (drawable pixels). False on a miss.
bool pc_menu_click_targets_hit(int kind, float px, float py, float margin, int* tag);

// One row of cards, such as the three save files. Each card's cell is a tall box around
// the union of its target rectangles (rects with the same tag). Neighbouring cells touch
// at the midpoint between the two card centres, so a pointer sweeping along the row is
// always over exactly one card. The outer cells reach out as far as half the distance to
// the neighbour, and each cell is that half-pitch above and below its card's centre, never
// smaller than the card. Cells are half-open: [minX, maxX) x [minY, maxY).
// Returns the number of cells, sorted left to right.
int pc_menu_click_row_cells(const PcMenuClickRect* rects, const int* tags, int count, PcMenuClickRect* cells,
                            int* cellTags, int maxCells);
// The cell under the point. The current card's cell is widened by hold on every side, so
// a card that grows when selected does not flicker at the boundary. -1 when outside every cell.
int pc_menu_click_row_pick(float x, float y, const PcMenuClickRect* cells, const int* cellTags, int cellCount,
                           int currentTag, float hold);
// The committed targets of this kind as a row of cards (drawable pixels). False outside every cell.
bool pc_menu_click_targets_hit_row(int kind, float px, float py, int currentTag, float hold, int* tag);

#endif
