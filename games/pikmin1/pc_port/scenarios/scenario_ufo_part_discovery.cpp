// ufo_part_discovery: the ship-part discovery text of all 30 parts (coverage/milestones.md
// DF32-61, SP14). Navi::demoCheck sets DEMOFLAG_UfoPartDiscoveryOffset + the part's index
// when the captain comes within demoParms mDemoTriggerRadius (40 here, the disc's) of a part
// lying on the ground, and sets gameflow.mShipTextType SHIPTEXT_PartDiscovery and
// mShipTextPartID to the index. The check is not stage-gated, so it runs on the sandbox.
//
// One part at a time, in index order: a ground pellet with the part's model id is born at a
// fixed spot, the captain is put 150 units south of it, and the scenario checks
//   - for the first part only, a captain held 30 units beyond the trigger radius (standing in
//     NaviWalkState, which is where demoCheck runs) does not set the flag in 2 s;
//   - the captain walking up sets exactly flag 32 + index (no other new flag), with the ship
//     text type and part index, and opening the text window of TUT_PartsGetOnlyOffset (32) +
//     the index (movie 12 is no cinema: MoviePlayer::startMovie asks for TUT_GetParts, which
//     with this ship text type shows the part's "get only" page); A closes it;
//   - the flag stays set and nothing else sets for another second with the captain in the
//     radius (it triggers once), and the text type and index are not rewritten.
// Index 4, the Main Engine, is the exception the game makes: DemoFlags marks its discovery
// seen from the start (gameDemo.cpp), so the captain beside it sets nothing; the scenario
// asserts that instead.
//
// Only uf01 and ust5 have a pelMgr.bin config on the stand-in tree. The others are made here:
// a config with the part's model id, type 3 and uf01's shape, put on a uf01-born pellet
// before it is initialised, so demoCheck sees the part's own id (PelletMgr::getUfoIndexFromID).
// Not covered: the wording and layout of the pages (stand-in pages hold one text box).
//
// Negative control (manual): move the first part's far check to 10 units inside the radius,
// or skip the trigger, and the run fails naming the flag.
#include "scenarios/pc_scenario_list.h"

#if PIKI_SCENARIOS

#include <cstdio>
#include <vector>

#include "Demo.h"
#include "NaviMgr.h"
#include "NaviState.h"
#include "Parameters.h"
#include "Pellet.h"
#include "gameflow.h"
#include "zen/ogTutorial.h"
#include "scenarios/scenario_carry_common.h"

namespace {

using namespace carry;

constexpr float kPartX = 0.0f, kPartZ = 450.0f; // south of the sandbox gate (x -31, z -207), open floor
constexpr float kStartGap = 150.0f;             // the captain starts this far short of the part
constexpr unsigned kEngineIndex = 4;

class UfoPartDiscovery : public PcScenario {
public:
	const char* name() const override { return "ufo_part_discovery"; }

	void onStageReady() override
	{
		mRadius = demoParms->mParms.mDemoTriggerRadius();
		mShape  = pelletMgr->getConfig(id("uf01"));
		std::printf("[scenario] ufo_part_discovery: trigger radius %.1f, %d parts, uf01 config %d\n", mRadius, MAX_UFO_PARTS, (int)(mShape != nullptr));
		if (!mShape || !pelletMgr->getConfig(id("ust5")) || mRadius != 40.0f) {
			fail(0, "the stand-in tree's uf01 and ust5 configs and the disc's 40-unit trigger radius", "uf01 %d ust5 %d radius %.1f", (int)(mShape != nullptr),
			     (int)(pelletMgr->getConfig(id("ust5")) != nullptr), mRadius);
			return;
		}
		for (int i = 0; i < MAX_UFO_PARTS; i++) {
			if (playerState->mDemoFlags.isFlag(DEMOFLAG_UfoPartDiscoveryOffset + i) != (i == int(kEngineIndex))) {
				fail(0, "a new game has only the Main Engine's discovery marked seen", "flag %d", DEMOFLAG_UfoPartDiscoveryOffset + i);
				return;
			}
		}
		mFlags = demoFlags();
	}

