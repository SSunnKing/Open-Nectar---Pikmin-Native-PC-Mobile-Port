// throw_levels: the throw at each charge level and A with nothing to throw (coverage/edge-cases.md
// EC-28, EC-31, EC-40, EC-42), on the sandbox stand-in stage. Its naviMgr.bin carries the disc's
// throw parms, where min and max are equal: distance 140..140 and height 75..75.
//
// In order, each within a bound in seconds:
//   Away   The captain walks sideways for 4 s, away from the ship (A beside it enters the ship)
//          and from the sandbox gate ahead (a thrown Pikmin hitting it drops out of Flying).
//   Empty  EC-31: no Pikmin on the field. One A press: NaviThrowWaitState::init finds nobody, so
//          exec goes to Attack (the punch). The captain holds nothing, never enters Throw and is
//          back in Walk within 3 s, and pikiMgr stays empty.
//   Throws Six blue Pikmin are born into the party. Each throw holds A until
//          NaviThrowWaitState::mThrowChargeLevel is the wanted level, then releases, aimed 90 from
//          the captain (the cursor offset is set each tick, as the stick would) and 60 degrees
//          round from the last. The stand-in ThrowWait motion has no KEY_LoopEnd, so the level
//          never rises by itself: the scenario sets it as KEY_LoopEnd would. Navi::throwPiki adds
//          the captain's whole velocity to the Pikmin's, the vertical part too, and a captain
//          standing in ThrowWait sometimes carries a falling speed (seen down to -24.4 at y 0)
//          that lowers the arc; the scenario zeroes his vertical speed so the peak shows the
//          charge alone.
//          EC-28: the Pikmin is Hanged before Throw, Flying after it and Normal on landing.
//          mThrowDistance follows the level and is never read: the landing point is the cursor.
//          EC-40 in part: each Pikmin lands on the ground (y within 1 of MapMgr::getMinY).
//          Throws 0-3 (levels 0, 1, 2, 3, the disc's parms): each lands within 15 of the cursor,
//          and the peak above the launch is the same within 1 at every level (EC-42: with min ==
//          max the charge changes nothing, so on the disc a charged throw is no higher).
//          Throws 4-5 (control, mThrowMaxHeight raised by 40 and restored after): level 0 peaks
//          as before (it throws at mThrowMinHeight) and level 3 peaks at least 2 higher, so the
//          checks above would see a charge that did change the height. The raised arc lands past
//          the cursor (23 at 30 Hz), so the cursor check is for the disc's parms only.
//
// Not covered: a cliff or wall in the flight (EC-40's main case; the stand-in sandbox is flat
// where the captain stands), water (EC-41: piki_throw_water, on sandbox_water), Chain Actions timing (EC-43), and
// a yellow Pikmin (its fixed mYellowThrowHeight).
//
// The peak differs by tick rate (+61.3 at 30 Hz, +68.0 at 60 Hz) but is the same across levels at
// each rate; the checks compare within one run.
//
// Negative controls: Empty is the contrast to the throws (A with Pikmin does enter Throw), and
// throws 4-5 are the contrast to EC-42's equal peaks. Manual (2026-10-31, local, not committed):
// kRaise 40 -> 0 fails "EC-42 control: with mThrowMaxHeight raised the peak rises with the charge
// level" at 30 Hz (level 3 +61.3, level 0 +61.3) and at 60 Hz (+68.0, +68.0).
#include "scenarios/pc_scenario_list.h"

#if PIKI_SCENARIOS

#include <cmath>
#include <cstdarg>
#include <cstdio>

#include "AIConstant.h"
#include "CPlate.h"
#include "Dolphin/pad.h"
#include "GameStat.h"
#include "KeyConfig.h"
#include "MapMgr.h"
#include "Navi.h"
#include "NaviMgr.h"
#include "NaviState.h"
#include "Piki.h"
#include "PikiMgr.h"
#include "PikiState.h"

