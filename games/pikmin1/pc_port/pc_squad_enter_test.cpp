// Disc-free check for the co-op onion-enter rule. The game side
// (Navi::enterAllPikis, GameCoreSection::enterFreePikmins) calls these
// helpers; ActEnter cannot be constructed here.

#include "pc_squad_enter.h"

#include <cstdio>

namespace {

int sFailures = 0;

void check(bool condition, const char* what)
{
	if (!condition) {
		std::printf("FAIL: %s\n", what);
		++sFailures;
	}
}

struct Captain {
	int id;
};

} // namespace

int main()
{
	Captain olimar{0};
	Captain louie{1};

	check(pc_cutscene_enter_claims(&olimar, &olimar), "olimar claims his own squad");
	check(pc_cutscene_enter_claims(&louie, &louie), "louie claims his own squad");
	check(pc_cutscene_enter_claims(&olimar, static_cast<Captain*>(nullptr)), "olimar adopts an unassigned pikmin");
	check(!pc_cutscene_enter_claims(&olimar, &louie), "olimar does not claim louie's squad");
	check(!pc_cutscene_enter_claims(&louie, &olimar), "louie does not claim olimar's squad");
	check(!pc_cutscene_enter_claims(static_cast<Captain*>(nullptr), &louie), "no actor does not claim a squad");

	check(pc_enter_goal_captain(&louie, &olimar) == &louie, "enter writes the pikmin's own captain");
	check(pc_enter_goal_captain(static_cast<Captain*>(nullptr), &olimar) == &olimar, "enter falls back when the pikmin has no captain");
	check(pc_enter_goal_captain(static_cast<Captain*>(nullptr), static_cast<Captain*>(nullptr)) == nullptr,
	      "enter with nobody returns null");

	if (sFailures) {
		std::printf("%d failed\n", sFailures);
		return 1;
	}
	std::printf("pc_squad_enter_test ok\n");
	return 0;
}
