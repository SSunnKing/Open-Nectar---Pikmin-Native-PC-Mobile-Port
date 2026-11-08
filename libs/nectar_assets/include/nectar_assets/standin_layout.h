#pragma once

// Stand-in 640x480 layout for the HUD and Y-menu .blo files (issues #115, #99).
//
// Provenance (#190): authored on a 16-pixel grid with 16-pixel margins. The pane
// tags and types are the game's own search(.., true) calls (standin_ui.cpp cites
// each); the positions answer only to what the game code does with them:
//   - play10 (life gauge, captain portrait) is drawn at x offset 0 and play_day
//     (counters) at the wide-view offset dx (DrawGameInfo::draw, drawPlayer), so
//     the life cluster sits bottom left and the counters bottom right;
//   - hidePanesByRow splits play_day at y 300: the day digits stay above it, the
//     counters below;
//   - SunMove moves the sun panes between the centres of 'stat' and 'goal', so
//     those two are the ends of the sun track;
//   - checkStandInHudLayout (drawGameInfo.cpp, scenario builds) and
//     blo_writer_test check the relations below (on screen, sun track in the top
//     band, counters clear of the life gauge).
// The menu pages carry their own composition (map page: counts panel left,
// radar right; controller page: the pad in the middle, labelled windows in two
// columns; the page arrows at mid-height on the side edges, clear of the HUD
// corners, which stay drawn under the menu). Screenshot region checks
// (check_scenario_map_menu_screenshot.cmake) read these rectangles.