namespace {

// The throws in order: the charge level, and whether mThrowMaxHeight is raised for it.
constexpr int kThrows                = 6;
constexpr int kLevel[kThrows]        = { 0, 1, 2, 3, 0, 3 };
constexpr bool kRaised[kThrows]      = { false, false, false, false, true, true };
constexpr float kRaise               = 40.0f;
constexpr float kSamePeak            = 1.0f;
constexpr float kSelect        = 80.0f; // NaviThrowWaitState::init's pick radius
constexpr float kCursorSlack   = 15.0f;
constexpr float kGroundSlack   = 1.0f;
constexpr float kMinHeightStep = 2.0f;
constexpr float kAim           = 90.0f;

float flat(const Vector3f& a, const Vector3f& b)
{
	const float dx = a.x - b.x, dz = a.z - b.z;
	return std::sqrt(dx * dx + dz * dz);
}

class ThrowLevels : public PcScenario {
public:
	const char* name() const override { return "throw_levels"; }

	void onStageReady() override
	{
		Navi* navi = naviMgr->getNavi();
		if (!navi || !KeyConfig::_instance || KeyConfig::_instance->mThrowKey.mBind != KBBTN_A || pikiMgr->getSize() != 0) {
			fail(0, "a captain, the throw key on A and no Pikmin on the field", "captain %d, Pikmin %d", (int)(navi != nullptr), pikiMgr->getSize());
			return;
		}
		const float hMin = C_NAVI_PARM(navi, mThrowMinHeight), hMax = C_NAVI_PARM(navi, mThrowMaxHeight);
		std::printf("[scenario] throw_levels: distance %.0f..%.0f, height %.0f..%.0f, hold max %.2f s, flight %.2f s, gravity %.0f\n",
		            C_NAVI_PARM(navi, mThrowMinDistance), C_NAVI_PARM(navi, mThrowMaxDistance), hMin, hMax, C_NAVI_PARM(navi, mThrowHoldMaxTime),
		            C_NAVI_PARM(navi, mThrowFlightTime), AICONST.mGravity());
		if (hMax != hMin || C_NAVI_PARM(navi, mThrowMaxDistance) != C_NAVI_PARM(navi, mThrowMinDistance)) {
			fail(0, "the disc's throw parms: min and max height equal, min and max distance equal", "height %.1f, %.1f", hMin, hMax);
			return;
		}
		mMaxHeight = hMax;
		enter(Away, 0);
	}

	void providePads(unsigned t, PcInputPad& pad) override
	{
		if (mPhase == Away) pad.stickX = 100; // sideways: the sandbox gate stands ahead
		if (mPhase == Empty && t == mPress) pad.button = PAD_BUTTON_A;
		if (mPhase == Held) pad.button = PAD_BUTTON_A;
		// Here, before the update the throw keyframe (KEY_Action0, a few ticks into
		// Throw) fires in.
		if (mPhase == Held || mPhase == Release || mPhase == Flight) {
			if (Navi* navi = naviMgr->getNavi()) {
				// The offset too (and its target, which the cursor eases toward), so the
				// captain's own update and Navi::refresh keep the aim.
				navi->mCursorPosition.set(mAim.x - navi->mSRT.t.x, 0.0f, mAim.z - navi->mSRT.t.z);
				navi->mCursorTargetPosition = navi->mCursorPosition;
				navi->mCursorWorldPos       = mAim;
				// Navi::throwPiki adds the captain's whole velocity to the throw, and a
				// captain standing in ThrowWait sometimes carries a falling speed (seen
				// down to -24.4 at y 0) that lowers the arc by about 11. Zero it so the
				// peak shows the charge alone.
				navi->mVelocity.y = 0.0f;
			}
		}
	}

