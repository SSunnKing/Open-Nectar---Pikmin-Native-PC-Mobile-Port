#ifndef _PLAYERSTATE_H
#define _PLAYERSTATE_H

// Host stand-in for pc_diary_misc_pool_test. resultFlag.cpp only calls getCurrDay().
struct PlayerState {
	int getCurrDay();
};

extern PlayerState* playerState;

#endif
