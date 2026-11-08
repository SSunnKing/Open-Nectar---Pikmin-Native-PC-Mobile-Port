// enemy_kills: every enemy on the stand-in stage killed once, the kill count, the diary flags,
// and the buried enemies that cannot die until they surface (coverage/edge-cases.md EC-50,
// coverage/milestones.md EN1), on sandbox_all_objects (7 tekis, rolling boulders, the snake and
// the geyser).
//
// What the code does (GameCube original; characterised, nothing fixed): a teki at health 0 runs
// its TAIAdeadCheck, then BTeki::dieSoon (tekibteki.cpp:616-660) drops a corpse pellet when
// TPI_CorpseType is LeaveCorpse and counts GameStat::killTekis once. A boss counts once in its
// AI's die state (SnakeAi::dieState, SnakeAi.cpp:1137). The Burrowing Snagret (snake) checks
// dieTransit only once it has surfaced (SNAKEAI_Under has no die check), and a Sheargrub
// (kabekuiA/B) that has burrowed (its Waiting state: invincible, no TAIAdeadCheck) stays at 0
// health uncounted until the captain comes near. The plugged geyser (mizu, a boss object) is not
// an enemy: its life at 0 counts nothing. Rolling boulders (iwagon) come from a generator, break
// on their own through dieSoon and each adds 1 to killTekis; the scenario attributes those steps
// to the boulder leaving tekiMgr on that tick and keeps them out of the per-enemy count. Beatle's
// diary flag is 2 (setSeen) when it dies (TAIbeatle.cpp:794) and Snake's when it starts to die
// (SnakeAi::initDie, SnakeAi.cpp:945).
//
// In order, each within a bound in seconds:
//   Kill    Each teki (chappy, swallow, palm, beatle, kabekuiA, otama, kabekuiB) at health 0
//           adds exactly 1 to killTekis within 15 s, and nothing more in the next 2 s. After
//           beatle, RESFLAG_Beatle is 2.
//   Buried  A teki still uncounted after 5 s must be burrowed (TEKIOPT_Invincible); the captain
//           is moved beside it and it is counted once within 30 s. At least one is (kabekuiA at
//           both rates).
//   Snake   At life 0: killTekis unchanged for 5 s (flag still 0); the captain is moved beside it,
//           it surfaces and is counted once within 30 s, and its flag is 2.
//   Geyser  mizu at life 0: killTekis unchanged for 5 s.
//   Total   killTekis is 8 plus the boulder breaks (at least 1).
//
// Negative controls: the Snake hold and Geyser are in-scenario contrasts: an enemy at 0 health
// that is not counted passes only because the count comes from the death path, and the snake must
// then count, so a count that always (or never) moved fails one or the other. Manual (2026-10-31,
// local, not committed), at 30 Hz: with begin() leaving health as it is, it fails "EC-50: a
// surfaced teki at health 0 is counted once within 15 s" (teki 0: kills 0 -> 0); with
// moveCaptainBeside a no-op, it fails "a buried enemy surfaces and is counted once within 30 s" (teki 4: kills 4 -> 4).
#include "scenarios/pc_scenario_list.h"

#if PIKI_SCENARIOS

#include <cstdarg>
#include <cstdio>
#include <vector>

#include "Boss.h"
#include "GameStat.h"
#include "MapMgr.h"
#include "Navi.h"
#include "NaviMgr.h"
#include "PlayerState.h"
#include "scenarios/scenario_progress.h"
#include "teki.h"

namespace {

using scenario_progress::ResultPeek;

constexpr int kBoulder    = 2;  // iwagon (TEKI_Iwagon, rolling boulder)
constexpr int kSnakeObj   = 41; // OBJTYPE_Snake
constexpr int kMizuObj    = 49; // OBJTYPE_Mizu

struct Target {
	Teki* teki = nullptr;
	Boss* boss = nullptr;
	bool buried = false, geyser = false;
	const char* what = "";
};

class EnemyKills : public PcScenario {
public:
	const char* name() const override { return "enemy_kills"; }

	void onStageReady() override
	{
		std::vector<Target> late;
		Iterator it(tekiMgr);
		CI_LOOP(it)
		{
			Target t;
			t.teki = static_cast<Teki*>(*it);
			if (t.teki->mTekiType == kBoulder) continue; // counted by countBoulderBreaks
			t.what = "teki";
			mTargets.push_back(t);
		}
		Iterator bt(bossMgr);
		CI_LOOP(bt)
		{
			Target t;
			t.boss   = static_cast<Boss*>(*bt);
			t.buried = t.boss->mObjType == kSnakeObj;
			t.geyser = t.boss->mObjType == kMizuObj;
			t.what   = t.buried ? "buried snake" : (t.geyser ? "geyser" : "boss");
			late.push_back(t);
		}
		mSurfaced = (int)mTargets.size();
		for (const Target& t : late) mTargets.push_back(t);
		std::printf("[scenario] enemy_kills: %d targets (%d surfaced tekis)\n", (int)mTargets.size(), mSurfaced);
	}

