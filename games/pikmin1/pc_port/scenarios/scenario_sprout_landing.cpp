// sprout_landing: seeds shot at a steep cliff land and plant (upstream #208).
// Runs on the opt-in object stage, stages/sandbox_objects.ini (written by
// nectar_standin_generator, tools/scenarios/standin_objects_stage.h): a flat
// floor with a 200-unit cliff on its east side, a blue Onion, three buried
// sprouts, a one-pellet red pellet and a sleeping Dwarf Bulborb, all placed by
// the stage's default.gen from stand-in assets.
//
// Preconditions: the stage's own content exists (one Onion, three sprouts, one
// pellet, one enemy), so a generator or asset regression shows here and not as a
// vague later failure.
//
// Then six seeds are launched the way GoalAI::BootEmit launches them (an
// Onion-born PikiHeadItem: init at a position, colour, velocity, startAI, parent
// Onion, start Flying), from the cliff's foot toward the face at different
// heights and speeds, a few ticks apart. Each is followed through Flying,
// Bounce and Bury to the planted Wait state. Only what holds with or without
// upstream #208 (which keeps the bounce's height and speed at 30 Hz instead of
// dropping the seed to the ground) is asserted:
//  - every seed plants (Wait) within a bound, so none is lost in the air or
//    pushed back and forth forever;
//  - a planted seed's ground triangle is not a steep face (normal y >= 0.5):
//    the push off a wall that fixed upstream issue #65 stays at every frame rate;
//  - it is not below the ground at its position, and not floating above it by
//    more than a bounce's worth: the "planted in mid-air on the cliff" bug.
// What #208 changes (the height and speed at the start of Bury at 30 Hz) is
// printed per seed and not asserted, so the run records which behaviour it saw.
//
// Known limit: on this stage the 76 degree face is a wall to a flying seed, so
// the seeds stop about 13 units short of it (x = 37), bounce and drop to the
// floor, and BuryExec's push off a steep face never runs ("pushed off 0"). The
// edge case of upstream issue #65, a seed landing on a triangle whose collision normal
// says ground and whose face normal says wall, needs a surface built for it;
// the stage does not have one yet.
#include "scenarios/pc_scenario_list.h"

#if PIKI_SCENARIOS

#include <cmath>
#include <cstdio>
#include <vector>

#include "Creature.h"
#include "Generator.h"
#include "GoalItem.h"
#include "ItemMgr.h"
#include "MapMgr.h"
#include "ObjType.h"
#include "Pellet.h"
#include "PikiHeadItem.h"
#include "PlayerState.h"
#include "teki.h"

namespace {

constexpr int kSeeds = 6;
// A planted seed may hover this far above the ground it was dropped on (a
// bounce's residual height) before it counts as planted in mid-air. The bug put
// seeds up the cliff face, 50 to 200 units above the floor at their position.
constexpr float kMaxGapAboveGround = 30.0f;
// Cliff geometry of the stage (standin_objects_stage.h): the face runs from the
// foot at x = 50 to the top at x = 100 and rises 200.
constexpr float kFootX = 50.0f;

struct Tracked {
	PikiHeadItem* seed = nullptr;
	bool sawBury       = false;
	bool planted       = false;
	float buryY = 0, buryGroundY = 0, burySpeed = 0;
	float plantedY = 0, plantedGroundY = 0, plantedNormalY = 1.0f;
	unsigned launchTick = 0, buryTick = 0, plantTick = 0;
	int lastId = -1, pushes = 0; // times BuryExec sent the seed back to Flying (the steep-face push off)
	float buryX = 0, buryZ = 0;
};

class SproutLanding : public PcScenario {
public:
	const char* name() const override { return "sprout_landing"; }

