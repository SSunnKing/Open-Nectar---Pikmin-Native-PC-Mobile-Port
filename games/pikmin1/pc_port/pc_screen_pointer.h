#ifndef PC_SCREEN_POINTER_H
#define PC_SCREEN_POINTER_H

// Hit tests for the screens that have no GameCube glyph to click: the diary
// day grid, the world-map Start chip, the in-level map's Close chip, the
// onion's up/down arrow and OK chip, and the HUD colour icon. Nothing here
// reads the window or the disc. The screen records a rectangle while it
// draws; the next input sample asks whether the click landed in it.

#include "pc_menu_click.h"

enum {
	PC_HUD_ICON_NONE       = 0,
	PC_HUD_ICON_COLOR_NEXT = 1, // left click: the next squad colour
	PC_HUD_ICON_COLOR_PREV = 2, // right click: the previous squad colour
};

// Diary day grid, in the 640x480 graph. days[i] is day i. A day after
// currentDay is locked and is not a hit, even when the point is inside it.
// Returns the day index, or -1.
int pc_diary_day_hit(float x, float y, const PcMenuClickRect* days, int dayCount, int currentDay);

// Chips drawn by the port, in the graph space of the screen that draws them.
PcMenuClickRect pc_worldmap_start_rect(void);
PcMenuClickRect pc_map_menu_close_rect(void);
// Bottom centre of the onion window. virtW/virtH is the HUD-wide graph.
PcMenuClickRect pc_onion_confirm_rect(int virtW, int virtH);
// Top-right of the gameplay HUD graph.
PcMenuClickRect pc_hud_pause_rect(int virtW);

// Half-open: the shared edge belongs to the rect on the greater side.
bool pc_graph_rect_contains(PcMenuClickRect rect, float x, float y);

// The in-level map's L/R arrows are 50x80 pictures. The letter sits inside
// that picture; a click on the whole picture is the page turn. True when the
// letter box is inside the picture.
bool pc_map_arrow_letter_inside(float width, float height);

// Two projected corners (window pixels, either order) as one click rectangle.
PcMenuClickRect pc_projected_rect(float x0, float y0, float x1, float y1);

// Onion arrow in window pixels. -1 is the upper half (send Pikmin in), +1 the
// lower half (take Pikmin out), 0 a miss. y grows downward.
int pc_onion_arrow_step(float x, float y, float x0, float y0, float x1, float y1);

// HUD colour icon. ageFrames is how many input samples have passed since it
// was drawn; older than a few samples means the HUD is gone. left selects the
// next colour, a right click the previous one. The pad around the icon matches
// the touch target (about a third of its height).
int pc_hud_icon_action(float x, float y, float x0, float y0, float x1, float y1, int ageFrames, bool left);

// The rectangles recorded while drawing, and the clicks waiting on them.
void pc_screen_pointer_begin_frame(void);
void pc_screen_pointer_reset(void);

void pc_onion_arrow_mark(float x0, float y0, float x1, float y1);
int pc_onion_arrow_step_at(float x, float y);

void pc_onion_confirm_mark(float x0, float y0, float x1, float y1);
bool pc_onion_confirm_hit(float x, float y);

void pc_hud_color_icon_mark(float x0, float y0, float x1, float y1);
int pc_hud_icon_action_at(float x, float y, bool left);

void pc_hud_pause_mark(float x0, float y0, float x1, float y1);
bool pc_hud_pause_hit(float x, float y);

#endif
