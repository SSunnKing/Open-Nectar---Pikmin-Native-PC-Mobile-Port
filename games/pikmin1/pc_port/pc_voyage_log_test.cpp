/**
 * @file pc_voyage_log_test.cpp
 * @brief Day-1 voyage log after Skip tutorial. No disc, save, or window.
 *
 * The log opens day 1 as mDaysSeen[1]. A normal day end writes that slot from
 * ResultFlags::getDocument after the clock has moved to day 2. Skipping the
 * tutorial used to move the clock and skip getDocument, so getDayDocument
 * returned length 0 and the page opened with stars and no text.
 */

#include "PlayerState.h"
#include "ResultFlags.h"
#include "sysNew.h"

#include <cstddef>
#include <cstdio>
#include <cstdlib>

static int gCurrDay = 1;

int PlayerState::getCurrDay()
{
	return gCurrDay;
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

struct Doc {
	int id;
	int length;
};

Doc dayDocument(ResultFlags& flags, int day)
{
	Doc doc;
	doc.length = -1;
	doc.id     = flags.getDayDocument(day, doc.length);
	return doc;
}

// cleanupDayEnd on the Impact Site, the flags a skip has set before the log
// is recorded.
void grantDayOneFlags(ResultFlags& flags)
{
	flags.setOn(zen::RESFLAG_EndFirstDay);
	flags.setOn(zen::RESFLAG_UnusedControls2);
}

const int kDayOnePages = zen::RESFLAG_FirstVisitForest - zen::RESFLAG_EndFirstDay;

}