	void onTick(unsigned t) override
	{
		if (mPhase == Done) return;
		countBoulderBreaks(t);
		mRate          = (unsigned)pc_scenario_ticks_per_second();
		const int k    = GameStat::killTekis - mBoulders; // kills not counting broken boulders
		const float s  = float(t - mPhaseTick) / float(mRate);
		const int nTek = 7;

		if (mPhase == Start) {
			if ((int)mTargets.size() != nTek + 2 || mSurfaced != nTek) {
				fail(t, "sandbox_all_objects: 7 tekis, the snake and the geyser (and boulders)", "%d targets, %d tekis",
				     (int)mTargets.size(), mSurfaced);
				return;
			}
			if (t < mRate) return;
			begin(t, k);
			return;
		}

		Target& cur = mTargets[mIdx];
		switch (mPhase) {
		case Count:
			if (k > mK0 + 1) {
				fail(t, "a kill is counted once", "%s %d: kills %d -> %d", cur.what, mIdx, mK0, k);
			} else if (k == mK0 + 1) {
				std::printf("[scenario] enemy_kills: %s %d counted at %.2f s (kills %d)\n", cur.what, mIdx, s, k);
				enter(Confirm, t);
			} else if (cur.teki && !cur.buried && s > 5.0f && cur.teki->getTekiOption(TEKIOPT_Invincible)) {
				// Burrowed (a kabekui's Waiting state: invincible, no TAIAdeadCheck) at 0 health.
				std::printf("[scenario] enemy_kills: %s %d (type %d) buried at 0 health, uncounted for 5 s\n", cur.what, mIdx, cur.teki->mTekiType);
				cur.buried = true;
				++mBuriedTekis;
				moveCaptainBeside(cur);
				enter(Count, t);
			} else if (s > (cur.buried ? 30.0f : 15.0f)) {
				if (cur.buried) {
					fail(t, "a buried enemy surfaces and is counted once within 30 s", "%s %d: kills %d -> %d", cur.what, mIdx, mK0, k);
				} else {
					fail(t, "EC-50: a surfaced teki at health 0 is counted once within 15 s", "%s %d (type %d): kills %d -> %d", cur.what, mIdx,
					     cur.teki ? cur.teki->mTekiType : -1, mK0, k);
				}
			}
			break;
		case Confirm:
			if (k != mK0 + 1) {
				fail(t, "a kill is counted once (no second count within 2 s)", "%s %d: kills %d -> %d", cur.what, mIdx, mK0, k);
			} else if (s >= 2.0f) {
				if (!flagsAfter(t, cur)) return;
				next(t, k);
			}
			break;
		case Hold:
			if (k != mK0) {
				fail(t, cur.geyser ? "the geyser at life 0 is not counted as a kill" : "EC-50: a buried enemy at health 0 is not counted",
				     "%s %d: kills %d -> %d after %.2f s", cur.what, mIdx, mK0, k, s);
			} else if (s >= 5.0f) {
				if (cur.boss && cur.buried && flag(zen::RESFLAG_Snake) != 0) {
					fail(t, "the buried snake's diary flag is still 0", "%d", flag(zen::RESFLAG_Snake));
					return;
				}
				std::printf("[scenario] enemy_kills: %s %d at 0 health uncounted for 5 s\n", cur.what, mIdx);
				if (cur.geyser) {
					next(t, k);
				} else {
					moveCaptainBeside(cur);
					enter(Count, t);
				}
			}
			break;
		default:
			break;
		}
	}

private:
	enum Phase { Start, Count, Confirm, Hold, Done };

