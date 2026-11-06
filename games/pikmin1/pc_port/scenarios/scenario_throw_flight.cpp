// throw_flight: the throw itself, beyond its timing (throw_cadence), measured the same way on the
// stand-in tree and on a disc install (PIKMIN_SCENARIO_DATA_DIR) so the two logs can be diffed
// float for float. Run it on stages/stage1.ini on both trees (PIKMIN_SCENARIO_STAGE on the
// stand-in). PIKMIN_TF_SPOT="x,z" picks a flat, wall-free spot for the captain (per tree; with
// none he stays where the intro left him). PIKMIN_TF_PROBE=1 instead prints the ground range
// within 170 of each point of a grid round the start and passes.
//
// The spot must also be clear of objects for every throw, not only flat: a yellow Pikmin flies
// 104 over the release height (a blue one 61), so it can land on an Onion that the others pass
// under, and that landing ends its flight ticks early (yellow on the stand-in once gave 22 and
// 27 ticks for 37 from (-228,-276), 30 units from the red Onion at (-120,-250)). A throw whose
// Pikmin lands more than kOnGround over the map therefore fails the run (OFF GROUND). Spots
// checked for every case at 30 and 60 Hz (2026-11-03):
//   disc stage1 (GPIE01 rev 1)  PIKMIN_TF_SPOT=-228,1976
//   stand-in stages/stage1.ini  PIKMIN_TF_SPOT=-350,350   (every landing on the map at
//                               y-ground 0.00, at least 200 from the Onions and the cliff)
//
// Every throw starts the same way: the captain is put back on the spot facing +z with no speed,
// one Pikmin is born 8 behind him (inside the 15-unit hand-over range, so nobody walks in) and
// both stand for 1 s. A goes down (tick tA) and stays down until the grab (ThrowWait
// mIsHoldingThrowPiki), then up on the next tick (tap) or 2.6 s later (hold: past
// mThrowHoldMaxTime). The cursor is set every tick, before the sim, to the target: kDist from the
// spot along kYaw (0 is the captain's feet). Measured per throw, in ticks:
//   grab     tA -> the grab keyframe (KEY_Action0 in ThrowWait)
//   release  grab -> the Pikmin is Flying (KEY_Action0 in Throw, Navi::throwPiki)
//   flight   release -> landing. Landing is the first tick the Pikmin is no longer Flying:
//            PikiFlyingState::procBounceMsg (the ground-contact bounce) moves it to Normal. A
//            Pikmin that touches the ground for 10 s of ticks without a bounce is forced out
//            by the same call (FLYING MIGHT LAST FOREVER), which never happened here.
// and, in world units: the release height (captain y + 10, where throwPiki starts the flight),
// the peak y while Flying, peak above the release and above the ground under the peak, the
// launch velocity throwPiki computes (from the parms, gravity and mThrowHoldTime at release)
// and the Pikmin's velocity at the end of the release tick, the landing point and its offset
// from the target. HEX lines repeat the raw floats with %a for a bit-for-bit diff.
//
// Cases: leaf (blue leaf, tap, 6 distances x 3 directions x 2 repeats), bud, flower, yellow (leaf,
// throws at mYellowThrowHeight) and hold (blue leaf, 2.6 s hold). Flower Pikmin glide down once
// past the peak (PikiFlyingState::exec) with gravity x pikiMgr p48 mFlowerGravityScale.
#include "scenarios/pc_scenario_list.h"

#if PIKI_SCENARIOS

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "AIConstant.h"
#include "Dolphin/pad.h"
#include "GameStat.h"
#include "MoviePlayer.h"
#include "gameflow.h"
#include "GlobalGameOptions.h"
#include "KeyConfig.h"
#include "MapMgr.h"
#include "Navi.h"
#include "NaviMgr.h"
#include "NaviState.h"
#include "Piki.h"
#include "PikiMgr.h"
#include "PikiState.h"
#include "settings/pc_settings.h"