	void onTick(unsigned t) override
	{
		if (mPhase == Done) return;
		mRate          = unsigned(pc_scenario_ticks_per_second());
		Navi* navi     = naviMgr->getNavi();
		const int cap  = navi->getCurrState()->getID();
		const float s  = (t - mPhaseTick) / float(mRate);

		switch (mPhase) {
		case Away:
			if (s >= 4) {
				enter(Empty, t);
				mPress = t + 1;
			}
			return;
		case Empty:
			if (t < mPress) return;
			if (cap != mLastState) {
				std::printf("[scenario] throw_levels: EC-31: tick %u (+%u): captain state %d\n", t, t - mPress, cap);
				mLastState = cap;
			}
			if (cap == NAVISTATE_ThrowWait && static_cast<NaviThrowWaitState*>(navi->getCurrState())->mHeldThrowPiki) {
				fail(t, "EC-31: A with no Pikmin holds nothing", "a Pikmin in the hand");
				return;
			}
			if (cap == NAVISTATE_Attack) mSawAttack = true;
			if (cap == NAVISTATE_Throw || pikiMgr->getSize() != 0) {
				fail(t, "EC-31: A with no Pikmin never throws and makes no Pikmin", "captain state %d, Pikmin %d", cap, pikiMgr->getSize());
				return;
			}
			if (t > mPress + 1 && cap == NAVISTATE_Walk) {
				if (!mSawAttack) {
					fail(t, "EC-31: A with no Pikmin goes ThrowWait -> Attack -> Walk", "no Attack");
					return;
				}
				std::printf("[scenario] throw_levels: EC-31: back in Walk %.2f s after the press\n", (t - mPress) / float(mRate));
				spawn(navi);
				enter(Settle, t);
			} else if (s > 3) {
				fail(t, "EC-31: back in Walk within 3 s of an A with no Pikmin", "captain state %d", cap);
			}
			return;
		case Settle:
			if (cap == NAVISTATE_Walk && plateNormal(navi) > 0 && speed(navi) < 1.0f) {
				mSawHanged = mSawFlying = false;
				mThrown    = nullptr;
				mLevel     = kLevel[mThrow];
				C_NAVI_PARM(navi, mThrowMaxHeight) = mMaxHeight + (kRaised[mThrow] ? kRaise : 0.0f);
				// The idle cursor sits at the captain's feet, which throws straight up; aim
				// kAim away from him instead, as the stick would.
				// Each throw a different way (60 degrees apart), clear of the Pikmin
				// that landed before.
				const float yaw = navi->mFaceDirection + 1.0471976f * mThrow;
				mAim.set(navi->mSRT.t.x + kAim * sinf(yaw), 0.0f, navi->mSRT.t.z + kAim * cosf(yaw));
				mAim.y = mapMgr->getMinY(mAim.x, mAim.z, true) + 1.0f;
				enter(Held, t);
			} else if (s > 10) {
				fail(t, "captain still in Walk with a Normal Pikmin within 80 in the party", "state %d, %d near, speed %.1f", cap, plateNormal(navi), speed(navi));
			}
			return;
		case Held: {
			if (cap != NAVISTATE_ThrowWait) {
				if (s > 0.5f) fail(t, "ThrowWait while A is held", "captain state %d", cap);
				return;
			}
			NaviThrowWaitState* wait = static_cast<NaviThrowWaitState*>(navi->getCurrState());
			if (wait->mIsHoldingThrowPiki && wait->mHeldThrowPiki && wait->mHeldThrowPiki->getState() == PIKISTATE_Hanged) mSawHanged = true;
			if (wait->mThrowChargeLevel > mLevel) {
				fail(t, "the release tick at the wanted charge level", "level %d, want %d", wait->mThrowChargeLevel, mLevel);
				return;
			}
			if (wait->mIsHoldingThrowPiki && wait->mThrowChargeLevel < mLevel) {
				// The stand-in ThrowWait motion has no KEY_LoopEnd, so the level never
				// rises by itself: set it as KEY_LoopEnd would. exec reads it next tick.
				wait->mThrowChargeLevel = mLevel;
				return;
			}
			if (wait->mIsHoldingThrowPiki && wait->mThrowChargeLevel == mLevel) {
				const float want = C_NAVI_PARM(navi, mThrowMinDistance)
				                 + (mLevel / 3.0f) * (C_NAVI_PARM(navi, mThrowMaxDistance) - C_NAVI_PARM(navi, mThrowMinDistance));
				if (std::fabs(navi->mThrowDistance - want) > 0.01f) {
					fail(t, "mThrowDistance follows the charge level", "level %d: %.2f, want %.2f", mLevel, navi->mThrowDistance, want);
					return;
				}
				mDist[mThrow] = navi->mThrowDistance;
				mHeldSecs     = s;
				enter(Release, t);
			} else if (s > 3.0f + C_NAVI_PARM(navi, mThrowHoldMaxTime)) {
				fail(t, "the charge level reached while A is held", "level %d of %d after %.1f s, holding %d", wait->mThrowChargeLevel, mLevel, s,
				     (int)wait->mIsHoldingThrowPiki);
			}
			return;
		}
		case Release:
			if (cap == NAVISTATE_Throw) {
				mThrown = static_cast<NaviThrowState*>(navi->getCurrState())->mTargetPiki;
				if (!mThrown || !mSawHanged || mThrown->getState() == PIKISTATE_Flying) {
					fail(t, "EC-28: the Throw state names a Pikmin, Hanged before Throw, not yet Flying", "Pikmin %d, Hanged %d", (int)(mThrown != nullptr),
					     (int)mSawHanged);
					return;
				}
				mCursor  = mAim;
				mFrom    = navi->mSRT.t;
				mLaunchY = navi->mSRT.t.y + 10.0f; // Navi::throwPiki starts the flight 10 above the captain
				mPeak    = -1e9f;
				enter(Flight, t);
			} else if (s > 2) {
				fail(t, "Throw within 2 s of the release", "captain state %d", cap);
			}
			return;
		case Flight: {
			const int ps = mThrown->getState();
			if (ps == PIKISTATE_Flying) {
				mSawFlying = true;
				mPeak      = std::fmax(mPeak, mThrown->mSRT.t.y);
			} else if (mSawFlying && ps == PIKISTATE_Normal) {
				const Vector3f at = mThrown->mSRT.t;
				const float ground = mapMgr->getMinY(at.x, at.z, true);
				const float rise   = mPeak - mLaunchY;
				std::printf("[scenario] throw_levels: throw %d, level %d after %.2f s held, max height %.0f: mThrowDistance %.0f, peak +%.1f, landed %.1f "
				            "from the captain, %.1f from the cursor (%.1f from him), y %.2f over ground %.2f\n",
				            mThrow, mLevel, mHeldSecs, C_NAVI_PARM(navi, mThrowMaxHeight), mDist[mThrow], rise, flat(at, mFrom), flat(at, mCursor), flat(mCursor, mFrom), at.y, ground);
				// Only with the disc's parms: the raised arc flies past the cursor (23
				// beyond at 115 high), as Navi::throwPiki aims for mThrowFlightTime and the
				// Flying state's lighter gravity stretches a higher arc.
				if ((!kRaised[mThrow] && flat(at, mCursor) > kCursorSlack) || std::fabs(at.y - ground) > kGroundSlack) {
					fail(t, "EC-40: the Pikmin lands on the ground at the cursor", "%.1f from the cursor, y %.2f, ground %.2f", flat(at, mCursor), at.y, ground);
					return;
				}
				if (!kRaised[mThrow] && mThrow > 0 && std::fabs(rise - mRise[0]) > kSamePeak) {
					fail(t, "EC-42: with the disc's parms the peak is the same at every charge level", "level %d +%.1f, level 0 +%.1f", mLevel, rise, mRise[0]);
					return;
				}
				if (kRaised[mThrow] && mLevel > 0 && !(rise >= mRise[mThrow - 1] + kMinHeightStep)) {
					fail(t, "EC-42 control: with mThrowMaxHeight raised the peak rises with the charge level", "level %d +%.1f, level 0 +%.1f", mLevel, rise,
					     mRise[mThrow - 1]);
					return;
				}
				if (kRaised[mThrow] && mLevel == 0 && std::fabs(rise - mRise[0]) > kSamePeak) {
					fail(t, "EC-42 control: level 0 throws at mThrowMinHeight whatever the max", "+%.1f, before +%.1f", rise, mRise[0]);
					return;
				}
				mRise[mThrow] = rise;
				mLand[mThrow] = flat(at, mFrom);
				enter(Back, t);
			} else if (s > 4) {
				fail(t, "EC-28: Flying, then Normal on landing, within 4 s", "Pikmin state %d, seen Flying %d", ps, (int)mSawFlying);
			}
			return;
		}
		case Back:
			if (cap == NAVISTATE_Walk) {
				if (++mThrow == kThrows) {
					std::printf("[scenario] throw_levels: EC-42: peaks +%.1f +%.1f +%.1f +%.1f (raised: +%.1f +%.1f), landing distance %.1f %.1f %.1f %.1f %.1f %.1f\n",
					            mRise[0], mRise[1], mRise[2], mRise[3], mRise[4], mRise[5], mLand[0], mLand[1], mLand[2], mLand[3], mLand[4], mLand[5]);
					C_NAVI_PARM(navi, mThrowMaxHeight) = mMaxHeight;
					mPhase = Done;
					scenarioPass();
					return;
				}
				enter(Settle, t);
			} else if (s > 3) {
				fail(t, "the captain back in Walk within 3 s of the landing", "state %d", cap);
			}
			return;
		default: return;
		}
	}

private:
	enum Phase { Away, Empty, Settle, Held, Release, Flight, Back, Done };

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
		if (Navi* navi = naviMgr->getNavi()) C_NAVI_PARM(navi, mThrowMaxHeight) = mMaxHeight;
		scenarioFailf(t, expected, "%s", buf);
	}