	void onStageReady() override
	{
		int goals = 0, sprouts = 0, pellets = 0, tekis = 0;
		{
			// Onions live in the melting-pot manager; getContainer finds one by colour (the stage places the blue one).
			for (int color = 0; color < 3; color++) {
				if (itemMgr->getContainer(color)) goals++;
			}
			mOnion = itemMgr->getContainer(0);
		}
		{
			Iterator it(itemMgr->getPikiHeadMgr());
			CI_LOOP(it)
			{
				if ((*it)->mObjType == OBJTYPE_Pikihead) sprouts++;
			}
		}
		{
			Iterator it(pelletMgr);
			CI_LOOP(it) pellets++;
		}
		{
			Iterator it(tekiMgr);
			CI_LOOP(it) tekis++;
		}
		std::printf("[scenario] sprout_landing ground: floor %.0f at x=0, face %.0f at x=75, plateau %.0f at x=200\n", mapMgr->getMinY(0.0f, 0.0f, true),
		            mapMgr->getMinY(75.0f, 0.0f, true), mapMgr->getMinY(200.0f, 0.0f, true));
		std::printf("[scenario] sprout_landing containers: blue %d red %d yellow %d\n", playerState->hasContainer(0), playerState->hasContainer(1), playerState->hasContainer(2));
		std::printf("[scenario] sprout_landing stage content: %d Onion, %d sprouts, %d pellet, %d enemy\n", goals, sprouts, pellets, tekis);
		if (goals != 1 || sprouts != 3 || pellets != 1 || tekis != 1) {
			scenarioFailf(0, "the object stage's content: 1 Onion, 3 sprouts, 1 pellet, 1 enemy", "%d Onion, %d sprouts, %d pellet, %d enemy", goals, sprouts, pellets, tekis);
			return;
		}
	}

	void onTick(unsigned t) override
	{
		const int rate = pc_scenario_ticks_per_second();
		const unsigned launchGap = (unsigned)rate / 3u; // a third of a second between seeds
		const unsigned bound     = 30u * (unsigned)rate; // thirty seconds for the last seed to plant

		// Launch one seed per gap, starting at tick 3.
		if (mLaunched < kSeeds && t >= 3 && (t - 3) % launchGap == 0) launch(t);

		int plantedCount = 0;
		for (Tracked& k : mSeeds) {
			if (k.planted) {
				plantedCount++;
				continue;
			}
			const int id = k.seed->getCurrState()->getID();
			if (k.lastId == PikiHeadAI::PIKIHEAD_Bury && id == PikiHeadAI::PIKIHEAD_Flying) k.pushes++;
			k.lastId = id;
			if (!k.sawBury && id == PikiHeadAI::PIKIHEAD_Bury) {
				// BuryInit has run this tick: at 30 Hz with #208 the seed is where the bounce left it, without it the
				// seed is on the ground with no velocity.
				k.sawBury     = true;
				k.buryTick    = t;
				k.buryX       = k.seed->mSRT.t.x;
				k.buryZ       = k.seed->mSRT.t.z;
				k.buryY       = k.seed->mSRT.t.y;
				k.buryGroundY = mapMgr->getMinY(k.seed->mSRT.t.x, k.seed->mSRT.t.z, true);
				k.burySpeed   = std::sqrt(k.seed->mVelocity.x * k.seed->mVelocity.x + k.seed->mVelocity.y * k.seed->mVelocity.y
				                          + k.seed->mVelocity.z * k.seed->mVelocity.z);
			}
			if (id == PikiHeadAI::PIKIHEAD_Wait || id == PikiHeadAI::PIKIHEAD_Unk5) {
				k.planted        = true;
				k.plantTick      = t;
				k.plantedY       = k.seed->mSRT.t.y;
				k.plantedGroundY = mapMgr->getMinY(k.seed->mSRT.t.x, k.seed->mSRT.t.z, true);
				const CollTriInfo* tri = k.seed->mGroundTriangle;
				k.plantedNormalY       = tri ? tri->mTriangle.mNormal.y : 1.0f;
				plantedCount++;
			}
		}

		if (mLaunched == kSeeds && plantedCount == kSeeds) {
			report(t);
			return;
		}
		if (t > bound) {
			scenarioFailf(t, "every seed plants within the bound", "%d of %d planted after %u ticks (%d launched)", plantedCount, kSeeds, bound, mLaunched);
		}
	}

private:
	void launch(unsigned t)
	{
		const int i = mLaunched;
		PikiHeadItem* seed = static_cast<PikiHeadItem*>(itemMgr->birth(OBJTYPE_Pikihead));
		if (!seed) {
			scenarioFailf(t, "a seed born", "itemMgr->birth(OBJTYPE_Pikihead) returned null for seed %d", i);
			return;
		}
		// As GoalAI::BootEmit (itemAI.cpp:775-800): position, colour, velocity, startAI, parent, Flying. The launch point
		// is 30 units west of the foot of the face (x = 50) at heights between 40 and 115, aimed east at 400-600 units
		// per second with a little lift, so the first contact is the face (it is 4 units higher per unit of x) and
		// not the floor; z spreads the seeds along the cliff.
		Vector3f pos(20.0f + 4.0f * i, 40.0f + 15.0f * i, -150.0f + 60.0f * i);
		seed->init(pos);
		seed->setColor(i % 3);
		seed->mVelocity.set(400.0f + 40.0f * i, 30.0f + 15.0f * i, 0.0f);
		seed->startAI(0);
		seed->mParentOnion = mOnion;
		C_SAI(seed)->start(seed, PikiHeadAI::PIKIHEAD_Flying);
		Tracked k;
		k.seed       = seed;
		k.launchTick = t;
		mSeeds.push_back(k);
		mLaunched++;
	}

