/**
 * Drawn button prompts as click targets, without a window and without a disc.
 *
 * Each tick runs in the window layer's order: begin_tick, the click and pointer events, the
 * prompt sample (before mouse bindings), and the frame's draw, which adds the prompts and
 * ends the frame at the buffer swap. Rows use the world map's "Y Ship's Log" layout.
 */

#include "pc_button_prompt.h"
#include "pc_menu_click.h"

#include <cstdio>
#include <cstring>

namespace {

int sFailures = 0;

void check(bool condition, const char* what)
{
	if (!condition) {
		std::printf("FAIL: %s\n", what);
		++sFailures;
	}
}

constexpr int kDrawW = 1280, kDrawH = 960; // 640x480 layout at 2x

// Ship's Log in w_data1: the Y glyph, four dots, the label, and its bubble behind it.
const PcMenuClickRect kGlyph = { 328, 423, 360, 455 };
const PcButtonPromptSibling kShipsLog[] = {
	{ { 366, 431, 384, 449 }, false }, // dot
	{ { 386, 431, 404, 449 }, false }, // dot
	{ { 406, 431, 424, 449 }, false }, // dot
	{ { 426, 431, 444, 449 }, false }, // dot
	{ { 450, 433, 656, 474 }, false }, // "Ship's Log" text
	{ { 0, 0, 640, 480 }, false },     // full-screen background: never joins
	{ { 40, 40, 280, 300 }, false },   // a panel on another row
	{ { 80, 424, 112, 456 }, true },   // another glyph on the row: its own prompt
};

PcMenuClickRect toWindow(PcMenuClickRect r)
{
	return PcMenuClickRect{ r.minX * 2, r.minY * 2, r.maxX * 2, r.maxY * 2 };
}

// One drawn frame with the Ship's Log prompt (and optionally a stick glyph and a diagram).
void drawShipsLog(const char* screen = "w_data1.blo")
{
	const PcMenuClickRect row = pc_button_prompt_group(kGlyph, kShipsLog, int(sizeof(kShipsLog) / sizeof(kShipsLog[0])));
	pc_button_prompt_add(PC_BUTTON_PROMPT_PAD_Y, toWindow(row), screen, "screen/tex/y_btn.bti", true, "");
	pc_button_prompt_frame_end();
}

// The pointer arms on motion, so the first position is a nudge away.
void movePointer(float px, float py)
{
	pc_menu_click_set_pointer((px - 1.0f) / kDrawW, py / kDrawH);
	pc_menu_click_set_pointer(px / kDrawW, py / kDrawH);
}

std::uint16_t tick(bool live = true, bool leftHeld = false)
{
	pc_menu_click_begin_tick();
	return pc_button_prompt_sample(live, kDrawW, kDrawH, leftHeld);
}

void start()
{
	pc_menu_click_reset();
	pc_button_prompt_reset();
	pc_button_prompt_set_log(nullptr);
}

void testClassify()
{
	check(pc_button_prompt_button_for_tag('a') == 0x0100, "a is A");
	check(pc_button_prompt_button_for_tag('y') == 0x0800, "y is Y");
	check(pc_button_prompt_button_for_tag('x') == 0x0400, "x is X");
	check(pc_button_prompt_button_for_tag('b') == 0x0200, "b is B");
	check(pc_button_prompt_button_for_tag('p') == 0x1000, "p is Start");
	check(pc_button_prompt_button_for_tag('z') == 0x0010 && pc_button_prompt_button_for_tag('l') == 0x0040
	          && pc_button_prompt_button_for_tag('r') == 0x0020,
	      "z, l and r are the triggers");
	check(pc_button_prompt_button_for_tag('c') == 0 && pc_button_prompt_button_for_tag('s') == 0,
	      "the sticks are not a press");
	check(pc_button_prompt_button_for_tag(0) == 0, "no tag, no button");
	check(pc_button_prompt_screen_opted_out("cont_00.blo") && pc_button_prompt_screen_opted_out("screen/blo/cont_00.blo"),
	      "the controller page is a diagram, with or without a folder");
	check(!pc_button_prompt_screen_opted_out("w_data1.blo") && !pc_button_prompt_screen_opted_out(nullptr),
	      "other screens are prompts");
}

void testGroup()
{
	const PcMenuClickRect row = pc_button_prompt_group(kGlyph, kShipsLog, int(sizeof(kShipsLog) / sizeof(kShipsLog[0])));
	check(row.minX == 328 && row.maxX == 656, "Ship's Log: glyph, dots and label make one row");
	check(row.minY == 423 && row.maxY == 474, "the row is as tall as its tallest part, not the background");

	// Copy on the file screen: the label is left of the glyph.
	const PcMenuClickRect copyGlyph = { 300, 400, 332, 432 };
	const PcButtonPromptSibling copy[] = {
		{ { 220, 404, 296, 430 }, false }, // "Copy"
		{ { 120, 404, 180, 430 }, false }, // a word too far away
	};
	const PcMenuClickRect copyRow = pc_button_prompt_group(copyGlyph, copy, 2);
	check(copyRow.minX == 220 && copyRow.maxX == 332, "a label left of the glyph joins; one too far away does not");

	const PcButtonPromptSibling below[] = { { { 330, 440, 600, 470 }, false } };
	const PcMenuClickRect alone = pc_button_prompt_group(copyGlyph, below, 1);
	check(alone.minX == 300 && alone.maxX == 332 && alone.maxY == 432, "text on the next row does not join");
	check(pc_button_prompt_group(kGlyph, nullptr, 0).maxX == kGlyph.maxX, "no siblings: the glyph alone");

	// Ship parts screen: "B Back" and "A End" on one bar, each glyph in a ring.
	const PcMenuClickRect bGlyph = { 362, 36, 391, 66 };
	const PcMenuClickRect aGlyph = { 498, 36, 526, 66 };
	const PcButtonPromptSibling bar[] = {
		{ { 390, 37, 471, 66 }, false }, // "Back"
		{ { 523, 37, 604, 66 }, false }, // "End"
		{ { 361, 35, 392, 68 }, false }, // ring
		{ { 496, 35, 527, 68 }, false }, // ring
		{ aGlyph, true },
		{ bGlyph, true },
	};
	const PcMenuClickRect backRow = pc_button_prompt_group(bGlyph, bar, 6);
	const PcMenuClickRect endRow  = pc_button_prompt_group(aGlyph, bar, 6);
	check(backRow.minX == 361 && backRow.maxX == 471, "B takes Back and its ring, not the A side");
	check(endRow.minX == 496 && endRow.maxX == 604, "A takes End and its ring, not the B side");
}

void testClickPressesTheButton()
{
	start();
	drawShipsLog();
	check(pc_button_prompt_count() == 1, "the drawn prompt is a target");

	tick();
	pc_menu_click_post(1200.0f / kDrawW, 900.0f / kDrawH); // on the label, right end
	check(tick() == PC_BUTTON_PROMPT_PAD_Y, "a click on the label is Y for this sample");
	check(!pc_menu_click_pending(nullptr, nullptr), "the click is taken, so the screen does not see it too");
	check(pc_menu_click_pointer_menu_active(), "the click claims the pointer, so it is not also A");
	check(tick() == 0, "the press lasts one sample");

	pc_menu_click_post(700.0f / kDrawW, 880.0f / kDrawH); // on the glyph
	check(tick() == PC_BUTTON_PROMPT_PAD_Y, "a click on the glyph is Y");

	pc_menu_click_post(100.0f / kDrawW, 100.0f / kDrawH);
	check(tick() == 0, "a click elsewhere is no press");
	check(pc_menu_click_pending(nullptr, nullptr), "and stays for the screen's own menu");
}

void testNotLive()
{
	start();
	drawShipsLog();
	tick(false);
	pc_menu_click_post(700.0f / kDrawW, 880.0f / kDrawH);
	check(tick(false) == 0, "a running level owns the pointer: no press");
	check(pc_menu_click_pending(nullptr, nullptr), "the click is left alone");
}

void testFrameRebuilds()
{
	start();
	drawShipsLog();
	pc_button_prompt_frame_end(); // a frame that drew no prompt
	check(pc_button_prompt_count() == 0, "a prompt that is not drawn is no target");
	tick();
	pc_menu_click_post(700.0f / kDrawW, 880.0f / kDrawH);
	check(tick() == 0, "so a click there is no press");

	// A glyph and its shadow: one target.
	pc_button_prompt_add(PC_BUTTON_PROMPT_PAD_A, { 100, 100, 200, 140 }, "m_menu_r.blo", "a_40.bti", true, "");
	pc_button_prompt_add(PC_BUTTON_PROMPT_PAD_A, { 104, 104, 204, 144 }, "m_menu_r.blo", "a_40.bti", true, "");
	pc_button_prompt_add(PC_BUTTON_PROMPT_PAD_X, { 150, 100, 250, 140 }, "data_t1.blo", "x_btn.bti", true, "");
	pc_button_prompt_add(0, { 300, 100, 340, 140 }, "m_menu2.blo", "c_btn.bti", false, "stick");
	pc_button_prompt_add(PC_BUTTON_PROMPT_PAD_A, { 400, 100, 440, 140 }, "cont_00.blo", "a_btn.bti", false, "diagram");
	pc_button_prompt_frame_end();
	check(pc_button_prompt_count() == 2, "overlapping same-button glyphs merge; other buttons, sticks and diagrams do not");
}

void testHoverClaimsAndShowsHand()
{
	start();
	drawShipsLog();
	tick();
	movePointer(1000.0f, 890.0f);
	tick();
	check(pc_menu_click_pointer_menu_active(), "over the row: the pointer is claimed, so a bound click is not A");
	tick();
	check(pc_menu_click_hand_wanted(), "over the row: the hand");

	movePointer(100.0f, 100.0f);
	tick();
	tick();
	tick();
	check(!pc_menu_click_hand_wanted(), "off the row: the arrow");
	check(!pc_menu_click_pointer_menu_active(), "off the row the claim lapses, so the level's clicks are its own");
}

void testLongPressStaysClaimed()
{
	start();
	drawShipsLog();
	tick();
	pc_menu_click_post(700.0f / kDrawW, 880.0f / kDrawH);
	check(tick(true, true) == PC_BUTTON_PROMPT_PAD_Y, "pressed");
	pc_button_prompt_frame_end(); // the screen closed: no prompt any more
	for (int i = 0; i < 6; i++) tick(true, true);
	check(pc_menu_click_pointer_menu_active(), "held: still claimed, so the held button is not A on the next screen");
	tick(true, false);
	tick(true, false);
	tick(true, false);
	check(!pc_menu_click_pointer_menu_active(), "released: the claim lapses");
}

void testLog()
{
	start();
	std::FILE* out = std::tmpfile();
	pc_button_prompt_set_log(out);
	tick();
	drawShipsLog();
	drawShipsLog(); // unchanged: no second block
	pc_button_prompt_add(0, { 300, 100, 340, 140 }, "m_menu2.blo", "screen/tex/c_btn.bti", false, "stick glyph");
	pc_button_prompt_frame_end();
	pc_menu_click_post(700.0f / kDrawW, 880.0f / kDrawH);
	drawShipsLog();
	tick();

	std::rewind(out);
	char text[4096] = {};
	const size_t n = std::fread(text, 1, sizeof(text) - 1, out);
	text[n]        = '\0';
	std::fclose(out);
	pc_button_prompt_set_log(nullptr);

	int frames = 0;
	for (const char* p = text; (p = std::strstr(p, "prompt(s) drawn")); ++p) frames++;
	check(frames == 3, "one log block per change, none for an unchanged frame");
	check(std::strstr(text, "w_data1.blo") && std::strstr(text, "y_btn.bti") && std::strstr(text, "target=yes"),
	      "the drawn prompt is logged with its screen, texture and target");
	check(std::strstr(text, "c_btn.bti") && std::strstr(text, "target=no: stick glyph"),
	      "a prompt with no target is logged with why");
	check(std::strstr(text, "click on Y prompt (w_data1.blo): pressed Y") != nullptr, "a click is logged");
	if (sFailures) std::printf("%s", text);
}

} // namespace

int main()
{
	testClassify();
	testGroup();
	testClickPressesTheButton();
	testNotLive();
	testFrameRebuilds();
	testHoverClaimsAndShowsHand();
	testLongPressStaysClaimed();
	testLog();
	if (sFailures) {
		std::printf("%d failure(s)\n", sFailures);
		return 1;
	}
	std::printf("pc_button_prompt_test: all passed\n");
	return 0;
}
