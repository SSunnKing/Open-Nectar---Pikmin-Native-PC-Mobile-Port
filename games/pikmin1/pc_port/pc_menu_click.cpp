#include "pc_menu_click.h"

static bool sPending = false;
static float sNx     = 0.0f;
static float sNy     = 0.0f;
static int sTick     = 0;
static int sPostedTick = -100;
static bool sHasPointer = false;
static float sPointerNx = 0.0f;
static float sPointerNy = 0.0f;
static bool sSeenPointer = false;
static bool sMouseDrives = false;

bool pc_menu_click_is_button_rect(PcMenuClickRect pane, PcMenuClickRect root)
{
	const int paneW = pane.maxX - pane.minX;
	const int paneH = pane.maxY - pane.minY;
	const int rootW = root.maxX - root.minX;
	const int rootH = root.maxY - root.minY;
	if (paneW <= 0 || paneH <= 0 || rootW <= 0 || rootH <= 0) {
		return false;
	}
	const bool coversRoot = paneW >= rootW && paneH >= rootH;
	return !coversRoot;
}

int pc_menu_click_hit(float x, float y, const PcMenuClickRect* rects, const int* active, int count, float margin)
{
	if (!rects || count <= 0) {
		return -1;
	}
	for (int i = 0; i < count; i++) {
		if (active && !active[i]) {
			continue;
		}
		const PcMenuClickRect& b = rects[i];
		if (x >= float(b.minX) - margin && x <= float(b.maxX) + margin && y >= float(b.minY) - margin
		    && y <= float(b.maxY) + margin) {
			return i;
		}
	}
	return -1;
}

static bool pc_menu_click_is_menu_screen(int screen)
{
	return screen == PC_MENU_CLICK_TITLE || screen == PC_MENU_CLICK_MAIN;
}

PcMenuClickResult pc_menu_click_hover(int screen, float x, float y, const PcMenuClickRect* rects, const int* active,
                                      int count, float margin)
{
	PcMenuClickResult result;
	result.action = PC_MENU_CLICK_IGNORE;
	result.index  = -1;
	if (!pc_menu_click_is_menu_screen(screen)) {
		return result;
	}
	const int hit = pc_menu_click_hit(x, y, rects, active, count, margin);
	if (hit < 0) {
		return result;
	}
	result.index  = hit;
	result.action = PC_MENU_CLICK_HIGHLIGHT;
	return result;
}

PcMenuClickResult pc_menu_click_resolve(int screen, float x, float y, const PcMenuClickRect* rects, const int* active,
                                        int count, float margin)
{
	PcMenuClickResult result;
	result.action = PC_MENU_CLICK_IGNORE;
	result.index  = -1;
	if (!pc_menu_click_is_menu_screen(screen)) {
		return result;
	}
	const int hit = pc_menu_click_hit(x, y, rects, active, count, margin);
	if (hit < 0) {
		return result;
	}
	result.index = hit;
	result.action = (screen == PC_MENU_CLICK_TITLE) ? PC_MENU_CLICK_PRESS_START : PC_MENU_CLICK_ACTIVATE;
	return result;
}

void pc_menu_click_reset(void)
{
	sPending    = false;
	sNx         = 0.0f;
	sNy         = 0.0f;
	sTick       = 0;
	sPostedTick = -100;
	sHasPointer = false;
	sPointerNx  = 0.0f;
	sPointerNy  = 0.0f;
	sSeenPointer = false;
	sMouseDrives = false;
}

void pc_menu_click_set_pointer(float nx, float ny)
{
	// A pointer that moved since the last sample hands the menus to the mouse.
	// The first sample after a reset is not a move.
	if (sSeenPointer && (nx != sPointerNx || ny != sPointerNy)) {
		sMouseDrives = true;
	}
	sSeenPointer = true;
	sHasPointer  = true;
	sPointerNx   = nx;
	sPointerNy   = ny;
}

void pc_menu_click_clear_pointer(void)
{
	sHasPointer = false;
}

void pc_menu_click_note_mouse(void)
{
	sMouseDrives = true;
}

void pc_menu_click_note_keys(void)
{
	sMouseDrives = false;
}

bool pc_menu_click_read_pointer_graph(int virtW, int virtH, float* x, float* y)
{
	if (!sHasPointer || !sMouseDrives || virtW <= 0 || virtH <= 0) {
		return false;
	}
	if (x) {
		*x = sPointerNx * float(virtW);
	}
	if (y) {
		*y = sPointerNy * float(virtH);
	}
	return true;
}

void pc_menu_click_begin_tick(void)
{
	sTick++;
	if (sPending && (sTick - sPostedTick) > 1) {
		sPending = false;
	}
}

void pc_menu_click_post(float nx, float ny)
{
	sPending    = true;
	sNx         = nx;
	sNy         = ny;
	sPostedTick = sTick;
}

void pc_menu_click_clear(void)
{
	sPending = false;
}

bool pc_menu_click_take(float* nx, float* ny)
{
	if (!sPending) {
		return false;
	}
	if (nx) {
		*nx = sNx;
	}
	if (ny) {
		*ny = sNy;
	}
	sPending = false;
	return true;
}

bool pc_menu_click_take_graph(int virtW, int virtH, float* x, float* y)
{
	if (virtW <= 0 || virtH <= 0 || !sPending) {
		return false;
	}
	float nx = 0.0f;
	float ny = 0.0f;
	if (!pc_menu_click_take(&nx, &ny)) {
		return false;
	}
	if (x) {
		*x = nx * float(virtW);
	}
	if (y) {
		*y = ny * float(virtH);
	}
	return true;
}

bool pc_menu_click_window_to_graph(int winW, int winH, int x, int y, int virtW, int virtH, float* gx, float* gy)
{
	if (winW <= 0 || winH <= 0 || virtW <= 0 || virtH <= 0) {
		return false;
	}
	const float nx = float(x) / float(winW);
	const float ny = float(y) / float(winH);
	if (gx) {
		*gx = nx * float(virtW);
	}
	if (gy) {
		*gy = ny * float(virtH);
	}
	return true;
}
