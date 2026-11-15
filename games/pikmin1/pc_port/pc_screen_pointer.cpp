#include "pc_screen_pointer.h"

#include <algorithm>

namespace {

constexpr int kStale = 3;

struct Mark {
	float x0 = 0.0f;
	float y0 = 0.0f;
	float x1 = 0.0f;
	float y1 = 0.0f;
	int age  = 999;
};

Mark gOnionArrow;
Mark gOnionConfirm;
Mark gHudIcon;
Mark gHudPause;

void age(Mark& mark)
{
	if (mark.age < 999) mark.age++;
}

void markBox(Mark& mark, float x0, float y0, float x1, float y1)
{
	mark.x0  = std::min(x0, x1);
	mark.y0  = std::min(y0, y1);
	mark.x1  = std::max(x0, x1);
	mark.y1  = std::max(y0, y1);
	mark.age = (mark.x1 > mark.x0 && mark.y1 > mark.y0) ? 0 : 999;
}

bool fresh(const Mark& mark)
{
	return mark.age <= kStale;
}

} // namespace

int pc_diary_day_hit(float x, float y, const PcMenuClickRect* days, int dayCount, int currentDay)
{
	if (!days || dayCount <= 0) return -1;
	if (dayCount > 30) dayCount = 30;
	int active[30];
	for (int i = 0; i < dayCount; i++) active[i] = i <= currentDay ? 1 : 0;
	return pc_menu_click_hit(x, y, days, active, dayCount, 4.0f);
}

PcMenuClickRect pc_worldmap_start_rect(void)
{
	return PcMenuClickRect{ 508, 8, 632, 40 };
}

PcMenuClickRect pc_map_menu_close_rect(void)
{
	return PcMenuClickRect{ 508, 432, 632, 468 };
}

PcMenuClickRect pc_onion_confirm_rect(int virtW, int virtH)
{
	const int cx = virtW / 2;
	return PcMenuClickRect{ cx - 72, virtH - 64, cx + 72, virtH - 16 };
}

PcMenuClickRect pc_hud_pause_rect(int virtW)
{
	return PcMenuClickRect{ virtW - 112, 8, virtW - 8, 36 };
}

bool pc_graph_rect_contains(PcMenuClickRect rect, float x, float y)
{
	return x >= float(rect.minX) && y >= float(rect.minY) && x < float(rect.maxX) && y < float(rect.maxY);
}

bool pc_map_arrow_letter_inside(float width, float height)
{
	const float x0 = width * 0.5f - 14.0f;
	const float x1 = width * 0.5f + 14.0f;
	return width > 0.0f && height > 0.0f && x0 >= 0.0f && x1 <= width && 27.0f >= 0.0f && 59.0f <= height;
}

PcMenuClickRect pc_projected_rect(float x0, float y0, float x1, float y1)
{
	const float minX = std::min(x0, x1);
	const float minY = std::min(y0, y1);
	const float maxX = std::max(x0, x1);
	const float maxY = std::max(y0, y1);
	return PcMenuClickRect{ int(minX), int(minY), int(maxX), int(maxY) };
}

int pc_onion_arrow_step(float x, float y, float x0, float y0, float x1, float y1)
{
	const float minX = std::min(x0, x1);
	const float minY = std::min(y0, y1);
	const float maxX = std::max(x0, x1);
	const float maxY = std::max(y0, y1);
	if (x < minX || x > maxX || y < minY || y > maxY) return 0;
	const float mid = 0.5f * (minY + maxY);
	return y < mid ? -1 : 1;
}

int pc_hud_icon_action(float x, float y, float x0, float y0, float x1, float y1, int ageFrames, bool left)
{
	if (ageFrames > kStale) return PC_HUD_ICON_NONE;
	const float minX = std::min(x0, x1);
	const float minY = std::min(y0, y1);
	const float maxX = std::max(x0, x1);
	const float maxY = std::max(y0, y1);
	const float pad  = (maxY - minY) * 0.35f;
	if (x < minX - pad || x > maxX + pad || y < minY - pad || y > maxY + pad) return PC_HUD_ICON_NONE;
	return left ? PC_HUD_ICON_COLOR_NEXT : PC_HUD_ICON_COLOR_PREV;
}

void pc_screen_pointer_begin_frame(void)
{
	age(gOnionArrow);
	age(gOnionConfirm);
	age(gHudIcon);
	age(gHudPause);
}

void pc_screen_pointer_reset(void)
{
	gOnionArrow = Mark();
	gOnionConfirm = Mark();
	gHudIcon = Mark();
	gHudPause = Mark();
}

void pc_onion_arrow_mark(float x0, float y0, float x1, float y1) { markBox(gOnionArrow, x0, y0, x1, y1); }

int pc_onion_arrow_step_at(float x, float y)
{
	if (!fresh(gOnionArrow)) return 0;
	return pc_onion_arrow_step(x, y, gOnionArrow.x0, gOnionArrow.y0, gOnionArrow.x1, gOnionArrow.y1);
}

void pc_onion_confirm_mark(float x0, float y0, float x1, float y1) { markBox(gOnionConfirm, x0, y0, x1, y1); }

bool pc_onion_confirm_hit(float x, float y)
{
	if (!fresh(gOnionConfirm)) return false;
	return x >= gOnionConfirm.x0 && x <= gOnionConfirm.x1 && y >= gOnionConfirm.y0 && y <= gOnionConfirm.y1;
}

void pc_hud_color_icon_mark(float x0, float y0, float x1, float y1) { markBox(gHudIcon, x0, y0, x1, y1); }

int pc_hud_icon_action_at(float x, float y, bool left)
{
	if (!fresh(gHudIcon)) return PC_HUD_ICON_NONE;
	return pc_hud_icon_action(x, y, gHudIcon.x0, gHudIcon.y0, gHudIcon.x1, gHudIcon.y1, gHudIcon.age, left);
}

void pc_hud_pause_mark(float x0, float y0, float x1, float y1) { markBox(gHudPause, x0, y0, x1, y1); }

bool pc_hud_pause_hit(float x, float y)
{
	if (!fresh(gHudPause)) return false;
	return x >= gHudPause.x0 && x <= gHudPause.x1 && y >= gHudPause.y0 && y <= gHudPause.y1;
}
