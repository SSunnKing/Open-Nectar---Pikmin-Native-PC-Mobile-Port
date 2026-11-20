/**
 * @file pc_carry_gauge_colour_test.cpp
 * @brief Locks the carry counter colours (upstream issue #360).
 *
 * GameCube (the default): blue target number over a red count and line, no
 * matter who carries or where the object goes. Pikmin 2 style: both numbers
 * take the colour of the destination Onion or the main carriers.
 */

#include "pc_carry_gauge_colour.h"

#include <cstdio>

namespace {

int sFailures = 0;

bool same(const PcCarryGaugeRgb& a, const PcCarryGaugeRgb& b)
{
	return a.r == b.r && a.g == b.g && a.b == b.b;
}

void check(bool condition, const char* what)
{
	if (!condition) {
		std::printf("FAIL: %s\n", what);
		++sFailures;
	}
}

const PcCarryGaugeRgb kRed = { 255, 32, 32 };
const PcCarryGaugeRgb kBlue = { 32, 32, 255 };
const PcCarryGaugeRgb kYellow = { 255, 255, 32 };

// PikiColor: Blue 0, Red 1, Yellow 2; -1 = no carriers or destination.
const int kCarryColors[] = { -1, 0, 1, 2 };

} // namespace

int main()
{
	for (int color : kCarryColors) {
		const PcCarryGaugeColours gc = pc_carry_gauge_colours(false, color);
		check(same(gc.top, kBlue), "GameCube: top number is always blue");
		check(same(gc.bottom, kRed), "GameCube: bottom number and line are always red");
	}

	const PcCarryGaugeColours none = pc_carry_gauge_colours(true, -1);
	check(same(none.top, kBlue) && same(none.bottom, kRed), "Pikmin 2 style with no carrier colour keeps blue over red");

	const PcCarryGaugeColours blue = pc_carry_gauge_colours(true, 0);
	check(same(blue.top, kBlue) && same(blue.bottom, kBlue), "Pikmin 2 style: blue carriers colour both numbers blue");

	const PcCarryGaugeColours red = pc_carry_gauge_colours(true, 1);
	check(same(red.top, kRed) && same(red.bottom, kRed), "Pikmin 2 style: red carriers colour both numbers red");

	const PcCarryGaugeColours yellow = pc_carry_gauge_colours(true, 2);
	check(same(yellow.top, kYellow) && same(yellow.bottom, kYellow), "Pikmin 2 style: yellow carriers colour both numbers yellow");

	if (sFailures) return 1;
	std::printf("pc_carry_gauge_colour_test: all checks passed\n");
	return 0;
}
