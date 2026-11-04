#include "pc_button_prompt.h"

#include <cstdlib>
#include <cstring>

namespace {

// Drawable pixels around a target that still count, like the menus' own margin.
constexpr float kPromptMargin = 4.0f;
// How far apart two panes of one row may sit (layout pixels): the dots between a glyph
// and its label, or a label bubble that starts under the glyph.
constexpr int kRowGap = 24;
// Wider than this, or taller than this many glyphs, a pane is a background or a panel.
constexpr int kRowMaxWidth       = 320;
constexpr int kRowMaxHeightRatio = 2;
// A logged rectangle that moved less than this is the same prompt (blink, rounding).
constexpr int kLogMoveTolerance = 2;

struct Entry {
	std::uint16_t button;
	PcMenuClickRect rect;
	bool registered;
	char screen[32];
	char tex[24];
	char reason[48];
};

Entry sBuilding[PC_BUTTON_PROMPT_MAX];
int sBuildingCount = 0;

// Current targets (registered only, merged) and what was drawn, for the log.
PcMenuClickRect sTargetRect[PC_BUTTON_PROMPT_MAX];
std::uint16_t sTargetButton[PC_BUTTON_PROMPT_MAX];
char sTargetScreen[PC_BUTTON_PROMPT_MAX][32];
int sTargetCount = 0;

Entry sLogged[PC_BUTTON_PROMPT_MAX];
int sLoggedCount  = 0;
bool sLoggedLive  = false;
bool sLive        = false;
bool sLatched     = false;
long sFrame       = 0;
bool sLogResolved = false;
std::FILE* sLog   = nullptr;

std::FILE* promptLog()
{
	if (!sLogResolved) {
		sLogResolved    = true;
		const char* env = std::getenv("PC_BUTTON_PROMPT_LOG");
		sLog            = (env && *env && std::strcmp(env, "0") != 0) ? stderr : nullptr;
	}
	return sLog;
}

void copyName(char* dst, std::size_t size, const char* src)
{
	if (!src) src = "";
	const char* name = src;
	for (const char* p = src; *p; ++p) {
		if (*p == '/' || *p == '\\') name = p + 1;
	}
	std::strncpy(dst, name, size - 1);
	dst[size - 1] = '\0';
}

bool rectsOverlap(const PcMenuClickRect& a, const PcMenuClickRect& b)
{
	return a.minX < b.maxX && b.minX < a.maxX && a.minY < b.maxY && b.minY < a.maxY;
}

PcMenuClickRect rectUnion(const PcMenuClickRect& a, const PcMenuClickRect& b)
{
	PcMenuClickRect r;
	r.minX = a.minX < b.minX ? a.minX : b.minX;
	r.minY = a.minY < b.minY ? a.minY : b.minY;
	r.maxX = a.maxX > b.maxX ? a.maxX : b.maxX;
	r.maxY = a.maxY > b.maxY ? a.maxY : b.maxY;
	return r;
}

int absInt(int v) { return v < 0 ? -v : v; }

// Horizontal space between two rectangles; 0 when they overlap.
int gapX(const PcMenuClickRect& a, const PcMenuClickRect& b)
{
	if (a.maxX < b.minX) return b.minX - a.maxX;
	if (b.maxX < a.minX) return a.minX - b.maxX;
	return 0;
}

// a and b share a row: they overlap by at least half the shorter height.
bool sameRow(const PcMenuClickRect& a, const PcMenuClickRect& b)
{
	const int top     = a.minY > b.minY ? a.minY : b.minY;
	const int bottom  = a.maxY < b.maxY ? a.maxY : b.maxY;
	const int ha      = a.maxY - a.minY;
	const int hb      = b.maxY - b.minY;
	const int shorter = ha < hb ? ha : hb;
	return shorter > 0 && (bottom - top) * 2 >= shorter;
}

bool sameEntry(const Entry& a, const Entry& b)
{
	return a.button == b.button && a.registered == b.registered && std::strcmp(a.screen, b.screen) == 0
	    && std::strcmp(a.tex, b.tex) == 0 && std::strcmp(a.reason, b.reason) == 0
	    && absInt(a.rect.minX - b.rect.minX) <= kLogMoveTolerance && absInt(a.rect.minY - b.rect.minY) <= kLogMoveTolerance
	    && absInt(a.rect.maxX - b.rect.maxX) <= kLogMoveTolerance && absInt(a.rect.maxY - b.rect.maxY) <= kLogMoveTolerance;
}

void logFrameIfChanged()
{
	std::FILE* out = promptLog();
	if (!out) return;
	bool changed = sBuildingCount != sLoggedCount || sLive != sLoggedLive;
	for (int i = 0; !changed && i < sBuildingCount; i++) {
		changed = !sameEntry(sBuilding[i], sLogged[i]);
	}
	if (!changed) return;
	std::fprintf(out, "[button prompt] frame %ld: %d prompt(s) drawn, %d target(s), pointer %s\n", sFrame, sBuildingCount,
	             sTargetCount, sLive ? "free for menus" : "not free (level or overlay owns it): no clicks");
	for (int i = 0; i < sBuildingCount; i++) {
		const Entry& e = sBuilding[i];
		std::fprintf(out, "[button prompt]   %-14s %-12s %-5s rect=(%d,%d)-(%d,%d) target=%s%s%s\n", e.screen[0] ? e.screen : "(unnamed)",
		             e.tex, pc_button_prompt_button_name(e.button), e.rect.minX, e.rect.minY, e.rect.maxX, e.rect.maxY,
		             e.registered ? "yes" : "no", e.reason[0] ? ": " : "", e.reason);
		sLogged[i] = e;
	}
	sLoggedCount = sBuildingCount;
	sLoggedLive  = sLive;
	std::fflush(out);
}

} // namespace

