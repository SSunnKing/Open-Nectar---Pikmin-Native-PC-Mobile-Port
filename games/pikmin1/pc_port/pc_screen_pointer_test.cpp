#include "pc_screen_pointer.h"

#include <cstdio>

static int gFailures = 0;

static void check(bool cond, const char* what)
{
	if (cond) return;
	std::fprintf(stderr, "FAIL: %s\n", what);
	gFailures++;
}

static PcMenuClickRect dayBox(int col, int row)
{
	const int x = 40 + col * 56;
	const int y = 80 + row * 70;
	return PcMenuClickRect{ x, y, x + 48, y + 48 };
}

int main()
{
	PcMenuClickRect days[30];
	for (int i = 0; i < 30; i++) days[i] = dayBox(i % 10, i / 10);

	const PcMenuClickRect first = days[0];
	check(pc_diary_day_hit(first.minX + 4, first.minY + 4, days, 30, 4) == 0, "day 0 centre");
	const PcMenuClickRect day4 = days[4];
	check(pc_diary_day_hit(day4.minX + 4, day4.minY + 4, days, 30, 4) == 4, "current day");
	const PcMenuClickRect day5 = days[5];
	check(pc_diary_day_hit(day5.minX + 4, day5.minY + 4, days, 30, 4) == -1, "locked day is not a hit");
	check(pc_diary_day_hit(0, 0, days, 30, 4) == -1, "empty space");
	check(pc_diary_day_hit(first.minX + 4, first.minY + 4, nullptr, 0, 4) == -1, "no days");

	const PcMenuClickRect start = pc_worldmap_start_rect();
	check(pc_graph_rect_contains(start, 520, 20), "start chip");
	check(!pc_graph_rect_contains(start, 320, 240), "map centre is not start");
	check(!pc_graph_rect_contains(start, start.maxX, start.minY + 1), "half-open max edge");

	const PcMenuClickRect close = pc_map_menu_close_rect();
	check(pc_graph_rect_contains(close, 560, 450), "map close chip");
	check(!pc_graph_rect_contains(close, 20, 40), "arrow is not the close chip");

	const PcMenuClickRect ok = pc_onion_confirm_rect(640, 480);
	check(pc_graph_rect_contains(ok, 320, 440), "onion OK");
	check(!pc_graph_rect_contains(ok, 320, 200), "onion numbers are not OK");

	const PcMenuClickRect pause = pc_hud_pause_rect(800);
	check(pc_graph_rect_contains(pause, 760, 16), "pause chip on a wide HUD");
	check(!pc_graph_rect_contains(pause, 40, 16), "left side is not pause");

	check(pc_map_arrow_letter_inside(50, 80), "letter sits inside the 50x80 arrow");
	check(!pc_map_arrow_letter_inside(20, 20), "a tiny pane does not hold the letter");

	const PcMenuClickRect projected = pc_projected_rect(90, 40, 10, 80);
	check(projected.minX == 10 && projected.minY == 40 && projected.maxX == 90 && projected.maxY == 80,
	      "projected corners swap into a rect");

	check(pc_onion_arrow_step(40, 10, 20, 0, 60, 40) == -1, "upper half sends Pikmin in");
	check(pc_onion_arrow_step(40, 30, 20, 0, 60, 40) == 1, "lower half takes Pikmin out");
	check(pc_onion_arrow_step(5, 10, 20, 0, 60, 40) == 0, "outside the arrow");
	check(pc_onion_arrow_step(40, 10, 60, 40, 20, 0) == -1, "swapped corners still split on y");

	check(pc_hud_icon_action(50, 50, 40, 40, 60, 60, 0, true) == PC_HUD_ICON_COLOR_NEXT, "left click next colour");
	check(pc_hud_icon_action(50, 50, 40, 40, 60, 60, 0, false) == PC_HUD_ICON_COLOR_PREV, "right click previous colour");
	check(pc_hud_icon_action(40 - 5, 50, 40, 40, 60, 60, 1, true) == PC_HUD_ICON_COLOR_NEXT, "pad around the icon");
	check(pc_hud_icon_action(10, 10, 40, 40, 60, 60, 0, true) == PC_HUD_ICON_NONE, "far from the icon");
	check(pc_hud_icon_action(50, 50, 40, 40, 60, 60, 4, true) == PC_HUD_ICON_NONE, "icon drawn too long ago");

	pc_screen_pointer_reset();
	check(pc_hud_icon_action_at(50, 50, true) == PC_HUD_ICON_NONE, "no icon until it is drawn");
	pc_hud_color_icon_mark(60, 60, 40, 40);
	check(pc_hud_icon_action_at(50, 50, true) == PC_HUD_ICON_COLOR_NEXT, "marked icon");
	pc_screen_pointer_begin_frame();
	pc_screen_pointer_begin_frame();
	pc_screen_pointer_begin_frame();
	check(pc_hud_icon_action_at(50, 50, true) == PC_HUD_ICON_COLOR_NEXT, "still fresh after three samples");
	pc_screen_pointer_begin_frame();
	check(pc_hud_icon_action_at(50, 50, true) == PC_HUD_ICON_NONE, "stale after the HUD stops drawing");

	pc_screen_pointer_reset();
	pc_onion_arrow_mark(10, 10, 30, 50);
	check(pc_onion_arrow_step_at(20, 15) == -1, "marked arrow upper half");
	pc_onion_confirm_mark(100, 100, 140, 120);
	check(pc_onion_confirm_hit(120, 110), "marked OK");
	check(!pc_onion_confirm_hit(20, 20), "arrow is not OK");
	pc_hud_pause_mark(200, 8, 280, 36);
	check(pc_hud_pause_hit(240, 20), "marked pause");
	check(!pc_hud_pause_hit(50, 50), "icon is not pause");

	if (gFailures) {
		std::fprintf(stderr, "pc_screen_pointer_test: %d failed\n", gFailures);
		return 1;
	}
	std::printf("pc_screen_pointer_test: all passed\n");
	return 0;
}
