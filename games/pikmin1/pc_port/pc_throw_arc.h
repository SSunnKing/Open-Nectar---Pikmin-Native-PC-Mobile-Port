#ifndef PC_THROW_ARC_H
#define PC_THROW_ARC_H

/**
 * @file pc_throw_arc.h
 * @brief Throw flight at the GameCube's 30 Hz step.
 *
 * Navi::throwPiki sets the launch speed once, from gravity, the throw height
 * and the flight time. Creature::update then integrates that velocity twice
 * per tick (the zeroed volatile pass, then the real velocity), and moveNew
 * subtracts gravity times the tick dt before moving. That step was written
 * for the disc's 1/30 s. A smaller tick climbs a higher arc and stays up
 * longer, so the landing point moves with the frame rate (upstream #320).
 *
 * Above 30 Hz the flight takes a whole 1/30 s step, then waits until another
 * 1/30 s of tick time has passed before the next one. 30 Hz never enters:
 * every tick integrates with the frame dt, as before.
 */

struct Creature;
struct Piki;

/// Disc step. 1/30 s, the same length as two GameCube retraces.
constexpr float kPcThrowArcDiscDt = 1.0f / 30.0f;

enum class PcThrowArcPhase : int { Idle = 0, Primed = 1, Coasting = 2 };

enum class PcThrowArcAction : int { Passthrough = 0, Hold = 1, DiscStep = 2 };

struct PcThrowArcClock {
	float bank = 0.0f;
	PcThrowArcPhase phase = PcThrowArcPhase::Idle;
};

/// What this tick should do. Does not modify the clock.
inline PcThrowArcAction pc_throw_arc_peek(const PcThrowArcClock& clock, float frameDt, int ticksPer30Hz)
{
	if (ticksPer30Hz <= 1 || clock.phase == PcThrowArcPhase::Idle) {
		return PcThrowArcAction::Passthrough;
	}
	if (clock.phase == PcThrowArcPhase::Primed) {
		return PcThrowArcAction::DiscStep;
	}
	// A float sum of 1/60 or 1/120 steps can land a ulp under 1/30.
	if (clock.bank + frameDt + 1.0e-4f < kPcThrowArcDiscDt) {
		return PcThrowArcAction::Hold;
	}
	return PcThrowArcAction::DiscStep;
}

/// Record that peek. The launch tick steps immediately and does not bank the
/// dt from before the throw. Later ticks bank frameDt.
inline void pc_throw_arc_commit(PcThrowArcClock* clock, float frameDt, int ticksPer30Hz)
{
	if (clock == nullptr) {
		return;
	}
	const PcThrowArcAction action = pc_throw_arc_peek(*clock, frameDt, ticksPer30Hz);
	if (action == PcThrowArcAction::Passthrough) {
		return;
	}
	if (clock->phase == PcThrowArcPhase::Primed) {
		clock->phase = PcThrowArcPhase::Coasting;
		clock->bank  = 0.0f;
		return;
	}
	clock->bank += frameDt;
	if (action == PcThrowArcAction::DiscStep) {
		clock->bank -= kPcThrowArcDiscDt;
		if (clock->bank < 0.0f) {
			clock->bank = 0.0f;
		}
	}
}

inline void pc_throw_arc_prime(PcThrowArcClock* clock)
{
	if (clock == nullptr) {
		return;
	}
	clock->phase = PcThrowArcPhase::Primed;
	clock->bank  = 0.0f;
}

/// Arm a throw. No effect at 30 Hz, so that path does not gain state.
void pc_throw_arc_launch(Piki* piki);

/// Drop a throw. Landing, and Piki::init when the slot is reused, both call this.
void pc_throw_arc_release(Piki* piki);

/// True when this tick of an armed throw should skip integration. Creature
/// movement decides the tick; flying exec reads it afterwards.
bool pc_throw_arc_would_hold(Piki* piki);

/// dt for one integration of an armed throw: 1/30 s on a disc step, otherwise
/// the frame dt. At 30 Hz this is the frame dt.
float pc_throw_arc_integrate_dt(Piki* piki, float frameDt);

/// Called once from Creature::update, after AI and before the two moveNew
/// passes. Returns true when the throw owns this tick. *hold skips both
/// passes. Otherwise *stepDt replaces the frame dt for those passes.
bool pc_throw_arc_movement(Creature* creature, float frameDt, bool* hold, float* stepDt);

#endif
