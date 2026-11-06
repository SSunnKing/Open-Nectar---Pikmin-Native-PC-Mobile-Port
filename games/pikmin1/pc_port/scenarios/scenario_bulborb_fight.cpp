// bulborb_fight: ten blue Pikmin against a Dwarf Bulborb, with the real enemy AI and no state poked
// (issue #136: during the hand play-test a squad died to the Bulborb and Pikmin thrown off its back
// stopped attacking). Run on the stand-in object stage and on a disc install (the stage's first Dwarf
// Bulborb) the same way, then compare.
//
// The captain stands 90 units from the Bulborb and the Pikmin are born 60 units from it on his side,
// then handed to actOnSituaton (what a thrown Pikmin does on landing). Every second the scenario prints
// the Bulborb's health and the Pikmin by state, and counts, over the run: Pikmin lost, Flick entries
// (a Pikmin shaken off) and, for each Flick, whether that Pikmin attacks again before the end.
//
// Asserted: the Bulborb is found and dies within 10 s, and a Flick lasts 4 s at most on average. Measured
// 2026-11-02 at 30 Hz, ten Pikmin: the disc kills it in 4 s with 0 lost, 3 flicks and a Flick of 2.9 s on
// average; the stand-ins (before the Flick motion, info 58, carried the disc's timing, #136) took 14 s with
// 2 lost, 9 flicks and a Flick of 4.6 s, and now take 5 s with 0 lost, 5 flicks and 3.6 s. Everything else is
// printed.
//
// Issue #154 (2026-11-04): Attack, StillJump, JKoke and GetUp carry the disc's timing as well
// (standin_anims.h). Disc on this branch: dead at 3.4 s (30 Hz) and 2.5 s (60 Hz), Flick 2.9 s and 2.8 s.
// Stand-ins before: 4.3 s and 4.5 s, Flick 3.6 s (5 flicks at 30 Hz, 1 at 60 Hz). After: 2.2 s and 1.5 s,
// Flick 1.8 s (5 flicks at 30 Hz, none at 60 Hz).
//
// The stand-in parms/pikiMgr.bin now carries every disc value (standin-piki-parms.md), among them the
// knock-down time of 1 to 3 s (p74, p43; the code's is 0.7 s) and health 30 (p03; code 100). Stand-ins:
// dead at 2.4 s (30 Hz) and 1.6 s (60 Hz), Flick 3.3 s (5 flicks at 30 Hz, none at 60 Hz); the disc 3.4 s
// and 2.5 s, Flick 2.9 s. They still win sooner than the disc; the open lead is the stand-in Bulborb's
// collision spheres, which sit lower than the disc's (#154).
#include "scenarios/pc_scenario_list.h"

#if PIKI_SCENARIOS

#include <cstdio>
#include <vector>

#include "GameStat.h"
#include "MapMgr.h"
#include "Navi.h"
#include "NaviMgr.h"
#include "Piki.h"
#include "PikiMgr.h"
#include "PikiState.h"
#include "scenarios/scenario_carry_common.h"
#include "teki.h"

namespace {

constexpr int kPikmin  = 10;
constexpr float kRunS  = 30.0f;

class BulborbFight : public PcScenario {
public:
	const char* name() const override { return "bulborb_fight"; }

	void onStageReady() override
	{
		Iterator it(tekiMgr);
		CI_LOOP(it)
		{
			Teki* t = static_cast<Teki*>(*it);
			if (t->mTekiType == TEKI_Chappy && !mBulb) mBulb = t;
		}
		Navi* navi = naviMgr->getNavi();
		if (!mBulb || !navi) {
			scenarioFailf(0, "a Dwarf Bulborb and a captain", "bulborb %d, captain %d", (int)(mBulb != nullptr), (int)(navi != nullptr));
			mDone = true;
			return;
		}
		const Vector3f at = mBulb->mSRT.t;
		navi->mSRT.t.set(at.x + 90.0f, mapMgr->getMinY(at.x + 90.0f, at.z, true), at.z);
		std::printf("[scenario] bulborb_fight: Bulborb at (%.0f, %.0f), health %.0f\n", at.x, at.z, mBulb->mHealth);
		carry::spawnRow(navi, Blue, kPikmin, at.x + 60.0f, at.z, mLanding);
		for (Piki* p : mLanding) mPiki.push_back(p);
		carry::land(mLanding);
		mHealth0 = mBulb->mHealth;
	}

	void providePads(unsigned, PcInputPad&) override {}