namespace {

constexpr unsigned kWalkStartTicks = 45;
constexpr int kDists               = 6;
constexpr float kDist[kDists]      = { 0.0f, 25.0f, 50.0f, 75.0f, 100.0f, 140.0f }; // 100 = p46 cursor max, 140 = p10
constexpr int kYaws                = 3;
constexpr float kYawDeg[kYaws]     = { 0.0f, 90.0f, 225.0f };
constexpr float kHoldSecs          = 2.6f;
constexpr float kOnGround          = 1.0f; // a landing higher than this over the map is on an object

struct Stat {
	double n = 0, s = 0, ss = 0, lo = 1e30, hi = -1e30;
	void add(double v)
	{
		n += 1, s += v, ss += v * v;
		lo = v < lo ? v : lo;
		hi = v > hi ? v : hi;
	}
	double mean() const { return n ? s / n : 0; }
	double sd() const { return n > 1 ? std::sqrt(std::fmax(0.0, (ss - s * s / n) / (n - 1))) : 0; }
};

class ThrowFlight : public PcScenario {
public:
	ThrowFlight(const char* name, int colour, int happa, bool hold, int reps)
	    : mName(name)
	    , mColour(colour)
	    , mHappa(happa)
	    , mHoldA(hold)
	    , mReps(reps)
	{
	}
	const char* name() const override { return mName; }

	void onStageReady() override
	{
		KeyConfig* k = KeyConfig::_instance;
		Navi* navi   = naviMgr->getNavi();
		if (!k || !navi) {
			scenarioFailf(0, "KeyConfig and a captain", "none");
			return;
		}
		k->mThrowKey.mBind     = KBBTN_A;
		k->mSetCursorKey.mBind = KBBTN_B;
		pc_settings_scenario_set_quick_grab(0);
		const unsigned intro = pc_scenario_intro_ticks();
		mHold                = intro < kWalkStartTicks ? kWalkStartTicks - intro : 0;
		mTotal               = kDists * kYaws * mReps;
		// PIKMIN_TF_FLOWER_GRAV=f (diagnostic only): set pikiMgr p48 mFlowerGravityScale for
		// this run, to show what the stand-in's flower glide does with the disc's 0.5.
		if (const char* fg = std::getenv("PIKMIN_TF_FLOWER_GRAV")) {
			pikiMgr->mPikiParms->mPikiParms.mFlowerGravityScale() = float(std::atof(fg));
			std::printf("[scenario] %s: FLOWER_GRAV override %s\n", mName, fg);
		}
		std::printf("[scenario] %s: PARMS gravity %a (%.3f) flight %a hold max %a min h %a max h %a yellow h %a dist %a..%a cursor max %a "
		            "flower grav %a\n",
		            mName, AICONST.mGravity(), AICONST.mGravity(), C_NAVI_PARM(navi, mThrowFlightTime), C_NAVI_PARM(navi, mThrowHoldMaxTime),
		            C_NAVI_PARM(navi, mThrowMinHeight), C_NAVI_PARM(navi, mThrowMaxHeight), C_NAVI_PARM(navi, mYellowThrowHeight),
		            C_NAVI_PARM(navi, mThrowMinDistance), C_NAVI_PARM(navi, mThrowMaxDistance), C_NAVI_PARM(navi, mCursorMaxRadius),
		            pikiMgr->mPikiParms->mPikiParms.mFlowerGravityScale());
		std::printf("[scenario] %s: PARMSDEC gravity %.3f flight %.3f hold max %.3f height %.1f..%.1f yellow %.1f dist %.1f..%.1f cursor max %.1f "
		            "flower grav %.4f\n",
		            mName, AICONST.mGravity(), C_NAVI_PARM(navi, mThrowFlightTime), C_NAVI_PARM(navi, mThrowHoldMaxTime), C_NAVI_PARM(navi, mThrowMinHeight),
		            C_NAVI_PARM(navi, mThrowMaxHeight), C_NAVI_PARM(navi, mYellowThrowHeight), C_NAVI_PARM(navi, mThrowMinDistance),
		            C_NAVI_PARM(navi, mThrowMaxDistance), C_NAVI_PARM(navi, mCursorMaxRadius), pikiMgr->mPikiParms->mPikiParms.mFlowerGravityScale());
		mPhase     = mHold == 0 ? Still : Hold;
		mPhaseTick = 0;
	}