	void providePads(unsigned t, PcInputPad& pad) override
	{
		Navi* navi = naviMgr ? naviMgr->getNavi() : nullptr;
		if (!navi || mPhase == Done) return;
		if (gameflow.mIsUIOverlayActive) {
			if (t % 6 == 0) pad.button = PAD_BUTTON_A;
			return;
		}
		const int state = navi->getCurrState()->getID();
		if ((state != NAVISTATE_Walk && state != NAVISTATE_Idle) || currentMovie() != -1) return;
		if (mPhase == Approach) steer(navi, mPart->mSRT.t, pad, 0.0f);
		// A small back-and-forth keeps the captain in NaviWalkState without closing the gap.
		if (mPhase == Far) pad.stickX = (t / 15) % 2 ? 12 : -12;
	}

	void onTick(unsigned t) override
	{
		if (mPhase == Done) return;
		const unsigned rate = unsigned(pc_scenario_ticks_per_second());
		const unsigned span = t - mPhaseTick;
		Navi* navi          = naviMgr->getNavi();
		switch (mPhase) {
		case Place:
			if (mIndex >= MAX_UFO_PARTS) {
				std::printf("[scenario] ufo_part_discovery: all %d parts passed (flags %s)\n", MAX_UFO_PARTS, setBits(demoFlags()).c_str());
				mPhase = Done;
				scenarioPass();
				return;
			}
			if (!place(t, navi)) return;
			enter(mIndex == 0 ? Far : Approach, t);
			return;
		case Far:
			if (newFlags(mFlags, demoFlags()).size()) {
				fail(t, "a captain 30 units beyond the trigger radius sets no flag", "new flags %s", list(newFlags(mFlags, demoFlags())).c_str());
				return;
			}
			if (span >= 2 * rate) {
				const float gap = flatDistance(mPart->mSRT.t, navi->mSRT.t) - mPart->getBottomRadius();
				std::printf("[scenario] ufo_part_discovery: part 0 untouched for 2 s with the captain %.1f from its edge (radius %.0f)\n", gap, mRadius);
				if (gap <= mRadius) {
					fail(t, "the far check stays outside the radius", "%.1f from the edge", gap);
					return;
				}
				enter(Approach, t);
			}
			return;
		case Approach: {
			const std::vector<int> fresh = newFlags(mFlags, demoFlags());
			if (mIndex == int(kEngineIndex)) {
				if (!fresh.empty()) {
					fail(t, "the Main Engine's discovery is preset: no flag sets beside it", "new flags %s", list(fresh).c_str());
					return;
				}
				const float gap = flatDistance(mPart->mSRT.t, navi->mSRT.t) - mPart->getBottomRadius();
				if (gap <= mRadius - 5.0f && span > rate) {
					std::printf("[scenario] ufo_part_discovery: part 4 (the Main Engine) inside the radius (%.1f) for %u ticks, nothing set\n", gap, span);
					next(t);
				} else if (span > 20 * rate) {
					fail(t, "the captain reaches the Main Engine", "%.1f from its edge", gap);
				}
				return;
			}
			if (!fresh.empty()) {
				const float gap = flatDistance(mPart->mSRT.t, navi->mSRT.t) - mPart->getBottomRadius();
				std::printf("[scenario] ufo_part_discovery: part %d flag(s) %s at t=%u, %.1f from its edge, ship text type %d part %d, movie %d\n", mIndex,
				            list(fresh).c_str(), t, gap, gameflow.mShipTextType, gameflow.mShipTextPartID, currentMovie());
				if (fresh.size() != 1 || fresh[0] != DEMOFLAG_UfoPartDiscoveryOffset + mIndex) {
					fail(t, "flag 32 + the part's index, and no other", "part %d: new flags %s", mIndex, list(fresh).c_str());
					return;
				}
				if (gameflow.mShipTextType != SHIPTEXT_PartDiscovery || gameflow.mShipTextPartID != mIndex) {
					fail(t, "ship text type PartDiscovery and the part's index", "part %d: type %d index %d", mIndex, gameflow.mShipTextType, gameflow.mShipTextPartID);
					return;
				}
				if (gap > mRadius) {
					fail(t, "the flag sets inside the trigger radius", "part %d: %.1f from its edge", mIndex, gap);
					return;
				}
				mFlags    = demoFlags();
				mHoldFrom = t;
				mTextsBefore = pc_scenario_text_windows_opened(nullptr);
				enter(Hold, t);
			} else if (span > 20 * rate) {
				fail(t, "the captain reaches the part and discovers it within 20 s", "part %d: %.1f from its edge", mIndex,
				     flatDistance(mPart->mSRT.t, navi->mSRT.t) - mPart->getBottomRadius());
			}
			return;
		}
		case Hold: {
			// The window opens a tick or two after the flag (the message queue), then A closes it; then
			// the captain stays in the radius for a second.
			int textId = -1;
			const int opened = pc_scenario_text_windows_opened(&textId);
			if (opened == mTextsBefore || gameflow.mIsUIOverlayActive || currentMovie() != -1) {
				mHoldFrom = t + 1;
				if (span > 20 * rate) fail(t, "the discovery text opens and closes within 20 s", "part %d: windows opened %d (was %d), overlay %d", mIndex, opened, mTextsBefore,
				                           (int)gameflow.mIsUIOverlayActive);
				return;
			}
			if (opened != mTextsBefore + 1 || textId != zen::ogScrTutorialMgr::TUT_PartsGetOnlyOffset + mIndex) {
				fail(t, "one text window, the part's PartsGetOnly text (32 + index)", "part %d: %d windows, text id %d", mIndex, opened - mTextsBefore, textId);
				return;
			}
			const std::vector<int> fresh = newFlags(mFlags, demoFlags());
			if (!fresh.empty() || gameflow.mShipTextType != SHIPTEXT_PartDiscovery || gameflow.mShipTextPartID != mIndex) {
				fail(t, "the discovery sets once: no new flag and the text type and index stay", "part %d: new flags %s, type %d index %d", mIndex, list(fresh).c_str(),
				     gameflow.mShipTextType, gameflow.mShipTextPartID);
				return;
			}
			if (t >= mHoldFrom + rate) next(t);
			return;
		}
		case Done: return;
		}
	}

private:
	enum Phase { Place, Far, Approach, Hold, Done };

