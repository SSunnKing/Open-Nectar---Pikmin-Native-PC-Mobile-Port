#include "netplay/pc_online_menu_rows.h"

#include <cstdio>

static int gFailures = 0;

static void check(bool cond, const char* what)
{
	if (cond) return;
	std::fprintf(stderr, "FAIL: %s\n", what);
	gFailures++;
}

int main()
{
	// The panel draw() places at the centre of 640x480 for a short page.
	const int panelW = 400;
	const int panelX = (640 - panelW) / 2;
	const int panelY = 80;
	PcMenuClickRect rows[5];
	pc_online_menu_row_rects(panelX, panelY, panelW, 5, rows);

	check(rows[0].minY == panelY + PC_ONLINE_ROW_TOP - 4, "first row sits under the title");
	check(rows[1].minY == rows[0].minY + PC_ONLINE_ROW_H, "rows step by the row height");
	check(rows[4].minX == rows[0].minX, "rows share the panel's left inset");

	const float midX = 0.5f * float(rows[2].minX + rows[2].maxX);
	const float midY = 0.5f * float(rows[2].minY + rows[2].maxY);
	check(pc_online_menu_row_hit(midX, midY, rows, 5) == 2, "middle of row 2");
	check(pc_online_menu_row_hit(midX, float(rows[0].minY) + 2.0f, rows, 5) == 0, "first row");
	check(pc_online_menu_row_hit(midX, float(rows[4].maxY) - 2.0f, rows, 5) == 4, "last row is Back");
	check(pc_online_menu_row_hit(midX, float(panelY + 20), rows, 5) == -1, "the title is not a row");
	check(pc_online_menu_row_hit(10, midY, rows, 5) == -1, "left of the panel");
	check(pc_online_menu_row_hit(midX, midY, nullptr, 0) == -1, "no rows");

	if (gFailures) {
		std::fprintf(stderr, "pc_online_menu_rows_test: %d failed\n", gFailures);
		return 1;
	}
	std::printf("pc_online_menu_rows_test: all passed\n");
	return 0;
}