	void onTick(unsigned t) override
	{
		if (mDone) return;
		const unsigned rate = unsigned(pc_scenario_ticks_per_second());
		for (size_t i = 0; i < mPiki.size(); i++) {
			Piki* p = mPiki[i];
			if (!p->isAlive()) {
				if (!mDead[i]) {
					mDead[i] = true;
					mLost++;
					std::printf("[scenario] bulborb_fight: t=%.1f s Pikmin %zu lost (state %d)\n", t / float(rate), i, p->getState());
				}
				continue;
			}
			const int state = p->getState();
			if (state != PIKISTATE_Flick && mState[i] == PIKISTATE_Flick) {
				const float d = (t - mFlickTick[i]) / float(rate);
				mFlickSum += d;
				mFlickMax = d > mFlickMax ? d : mFlickMax;
				mFlickEnds++;
			}
			if (state == PIKISTATE_Flick && !mPrinted) {
				mPrinted       = true;
				AnimInfo* info = p->mPikiAnimMgr.getUpperAnimator().mAnimInfo;
				int table      = -1;
				for (int k = 0; info && info->mMgr && k < info->mMgr->countAnims() && table < 0; k++) {
					if (info->mMgr->findAnim(k) == info) table = k;
				}
				if (info) {
					std::printf("[scenario] bulborb_fight: Flick motion: table index %d, %d frames, flags %d, speed %.1f; anim keys", table, info->mData ? info->mData->mTotalFrameCount : -1,
					            (int)info->mParams.mFlags(), info->mParams.mSpeed());
					for (int k = 0; k < info->countAKeys(); k++) std::printf(" %d", info->getKeyValue(k));
					std::printf("; info keys");
					for (int k = 0; k < info->countIKeys(); k++) std::printf(" (key %d type %d)", info->getInfoKey(k)->mFrameIndex, info->getInfoKey(k)->mKeyType);
					std::printf("\n");
				}
			}
			if (state == PIKISTATE_Flick && mState[i] != PIKISTATE_Flick) {
				mFlicks++;
				mFlicked[i]    = true;
				mFlickTick[i]  = t;
				mAttackedAfter[i] = false;
			}
			if (mFlicked[i] && mAttackedAfter[i] == false && state == PIKISTATE_Normal && p->mMode != PikiMode::FormationMode && attacking(p)) {
				mAttackedAfter[i] = true;
				mReattack++;
			}
			mState[i] = state;
		}
		if (!mKillTick && mBulb->mHealth <= 0.0f) mKillTick = t;
		if (t % rate == 0) census(t, rate);
		if (t >= unsigned(kRunS * rate)) finish(t, rate);
	}

private:
	bool attacking(Piki* p) { return mBulb && p->mStickTarget == mBulb; }

	void census(unsigned t, unsigned rate)
	{
		int byState[64] = {}, alive = 0, stuck = 0;
		for (Piki* p : mPiki) {
			if (!p->isAlive()) continue;
			alive++;
			byState[p->getState() & 63]++;
			stuck += attacking(p);
		}
		std::printf("[scenario] bulborb_fight: t=%.0f s Bulborb health %.0f/%.0f; alive %d, on its back %d; states", t / float(rate), mBulb->mHealth, mHealth0, alive, stuck);
		for (int i = 0; i < 64; i++)
			if (byState[i]) std::printf(" %d x%d", i, byState[i]);
		std::printf("\n");
	}

	void finish(unsigned t, unsigned rate)
	{
		std::printf("[scenario] bulborb_fight: Flick lasted %.2f s on average, %.2f s at most (%d ended)\n", mFlickEnds ? mFlickSum / mFlickEnds : 0.0f, mFlickMax, mFlickEnds);
		std::printf("[scenario] bulborb_fight: end at %.0f s: Pikmin lost %d of %d, flicks %d, re-attacks after a flick %d, Bulborb health %.0f of %.0f\n", t / float(rate), mLost, kPikmin,
		            mFlicks, mReattack, mBulb->mHealth, mHealth0);
		const float killS = mKillTick ? mKillTick / float(rate) : -1.0f;
		const float flick = mFlickEnds ? mFlickSum / mFlickEnds : 0.0f;
		if (killS < 0.0f || killS > 10.0f) {
			mDone = true;
			scenarioFailf(t, "ten Pikmin kill the Dwarf Bulborb within 10 s", "killed at %.1f s (-1: not killed)", killS);
			return;
		}
		if (flick > 4.0f) {
			mDone = true;
			scenarioFailf(t, "a shaken-off Pikmin is down 4 s or less on average (the disc: 2.9 s)", "%.2f s", flick);
			return;
		}
		std::printf("[scenario] bulborb_fight: PASS at tick %u (Bulborb dead at %.1f s)\n", t, killS);
		mDone = true;
		scenarioPass();
	}

	Teki* mBulb = nullptr;
	std::vector<Piki*> mLanding, mPiki;
	bool mDone = false, mDead[kPikmin] = {}, mFlicked[kPikmin] = {}, mAttackedAfter[kPikmin] = {};
	int mState[kPikmin] = {};
	unsigned mFlickTick[kPikmin] = {};
	int mLost = 0, mFlicks = 0, mReattack = 0;
	float mHealth0 = 0.0f, mFlickSum = 0.0f, mFlickMax = 0.0f;
	int mFlickEnds = 0;
	unsigned mKillTick = 0;
	bool mPrinted = false;
};

} // namespace

PcScenario* pc_scenario_make_bulborb_fight(void)
{
	return new BulborbFight();
}

#endif
