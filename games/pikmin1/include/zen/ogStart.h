#ifndef _ZEN_OGSTART_H
#define _ZEN_OGSTART_H

#include "types.h"

class Controller;
class Graphics;
class P2DPane;
class P2DScreen;

namespace zen {

/**
 * @brief TODO
 *
 * @note Size: 0x14.
 */
struct ogScrStartMgr {

	/**
	 * @brief TODO
	 */
	enum StartStatus {
		START_NULL      = -1,
		START_Oscillate = 0,
		START_FadeIn    = 1,
		START_FadeOut   = 2,
		START_Exit      = 3,
	};

	ogScrStartMgr();

	void start();
	void stop();
	StartStatus update(Controller*);
	void draw(Graphics&);
#if defined(PIKI_PC_PORT)
	// True when a left click landed on a Press Start button this frame.
	// A miss still consumes the click so it cannot fall through. No button
	// panes yet (before the first draw) leaves the click pending.
	bool pcPointerPressed();
#endif

	StartStatus mState;           // _00, see ogScrStartState enum
	StartStatus mExitState;       // _04, see ogScrStartState enum
	P2DScreen* mPressStartScreen; // _08
	P2DPane* mRootPane;           // _0C, pane 'root'
	f32 mFadeTimer;               // _10, controls oscillating alpha level + fade in/out
};

} // namespace zen

#endif