	void providePads(unsigned t, PcInputPad& pad) override
	{
		pad.button = 0;
		Navi* navi = naviMgr->getNavi();
		if (mPhase == Prep || mPhase == Press || mPhase == Flight || mPhase == Back) {
			// Aim as the stick would: the offset, its target and the world point.
			navi->mCursorPosition.set(mAim.x - navi->mSRT.t.x, 0.0f, mAim.z - navi->mSRT.t.z);
			navi->mCursorTargetPosition = navi->mCursorPosition;
			navi->mCursorWorldPos       = mAim;
		}
		if (mPhase == Press && t >= mTA && (mUpAt == ~0u || t < mUpAt)) pad.button = PAD_BUTTON_A;
		if (mPhase == Press) {
			// Navi::throwPiki adds the captain's whole velocity to the launch, and a captain
			// standing still on flat ground carries, on some ticks only, a falling speed of
			// about -22 that the ground snap has not cleared yet: it takes 22 off the launch
			// vy (271 -> 249 on the release tick), 10 off the peak and 3 ticks off the
			// flight. Which ticks carry it is not the same on both trees, so by default it is
			// zeroed here, before the update the throw keyframe fires in (as throw_levels
			// does); PIKMIN_TF_NAVI_VY=keep leaves it, for the free-play picture. The value
			// found is logged per throw (NAVIVY).
			mNaviVyIn = navi->mVelocity.y;
			if (!mKeepNaviVy) navi->mVelocity.y = 0.0f;
		}
	}

	void onTick(unsigned t) override
	{
		Navi* navi     = naviMgr->getNavi();
		const int rate = pc_scenario_ticks_per_second();
		const int cap  = navi->getCurrState()->getID();
		switch (mPhase) {
		case Hold:
			if (t + 1 >= mHold) enter(Still, t + 1);
			return;
		case Still:
			// A cutscene still running after the intro (the stand-in plays demo 45 after
			// its intro is reported done; the disc before) restarts the still second, so
			// the Pikmin are never born under a movie.
			if (gameflow.mMoviePlayer && gameflow.mMoviePlayer->mIsActive) {
				mPhaseTick = t + 1;
				return;
			}
			if (t + 1 >= mPhaseTick + unsigned(rate)) {
				if (std::getenv("PIKMIN_TF_PROBE")) {
					probe(navi);
					return;
				}
				if (const char* s = std::getenv("PIKMIN_TF_SPOT")) {
					std::sscanf(s, "%f,%f", &mSpot.x, &mSpot.z);
				} else {
					mSpot = navi->mSRT.t;
				}
				mSpot.y = mapMgr->getMinY(mSpot.x, mSpot.z, true);
				std::printf("[scenario] %s: spot (%.2f, %.2f, %.2f), ground within 170: %.3f\n", mName, mSpot.x, mSpot.y, mSpot.z, groundRange(mSpot, 170.0f));
				startThrow(navi, t + 1);
			}
			return;
		case Prep:
			// Fixed offsets: the whole Prep second pins the captain on the spot (facing +z,
			// no speed) and the Pikmin 8 units behind him (facing him, no speed), so the A
			// press always finds the same picture, whatever the AI wanted to do meanwhile.
			pin(navi);
			if (t + 1 >= mPhaseTick + unsigned(rate)) {
				const Vector3f d = mPiki->mSRT.t - navi->mSRT.t;
				mPikiDist        = std::sqrt(d.x * d.x + d.z * d.z);
				mTA              = t + 1;
				mUpAt            = ~0u;
				mGrab = mRel = mLand = ~0u;
				enter(Press, t + 1);
			}
			return;
		case Press: {
			if (mGrab == ~0u && cap == NAVISTATE_ThrowWait && static_cast<NaviThrowWaitState*>(navi->getCurrState())->mIsHoldingThrowPiki) {
				mGrab = t;
				mUpAt = t + 1 + (mHoldA ? unsigned(kHoldSecs * rate + 0.5f) : 0u);
			}
			if (cap == NAVISTATE_ThrowWait) mCharge = static_cast<NaviThrowWaitState*>(navi->getCurrState())->mThrowChargeLevel;
			if (mPiki->getState() == PIKISTATE_Flying) {
				if (mGrab == ~0u) mGrab = t;
				mRel = t;
				onRelease(navi);
				enter(Flight, t);
			} else if (t > mTA + 10u * unsigned(rate)) {
				fail(t, "a release within 10 s of A", "captain %d, Pikmin state %d", cap, mPiki->getState());
			}
			return;
		}
		case Flight:
			if (mPiki->getState() == PIKISTATE_Flying) {
				track();
				if (t > mRel + 10u * unsigned(rate)) fail(t, "landing within 10 s", "still Flying");
				return;
			}
			track(); // the landing tick's position counts for the peak too
			mLand = t;
			onLand(navi, rate);
			enter(Back, t);
			return;
		case Back:
			if (t >= mPhaseTick + unsigned(rate) / 2 && (cap == NAVISTATE_Walk)) {
				if (++mIdx >= mTotal) finish(t, rate);
				else startThrow(navi, t + 1);
			} else if (t > mPhaseTick + 5u * unsigned(rate)) {
				fail(t, "captain back in Walk", "state %d", cap);
			}
			return;
		default: return;
		}
	}

private:
	enum Phase { Hold, Still, Prep, Press, Flight, Back, Done };

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
		std::printf("[scenario] %s: FAIL at throw %d\n", mName, mIdx);
		scenarioFailf(t, expected, "%s", buf);
	}

