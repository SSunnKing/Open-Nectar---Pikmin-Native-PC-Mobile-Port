// pellet_table: the pellet configuration table (coverage/edge-cases.md EC-46). Walks every
// PelletConfig the game loaded from parms/pelMgr.bin and asserts what each one must hold:
// a known type (0 blue or enemy corpse, 1 red, 2 yellow, 3 ship part), a colour in -2..2 that fits the type,
// a carrier range of at least one with max >= min, Onion seed counts that are not below -1
// (a negative count is the fireworks sentinel, GoalItem::suckMe), a model id and a shape id,
// and a model id that is unique and found again by PelletMgr::getConfig. The config count is
// checked against the table of the tree under test.
//
// Stand-in tree (stages/sandbox*.ini, stages/practice.ini): the count and every number are the
// generator's own (libs/nectar_assets standin_objects.cpp): blue, red and yellow pellets at 1,
// 5, 10 and 20, the Main Engine, the Whimsical Radar and the six enemy corpses the stand-in
// stages place (tkch, tksw, tkbe, tkka, tkkb, tkot: type 0, colour -1, W7 #98), with the carry and
// seed numbers there.
// Disc tree (any other stage, the ctest `_disc` entry): 62 configs, the disc's table; only the
// range checks apply, since those values are the disc's.
//
// Negative control (manual): change kStandInConfigs to 21, or a pellet's seeds in the
// generator, and the run fails with the expected and observed numbers.
#include "scenarios/pc_scenario_list.h"

#if PIKI_SCENARIOS

#include <cstdio>
#include <cstring>
#include <set>
#include <string>

#include "Pellet.h"
#include "scenarios/scenario_carry_common.h"

namespace {

constexpr int kStandInConfigs = 20; // 12 coloured pellets, the Main Engine, the Whimsical Radar, 6 corpses
constexpr int kDiscConfigs    = 62; // the VERSION_GPIE01_01 install's parms/pelMgr.bin

struct StandInPellet {
	const char* model;
	int type;
	int colour;
	int carryMin, carryMax;
	int matching, other;
};

class PelletTable : public PcScenario {
public:
	const char* name() const override { return "pellet_table"; }

	void onStageReady() override
	{
		const bool standIn = carry::isStandInTree();
		const int want     = standIn ? kStandInConfigs : kDiscConfigs;
		int count          = 0;
		std::set<std::string> ids;
		for (PelletConfig* c = pelletMgr->pcFirstConfig(); c; c = static_cast<PelletConfig*>(c->mNext)) {
			count++;
			if (!check(*c, standIn, ids)) return;
		}
		std::printf("[scenario] pellet_table: %s tree, %d configs (node count), PelletMgr says %d\n", standIn ? "stand-in" : "disc", count, pelletMgr->getNumConfigs());
		if (count != want || pelletMgr->getNumConfigs() != want) {
			scenarioFailf(0, standIn ? "20 configs in the stand-in table" : "62 configs in the disc's table", "%d nodes, getNumConfigs %d", count, pelletMgr->getNumConfigs());
			mDone = true;
			return;
		}
		if (standIn && !checkStandIn()) mDone = true;
	}

	void onTick(unsigned) override
	{
		if (!mDone) scenarioPass();
		mDone = true;
	}

private:
	bool bad(const char* expected, PelletConfig& c, const char* what)
	{
		scenarioFailf(0, expected, "config %s (%s): %s", c.mModelId.mStringID, c.mPelletName().mString, what);
		mDone = true;
		return false;
	}