	static float speed(Navi* navi) { return std::sqrt(navi->mVelocity.x * navi->mVelocity.x + navi->mVelocity.z * navi->mVelocity.z); }

	void spawn(Navi* navi)
	{
		for (int i = 0; i < kThrows; ++i) {
			Piki* piki = static_cast<Piki*>(pikiMgr->birth());
			if (!piki) {
				fail(0, "Pikmin born", "birth returned null at %d", i);
				return;
			}
			GameStat::workPikis.inc(Blue);
			piki->init(navi);
			const float a = 6.2831853f * i / kThrows;
			Vector3f at   = navi->mSRT.t;
			at.x += 40.0f * std::cos(a);
			at.z += 40.0f * std::sin(a);
			at.y = mapMgr->getMinY(at.x, at.z, true);
			piki->Creature::init(at);
			piki->initColor(Blue);
			piki->changeMode(PikiMode::FormationMode, navi);
		}
		GameStat::update();
	}

	int plateNormal(Navi* navi)
	{
		int n = 0;
		CPlate* plate = navi->mPlateMgr;
		for (int i = plate->getFirst(); !plate->isDone(i); i = plate->getNext(i)) {
			Creature* c = plate->getCreature(i);
			if (c && c->mObjType == OBJTYPE_Piki && static_cast<Piki*>(c)->getState() == PIKISTATE_Normal && flat(c->mSRT.t, navi->mSRT.t) < kSelect) ++n;
		}
		return n;
	}

	Phase mPhase        = Away;
	unsigned mPhaseTick = 0, mRate = 30, mPress = 0;
	int mLastState = -1, mLevel = 0, mThrow = 0;
	bool mSawAttack = false, mSawHanged = false, mSawFlying = false;
	Piki* mThrown = nullptr;
	Vector3f mAim, mCursor, mFrom;
	float mLaunchY = 0, mPeak = 0, mHeldSecs = 0, mMaxHeight = 0;
	float mDist[kThrows] = {}, mRise[kThrows] = {}, mLand[kThrows] = {};
};

} // namespace

PcScenario* pc_scenario_make_throw_levels(void)
{
	static ThrowLevels s;
	return &s;
}

#endif
