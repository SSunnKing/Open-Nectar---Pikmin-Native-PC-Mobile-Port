#include "pc_throw_arc.h"

#include "Creature.h"
#include "Piki.h"
#include "PikiState.h"
#include "system.h"

#include <unordered_map>

namespace {

struct ArcEntry {
	PcThrowArcClock clock;
	PcThrowArcAction action = PcThrowArcAction::Passthrough;
	u32 frame               = 0;
	bool decided            = false;
};

std::unordered_map<Piki*, ArcEntry> sArcs;

int ticksPer30()
{
	return gsys != nullptr ? gsys->pcTicksPer30Hz() : 1;
}

u32 frameNow()
{
	return gsys != nullptr ? gsys->mTotalFrames : 0;
}

ArcEntry* findFlying(Piki* piki)
{
	if (piki == nullptr) {
		return nullptr;
	}
	auto it = sArcs.find(piki);
	if (it == sArcs.end()) {
		return nullptr;
	}
	// 30 Hz, or a Pikmin that has landed: drop the clock and integrate normally.
	if (ticksPer30() <= 1 || piki->getState() != PIKISTATE_Flying) {
		sArcs.erase(it);
		return nullptr;
	}
	return &it->second;
}

const ArcEntry* decisionThisFrame(Piki* piki)
{
	const ArcEntry* entry = findFlying(piki);
	if (entry == nullptr || !entry->decided || entry->frame != frameNow()) {
		return nullptr;
	}
	return entry;
}

} // namespace

void pc_throw_arc_release(Piki* piki)
{
	if (piki != nullptr) {
		sArcs.erase(piki);
	}
}

void pc_throw_arc_launch(Piki* piki)
{
	if (piki == nullptr || ticksPer30() <= 1) {
		return;
	}
	ArcEntry& entry = sArcs[piki];
	pc_throw_arc_prime(&entry.clock);
	entry.decided = false;
	entry.action  = PcThrowArcAction::Passthrough;
	entry.frame   = 0;
}

bool pc_throw_arc_would_hold(Piki* piki)
{
	const ArcEntry* entry = decisionThisFrame(piki);
	return entry != nullptr && entry->action == PcThrowArcAction::Hold;
}

float pc_throw_arc_integrate_dt(Piki* piki, float frameDt)
{
	const ArcEntry* entry = decisionThisFrame(piki);
	if (entry != nullptr && entry->action == PcThrowArcAction::DiscStep) {
		return kPcThrowArcDiscDt;
	}
	return frameDt;
}

bool pc_throw_arc_movement(Creature* creature, float frameDt, bool* hold, float* stepDt)
{
	if (hold == nullptr || stepDt == nullptr || creature == nullptr || creature->mObjType != OBJTYPE_Piki) {
		return false;
	}
	Piki* piki      = static_cast<Piki*>(creature);
	ArcEntry* entry = findFlying(piki);
	if (entry == nullptr) {
		return false;
	}

	const int ticks                = ticksPer30();
	const PcThrowArcAction action = pc_throw_arc_peek(entry->clock, frameDt, ticks);
	pc_throw_arc_commit(&entry->clock, frameDt, ticks);
	entry->action  = action;
	entry->frame   = frameNow();
	entry->decided = true;

	if (action == PcThrowArcAction::Hold) {
		*hold = true;
		return true;
	}
	if (action == PcThrowArcAction::DiscStep) {
		*hold   = false;
		*stepDt = kPcThrowArcDiscDt;
		return true;
	}
	return false;
}
