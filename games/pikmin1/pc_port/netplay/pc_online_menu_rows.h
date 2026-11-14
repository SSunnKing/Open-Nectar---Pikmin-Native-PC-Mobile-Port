#ifndef PC_ONLINE_MENU_ROWS_H
#define PC_ONLINE_MENU_ROWS_H

// Row rectangles for the local/online menu. The panel is laid out in the
// 640x480 graph: a title, then one row per option. Nothing here knows which
// page is open or reads the pointer.

#include "pc_menu_click.h"

// Matches pc_online_menu.cpp's draw().
enum {
	PC_ONLINE_ROW_H   = 30,
	PC_ONLINE_PAD     = 28,
	PC_ONLINE_ROW_TOP = 62,
};

// out[i] is option i. rowCount is how many options the page has.
void pc_online_menu_row_rects(int panelX, int panelY, int panelW, int rowCount, PcMenuClickRect* out);

// The option under the point, or -1. Locked rows are not used: every option is live.
int pc_online_menu_row_hit(float x, float y, const PcMenuClickRect* rows, int count);

#endif