	void report(unsigned t)
	{
		int hits = 0;
		for (int i = 0; i < kSeeds; i++) {
			const Tracked& k = mSeeds[(std::size_t)i];
			std::printf("[scenario] sprout_landing seed %d: first bury at (%.0f, %.0f), pushed off %d time(s); bury at tick %u y=%.1f over ground %.1f (gap %.1f) speed %.1f; planted at tick %u y=%.1f over ground %.1f "
			            "(gap %.1f) face normal y=%.2f\n",
			            i, k.buryX, k.buryZ, k.pushes, k.buryTick, k.buryY, k.buryGroundY, k.buryY - k.buryGroundY, k.burySpeed, k.plantTick, k.plantedY, k.plantedGroundY,
			            k.plantedY - k.plantedGroundY, k.plantedNormalY);
			if (k.seed->mSRT.t.x > kFootX) hits++;
		}
		std::printf("[scenario] sprout_landing: %d of %d seeds ended beyond the cliff foot (x > %.0f)\n", hits, kSeeds, kFootX);
		for (int i = 0; i < kSeeds; i++) {
			const Tracked& k = mSeeds[(std::size_t)i];
			if (!k.sawBury) {
				scenarioFailf(t, "every seed passes through Bury", "seed %d planted without a Bury tick", i);
				return;
			}
			if (k.plantedNormalY < 0.5f) {
				scenarioFailf(t, "a planted seed is not on a steep face (ground normal y >= 0.5)", "seed %d planted on a face with normal y %.2f", i, k.plantedNormalY);
				return;
			}
			if (k.plantedY < k.plantedGroundY - 1.0f) {
				scenarioFailf(t, "a planted seed is not below the ground", "seed %d at y %.1f, ground %.1f", i, k.plantedY, k.plantedGroundY);
				return;
			}
			if (k.plantedY - k.plantedGroundY > kMaxGapAboveGround) {
				scenarioFailf(t, "a planted seed is not floating in mid-air", "seed %d at y %.1f, %.1f above the ground at its position", i, k.plantedY, k.plantedY - k.plantedGroundY);
				return;
			}
		}
		scenarioPass();
	}

	GoalItem* mOnion = nullptr;
	int mLaunched    = 0;
	std::vector<Tracked> mSeeds;
};

} // namespace

PcScenario* pc_scenario_make_sprout_landing(void)
{
	static SproutLanding s;
	return &s;
}

#endif