	// A rolling boulder (iwagon) breaks on its own and goes through BTeki::dieSoon like any kill;
	// the generator (iwagen) then rolls a new one. A killTekis step on the tick a boulder leaves
	// tekiMgr is the boulder's (dieSoon counts, then kill(false) removes it, in one call; its
	// TEKIOPT_Alive clears earlier, when it starts to break).
	void countBoulderBreaks(unsigned t)
	{
		std::vector<Teki*> alive;
		Iterator it(tekiMgr);
		CI_LOOP(it)
		{
			Teki* q = static_cast<Teki*>(*it);
			if (q->mTekiType == kBoulder) alive.push_back(q);
		}
		int edges = 0;
		for (Teki* q : mBoulderAlive) {
			bool still = false;
			for (Teki* a : alive) still = still || a == q;
			if (!still) ++edges;
		}
		mBoulderAlive = alive;
		const int step = int(GameStat::killTekis) - mLastKills;
		mLastKills     = GameStat::killTekis;
		const int mine = edges < step ? edges : step;
		mBoulders += mine;
		if (edges) std::printf("[scenario] enemy_kills: tick %u %d boulder(s) broke, killTekis +%d (%d boulders so far)\n", t, edges, step, mBoulders);
	}

	int flag(int f) const { return ResultPeek::get(playerState->mResultFlags, f); }

	bool flagsAfter(unsigned t, const Target& cur)
	{
		if (cur.teki && cur.teki->mTekiType == 17 && flag(zen::RESFLAG_Beatle) != 2) { // beatle
			fail(t, "EN1: Beatle's diary flag is seen (2) after it dies", "%d", flag(zen::RESFLAG_Beatle));
			return false;
		}
		if (cur.boss && cur.buried && flag(zen::RESFLAG_Snake) != 2) {
			fail(t, "EN1: Snake's diary flag is seen (2) after it dies", "%d", flag(zen::RESFLAG_Snake));
			return false;
		}
		return true;
	}

	void begin(unsigned t, int k)
	{
		Target& cur = mTargets[mIdx];
		mK0         = k;
		if (cur.teki) {
			cur.teki->mHealth = 0.0f;
		} else {
			cur.boss->subCurrentLife(cur.boss->getCurrentLife());
			cur.boss->mHealth = 0.0f;
		}
		std::printf("[scenario] enemy_kills: tick %u %s %d (type %d) at 0 health (kills %d)\n", t, cur.what, mIdx,
		            cur.teki ? cur.teki->mTekiType : cur.boss->mObjType, k);
		enter((cur.buried || cur.geyser) ? Hold : Count, t);
	}

	void next(unsigned t, int k)
	{
		if (++mIdx < (int)mTargets.size()) {
			begin(t, k);
			return;
		}
		if (mBuriedTekis < 1) {
			fail(t, "EC-50: at least one kabekui is buried (uncounted) at 0 health", "none buried");
			return;
		}
		if (k != 8 || mBoulders < 1 || GameStat::killTekis != 8 + mBoulders) {
			fail(t, "EC-50: killTekis is 8 plus one per broken boulder, and boulders broke", "killTekis %d, boulders %d",
			     (int)GameStat::killTekis, mBoulders);
			return;
		}
		mPhase = Done;
		std::printf("[scenario] enemy_kills: %d buried tekis woken; killTekis %d (%d boulders), beatle flag %d, snake flag %d\n", mBuriedTekis, (int)GameStat::killTekis, mBoulders, flag(zen::RESFLAG_Beatle), flag(zen::RESFLAG_Snake));
		scenarioPass();
	}

	void moveCaptainBeside(const Target& cur)
	{
		Navi* navi = naviMgr->getNavi();
		Vector3f p = cur.teki ? cur.teki->mSRT.t : cur.boss->mSRT.t;
		p.x += 40.0f;
		p.y = mapMgr->getMinY(p.x, p.z, true);
		navi->mSRT.t = p;
		std::printf("[scenario] enemy_kills: captain moved beside %s (%.0f, %.0f, %.0f)\n", cur.what, p.x, p.y, p.z);
	}

	void enter(Phase p, unsigned t)
	{
		mPhase     = p;
		mPhaseTick = t;
	}

	void fail(unsigned t, const char* expected, const char* fmt, ...) __attribute__((format(printf, 4, 5)))
	{
		char buf[256];
		va_list ap;
		va_start(ap, fmt);
		std::vsnprintf(buf, sizeof(buf), fmt, ap);
		va_end(ap);
		mPhase = Done;
		scenarioFailf(t, expected, "%s", buf);
	}

	std::vector<Target> mTargets;
	std::vector<Teki*> mBoulderAlive;
	int mBoulders = 0, mLastKills = 0, mBuriedTekis = 0;
	int mSurfaced = 0, mIdx = 0, mK0 = 0;
	Phase mPhase        = Start;
	unsigned mPhaseTick = 0, mRate = 30;
};

} // namespace

PcScenario* pc_scenario_make_enemy_kills(void)
{
	static EnemyKills s;
	return &s;
}

#endif
