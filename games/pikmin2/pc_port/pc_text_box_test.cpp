/**
 * @file pc_text_box_test.cpp
 * @brief Enter, Space and left click show a page, then turn it.
 */

#include "pc_text_box.h"

#include <cstdio>

namespace {

int sFailures = 0;

void expect(int actual, int expected, const char* what)
{
	if (actual != expected) {
		std::printf("FAIL: %s -> got %d, expected %d\n", what, actual, expected);
		++sFailures;
	}
}

} // namespace

int main()
{
	PcTextAccept accept = {};
	expect(pc_text_take_accept(accept, false), 0, "idle is not a press");
	expect(pc_text_take_accept(accept, true), 1, "the first sample of a hold is a press");
	expect(pc_text_take_accept(accept, true), 0, "the rest of the hold is not another press");
	expect(pc_text_take_accept(accept, false), 0, "release is not a press");
	expect(pc_text_take_accept(accept, true), 1, "the next press counts");

	expect(pc_text_box_port_press(false, false), PC_TEXT_BOX_NONE, "typing with no press");
	expect(pc_text_box_port_press(false, true), PC_TEXT_BOX_SHOW_PAGE, "a press while typing shows the page");
	expect(pc_text_box_port_press(true, true), PC_TEXT_BOX_NEXT_PAGE, "a press on a shown page turns it");
	expect(pc_text_box_port_press(true, false), PC_TEXT_BOX_NONE, "a shown page waits");

	// The press that shows the page is the only one in that hold.
	PcTextAccept held = {};
	const bool first = pc_text_take_accept(held, true);
	expect(pc_text_box_port_press(false, first), PC_TEXT_BOX_SHOW_PAGE, "the hold shows the page");
	expect(pc_text_box_port_press(true, pc_text_take_accept(held, true)), PC_TEXT_BOX_NONE,
	       "the same hold does not turn the page");
	pc_text_take_accept(held, false);
	expect(pc_text_box_port_press(true, pc_text_take_accept(held, true)), PC_TEXT_BOX_NEXT_PAGE,
	       "the next press turns the page");

	expect(pc_cutscene_port_skip(false, false, true), 1, "a cutscene with no text skips");
	expect(pc_cutscene_port_skip(false, true, true), 0, "an open text box spends the press");
	expect(pc_cutscene_port_skip(true, false, true), 0, "speedrun keeps the disc rule");
	expect(pc_cutscene_port_skip(false, false, false), 0, "no press leaves the cutscene");

	if (sFailures == 0) {
		std::printf("pc_text_box_test: all checks passed\n");
		return 0;
	}
	std::printf("pc_text_box_test: %d failure(s)\n", sFailures);
	return 1;
}