	float groundRange(const Vector3f& c, float r)
	{
		float lo = 1e9f, hi = -1e9f;
		for (float x = -r; x <= r; x += 10.0f)
			for (float z = -r; z <= r; z += 10.0f) {
				if (x * x + z * z > r * r) continue;
				const float y = mapMgr->getMinY(c.x + x, c.z + z, true);
				lo            = std::fmin(lo, y);
				hi            = std::fmax(hi, y);
			}
		return hi - lo;
	}

	void probe(Navi* navi)
	{
		const Vector3f s = navi->mSRT.t;
		std::printf("[scenario] %s: PROBE start (%.1f, %.1f, %.1f)\n", mName, s.x, s.y, s.z);
		for (int ix = -15; ix <= 15; ++ix)
			for (int iz = -15; iz <= 15; ++iz) {
				Vector3f c(s.x + 100.0f * ix, 0.0f, s.z + 100.0f * iz);
				c.y           = mapMgr->getMinY(c.x, c.z, true);
				const float g = groundRange(c, 170.0f);
				if (g < 0.5f) std::printf("[scenario] %s: PROBE (%.0f, %.0f) y %.2f range %.3f\n", mName, c.x, c.z, c.y, g);
			}
		mPhase = Done;
		scenarioPass();
	}

	void startThrow(Navi* navi, unsigned t)
	{
		const int d   = mIdx % kDists;
		const int y   = (mIdx / kDists) % kYaws;
		const float a = kYawDeg[y] * 3.14159265f / 180.0f;
		mAim.set(mSpot.x + kDist[d] * sinf(a), 0.0f, mSpot.z + kDist[d] * cosf(a));
		mAim.y = mapMgr->getMinY(mAim.x, mAim.z, true) + 1.0f;
		navi->mSRT.t = mSpot;
		navi->mVelocity.set(0.0f, 0.0f, 0.0f);
		navi->mTargetVelocity.set(0.0f, 0.0f, 0.0f);
		navi->mFaceDirection = 0.0f;
		Piki* piki = static_cast<Piki*>(pikiMgr->birth());
		if (!piki) {
			fail(t, "a Pikmin born", "birth returned null");
			return;
		}
		GameStat::workPikis.inc(mColour);
		piki->init(navi);
		piki->Creature::init(pikiAt());
		piki->initColor(mColour);
		piki->setFlower(mHappa);
		piki->changeMode(PikiMode::FormationMode, navi);
		GameStat::update();
		mPiki   = piki;
		mCharge = -1;
		enter(Prep, t);
	}

	Vector3f pikiAt() const
	{
		Vector3f at(mSpot.x, 0.0f, mSpot.z - 8.0f);
		at.y = mapMgr->getMinY(at.x, at.z, true);
		return at;
	}

	void pin(Navi* navi)
	{
		navi->mSRT.t = mSpot;
		navi->mVelocity.set(0.0f, 0.0f, 0.0f);
		navi->mTargetVelocity.set(0.0f, 0.0f, 0.0f);
		navi->mFaceDirection = 0.0f;
		mPiki->mSRT.t = pikiAt();
		mPiki->mVelocity.set(0.0f, 0.0f, 0.0f);
		mPiki->mTargetVelocity.set(0.0f, 0.0f, 0.0f);
		mPiki->mFaceDirection = 0.0f;
	}

