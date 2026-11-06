#pragma once

// Approximate GameCube 640x480 HUD layout for stand-in .blo files (issue #115).
// Positions follow the retail play09/play10/play_day screens; tolerances in tests
// allow a few pixels of slack for stand-in art tweaks.

namespace nectar::standin_layout {

struct Rect {
	int x, y, w, h;
};

inline constexpr int kScreenW = 640;
inline constexpr int kScreenH = 480;

inline constexpr Rect kPlay10Life { 552, 400, 72, 72 };
inline constexpr Rect kPlay10Navi { 564, 412, 48, 48 };
inline constexpr Rect kPlay10LiI { 564, 412, 48, 48 };
inline constexpr Rect kPlay10NaI { 552, 400, 72, 72 };

inline constexpr Rect kPlay09Stat { 0, 0, 640, 40 };
inline constexpr Rect kPlay09Goal { 280, 0, 80, 40 };
inline constexpr Rect kPlay09Suni { 300, 4, 320, 32 };
inline constexpr Rect kPlay09Sunc { 308, 8, 24, 24 };
inline constexpr Rect kPlay09Sun1 { 340, 6, 28, 28 };
inline constexpr Rect kPlay09Sun2 { 380, 6, 28, 28 };
inline constexpr Rect kPlay09Moon { 420, 6, 28, 28 };

inline constexpr Rect kPlayDayPiki { 8, 392, 48, 48 };
// Bottom-centre digit grid (left of the life gauge); last digit stays inside 640.
inline constexpr Rect kPlayDayCounters { 40, 416, 472, 56 };
inline constexpr Rect kPlayDayDate { 24, 52, 120, 32 };

inline constexpr int kCounterCell = 24;
inline constexpr int kCounterStep = 26;
inline constexpr int kCounterRow0Y  = 416;
inline constexpr int kCounterRow1Y  = 444;
inline constexpr int kCounterStartX = 48;
inline constexpr int kTopBandMaxY = 120;

// Bottom-right HUD cluster (life gauge + portrait); used by blo_writer_test.
inline constexpr int kBottomRightMinX = kPlay10Life.x;
inline constexpr int kBottomRightMinY = kPlay10Life.y;

// Map and controller menu (#99): cont_y, m_menu2, m_menu2m and cont_00.
inline constexpr Rect kMenuArrowL { 2, 204, 36, 72 };
inline constexpr Rect kMenuArrowR { 602, 204, 36, 72 };
inline constexpr Rect kMenuTitle { 64, 28, 512, 40 };
// The radar's window (m_menu2m 'maps'): left half, under the title.
inline constexpr Rect kMenuMap { 40, 84, 336, 320 };
inline constexpr Rect kMenuInfoPanel { 384, 80, 212, 304 };
inline constexpr int kMenuCountsX = 400;
// The pad drawing on the controller page, between the two columns of windows.
inline constexpr Rect kMenuPad { 232, 150, 172, 108 };

// Title menus (#99): m_select, m_selec2, option, s_select, v_select, ms_selec. Rows are centred
// text boxes stacked from kTitleRowY; the glass panel ('yoko') sits behind them with the
// menu's name on top. Press START sits low in the middle, under where the logo would be.
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

} // namespace nectar::standin_layout
