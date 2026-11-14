#pragma once
/**
 * @file pc_day_end_skip.h
 * @brief Whether this frame skips the end-of-day sequence.
 *
 * The PAL disc already skips the march (phase 0) with A or B, then the
 * takeoff (phase 1) on the next frame, and refuses the skip after the
 * captain falls, the Pikmin are wiped out, on day 30, or with every ship
 * part. The PC port uses that same gate on every region, and also accepts
 * Start. Speedrun mode keeps the GameCube behaviour: no skip.
 *
 * The click is eaten (mInputPressed only). A later check in the same frame
 * (the pause menu's Start, or the results screen's A, B or Start) must not
 * see it. The hold stays, so the next frame does not treat it as a new click.
 * Phase 0 goes through MoviePlayer::skipScene, which notes the skip for
 * audio. Phase 1 does not: the disc advances the state without replaying the
 * takeoff's remaining events, so the port only asks for Jac_NoteDemoSkipped.
 */

// DayOverModeState::STATE_PhaseZero / STATE_PhaseOne.
constexpr int PC_DAYEND_PHASE_MARCH   = 0;
constexpr int PC_DAYEND_PHASE_TAKEOFF = 1;

// Controller.h KeyboardButtons. Kept here so the test does not link the game.
constexpr unsigned PC_DAYEND_BTN_A     = 1u << 12;
constexpr unsigned PC_DAYEND_BTN_B     = 1u << 13;
constexpr unsigned PC_DAYEND_BTN_START = 1u << 24;
constexpr unsigned PC_DAYEND_SKIP_BUTTONS = PC_DAYEND_BTN_A | PC_DAYEND_BTN_B | PC_DAYEND_BTN_START;

struct PcDayEndSkipQuery {
	bool skippable;      // not captain-down and not extinction
	bool allParts;       // every ship part collected
	bool lastDay;        // day 30
	bool speedrun;
	int phase;           // PC_DAYEND_PHASE_MARCH or PC_DAYEND_PHASE_TAKEOFF
	bool alreadySkipped; // the march was skipped, so the takeoff goes too
	unsigned pressed;    // Controller::mInputPressed
};

struct PcDayEndSkipDecision {
	bool skip;         // leave this phase even if its movie is still playing
	bool skipScene;    // phase 0: MoviePlayer::skipScene(SCENESKIP_Skip)
	bool noteAudio;    // Jac_NoteDemoSkipped; phase 1, which does not skipScene
	unsigned eatPress; // clear these bits from mInputPressed only (the click)
};

inline PcDayEndSkipDecision pc_day_end_skip(const PcDayEndSkipQuery& q)
{
	PcDayEndSkipDecision d = {};
	if (q.speedrun || !q.skippable || q.allParts || q.lastDay) {
		return d;
	}
	const bool click = (q.pressed & PC_DAYEND_SKIP_BUTTONS) != 0;
	if (q.phase == PC_DAYEND_PHASE_MARCH && click) {
		d.skip      = true;
		d.skipScene = true;
		d.eatPress  = q.pressed & PC_DAYEND_SKIP_BUTTONS;
	} else if (q.phase == PC_DAYEND_PHASE_TAKEOFF && (q.alreadySkipped || click)) {
		d.skip      = true;
		d.noteAudio = true;
		if (click) {
			d.eatPress = q.pressed & PC_DAYEND_SKIP_BUTTONS;
		}
	}
	return d;
}
