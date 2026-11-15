#include "netplay/pc_online_menu_rows.h"

void pc_online_menu_row_rects(int panelX, int panelY, int panelW, int rowCount, PcMenuClickRect* out)
{
	if (!out || rowCount <= 0) return;
	const int width = panelW - 2 * PC_ONLINE_PAD + 20;
	int ry          = panelY + PC_ONLINE_ROW_TOP;
	for (int i = 0; i < rowCount; i++) {
		const int x = panelX + PC_ONLINE_PAD - 10;
		const int y = ry - 4;
		out[i]      = PcMenuClickRect{ x, y, x + width, y + (PC_ONLINE_ROW_H - 2) };
		ry += PC_ONLINE_ROW_H;
	}
}

int pc_online_menu_row_hit(float x, float y, const PcMenuClickRect* rows, int count)
{
	return pc_menu_click_hit(x, y, rows, nullptr, count, 4.0f);
}