namespace nectar::standin_layout {

struct Rect {
	int x, y, w, h;
};

inline constexpr int kScreenW = 640;
inline constexpr int kScreenH = 480;
inline constexpr int kGrid    = 16;

// Life gauge and portrait, bottom left (play10.blo).
inline constexpr Rect kPlay10Life { 16, 400, 64, 64 };
inline constexpr Rect kPlay10Navi { 24, 408, 48, 48 };
inline constexpr Rect kPlay10LiI { 24, 408, 48, 48 };
inline constexpr Rect kPlay10NaI { 16, 400, 64, 64 };
// The corner the life cluster must stay in (blo_writer_test).
inline constexpr int kLifeCornerMaxX = 96;
inline constexpr int kLifeCornerMinY = 384;

// Sun track along the top (play09.blo): the capsule at the left end ('stat'),
// the fixed sun marker at the right end ('goal'); 'suni', sun2 and moon ride
// between them.
inline constexpr Rect kPlay09Stat { 32, 16, 32, 32 };
inline constexpr Rect kPlay09Goal { 576, 16, 32, 32 };
inline constexpr Rect kPlay09Suni { 32, 16, 32, 32 };
inline constexpr Rect kPlay09Sunc { 32, 16, 32, 32 };
inline constexpr Rect kPlay09Sun1 { 576, 16, 32, 32 };
inline constexpr Rect kPlay09Sun2 { 32, 16, 32, 32 };
inline constexpr Rect kPlay09Moon { 32, 16, 32, 32 };
inline constexpr int kTopBandMaxY = 120;

// Counters, bottom right (play_day.blo): the Pikmin icon, then a grid of
// digit cells, two rows of up to seven. The band holds both.
inline constexpr int kCounterCell   = 24;
inline constexpr int kCounterStep   = 32;
inline constexpr int kCounterRow0Y  = 400;
inline constexpr int kCounterRow1Y  = 432;
inline constexpr int kCounterStartX = 400;
inline constexpr Rect kPlayDayPiki { 344, 400, 48, 48 };
inline constexpr Rect kPlayDayCounters { 336, 392, 288, 72 };
// Day digits, top left under the sun track (above play_day's y 300 split).
inline constexpr Rect kPlayDayDate { 32, 64, 96, 32 };
inline constexpr int kDateStep = 32;

// Map and controller menu (#99): cont_y, m_menu2, m_menu2m and cont_00.
// The page arrows are 48 wide on the screen edges: ogDrawLR::update pulses ya_l's scale about
// its local x 48 and ya_r's about its local x 0, so each shrinks toward its inner edge.
inline constexpr Rect kMenuArrowL { 0, 208, 48, 64 };
inline constexpr Rect kMenuArrowR { 592, 208, 48, 64 };
inline constexpr Rect kMenuTitle { 64, 16, 512, 48 };
// Map page: the counts panel on the left, the radar's window (m_menu2m 'maps') on the right.
inline constexpr Rect kMenuInfoPanel { 64, 80, 224, 320 };
inline constexpr int kMenuCountsX = 80;
inline constexpr Rect kMenuMap { 304, 80, 272, 320 };
// Controller page: the pad drawing in the middle, between the two columns of windows.
inline constexpr Rect kMenuPad { 240, 144, 160, 96 };
inline constexpr int kMenuWindowLeftX  = 56;
inline constexpr int kMenuWindowRightX = 408;
inline constexpr int kMenuWindowW      = 176;
inline constexpr int kMenuWindowH      = 44;
inline constexpr int kMenuWindowTopY   = 80;
inline constexpr int kMenuWindowStep   = 52;

// Title menus (#99): m_select, m_selec2, option, s_select, v_select, ms_selec. Rows are centred
// text boxes stacked from kTitleRowY; the glass panel ('yoko') sits behind them with the
// menu's name on top. Press START sits low in the middle.
inline constexpr int kTitleRowX    = 180;
inline constexpr int kTitleRowW    = 280;
inline constexpr int kTitleRowH    = 28;
inline constexpr int kTitleRowStep = 36;
inline constexpr int kTitleRowY    = 260;
inline constexpr int kTitlePanelPad = 26;
inline constexpr Rect kPressStart { 160, 340, 320, 40 };

inline constexpr Rect titlePanel(int items)
{
	return { kTitleRowX - kTitlePanelPad - 20, kTitleRowY - kTitlePanelPad - 34, kTitleRowW + 2 * (kTitlePanelPad + 20),
		     kTitleRowStep * (items - 1) + kTitleRowH + 2 * kTitlePanelPad + 34 };
}

inline constexpr bool rectInsideScreen(Rect r)
{
	return r.x >= 0 && r.y >= 0 && r.x + r.w <= kScreenW && r.y + r.h <= kScreenH;
}

inline constexpr bool rectsOverlap(Rect a, Rect b)
{
	return a.x < b.x + b.w && a.x + a.w > b.x && a.y < b.y + b.h && a.y + a.h > b.y;
}

// Relations the layout keeps (checked at compile time; checkStandInHudLayout repeats the
// HUD ones at run time against the loaded panes).
static_assert(rectInsideScreen(kPlay10Life) && rectInsideScreen(kPlay10Navi), "life cluster on screen");
static_assert(kPlay10Life.x + kPlay10Life.w <= kLifeCornerMaxX && kPlay10Life.y >= kLifeCornerMinY, "life cluster in its corner");
static_assert(kPlay09Suni.y <= kTopBandMaxY && kPlay09Stat.x < kPlay09Goal.x, "sun track in the top band, left to right");
static_assert(!rectsOverlap(kPlay10Life, kPlayDayCounters), "counters clear of the life gauge");
static_assert(rectInsideScreen({ kCounterStartX + 6 * kCounterStep, kCounterRow0Y, kCounterCell, kCounterCell }), "last counter digit on screen");
static_assert(kCounterRow0Y >= 300 && kPlayDayPiki.y >= 300 && kPlayDayDate.y + kPlayDayDate.h < 300, "play_day rows on their side of y 300");
static_assert(!rectsOverlap(kMenuInfoPanel, kMenuMap) && !rectsOverlap(kMenuMap, kMenuArrowR) && !rectsOverlap(kMenuInfoPanel, kMenuArrowL),
              "map page panes apart");
static_assert(kMenuArrowL.x + kMenuArrowL.w < kMenuWindowLeftX && kMenuWindowLeftX + kMenuWindowW < kMenuPad.x &&
                  kMenuPad.x + kMenuPad.w < kMenuWindowRightX && kMenuWindowRightX + kMenuWindowW < kMenuArrowR.x,
              "controller page columns apart: arrow, windows, pad, windows, arrow");

} // namespace nectar::standin_layout
