#ifndef PC_PROMPT_POINTER_H
#define PC_PROMPT_POINTER_H

// Geometry of the New Game and Captain prompts, shared by their draw and their
// mouse hit tests. Everything is in the prompt's graph space: the screenW x
// screenH ortho the prompt draws in (the wide menu width when the menu is wide).
// No SDL, GX, or game state here, so ctest can check it headlessly.

#include "pc_menu_click.h"

// ── New Game ──
// Three rows of two options (Difficulty, Game mode, Skip tutorial), then Start.
constexpr int kPcNewGamePanelW = 620;
constexpr int kPcNewGamePanelH = 420;
constexpr int kPcNewGameBoxW   = 230;
constexpr int kPcNewGameBoxH   = 44;
constexpr int kPcNewGameRows   = 3;

// Pointer targets: option row*2+col (0..5), then Start (6).
enum {
	PC_NEWGAME_PTR_START = kPcNewGameRows * 2,
	PC_NEWGAME_PTR_COUNT = kPcNewGameRows * 2 + 1,
};

int pc_newgame_panel_x(int screenW);
int pc_newgame_panel_y(int screenH);
int pc_newgame_row_y(int panelY, int row); // row kPcNewGameRows is the Start row
int pc_newgame_box_x(int panelX, int col);
int pc_newgame_start_x(int panelX);
void pc_newgame_pointer_rects(int screenW, int screenH, PcMenuClickRect out[PC_NEWGAME_PTR_COUNT]);

// ── Captain ──
// Rows of up to five cards centred in a 620 wide panel; page arrows flank the
// custom-character row when it has more than one page.
constexpr int kPcCaptainPanelW   = 620;
constexpr int kPcCaptainBoxW     = 104;
constexpr int kPcCaptainBoxGap   = 9;
constexpr int kPcCaptainRowSlots = 5;
constexpr int kPcCaptainArrowW   = 24;

// Panel corner for a panel 260 + extraH tall, centred on the screen.
void pc_captain_panel_origin(int screenW, int screenH, int extraH, int* panelX, int* panelY);
// Left edge of a row of `shown` cards, relative to the panel.
int pc_captain_row_left(int shown);
// Card at column col of the screenRow-th visible row, which shows `shown` cards.
PcMenuClickRect pc_captain_card_rect(int panelX, int panelY, int screenRow, int col, int shown, int boxH);
// Page arrows beside a row of `shown` cards (the draw puts the glyph 8 px in).
void pc_captain_arrow_rects(int panelX, int panelY, int screenRow, int shown, int boxH, PcMenuClickRect* left,
                            PcMenuClickRect* right);

#endif
