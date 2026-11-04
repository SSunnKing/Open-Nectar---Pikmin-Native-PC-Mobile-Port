#include "settings/pc_prompt_pointer.h"

int pc_newgame_panel_x(int screenW) { return screenW / 2 - kPcNewGamePanelW / 2; }
int pc_newgame_panel_y(int screenH) { return screenH / 2 - kPcNewGamePanelH / 2; }
int pc_newgame_row_y(int panelY, int row) { return panelY + 72 + row * 70; }
int pc_newgame_box_x(int panelX, int col) { return panelX + 40 + col * (kPcNewGameBoxW + 40); }
int pc_newgame_start_x(int panelX) { return panelX + kPcNewGamePanelW / 2 - kPcNewGameBoxW / 2; }

void pc_newgame_pointer_rects(int screenW, int screenH, PcMenuClickRect out[PC_NEWGAME_PTR_COUNT])
{
	const int panelX = pc_newgame_panel_x(screenW);
	const int panelY = pc_newgame_panel_y(screenH);
	for (int row = 0; row < kPcNewGameRows; ++row) {
		const int y = pc_newgame_row_y(panelY, row);
		for (int col = 0; col < 2; ++col) {
			const int x       = pc_newgame_box_x(panelX, col);
			out[row * 2 + col] = { x, y, x + kPcNewGameBoxW, y + kPcNewGameBoxH };
		}
	}
	const int sx              = pc_newgame_start_x(panelX);
	const int sy              = pc_newgame_row_y(panelY, kPcNewGameRows);
	out[PC_NEWGAME_PTR_START] = { sx, sy, sx + kPcNewGameBoxW, sy + kPcNewGameBoxH };
}

void pc_captain_panel_origin(int screenW, int screenH, int extraH, int* panelX, int* panelY)
{
	const int panelH = 260 + extraH;
	if (panelX) *panelX = screenW / 2 - kPcCaptainPanelW / 2;
	if (panelY) *panelY = screenH / 2 - panelH / 2;
}

int pc_captain_row_left(int shown)
{
	return (kPcCaptainPanelW - (shown * kPcCaptainBoxW + (shown - 1) * kPcCaptainBoxGap)) / 2;
}

PcMenuClickRect pc_captain_card_rect(int panelX, int panelY, int screenRow, int col, int shown, int boxH)
{
	const int x = panelX + pc_captain_row_left(shown) + col * (kPcCaptainBoxW + kPcCaptainBoxGap);
	const int y = panelY + 84 + screenRow * (boxH + kPcCaptainBoxGap);
	return { x, y, x + kPcCaptainBoxW, y + boxH };
}

void pc_captain_arrow_rects(int panelX, int panelY, int screenRow, int shown, int boxH, PcMenuClickRect* left,
                            PcMenuClickRect* right)
{
	const int rowLeft = panelX + pc_captain_row_left(shown);
	const int width   = shown * (kPcCaptainBoxW + kPcCaptainBoxGap) - kPcCaptainBoxGap;
	const int y       = panelY + 84 + screenRow * (boxH + kPcCaptainBoxGap);
	const int leftX   = rowLeft - 30;
	const int rightX  = rowLeft + width + 6;
	if (left) *left = { leftX, y, leftX + kPcCaptainArrowW, y + boxH };
	if (right) *right = { rightX, y, rightX + kPcCaptainArrowW, y + boxH };
}