	bool check(PelletConfig& c, bool standIn, std::set<std::string>& ids)
	{
		const int type = c.mPelletType(), colour = c.mPelletColor();
		char what[160];
		if (type < PELTYPE_Blue || type > PELTYPE_UfoPart) {
			std::snprintf(what, sizeof what, "type %d", type);
			return bad("type 0 to 3 (blue, red, yellow, ship part)", c, what);
		}
		if (colour < PELCOLOR_Part || colour > PELCOLOUR_MAX) {
			std::snprintf(what, sizeof what, "colour %d", colour);
			return bad("colour -2 to 2", c, what);
		}
		if (type == PELTYPE_UfoPart && colour > PELCOLOR_NULL) {
			std::snprintf(what, sizeof what, "colour %d", colour);
			return bad("a ship part has no colour (-2 or -1)", c, what);
		}
		if (standIn && c.mModelId.match('tk**')) {
			if (type != PELTYPE_Corpse || colour != PELCOLOR_NULL) {
				std::snprintf(what, sizeof what, "type %d colour %d", type, colour);
				return bad("a stand-in enemy corpse is type 0 with no colour (-1)", c, what);
			}
		} else if (standIn && type != PELTYPE_UfoPart && colour != type) {
			std::snprintf(what, sizeof what, "type %d colour %d", type, colour);
			return bad("a stand-in coloured pellet's type and colour are the same", c, what);
		}
		const int lo = c.mCarryMinPikis(), hi = c.mCarryMaxPikis();
		if (lo < 1 || hi < lo) {
			std::snprintf(what, sizeof what, "carriers %d to %d", lo, hi);
			return bad("at least one carrier and max >= min", c, what);
		}
		const int match = c.mMatchingOnyonSeeds(), other = c.mNonMatchingOnyonSeeds();
		if (match < -1 || other < -1 || match > 100 || other > 100) {
			std::snprintf(what, sizeof what, "seeds %d / %d", match, other);
			return bad("seeds from -1 (fireworks) to 100", c, what);
		}
		if (c.mModelId.mId == 0 || c.mPelletId.mId == 0 || c.mModelId.mId == 'none') return bad("a model id and a shape id", c, "empty id");
		if (!ids.insert(c.mModelId.mStringID).second) return bad("every model id once", c, "duplicate");
		if (pelletMgr->getConfig(c.mModelId.mId) != &c) return bad("getConfig finds the config by its model id", c, "found another");
		return true;
	}

	bool checkStandIn()
	{
		// type colour carryMin carryMax matching other
		static const StandInPellet table[] = {
			{ "pb01", 0, 0, 1, 2, 1, 1 },   { "pb05", 0, 0, 5, 10, 3, 1 },   { "pb10", 0, 0, 10, 20, 5, 2 },  { "pb20", 0, 0, 20, 40, 8, 3 },
			{ "pr01", 1, 1, 1, 2, 1, 1 },   { "pr05", 1, 1, 5, 10, 3, 1 },   { "pr10", 1, 1, 10, 20, 5, 2 },  { "pr20", 1, 1, 20, 40, 8, 3 },
			{ "py01", 2, 2, 1, 2, 1, 1 },   { "py05", 2, 2, 5, 10, 3, 1 },   { "py10", 2, 2, 10, 20, 5, 2 },  { "py20", 2, 2, 20, 40, 8, 3 },
			{ "ust5", 3, -1, 20, 40, 1, 1 }, { "uf01", 3, -1, 10, 20, 1, 1 },
			{ "tkch", 0, -1, 3, 6, 5, 5 },  { "tksw", 0, -1, 10, 20, 10, 10 }, { "tkbe", 0, -1, 20, 40, 20, 20 },
			{ "tkka", 0, -1, 2, 4, 2, 2 },  { "tkkb", 0, -1, 2, 4, 2, 2 },     { "tkot", 0, -1, 1, 2, 1, 1 },
		};
		for (const StandInPellet& e : table) {
			PelletConfig* c = pelletMgr->getConfig(carry::id(e.model));
			if (!c) {
				scenarioFailf(0, "the stand-in table's config", "%s missing", e.model);
				return false;
			}
			if (c->mPelletType() != e.type || c->mPelletColor() != e.colour || c->mCarryMinPikis() != e.carryMin || c->mCarryMaxPikis() != e.carryMax
			    || c->mMatchingOnyonSeeds() != e.matching || c->mNonMatchingOnyonSeeds() != e.other) {
				scenarioFailf(0, "the stand-in table's numbers (type colour carriers seeds)", "%s: %d %d %d-%d seeds %d/%d, expected %d %d %d-%d seeds %d/%d", e.model,
				              c->mPelletType(), c->mPelletColor(), c->mCarryMinPikis(), c->mCarryMaxPikis(), c->mMatchingOnyonSeeds(), c->mNonMatchingOnyonSeeds(), e.type,
				              e.colour, e.carryMin, e.carryMax, e.matching, e.other);
				return false;
			}
		}
		std::printf("[scenario] pellet_table: the %zu stand-in configs hold their table's numbers\n", sizeof table / sizeof table[0]);
		return true;
	}

	bool mDone = false;
};

} // namespace

PcScenario* pc_scenario_make_pellet_table(void)
{
	static PelletTable s;
	return &s;
}

#endif
