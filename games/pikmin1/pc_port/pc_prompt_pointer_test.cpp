/**
 * New Game and Captain prompt mouse targets, without a window and without a disc.
 *
 * The rectangles come from the same functions the prompts draw with. A window
 * click is mapped through the present letterbox, the render scale and the menu
 * scale (pc_menu_click_map_to_graph) and must land on the box drawn there, on an
 * ultrawide window, a pillarboxed or letterboxed picture, and a HiDPI drawable.
 */

#include "settings/pc_prompt_pointer.h"
#include "pc_menu_click.h"

#include <cmath>
#include <cstdio>

namespace {

int sFailures = 0;

void check(bool condition, const char* what)
{
	if (!condition) {
		std::printf("FAIL: %s\n", what);
		++sFailures;
	}
}

bool sameRect(PcMenuClickRect r, int x0, int y0, int x1, int y1)
{
	return r.minX == x0 && r.minY == y0 && r.maxX == x1 && r.maxY == y1;
}

float cx(PcMenuClickRect r) { return (r.minX + r.maxX) * 0.5f; }
float cy(PcMenuClickRect r) { return (r.minY + r.maxY) * 0.5f; }

int wideGraphW(int w, int h)
{
	const int v = int(lroundf(480.0f * float(w) / float(h)));
	return v < 640 ? 640 : v;
}

// The present blit plus the menu scale, as pc_gfx builds them. renderScale < 1 is the
// native framebuffer at a reduced size (renderScale in the settings file).
PcMenuClickFrame frameFor(int drawableW, int drawableH, float renderedAspect, float renderScale, int graphW, int graphH)
{
	PcMenuClickFrame f {};
	f.drawableW = drawableW;
	f.drawableH = drawableH;
	f.graphW    = graphW;
	f.graphH    = graphH;
	pc_menu_click_output_area(drawableW, drawableH, renderedAspect, &f.outX, &f.outY, &f.outW, &f.outH);
	f.targetW         = std::floor(float(f.outW) * renderScale);
	f.targetH         = std::floor(float(f.outH) * renderScale);
	const float scale = std::fmin(f.targetW / float(graphW), f.targetH / float(graphH));
	f.scaleX          = scale;
	f.scaleY          = scale;
	f.offsetX         = (f.targetW - float(graphW) * scale) * 0.5f;
	f.offsetY         = (f.targetH - float(graphH) * scale) * 0.5f;
	return f;
}

// Inverse of pc_menu_click_map_to_graph: where on the window (0..1) a graph point is drawn.
void graphToWindow(const PcMenuClickFrame& f, float gx, float gy, float* nx, float* ny)
{
	const float rtX   = f.offsetX + gx * f.scaleX;
	const float rtYUp = f.offsetY + (float(f.graphH) - gy) * f.scaleY;
	const float winX  = float(f.outX) + rtX / f.targetW * float(f.outW);
	const float winY  = float(f.outY) + (1.0f - rtYUp / f.targetH) * float(f.outH);
	*nx               = winX / float(f.drawableW);
	*ny               = winY / float(f.drawableH);
}

void testNewGameRects()
{
	PcMenuClickRect r[PC_NEWGAME_PTR_COUNT];
	pc_newgame_pointer_rects(640, 480, r);
	// panel (10, 30); rows at 102, 172, 242; Start at 312
	check(sameRect(r[0], 50, 102, 280, 146), "Difficulty Normal box");
	check(sameRect(r[1], 320, 102, 550, 146), "Difficulty Hard box");
	check(sameRect(r[3], 320, 172, 550, 216), "Permadeath box");
	check(sameRect(r[4], 50, 242, 280, 286), "Skip tutorial No box");
	check(sameRect(r[PC_NEWGAME_PTR_START], 205, 312, 435, 356), "Start box");

	for (int i = 0; i < PC_NEWGAME_PTR_COUNT; ++i) {
		const PcMenuClickResult hit
			= pc_menu_click_resolve(PC_MENU_CLICK_MAIN, cx(r[i]), cy(r[i]), r, nullptr, PC_NEWGAME_PTR_COUNT, kPcMenuClickMargin);
		check(hit.action == PC_MENU_CLICK_ACTIVATE && hit.index == i, "a click on a New Game box picks that box");
		const PcMenuClickResult hover
			= pc_menu_click_hover(PC_MENU_CLICK_MAIN, cx(r[i]), cy(r[i]), r, nullptr, PC_NEWGAME_PTR_COUNT, kPcMenuClickMargin);
		check(hover.action == PC_MENU_CLICK_HIGHLIGHT && hover.index == i, "hover over a New Game box highlights it only");
	}
	// Panel title, the gap between the two option columns, the description line, outside.
	const float empty[][2] = { { 320, 50 }, { 300, 124 }, { 320, 384 }, { 5, 5 }, { 600, 470 } };
	for (const auto& p : empty) {
		const PcMenuClickResult miss
			= pc_menu_click_resolve(PC_MENU_CLICK_MAIN, p[0], p[1], r, nullptr, PC_NEWGAME_PTR_COUNT, kPcMenuClickMargin);
		check(miss.action == PC_MENU_CLICK_IGNORE && miss.index == -1, "a click on empty New Game space does nothing");
	}
	// Randomizer: Skip tutorial is locked; its boxes must not pass the click on.
	int active[PC_NEWGAME_PTR_COUNT] = { 1, 1, 1, 1, 0, 0, 1 };
	const PcMenuClickResult locked
		= pc_menu_click_resolve(PC_MENU_CLICK_MAIN, cx(r[5]), cy(r[5]), r, active, PC_NEWGAME_PTR_COUNT, kPcMenuClickMargin);
	check(locked.action == PC_MENU_CLICK_IGNORE, "the locked Randomizer row ignores a click");

	// Wide menu graph on 2560x1080: the panel is centred in the wider graph.
	const int gw = wideGraphW(2560, 1080);
	check(gw == 1138, "ultrawide menu graph width");
	pc_newgame_pointer_rects(gw, 480, r);
	check(r[PC_NEWGAME_PTR_START].minX == gw / 2 - 310 + 195, "Start is centred in the wide graph");
}

struct CaptainLayout {
	PcMenuClickRect rect[16];
	int count;
	int arrowPrev;
	int arrowNext;
};

// Five Pikmin 1 cards, four Pikmin 2 cards, and a custom row of five with page arrows:
// three rows, so the short cards (104) and the panel 260 + 210 tall.
CaptainLayout captainLayout(int screenW, int screenH)
{
	CaptainLayout L {};
	const int boxH   = 104;
	const int extraH = 2 * (boxH + kPcCaptainBoxGap) - (120 - boxH);
	int panelX = 0, panelY = 0;
	pc_captain_panel_origin(screenW, screenH, extraH, &panelX, &panelY);
	const int shown[3] = { 5, 4, 5 };
	for (int row = 0; row < 3; ++row)
		for (int col = 0; col < shown[row]; ++col)
			L.rect[L.count++] = pc_captain_card_rect(panelX, panelY, row, col, shown[row], boxH);
	PcMenuClickRect left, right;
	pc_captain_arrow_rects(panelX, panelY, 2, 5, boxH, &left, &right);
	L.arrowPrev         = L.count;
	L.rect[L.count++]   = left;
	L.arrowNext         = L.count;
	L.rect[L.count++]   = right;
	return L;
}

void testCaptainRects()
{
	CaptainLayout L = captainLayout(640, 480);
	// panel (10, 5), a row of five starts 32 px in, cards 104 wide with a 9 px gap
	check(sameRect(L.rect[0], 42, 89, 146, 193), "Olimar card");
	check(sameRect(L.rect[4], 494, 89, 598, 193), "fifth Pikmin 1 card");
	check(L.rect[5].minY == 202 && L.rect[5].minX == 10 + 88, "the four-card row is centred under the first");
	check(L.rect[9].minY == 315 && L.rect[9].maxY == 419, "custom row sits inside 480");
	check(sameRect(L.rect[L.arrowPrev], 12, 315, 36, 419), "previous-page arrow beside the custom row");
	check(sameRect(L.rect[L.arrowNext], 604, 315, 628, 419), "next-page arrow beside the custom row");

	for (int i = 0; i < L.count; ++i) {
		const PcMenuClickResult hit
			= pc_menu_click_resolve(PC_MENU_CLICK_MAIN, cx(L.rect[i]), cy(L.rect[i]), L.rect, nullptr, L.count, kPcMenuClickMargin);
		check(hit.action == PC_MENU_CLICK_ACTIVATE && hit.index == i, "a click on a captain card or arrow picks it");
	}
	const float empty[][2] = { { 320, 30 }, { 320, 60 }, { 70, 230 }, { 320, 450 }, { 2, 240 } };
	for (const auto& p : empty) {
		const PcMenuClickResult miss
			= pc_menu_click_resolve(PC_MENU_CLICK_MAIN, p[0], p[1], L.rect, nullptr, L.count, kPcMenuClickMargin);
		check(miss.action == PC_MENU_CLICK_IGNORE, "a click on empty captain space does nothing");
	}
}

// Every card centre, drawn through a frame, must come back to the same card.
void roundTrip(const PcMenuClickFrame& f, const char* what)
{
	const CaptainLayout L = captainLayout(f.graphW, f.graphH);
	bool ok               = true;
	for (int i = 0; i < L.count; ++i) {
		float nx = 0.0f, ny = 0.0f, gx = 0.0f, gy = 0.0f;
		graphToWindow(f, cx(L.rect[i]), cy(L.rect[i]), &nx, &ny);
		if (!pc_menu_click_map_to_graph(f, nx, ny, &gx, &gy)) {
			ok = false;
			continue;
		}
		const PcMenuClickResult hit
			= pc_menu_click_resolve(PC_MENU_CLICK_MAIN, gx, gy, L.rect, nullptr, L.count, kPcMenuClickMargin);
		ok = ok && hit.action == PC_MENU_CLICK_ACTIVATE && hit.index == i;
	}
	check(ok, what);

	PcMenuClickRect ng[PC_NEWGAME_PTR_COUNT];
	pc_newgame_pointer_rects(f.graphW, f.graphH, ng);
	ok = true;
	for (int i = 0; i < PC_NEWGAME_PTR_COUNT; ++i) {
		float nx = 0.0f, ny = 0.0f, gx = 0.0f, gy = 0.0f;
		graphToWindow(f, cx(ng[i]), cy(ng[i]), &nx, &ny);
		pc_menu_click_map_to_graph(f, nx, ny, &gx, &gy);
		const PcMenuClickResult hit
			= pc_menu_click_resolve(PC_MENU_CLICK_MAIN, gx, gy, ng, nullptr, PC_NEWGAME_PTR_COUNT, kPcMenuClickMargin);
		ok = ok && hit.action == PC_MENU_CLICK_ACTIVATE && hit.index == i;
	}
	check(ok, what);
}

void testMapping()
{
	// Kyle's ultrawide, wide menu, native framebuffer at 2/3.
	roundTrip(frameFor(2560, 1080, 2560.0f / 1080.0f, 2.0f / 3.0f, wideGraphW(2560, 1080), 480),
	          "2560x1080 wide menu: every box maps back to itself");
	// HiDPI: the same window at 2x. A click is normalized by the window size in points, so a
	// point at (px, py) in a 1280x540 window is the same 0..1 position on the 2560x1080 drawable.
	{
		const PcMenuClickFrame hidpi = frameFor(2560, 1080, 2560.0f / 1080.0f, 1.0f, wideGraphW(2560, 1080), 480);
		const CaptainLayout L        = captainLayout(hidpi.graphW, 480);
		float nx = 0.0f, ny = 0.0f, gx = 0.0f, gy = 0.0f;
		graphToWindow(hidpi, cx(L.rect[7]), cy(L.rect[7]), &nx, &ny);
		const float pointX = nx * 1280.0f, pointY = ny * 540.0f; // what SDL reports in points
		pc_menu_click_map_to_graph(hidpi, pointX / 1280.0f, pointY / 540.0f, &gx, &gy);
		check(pc_menu_click_hit(gx, gy, L.rect, nullptr, L.count, kPcMenuClickMargin) == 7,
		      "HiDPI: a click in window points lands on the same card");
		roundTrip(hidpi, "2560x1080 HiDPI drawable: every box maps back to itself");
		roundTrip(frameFor(2880, 1800, 2880.0f / 1800.0f, 1.0f, wideGraphW(2880, 1800), 480),
		          "Retina 16:10 drawable: every box maps back to itself");
	}
	// Fixed 16:9 picture on the ultrawide: pillarboxed.
	const PcMenuClickFrame pillar = frameFor(2560, 1080, 16.0f / 9.0f, 1.0f, wideGraphW(1920, 1080), 480);
	check(pillar.outX > 0, "a 16:9 picture is pillarboxed on 2560x1080");
	roundTrip(pillar, "pillarboxed 16:9 picture: every box maps back to itself");
	// 16:9 picture in a 16:10 window: letterboxed top and bottom.
	const PcMenuClickFrame letter = frameFor(2880, 1800, 16.0f / 9.0f, 1.0f, wideGraphW(1920, 1080), 480);
	check(letter.outY > 0, "a 16:9 picture is letterboxed on 16:10");
	roundTrip(letter, "letterboxed 16:9 picture: every box maps back to itself");
	// PIKMIN_MENU_PILLARBOX: a 640 graph inside the wide window.
	roundTrip(frameFor(2560, 1080, 2560.0f / 1080.0f, 1.0f, 640, 480), "4:3 menu graph: every box maps back to itself");
}

} // namespace

int main()
{
	testNewGameRects();
	testCaptainRects();
	testMapping();
	if (sFailures) {
		std::printf("%d failure(s)\n", sFailures);
		return 1;
	}
	std::printf("pc_prompt_pointer_test: ok\n");
	return 0;
}
