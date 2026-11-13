/**
 * @file pc_day_end_skip_test.cpp
 * @brief Day-end skip gates, without a disc or a running game.
 */

#include "pc_day_end_skip.h"

#include <cstdio>

namespace {

int sFailures = 0;

void expect(bool cond, const char* what)
{
	if (!cond) {
		std::printf("FAIL: %s\n", what);
		++sFailures;
	}
}

PcDayEndSkipQuery allowed(int phase, unsigned pressed)
{
	PcDayEndSkipQuery q = {};
	q.skippable         = true;
	q.phase             = phase;
	q.pressed           = pressed;
	return q;
}

// What a later check in the same frame still sees after the skip eats the press.
unsigned leftover(unsigned pressed, unsigned eat)
{
	return pressed & ~eat;
}

constexpr unsigned kUnrelated = 1u << 19; // main stick up, not a skip button

} // namespace

int main()
{
	const unsigned buttons = PC_DAYEND_SKIP_BUTTONS;

	// March: A, B or Start each skip, and the click does not survive.
	const unsigned marchButtons[] = { PC_DAYEND_BTN_A, PC_DAYEND_BTN_B, PC_DAYEND_BTN_START };
	for (unsigned bit : marchButtons) {
		const PcDayEndSkipDecision d = pc_day_end_skip(allowed(PC_DAYEND_PHASE_MARCH, bit | kUnrelated));
		expect(d.skip && d.skipScene && !d.noteAudio, "march skip goes through skipScene");
		expect(d.eatPress == bit, "march skip eats only the button that was pressed");
		expect((leftover(bit | kUnrelated, d.eatPress) & buttons) == 0, "march skip leaves no A, B or Start click");
		expect((leftover(bit | kUnrelated, d.eatPress) & kUnrelated) != 0, "march skip leaves unrelated input");
	}

	// Start on the march must not also open pause or confirm the results.
	{
		const unsigned pressed        = PC_DAYEND_BTN_START | PC_DAYEND_BTN_A;
		const PcDayEndSkipDecision d  = pc_day_end_skip(allowed(PC_DAYEND_PHASE_MARCH, pressed));
		const unsigned left           = leftover(pressed, d.eatPress);
		expect((left & PC_DAYEND_BTN_START) == 0, "Start that skips the march does not open pause");
		expect((left & buttons) == 0, "A and Start that skip the march do not confirm the results");
	}

	// Takeoff: a fresh press skips without skipScene, and notes the audio.
	{
		const PcDayEndSkipDecision d = pc_day_end_skip(allowed(PC_DAYEND_PHASE_TAKEOFF, PC_DAYEND_BTN_B));
		expect(d.skip && d.noteAudio && !d.skipScene, "takeoff press skips and notes audio");
		expect(d.eatPress == PC_DAYEND_BTN_B, "takeoff press is eaten");
		expect((leftover(PC_DAYEND_BTN_B, d.eatPress) & buttons) == 0, "takeoff press does not confirm the results");
	}

	// Takeoff follows a skipped march on its own, with nothing new to eat.
	{
		PcDayEndSkipQuery q      = allowed(PC_DAYEND_PHASE_TAKEOFF, kUnrelated);
		q.alreadySkipped         = true;
		const PcDayEndSkipDecision d = pc_day_end_skip(q);
		expect(d.skip && d.noteAudio && !d.skipScene && d.eatPress == 0, "skipped march carries into the takeoff");
	}

	// A press on that carried takeoff is still eaten, so it cannot confirm.
	{
		PcDayEndSkipQuery q = allowed(PC_DAYEND_PHASE_TAKEOFF, PC_DAYEND_BTN_START);
		q.alreadySkipped    = true;
		const PcDayEndSkipDecision d = pc_day_end_skip(q);
		expect(d.skip && d.eatPress == PC_DAYEND_BTN_START, "Start on the carried takeoff is eaten");
		expect((leftover(PC_DAYEND_BTN_START, d.eatPress) & PC_DAYEND_BTN_START) == 0,
		       "Start on the carried takeoff does not open pause");
	}

	// The disc's exclusions, and Speedrun.
	{
		PcDayEndSkipQuery q = allowed(PC_DAYEND_PHASE_MARCH, PC_DAYEND_BTN_A);
		q.skippable         = false;
		expect(!pc_day_end_skip(q).skip && pc_day_end_skip(q).eatPress == 0, "captain down and extinction stay");
		q           = allowed(PC_DAYEND_PHASE_MARCH, PC_DAYEND_BTN_A);
		q.allParts  = true;
		expect(!pc_day_end_skip(q).skip, "all parts stays");
		q          = allowed(PC_DAYEND_PHASE_MARCH, PC_DAYEND_BTN_START);
		q.lastDay  = true;
		expect(!pc_day_end_skip(q).skip, "day 30 stays");
		q          = allowed(PC_DAYEND_PHASE_MARCH, PC_DAYEND_BTN_START);
		q.speedrun = true;
		expect(!pc_day_end_skip(q).skip && pc_day_end_skip(q).eatPress == 0, "speedrun does not skip or eat Start");
		q                = allowed(PC_DAYEND_PHASE_TAKEOFF, 0);
		q.alreadySkipped = true;
		q.speedrun       = true;
		expect(!pc_day_end_skip(q).skip, "speedrun does not carry a skip into the takeoff");
	}

	// Anything else is not a skip, including a later phase of the sequence.
	expect(!pc_day_end_skip(allowed(PC_DAYEND_PHASE_MARCH, 0)).skip, "the march plays out with no press");
	expect(!pc_day_end_skip(allowed(PC_DAYEND_PHASE_MARCH, kUnrelated)).skip, "the stick does not skip");
	expect(!pc_day_end_skip(allowed(PC_DAYEND_PHASE_TAKEOFF, 0)).skip, "the takeoff plays out on its own");
	expect(!pc_day_end_skip(allowed(2, PC_DAYEND_BTN_A | PC_DAYEND_BTN_B | PC_DAYEND_BTN_START)).skip,
	       "the results phase is not this skip");

	if (sFailures) {
		std::printf("%d failure(s)\n", sFailures);
		return 1;
	}
	std::printf("pc_day_end_skip_test: all passed\n");
	return 0;
}
