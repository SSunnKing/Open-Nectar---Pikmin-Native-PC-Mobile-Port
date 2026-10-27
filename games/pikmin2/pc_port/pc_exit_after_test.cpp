/**
 * @file pc_exit_after_test.cpp
 * @brief Checks the PIKMIN_EXIT_AFTER_FRAMES parser and trigger, no window needed.
 */

#include "pc_exit_after.h"

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

} // namespace

int main()
{
	// Off unless given a plain positive number.
	check(pc_exit_after_frames_parse(nullptr) == 0, "unset is off");
	check(pc_exit_after_frames_parse("") == 0, "empty is off");
	check(pc_exit_after_frames_parse("0") == 0, "zero is off");
	check(pc_exit_after_frames_parse("-5") == 0, "negative is off");
	check(pc_exit_after_frames_parse("+5") == 0, "a sign is off");
	check(pc_exit_after_frames_parse("12abc") == 0, "trailing garbage is off");
	check(pc_exit_after_frames_parse(" 12") == 0, "leading space is off");
	check(pc_exit_after_frames_parse("4294967296") == 0, "past UINT32_MAX is off");

	check(pc_exit_after_frames_parse("1") == 1, "1");
	check(pc_exit_after_frames_parse("1800") == 1800, "1800");
	check(pc_exit_after_frames_parse("4294967295") == 4294967295u, "UINT32_MAX");

	// Fires on the Nth retrace and stays fired; never when off.
	check(!pc_exit_after_frames_reached(0, 0), "off at 0");
	check(!pc_exit_after_frames_reached(0, 100000), "off stays off");
	check(!pc_exit_after_frames_reached(1800, 1799), "not before N");
	check(pc_exit_after_frames_reached(1800, 1800), "at N");
	check(pc_exit_after_frames_reached(1800, 1801), "after N");

	if (sFailures == 0) std::printf("pc_exit_after_test: all checks passed\n");
	return sFailures == 0 ? 0 : 1;
}