std::uint16_t pc_button_prompt_button_for_tag(char tag)
{
	switch (tag) {
	case 'a': return PC_BUTTON_PROMPT_PAD_A;
	case 'b': return PC_BUTTON_PROMPT_PAD_B;
	case 'x': return PC_BUTTON_PROMPT_PAD_X;
	case 'y': return PC_BUTTON_PROMPT_PAD_Y;
	case 'z': return PC_BUTTON_PROMPT_PAD_Z;
	case 'l': return PC_BUTTON_PROMPT_PAD_L;
	case 'r': return PC_BUTTON_PROMPT_PAD_R;
	case 'p': return PC_BUTTON_PROMPT_PAD_START;
	default: return 0; // 'c' and 's' are the sticks: a direction, not a press
	}
}

const char* pc_button_prompt_button_name(std::uint16_t button)
{
	switch (button) {
	case PC_BUTTON_PROMPT_PAD_A: return "A";
	case PC_BUTTON_PROMPT_PAD_B: return "B";
	case PC_BUTTON_PROMPT_PAD_X: return "X";
	case PC_BUTTON_PROMPT_PAD_Y: return "Y";
	case PC_BUTTON_PROMPT_PAD_Z: return "Z";
	case PC_BUTTON_PROMPT_PAD_L: return "L";
	case PC_BUTTON_PROMPT_PAD_R: return "R";
	case PC_BUTTON_PROMPT_PAD_START: return "Start";
	default: return "-";
	}
}

bool pc_button_prompt_screen_opted_out(const char* bloName)
{
	char name[32];
	copyName(name, sizeof(name), bloName);
	// The controller page: every button with what it does in a level. Pressing them here
	// would not do that, so they stay pictures.
	static const char* const kDiagrams[] = { "cont_00.blo" };
	for (const char* d : kDiagrams) {
		if (std::strcmp(name, d) == 0) return true;
	}
	return false;
}

PcMenuClickRect pc_button_prompt_group(PcMenuClickRect glyph, const PcButtonPromptSibling* siblings, int count)
{
	PcMenuClickRect row = glyph;
	const int glyphH    = glyph.maxY - glyph.minY;
	if (glyphH <= 0 || !siblings || count <= 0) return row;
	if (count > PC_BUTTON_PROMPT_SIBLING_MAX) count = PC_BUTTON_PROMPT_SIBLING_MAX;
	bool joined[PC_BUTTON_PROMPT_SIBLING_MAX] = {};
	bool grew = true;
	while (grew) {
		grew = false;
		for (int i = 0; i < count; i++) {
			if (joined[i] || siblings[i].glyph) continue;
			const PcMenuClickRect& r = siblings[i].rect;
			const int w = r.maxX - r.minX;
			const int h = r.maxY - r.minY;
			if (w <= 0 || h <= 0 || w > kRowMaxWidth || h > glyphH * kRowMaxHeightRatio) continue;
			if (!sameRow(r, glyph)) continue;
			// Next to what has joined so far.
			if (r.minX > row.maxX + kRowGap || r.maxX < row.minX - kRowGap) continue;
			// Two prompts on one row ("B Back  A End"): a pane belongs to the nearer glyph.
			bool nearerOther = false;
			for (int g = 0; g < count && !nearerOther; g++) {
				nearerOther = siblings[g].glyph && sameRow(siblings[g].rect, glyph)
				           && gapX(r, siblings[g].rect) < gapX(r, glyph);
			}
			if (nearerOther) continue;
			row       = rectUnion(row, r);
			joined[i] = true;
			grew      = true;
		}
	}
	return row;
}