	void onRelease(Navi* navi)
	{
		mRelY = navi->mSRT.t.y + 10.0f;
		mFrom = navi->mSRT.t;
		mPeak = -1e9f;
		mPeakAt = mPiki->mSRT.t;
		// The launch Navi::throwPiki computed (before the captain's velocity is added).
		const float halfTime = 0.5f * C_NAVI_PARM(navi, mThrowFlightTime);
		float h;
		if (mPiki->mColor == Yellow) h = C_NAVI_PARM(navi, mYellowThrowHeight);
		else
			h = C_NAVI_PARM(navi, mThrowMinHeight)
			  + (navi->mThrowHoldTime / C_NAVI_PARM(navi, mThrowHoldMaxTime)) * (C_NAVI_PARM(navi, mThrowMaxHeight) - C_NAVI_PARM(navi, mThrowMinHeight));
		mThrowH  = h;
		mVSpeed  = AICONST.mGravity() * 0.5f * halfTime + (h / halfTime);
		mRelPos  = mPiki->mSRT.t;
		mRelVel  = mPiki->mVelocity;
		mNaviVel = navi->mVelocity;
		mCursor  = navi->mCursorWorldPos;
		mHoldT   = navi->mThrowHoldTime;
		mTrTick  = 0;
		std::printf("[scenario] %s: NAVIVY %d %a (%.4f)%s\n", mName, mIdx, mNaviVyIn, mNaviVyIn, mKeepNaviVy ? " kept" : " zeroed");
		mTrace   = std::getenv("PIKMIN_TF_TRACE") != nullptr;
		track();
	}

	void track()
	{
		// PIKMIN_TF_TRACE=1: one line per Flight tick (0 = the release tick) with the
		// Pikmin's height over the release height and over the ground under it, and its
		// horizontal distance from the captain at the release, for height-vs-tick charts.
		if (mTrace) {
			const Vector3f& p = mPiki->mSRT.t;
			const float dx = p.x - mFrom.x, dz = p.z - mFrom.z;
			std::printf("[scenario] %s: TRACE %d %u %.4f %.4f %.4f %.4f\n", mName, mIdx, mTrTick++, p.y - mRelY, p.y - mapMgr->getMinY(p.x, p.z, true),
			            std::sqrt(dx * dx + dz * dz), mPiki->mVelocity.y);
		}
		if (mPiki->mSRT.t.y > mPeak) {
			mPeak   = mPiki->mSRT.t.y;
			mPeakAt = mPiki->mSRT.t;
		}
	}

	void onLand(Navi* navi, int rate)
	{
		const Vector3f at  = mPiki->mSRT.t;
		const float ground = mapMgr->getMinY(at.x, at.z, true);
		const float pkGnd  = mapMgr->getMinY(mPeakAt.x, mPeakAt.z, true);
		const float dx = at.x - mAim.x, dz = at.z - mAim.z;
		const float err = std::sqrt(dx * dx + dz * dz);
		const int d = mIdx % kDists, y = (mIdx / kDists) % kYaws;
		const unsigned g = mGrab - mTA, r = mRel - mGrab, f = mLand - mRel;
		std::printf("[scenario] %s: THROW %d dist %.0f yaw %.0f pikidist %.2f grab %u release %u flight %u (%.3f s) relY %.2f peak +%.2f "
		            "(over ground %.2f) h %.1f vS %.2f vel (%.2f %.2f %.2f) naviVel (%.2f %.2f %.2f) hold %.3f charge %d land (%.2f %.2f) "
		            "off (%.2f %.2f) err %.2f y-ground %.2f state %d mode %d\n",
		            mName, mIdx, kDist[d], kYawDeg[y], mPikiDist, g, r, f, f / float(rate), mRelY, mPeak - mRelY, mPeak - pkGnd, mThrowH, mVSpeed,
		            mRelVel.x, mRelVel.y, mRelVel.z, mNaviVel.x, mNaviVel.y, mNaviVel.z, mHoldT, mCharge, at.x - mFrom.x, at.z - mFrom.z, dx, dz, err,
		            at.y - ground, mPiki->getState(), int(mPiki->mMode));
		std::printf("[scenario] %s: HEX %d %u %u %u relpos %a %a %a relvel %a %a %a navivel %a %a %a peak %a %a land %a %a %a cursor %a %a %a hold %a\n",
		            mName, mIdx, g, r, f, mRelPos.x, mRelPos.y, mRelPos.z, mRelVel.x, mRelVel.y, mRelVel.z, mNaviVel.x, mNaviVel.y, mNaviVel.z,
		            mPeak - mRelY, mPeak - pkGnd, at.x - mFrom.x, at.y - mFrom.y, at.z - mFrom.z, mCursor.x - mFrom.x, mCursor.y - mFrom.y,
		            mCursor.z - mFrom.z, mHoldT);
		mS[d][0].add(g), mS[d][1].add(r), mS[d][2].add(f), mS[d][3].add(mPeak - mRelY), mS[d][4].add(mPeak - pkGnd), mS[d][5].add(err);
		if (mPiki->getState() != PIKISTATE_Normal) mBad++;
		if (std::fabs(at.y - ground) > kOnGround) {
			// Landed on an object (an Onion, say), not on the map: its flight says nothing about the throw.
			mOffGround++;
			std::printf("[scenario] %s: OFF GROUND throw %d landed %.2f over the map at (%.2f, %.2f); pick a clearer PIKMIN_TF_SPOT\n", mName, mIdx,
			            at.y - ground, at.x, at.z);
		}
		// Erase, not die: Piki::doKill would otherwise let a flower Pikmin leave a seed
		// (a getRand draw, then a PikiHeadItem born and killed without startAI, which
		// takes GameStat::mePikis below 0).
		mPiki->mEraseOnKill = true;
		mPiki->kill(false);
		mPiki = nullptr;
		(void)navi;
	}

