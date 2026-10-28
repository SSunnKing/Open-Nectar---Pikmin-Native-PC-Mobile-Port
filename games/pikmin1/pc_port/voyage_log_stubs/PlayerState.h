#ifndef _PLAYERSTATE_H
#define _PLAYERSTATE_H

// Host stand-in for pc_voyage_log_test. resultFlag.cpp only calls getCurrDay().
struct PlayerState {
	int getCurrDay();
};

extern PlayerState* playerState;

#endif
