#include "pc_menu_click.h"

#include <cmath>

static bool sPending     = false;
static float sNx         = 0.0f;
static float sNy         = 0.0f;
static int sTick         = 0;
static int sPostedTick   = -100;
static bool sHasPointer  = false;
static float sPointerNx  = 0.0f;
static float sPointerNy  = 0.0f;
static bool sSeenPointer = false;
static bool sMouseDrives = false;
static bool sActivity    = false;
static int sClaimTick    = -100;
static bool sBackPending = false;
static int sBackTick     = -100;
static bool sEnabled     = true;
// Hand cursor: what this tick's hover tests found, and the state begin_tick() latched.
static bool sHandHit     = false;
static bool sHandTested  = false;
static bool sHandShown   = false;

// Every hover test calls this with its result, so the pointer layer knows whether the
// pointer is over a clickable target without a second copy of any screen's geometry.
static void pc_menu_click_note_hand(bool hit)
{
	sHandTested = true;
	if (hit) {
		sHandHit = true;
	}
}

void pc_menu_click_output_area(int drawableW, int drawableH, float renderedAspect, int* outX, int* outY, int* outW, int* outH)
{
	const float windowAspect = float(drawableW) / float(drawableH);
	int x                    = 0;
	int y                    = 0;
	int w                    = 0;
	int h                    = 0;
	if (renderedAspect >= windowAspect) {
		w = drawableW;
		h = int(lroundf(float(drawableW) / renderedAspect));
		x = 0;
		y = (drawableH - h) / 2;
	} else {
		h = drawableH;
		w = int(lroundf(float(drawableH) * renderedAspect));
		x = (drawableW - w) / 2;
		y = 0;
	}
	if (outX) {
		*outX = x;
	}
	if (outY) {
		*outY = y;
	}
	if (outW) {
		*outW = w;
	}
	if (outH) {
		*outH = h;
	}
}

