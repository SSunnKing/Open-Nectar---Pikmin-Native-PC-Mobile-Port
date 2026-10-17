#pragma once

// Netplay audio ownership.
//
// A netplay session runs both captains' simulation on both PCs, so every
// captain-owned sound that is not positional (whistle, footsteps, the
// C-stick charge, the captain's voice, the Pikmin voices that captain
// triggers) would play on both machines, and the two captains' calls would
// fight over the single charge-sound state.
//
// Audio is local output, never sim state, so dropping the other captain's
// global sounds cannot desynchronise anything. Positional sounds are left
// alone: the listener follows the local presentation camera.
//
// Captain code marks which captain it is running with a PcAudioSource scope;
// anything outside a scope is always audible. Outside a deterministic
// netplay session (single player, local split screen, replays) every sound
// is audible as before. PIKMIN_NETPLAY_AUDIO_LEGACY=1 restores that in a
// session too.

#if defined(PIKI_PC_PORT) && defined(__cplusplus)

#include <cstdlib>

#include "netplay/pc_netplay_present.h"
#include "netplay/pc_netplay_session.h"

// Defined by the session TU in netplay builds only, so it is weak here.

inline int& pc_audio_source_captain_slot()
{
	static int sSourceCaptain = -1;
	return sSourceCaptain;
}

// RAII: the captain (0 = Olimar, 1 = Louie) whose code is running.
class PcAudioSource {
public:
	explicit PcAudioSource(int captain)
	    : mPrevious(pc_audio_source_captain_slot())
	{
		pc_audio_source_captain_slot() = captain;
	}
	~PcAudioSource() { pc_audio_source_captain_slot() = mPrevious; }
	PcAudioSource(const PcAudioSource&)            = delete;
	PcAudioSource& operator=(const PcAudioSource&) = delete;

private:
	int mPrevious;
};

inline bool pc_audio_in_netplay_session()
{
	return pc_netplay_session_active != nullptr && pc_netplay_session_active();
}

inline bool pc_audio_legacy_ownership()
{
	static const bool legacy = [] {
		const char* v = std::getenv("PIKMIN_NETPLAY_AUDIO_LEGACY");
		return v != nullptr && v[0] != '\0' && v[0] != '0';
	}();
	return legacy;
}

// True when a captain-owned, non-positional sound raised now should reach
// this PC's mixer.
inline bool pc_audio_source_audible()
{
	const int source = pc_audio_source_captain_slot();
	if (source < 0 || !pc_netplay_present_two_pass_active() || !pc_audio_in_netplay_session()
	    || pc_audio_legacy_ownership()) {
		return true;
	}
	return source == pc_netplay_present_local_player();
}

#endif