	void finish(unsigned t, int rate)
	{
		mPhase = Done;
		static const char* nm[6] = { "grab", "release", "flight", "peak_rel", "peak_gnd", "err" };
		for (int d = 0; d < kDists; ++d)
			for (int m = 0; m < 6; ++m)
				std::printf("[scenario] %s: SUMMARY rate %d dist %.0f %s n %.0f mean %.4f sd %.4f min %.4f max %.4f\n", mName, rate, kDist[d], nm[m], mS[d][m].n,
				            mS[d][m].mean(), mS[d][m].sd(), mS[d][m].lo, mS[d][m].hi);
		if (mBad) scenarioFailf(t, "every Pikmin Normal after landing", "%d not", mBad);
		else if (mOffGround) scenarioFailf(t, "every Pikmin landing on the map (a spot clear of objects)", "%d landed on something", mOffGround);
		else scenarioPass();
	}

	const char* mName;
	int mColour, mHappa;
	bool mHoldA;
	int mReps;
	Phase mPhase = Hold;
	unsigned mHold = 0, mPhaseTick = 0, mTA = 0, mUpAt = ~0u, mGrab = 0, mRel = 0, mLand = 0;
	int mIdx = 0, mTotal = 0, mBad = 0, mOffGround = 0, mCharge = -1;
	Piki* mPiki = nullptr;
	Vector3f mSpot, mAim, mFrom, mPeakAt, mRelPos, mRelVel, mNaviVel, mCursor;
	unsigned mTrTick = 0;
	float mNaviVyIn = 0.0f;
	bool mKeepNaviVy = std::getenv("PIKMIN_TF_NAVI_VY") != nullptr && std::strcmp(std::getenv("PIKMIN_TF_NAVI_VY"), "keep") == 0;
	bool mTrace = false;
	float mPikiDist = 0, mRelY = 0, mPeak = 0, mThrowH = 0, mVSpeed = 0, mHoldT = 0;
	Stat mS[kDists][6];
};

} // namespace

#define THROW_FLIGHT(fn, nm, colour, happa, hold, reps) \
	PcScenario* fn(void)                                \
	{                                                   \
		static ThrowFlight s(nm, colour, happa, hold, reps); \
		return &s;                                      \
	}
THROW_FLIGHT(pc_scenario_make_throw_flight_leaf, "throw_flight_leaf", Blue, Leaf, false, 2)
THROW_FLIGHT(pc_scenario_make_throw_flight_bud, "throw_flight_bud", Blue, Bud, false, 1)
THROW_FLIGHT(pc_scenario_make_throw_flight_flower, "throw_flight_flower", Blue, Flower, false, 1)
THROW_FLIGHT(pc_scenario_make_throw_flight_yellow, "throw_flight_yellow", Yellow, Leaf, false, 1)
THROW_FLIGHT(pc_scenario_make_throw_flight_hold, "throw_flight_hold", Blue, Leaf, true, 1)

#endif