bool pc_menu_click_map_to_graph(const PcMenuClickFrame& frame, float nx, float ny, float* x, float* y)
{
	if (frame.drawableW <= 0 || frame.drawableH <= 0 || frame.outW <= 0 || frame.outH <= 0 || frame.graphW <= 0 || frame.graphH <= 0) {
		return false;
	}
	if (!(frame.scaleX > 0.0f) || !(frame.scaleY > 0.0f) || !(frame.targetW > 0.0f) || !(frame.targetH > 0.0f)) {
		return false;
	}
	const float winX       = nx * float(frame.drawableW);
	const float winY       = ny * float(frame.drawableH);
	const float rtX        = (winX - float(frame.outX)) / float(frame.outW) * frame.targetW;
	const float rtYUp      = (1.0f - (winY - float(frame.outY)) / float(frame.outH)) * frame.targetH;
	const float localX     = (rtX - frame.offsetX) / frame.scaleX;
	const float fromBottom = (rtYUp - frame.offsetY) / frame.scaleY;
	if (x) {
		*x = localX;
	}
	if (y) {
		*y = float(frame.graphH) - fromBottom;
	}
	return true;
}

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
	// Rows can overlap: a text pane is taller than the row pitch. The row whose centre is
	// nearest wins, so the boundary falls halfway between two rows. The margin only widens
	// the search when no row contains the point.
	for (int pass = 0; pass < 2; pass++) {
		const float slack = pass == 0 ? 0.0f : margin;
		int best          = -1;
		float bestDist    = 0.0f;
		for (int i = 0; i < count; i++) {
			const PcMenuClickRect& b = rects[i];
			if (x < float(b.minX) - slack || x > float(b.maxX) + slack || y < float(b.minY) - slack
			    || y > float(b.maxY) + slack) {
				continue;
			}
			const float dx   = x - 0.5f * float(b.minX + b.maxX);
			const float dy   = y - 0.5f * float(b.minY + b.maxY);
			const float dist = dx * dx + dy * dy;
			if (best < 0 || dist < bestDist) {
				best     = i;
				bestDist = dist;
			}
		}
		if (best >= 0) {
			// A locked row can be the nearest one. Its active neighbour must not take the click.
			return (active && !active[best]) ? -1 : best;
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
	pc_menu_click_note_hand(hit >= 0);
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
	if (screen == PC_MENU_CLICK_TITLE) {
		// Press Start: any left click in the window proceeds, not only a hit on the prompt.
		result.action = PC_MENU_CLICK_PRESS_START;
		result.index  = -1;
		return result;
	}
	const int hit = pc_menu_click_hit(x, y, rects, active, count, margin);
	if (hit < 0) {
		return result;
	}
	result.index  = hit;
	result.action = PC_MENU_CLICK_ACTIVATE;
	return result;
}

void pc_menu_click_reset(void)
{
	sPending     = false;
	sNx          = 0.0f;
	sNy          = 0.0f;
	sTick        = 0;
	sPostedTick  = -100;
	sHasPointer  = false;
	sPointerNx   = 0.0f;
	sPointerNy   = 0.0f;
	sSeenPointer = false;
	sMouseDrives = false;
	sActivity    = false;
	sClaimTick   = -100;
	sBackPending = false;
	sEnabled     = true;
	sHandHit     = false;
	sHandTested  = false;
	sHandShown   = false;
}

void pc_menu_click_set_pointer(float nx, float ny)
{
	if (!sEnabled) {
		return;
	}
	// A pointer that moved since the last sample arms the highlight. The first sample
	// after a reset is not a move.
	if (sSeenPointer && (nx != sPointerNx || ny != sPointerNy)) {
		sMouseDrives = true;
		sActivity    = true;
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

void pc_menu_click_disarm_hover(void)
{
	sMouseDrives = false;
}

bool pc_menu_click_take_activity(void)
{
	const bool was = sActivity;
	sActivity      = false;
	return was;
}

void pc_menu_click_claim_pointer_menu(void)
{
	if (!sEnabled) {
		return;
	}
	sClaimTick = sTick;
}

bool pc_menu_click_pointer_menu_active(void)
{
	return sEnabled && sTick - sClaimTick <= 2;
}

void pc_menu_click_post_back(void)
{
	if (!sEnabled) {
		return;
	}
	sBackPending = true;
	sBackTick    = sTick;
}

bool pc_menu_click_take_back(void)
{
	if (!sEnabled) {
		return false;
	}
	const bool was = sBackPending;
	sBackPending   = false;
	return was;
}

bool pc_menu_click_read_pointer_normalized(float* nx, float* ny)
{
	if (!sHasPointer || !sMouseDrives) {
		return false;
	}
	if (nx) {
		*nx = sPointerNx;
	}
	if (ny) {
		*ny = sPointerNy;
	}
	return true;
}

bool pc_menu_click_read_pointer_graph(int virtW, int virtH, float* x, float* y)
{
	float nx = 0.0f;
	float ny = 0.0f;
	if (virtW <= 0 || virtH <= 0 || !pc_menu_click_read_pointer_normalized(&nx, &ny)) {
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

void pc_menu_click_begin_tick(void)
{
	// Latch the tick that just ended. An armed pointer shows what its hover tests found.
	// A disarmed pointer (after a click, key or pad press), or a tick that ran no test,
	// keeps the last state until the pointer moves, as a browser keeps its cursor. No
	// pointer, no menu claim, or pointer menus off: no hand.
	const bool claimed = sEnabled && sTick - sClaimTick <= 2;
	if (!sEnabled || !sHasPointer || !claimed) {
		sHandShown = false;
	} else if (sMouseDrives && sHandTested) {
		sHandShown = sHandHit;
	}
	sHandHit    = false;
	sHandTested = false;
	sTick++;
	if (sPending && (sTick - sPostedTick) > 1) {
		sPending = false;
	}
	if (sBackPending && (sTick - sBackTick) > 1) {
		sBackPending = false;
	}
}

void pc_menu_click_post(float nx, float ny)
{
	if (!sEnabled) {
		return;
	}
	sActivity   = true;
	sPending    = true;
	sNx         = nx;
	sNy         = ny;
	sPostedTick = sTick;
}

void pc_menu_click_clear(void)
{
	sPending = false;
}

bool pc_menu_click_pending(float* nx, float* ny)
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
	return true;
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

void pc_menu_click_set_enabled(bool enabled)
{
	sEnabled = enabled;
	if (!enabled) {
		sPending     = false;
		sHasPointer  = false;
		sMouseDrives = false;
		sClaimTick   = -100;
		sBackPending = false;
		sHandShown   = false;
	}
}

bool pc_menu_click_hand_wanted(void)
{
	return sHandShown && sHasPointer && pc_menu_click_pointer_menu_active();
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

static int sTargetKind = -1;
static int sPendingCount = 0;
static PcMenuClickRect sPendingRect[PC_MENU_CLICK_TARGET_MAX];
static int sPendingTag[PC_MENU_CLICK_TARGET_MAX];
static int sCommittedCount[PC_MENU_CLICK_TARGET_KINDS];
static PcMenuClickRect sCommittedRect[PC_MENU_CLICK_TARGET_KINDS][PC_MENU_CLICK_TARGET_MAX];
static int sCommittedTag[PC_MENU_CLICK_TARGET_KINDS][PC_MENU_CLICK_TARGET_MAX];

void pc_menu_click_targets_begin(int kind)
{
	sTargetKind   = (kind >= 0 && kind < PC_MENU_CLICK_TARGET_KINDS) ? kind : -1;
	sPendingCount = 0;
}

bool pc_menu_click_targets_collecting(void)
{
	return sTargetKind >= 0;
}

void pc_menu_click_targets_add(int tag, PcMenuClickRect windowRect)
{
	if (sTargetKind < 0 || sPendingCount >= PC_MENU_CLICK_TARGET_MAX) return;
	sPendingRect[sPendingCount] = windowRect;
	sPendingTag[sPendingCount]  = tag;
	++sPendingCount;
}

void pc_menu_click_targets_end(void)
{
	if (sTargetKind >= 0) {
		for (int i = 0; i < sPendingCount; i++) {
			sCommittedRect[sTargetKind][i] = sPendingRect[i];
			sCommittedTag[sTargetKind][i]  = sPendingTag[i];
		}
		sCommittedCount[sTargetKind] = sPendingCount;
	}
	sTargetKind = -1;
}

bool pc_menu_click_targets_hit(int kind, float px, float py, float margin, int* tag)
{
	if (kind < 0 || kind >= PC_MENU_CLICK_TARGET_KINDS || sCommittedCount[kind] <= 0) return false;
	const int index = pc_menu_click_hit(px, py, sCommittedRect[kind], nullptr, sCommittedCount[kind], margin);
	pc_menu_click_note_hand(index >= 0);
	if (index < 0) return false;
	if (tag) *tag = sCommittedTag[kind][index];
	return true;
}

int pc_menu_click_row_cells(const PcMenuClickRect* rects, const int* tags, int count, PcMenuClickRect* cells,
                            int* cellTags, int maxCells)
{
	if (!rects || !tags || !cells || !cellTags || count <= 0 || maxCells <= 0) {
		return 0;
	}
	// One card per tag: the union of its drawn rectangles.
	int n = 0;
	for (int i = 0; i < count; i++) {
		int at = -1;
		for (int c = 0; c < n; c++) {
			if (cellTags[c] == tags[i]) {
				at = c;
				break;
			}
		}
		if (at < 0) {
			if (n >= maxCells) continue;
			at           = n++;
			cellTags[at] = tags[i];
			cells[at]    = rects[i];
			continue;
		}
		PcMenuClickRect& u = cells[at];
		u.minX             = rects[i].minX < u.minX ? rects[i].minX : u.minX;
		u.minY             = rects[i].minY < u.minY ? rects[i].minY : u.minY;
		u.maxX             = rects[i].maxX > u.maxX ? rects[i].maxX : u.maxX;
		u.maxY             = rects[i].maxY > u.maxY ? rects[i].maxY : u.maxY;
	}
	// Left to right by centre.
	for (int i = 1; i < n; i++) {
		for (int j = i; j > 0 && cells[j].minX + cells[j].maxX < cells[j - 1].minX + cells[j - 1].maxX; j--) {
			const PcMenuClickRect r = cells[j];
			cells[j]                = cells[j - 1];
			cells[j - 1]            = r;
			const int t             = cellTags[j];
			cellTags[j]             = cellTags[j - 1];
			cellTags[j - 1]         = t;
		}
	}
	PcMenuClickRect card[PC_MENU_CLICK_TARGET_MAX];
	if (n > PC_MENU_CLICK_TARGET_MAX) n = PC_MENU_CLICK_TARGET_MAX;
	for (int i = 0; i < n; i++) card[i] = cells[i];
	auto centreX = [&](int i) { return (card[i].minX + card[i].maxX) / 2; };
	auto centreY = [&](int i) { return (card[i].minY + card[i].maxY) / 2; };
	for (int i = 0; i < n; i++) {
		const int cx = centreX(i);
		const int cy = centreY(i);
		// Half the distance to the nearer neighbour; a lone card keeps its own size.
		int half = 0;
		if (i > 0) half = (cx - centreX(i - 1)) / 2;
		if (i + 1 < n) {
			const int right = (centreX(i + 1) - cx) / 2;
			half            = (half == 0 || right < half) ? right : half;
		}
		const int ownW = (card[i].maxX - card[i].minX + 1) / 2;
		const int ownH = (card[i].maxY - card[i].minY + 1) / 2;
		if (half < ownW) half = ownW;
		const int halfV = half > ownH ? half : ownH;
		PcMenuClickRect& cell = cells[i];
		// Shared edges: the same midpoint on both sides, so there is no gap and no overlap.
		cell.minX = i > 0 ? (centreX(i - 1) + cx) / 2 : cx - half;
		cell.maxX = i + 1 < n ? (cx + centreX(i + 1)) / 2 : cx + half;
		cell.minY = cy - halfV;
		cell.maxY = cy + halfV;
	}
	return n;
}

static bool pc_menu_click_in_cell(float x, float y, const PcMenuClickRect& c, float grow)
{
	return x >= float(c.minX) - grow && x < float(c.maxX) + grow && y >= float(c.minY) - grow
	       && y < float(c.maxY) + grow;
}

int pc_menu_click_row_pick(float x, float y, const PcMenuClickRect* cells, const int* cellTags, int cellCount,
                           int currentTag, float hold)
{
	if (!cells || !cellTags || cellCount <= 0) return -1;
	for (int i = 0; i < cellCount; i++) {
		if (cellTags[i] == currentTag && pc_menu_click_in_cell(x, y, cells[i], hold)) return currentTag;
	}
	for (int i = 0; i < cellCount; i++) {
		if (pc_menu_click_in_cell(x, y, cells[i], 0.0f)) return cellTags[i];
	}
	return -1;
}

bool pc_menu_click_targets_hit_row(int kind, float px, float py, int currentTag, float hold, int* tag)
{
	if (kind < 0 || kind >= PC_MENU_CLICK_TARGET_KINDS || sCommittedCount[kind] <= 0) return false;
	PcMenuClickRect cells[PC_MENU_CLICK_TARGET_MAX];
	int cellTags[PC_MENU_CLICK_TARGET_MAX];
	const int n
	    = pc_menu_click_row_cells(sCommittedRect[kind], sCommittedTag[kind], sCommittedCount[kind], cells, cellTags,
	                              PC_MENU_CLICK_TARGET_MAX);
	const int hit = pc_menu_click_row_pick(px, py, cells, cellTags, n, currentTag, hold);
	pc_menu_click_note_hand(hit >= 0);
	if (hit < 0) return false;
	if (tag) *tag = hit;
	return true;
}