void pc_button_prompt_add(std::uint16_t button, PcMenuClickRect windowRect, const char* screen, const char* texName,
                         bool registered, const char* reason)
{
	if (sBuildingCount >= PC_BUTTON_PROMPT_MAX) return;
	Entry& e     = sBuilding[sBuildingCount++];
	e.button     = button;
	e.rect       = windowRect;
	e.registered = registered && button != 0 && windowRect.maxX > windowRect.minX && windowRect.maxY > windowRect.minY;
	copyName(e.screen, sizeof(e.screen), screen);
	copyName(e.tex, sizeof(e.tex), texName);
	std::strncpy(e.reason, reason ? reason : "", sizeof(e.reason) - 1);
	e.reason[sizeof(e.reason) - 1] = '\0';
	if (registered && !e.registered && !e.reason[0]) {
		std::strncpy(e.reason, button ? "drawn with no size" : "not a button", sizeof(e.reason) - 1);
	}
}

void pc_button_prompt_frame_end(void)
{
	sTargetCount = 0;
	for (int i = 0; i < sBuildingCount; i++) {
		const Entry& e = sBuilding[i];
		if (!e.registered) continue;
		int merged = -1;
		for (int t = 0; t < sTargetCount; t++) {
			if (sTargetButton[t] == e.button && rectsOverlap(sTargetRect[t], e.rect)) {
				merged = t;
				break;
			}
		}
		if (merged >= 0) {
			sTargetRect[merged] = rectUnion(sTargetRect[merged], e.rect);
			continue;
		}
		sTargetRect[sTargetCount]   = e.rect;
		sTargetButton[sTargetCount] = e.button;
		std::memcpy(sTargetScreen[sTargetCount], e.screen, sizeof(e.screen));
		++sTargetCount;
	}
	logFrameIfChanged();
	sBuildingCount = 0;
	++sFrame;
}

int pc_button_prompt_count(void) { return sTargetCount; }

std::uint16_t pc_button_prompt_sample(bool live, int drawableW, int drawableH, bool leftHeld)
{
	sLive = live;
	if (sLatched) {
		if (leftHeld) {
			pc_menu_click_claim_pointer_menu();
		} else {
			sLatched = false;
		}
	}
	if (!live || drawableW <= 0 || drawableH <= 0 || sTargetCount <= 0) return 0;

	float nx = 0.0f;
	float ny = 0.0f;
	if (pc_menu_click_pending(&nx, &ny)) {
		const int hit = pc_menu_click_hit(nx * float(drawableW), ny * float(drawableH), sTargetRect, nullptr, sTargetCount,
		                                  kPromptMargin);
		if (hit >= 0) {
			pc_menu_click_take(nullptr, nullptr);
			pc_menu_click_claim_pointer_menu();
			sLatched = leftHeld;
			if (std::FILE* out = promptLog()) {
				std::fprintf(out, "[button prompt] click on %s prompt (%s): pressed %s\n",
				             pc_button_prompt_button_name(sTargetButton[hit]),
				             sTargetScreen[hit][0] ? sTargetScreen[hit] : "(unnamed)",
				             pc_button_prompt_button_name(sTargetButton[hit]));
				std::fflush(out);
			}
			return sTargetButton[hit];
		}
	}
	if (pc_menu_click_read_pointer_normalized(&nx, &ny)) {
		const PcMenuClickResult over = pc_menu_click_hover(PC_MENU_CLICK_MAIN, nx * float(drawableW), ny * float(drawableH),
		                                                   sTargetRect, nullptr, sTargetCount, kPromptMargin);
		if (over.index >= 0) pc_menu_click_claim_pointer_menu();
	}
	return 0;
}

void pc_button_prompt_reset(void)
{
	sBuildingCount = 0;
	sTargetCount   = 0;
	sLoggedCount   = 0;
	sLoggedLive    = false;
	sLive          = false;
	sLatched       = false;
	sFrame         = 0;
}

void pc_button_prompt_set_log(std::FILE* out)
{
	sLogResolved = true;
	sLog         = out;
}