int main()
{
	// The blank page: day 1 was reached, the flag is pending, and nothing
	// wrote the slot the log reads. ogScrDiaryMgr::start returns on length 0.
	// ogDrawDiary::open still plays the starfield; that is the original screen
	// code, so this test does not ask the page to stay closed.
	{
		ResultFlags flags;
		grantDayOneFlags(flags);
		gCurrDay = 1;
		const Doc day1 = dayDocument(flags, 1);
		check(day1.id == -1 && day1.length == 0, "unrecorded day 1 has no text");
	}

	// Recording before the clock moves must not write slot 0 or slot 1.
	{
		ResultFlags flags;
		grantDayOneFlags(flags);
		gCurrDay   = 0;
		int pages  = 99;
		const int id = flags.commitTutorialSkipDocument(2, pages);
		check(id == -1 && pages == 0, "commit while the clock is still on day 1 does nothing");
		check(dayDocument(flags, 0).length == 0, "slot 0 stays empty");
		check(dayDocument(flags, 1).length == 0, "slot 1 stays empty");
		check(flags.commitTutorialSkipDocument(1, pages) == -1, "a clock that is not day 2 does not record");
	}

	// The skip, once the clock reads day 2: same entry a normal day end stores.
	{
		ResultFlags flags;
		grantDayOneFlags(flags);
		gCurrDay  = 1;
		int pages = 0;
		const int id = flags.commitTutorialSkipDocument(2, pages);
		const Doc day1 = dayDocument(flags, 1);
		check(id == zen::RESFLAG_EndFirstDay, "skip records the crash-landing entry");
		check(pages == kDayOnePages && day1.length == kDayOnePages, "crash landing is two pages");
		check(day1.id == zen::RESFLAG_EndFirstDay, "the log reads that entry from day 1");
		check(dayDocument(flags, 0).length == 0, "day 1 is not stored in slot 0");

		gCurrDay = 2;
		int nextPages = 0;
		const int next = flags.getDocument(nextPages);
		// UnusedControls2 is setSeen at init, and setOn leaves a seen flag
		// alone, so it never becomes the next entry. The auto-set misc log does.
		check(next == zen::RESFLAG_Misc1, "the next day does not repeat the crash landing");
		check(dayDocument(flags, 1).id == zen::RESFLAG_EndFirstDay, "day 1 is left in place");
	}

	// A second commit must not replace day 1 with a log earned afterwards.
	{
		ResultFlags flags;
		grantDayOneFlags(flags);
		gCurrDay = 1;
		int pages = 0;
		flags.commitTutorialSkipDocument(2, pages);
		flags.setOn(zen::RESFLAG_FirstVisitForest);
		pages = 0;
		const int again = flags.commitTutorialSkipDocument(2, pages);
		check(again == zen::RESFLAG_EndFirstDay && pages == kDayOnePages, "a second commit keeps day 1");
		gCurrDay = 2;
		int forestPages = 0;
		check(flags.getDocument(forestPages) == zen::RESFLAG_FirstVisitForest,
		      "a log earned after the skip is still shown on the next day");
	}

	// A day that was played normally is not rewritten on load.
	{
		ResultFlags flags;
		grantDayOneFlags(flags);
		gCurrDay = 1;
		int pages = 0;
		check(flags.getDocument(pages) == zen::RESFLAG_EndFirstDay, "a normal day end records day 1");
		check(!flags.repairUnrecordedDayOne(2), "a normal save is not repaired");
		const Doc day1 = dayDocument(flags, 1);
		check(day1.id == zen::RESFLAG_EndFirstDay && day1.length == kDayOnePages, "the normal entry stays");
	}

	// An already-skipped save, still on day 2, with another log pending.
	{
		ResultFlags flags;
		grantDayOneFlags(flags);
		flags.setOn(zen::RESFLAG_FirstVisitForest);
		check(!flags.repairUnrecordedDayOne(1), "a day-1 save is not repaired");
		check(dayDocument(flags, 1).length == 0, "day 1 is untouched while the day has not ended");
		check(flags.repairUnrecordedDayOne(2), "a skipped save on day 2 gains the crash landing");
		const Doc day1 = dayDocument(flags, 1);
		check(day1.id == zen::RESFLAG_EndFirstDay && day1.length == kDayOnePages, "repaired day 1 is the crash landing");
		check(!flags.repairUnrecordedDayOne(2), "repair does not run twice");
		gCurrDay = 2;
		int pages = 0;
		check(flags.getDocument(pages) == zen::RESFLAG_FirstVisitForest,
		      "repair leaves a later pending log for that day");
		check(dayDocument(flags, 1).id == zen::RESFLAG_EndFirstDay, "day 1 survives the next day end");
	}

	// The crash landing was already consumed into a later day. Putting it on
	// day 1 as well would show it twice, and the later day's real entry is gone.
	{
		ResultFlags flags;
		grantDayOneFlags(flags);
		flags.setOn(zen::RESFLAG_FirstVisitForest);
		gCurrDay = 2;
		int pages = 0;
		check(flags.getDocument(pages) == zen::RESFLAG_EndFirstDay, "a later day end absorbs the still-pending crash landing");
		check(dayDocument(flags, 1).length == 0, "day 1 was never recorded");
		check(dayDocument(flags, 2).id == zen::RESFLAG_EndFirstDay, "the later day holds the crash landing");
		check(!flags.repairUnrecordedDayOne(3), "that save is not rearranged");
		check(dayDocument(flags, 1).length == 0, "day 1 stays empty once the flag was consumed");
		check(dayDocument(flags, 2).id == zen::RESFLAG_EndFirstDay, "the later day keeps the entry it showed");
	}

	// No day-1 flag was ever set. Do not invent a page.
	{
		ResultFlags flags;
		check(!flags.repairUnrecordedDayOne(5), "a save with no pending day-1 flag is left alone");
		check(dayDocument(flags, 1).length == 0, "day 1 stays empty when the flag was never set");
	}

	if (gFailures != 0) {
		std::printf("pc_voyage_log_test: %d check(s) failed\n", gFailures);
		return 1;
	}
	std::printf("pc_voyage_log_test: all checks passed\n");
	return 0;
}
