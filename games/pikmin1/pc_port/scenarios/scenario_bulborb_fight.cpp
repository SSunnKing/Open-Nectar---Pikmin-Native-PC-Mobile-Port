// bulborb_fight: ten blue Pikmin against a Dwarf Bulborb, with the real enemy AI and no state poked
// (issue #136: during the hand play-test a squad died to the Bulborb and Pikmin thrown off its back
// stopped attacking). Run on the stand-in object stage; the same scenario can be run by hand on a
// user-supplied disc install (the stage's first Dwarf Bulborb) to compare, see below.
//
// The captain stands 90 units from the Bulborb and the Pikmin are born 60 units from it on his side,
// then handed to actOnSituaton (what a thrown Pikmin does on landing). Every second the scenario prints
// the Bulborb's health and the Pikmin by state, and counts, over the run: Pikmin lost, Flick entries
// (a Pikmin shaken off) and, for each Flick, whether that Pikmin attacks again before the end.
//
// Asserted: the Bulborb is found and dies within 10 s, and a Flick lasts 4 s at most on average. Those
// are smoke limits for the authored fixtures: the fight's timing comes from the stand-in Flick, Attack,
// StillJump, JKoke and GetUp motions (standin_anims.h; the Attack motion's KEY_Action0 is what lets ground
// attacks hit, #154) and from the Pikmin parameters, which are the source defaults (parms/pikiMgr.bin is an
// empty list: knock-down 0.7 s, health 100). They do not establish parity with the original game.
// Everything else is printed.
//
// Combat timing against the original game (issues #154 and #180) is a local exact-lane question: it
// needs the original motions and parameters, which only a user-supplied install has.
#include "scenarios/pc_scenario_list.h"

#if PIKI_SCENARIOS

#include <cstdio>
#include <vector>

#include "AIConstant.h"
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
		carry::spawnRow(navi, Blue, kPikmin, at.x + 36.0f, at.z, mLanding);
		for (Piki* p : mLanding) mPiki.push_back(p);
		carry::land(mLanding);
		// The source defaults of AIConstant p08 and p10 send a Pikmin back to the captain after one
		// attack. This fight is about Pikmin fighting until the enemy dies (and, after a Flick,
		// returning to the fight), so the scenario sets both off, as a test control.
		AICONST._A4() = 0;
		AICONST._C4() = 0;
		mHealth0 = mPrevHealth = mBulb->mHealth;
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
		if (mBulb->mHealth < mPrevHealth) {
			int onBack = 0;
			for (Piki* p : mPiki) onBack += p->isAlive() && attacking(p);
			std::printf("[scenario] bulborb_fight: HIT t=%.3f s damage %.2f health %.2f (on its back %d)\n", t / float(rate), mPrevHealth - mBulb->mHealth, mBulb->mHealth, onBack);
		}
		mPrevHealth = mBulb->mHealth;
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
			scenarioFailf(t, "a shaken-off Pikmin is down 4 s or less on average", "%.2f s", flick);
			return;
		}
		std::printf("[scenario] bulborb_fight: PASS at tick %u (Bulborb dead at %.1f s)\n", t, killS);
		mDone = true;
		scenarioPass();
	}

	Teki* mBulb = nullptr;
	float mPrevHealth = 0.0f;
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