	void enter(Phase p, unsigned t)
	{
		mPhase     = p;
		mPhaseTick = t;
	}

	void next(unsigned t)
	{
		if (mPart) mPart->kill(false);
		mPart = nullptr;
		mIndex++;
		enter(Place, t);
	}

	template <typename... A>
	void fail(unsigned t, const char* expected, const char* fmt, A... args)
	{
		mPhase = Done;
		scenarioFailf(t, expected, fmt, args...);
	}

	/// The part of this index on the ground at the fixed spot, the captain short of it.
	bool place(unsigned t, Navi* navi)
	{
		const u32 model = PelletMgr::getUfoIDFromIndex(mIndex);
		Pellet* part    = pelletMgr->newPellet(model == UFOID_MainEngine ? model : id("uf01"), nullptr);
		if (!part) {
			fail(t, "a part pellet is born", "part %d: newPellet returned null", mIndex);
			return false;
		}
		if (model != UFOID_MainEngine) {
			PelletConfig* c = new PelletConfig();
			c->mModelId.setID(model);
			c->mPelletId.setID(mShape->mPelletId.mId);
			c->mUnusedId.setID('none');
			c->mPelletType(PELTYPE_UfoPart);
			c->mPelletColor(PELCOLOR_NULL);
			c->mCarryMinPikis(10);
			c->mCarryMaxPikis(20);
			c->mRepairAnimJointIndex = -1;
			part->mConfig            = c;
		}
		Vector3f at(kPartX, mapMgr->getMinY(kPartX, kPartZ, true), kPartZ);
		part->init(at);
		part->mFaceDirection = part->mSRT.r.y;
		part->startAI(0);
		const float scale = part->mConfig->mPelletScale();
		part->mSRT.s.set(scale, scale, scale);
		part->mStateMachine->transit(part, 0);
		mPart = part;
		if (PelletMgr::getUfoIndexFromID(part->mConfig->mModelId.mId) != mIndex) {
			fail(t, "the part's model id is index i in the ship-part table", "part %d: index %d", mIndex, PelletMgr::getUfoIndexFromID(part->mConfig->mModelId.mId));
			return false;
		}
		const float start = mIndex == 0 ? mRadius + part->getBottomRadius() + 30.0f : kStartGap;
		navi->resetPosition(Vector3f(kPartX, mapMgr->getMinY(kPartX, kPartZ - start, true), kPartZ - start));
		navi->mVelocity.set(0.0f, 0.0f, 0.0f);
		return true;
	}

	Phase mPhase        = Place;
	unsigned mPhaseTick = 0, mHoldFrom = 0;
	int mIndex          = 0;
	int mTextsBefore    = 0;
	float mRadius       = 0.0f;
	Pellet* mPart       = nullptr;
	PelletConfig* mShape = nullptr;
	std::vector<int> mFlags;
};

} // namespace

PcScenario* pc_scenario_make_ufo_part_discovery(void)
{
	static UfoPartDiscovery s;
	return &s;
}

#endif
