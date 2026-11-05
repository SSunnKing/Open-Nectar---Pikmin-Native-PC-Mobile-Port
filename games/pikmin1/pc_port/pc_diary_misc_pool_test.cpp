/**
 * @file pc_diary_misc_pool_test.cpp
 * @brief No Day Limit: the voyage log never runs out of entries. No disc, save, or window.
 *
 * A day end with no entry of its own shows the next of the 27 Misc pages. A
 * 30-day game never needs more than 27, but No Day Limit holds the day below
 * 30, so the 28th plain day end found no entry: getDocument hit
 * ERROR("cannot happen !") and the day-end code halted on zero pages.
 */

#include "PlayerState.h"
#include "ResultFlags.h"
#include "sysNew.h"

#include <cstddef>
#include <cstdio>
#include <cstdlib>

int PlayerState::getCurrDay()
{
	return 1;
}

static PlayerState gPlayerState;
PlayerState* playerState = &gPlayerState;

void* operator new(size_t size)
{
	return std::malloc(size);
}

void* operator new[](size_t size)
{
	return std::malloc(size);
}

void operator delete(void* ptr) noexcept
{
	std::free(ptr);
}

void operator delete[](void* ptr) noexcept
{
	std::free(ptr);
}

void operator delete(void* ptr, size_t) noexcept
{
	std::free(ptr);
}

void operator delete[](void* ptr, size_t) noexcept
{
	std::free(ptr);
}

namespace {

int gFailures = 0;

void check(bool ok, const char* what)
{
	if (!ok) {
		std::printf("FAIL: %s\n", what);
		gFailures++;
	}
}

} // namespace

int main()
{
	ResultFlags flags;
	flags.initGame();

	for (int n = 0; n < 27; n++) {
		int pages = 0;
		check(flags.getDocument(pages, 1) == zen::RESFLAG_Misc1 + n && pages == 1, "a plain day end shows the next Misc page");
	}

	int pages = 0;
	check(flags.getDocument(pages, 1) == zen::RESFLAG_Misc1 && pages == 1, "the 28th plain day end starts the Misc pages over");
	pages = 0;
	check(flags.getDocument(pages, 1) == zen::RESFLAG_Misc2, "and continues in order");

	flags.setOn(zen::RESFLAG_FirstVisitForest);
	pages = 0;
	check(flags.getDocument(pages, 1) == zen::RESFLAG_FirstVisitForest, "an earned entry still comes first");

	for (int n = 0; n < 200; n++) {
		pages = 0;
		if (flags.getDocument(pages, 1) < 0 || pages <= 0) {
			check(false, "No Day Limit never runs out of diary pages");
			break;
		}
	}

	if (gFailures != 0) {
		std::printf("pc_diary_misc_pool_test: %d check(s) failed\n", gFailures);
		return 1;
	}
	std::printf("pc_diary_misc_pool_test: ok\n");
	return 0;
}
