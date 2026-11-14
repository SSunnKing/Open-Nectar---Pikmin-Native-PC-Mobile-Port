#pragma once

// Co-op onion enter.
//
// Piki::changeMode(EnterMode) starts ActEnter on piki->mNavi->mGoalItem. A
// null or non-goal target hits ActEnter::init's "karl caught a cold !" halt.
// Callers therefore have to write the onion onto the Pikmin's own captain
// before the mode change, and a cutscene whistle (DemoEventNavi::Fue), which
// calls Navi::enterAllPikis once per captain, must not claim another
// captain's squad on the way past.

// True when `actor`'s enterAllPikis may command a Pikmin whose captain is
// `owner`. A null owner is an unassigned squad member; the caller adopts it.
// Another captain's squad waits for that captain's own call.
template <typename Captain>
inline bool pc_cutscene_enter_claims(const Captain* actor, const Captain* owner)
{
	return owner == nullptr || owner == actor;
}

// The captain whose mGoalItem EnterMode will read: the Pikmin's own captain,
// or the only captain on the field when the Pikmin has none yet.
template <typename Captain>
inline Captain* pc_enter_goal_captain(Captain* owner, Captain* fallback)
{
	return owner ? owner : fallback;
}
