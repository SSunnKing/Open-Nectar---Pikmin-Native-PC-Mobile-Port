#ifndef PC_THROW_RELEASE_H
#define PC_THROW_RELEASE_H

/**
 * @file pc_throw_release.h
 * @brief When a throw grab may leave the hand, and when a mash is remembered.
 *
 * NaviThrowWaitState and NaviThrowState call these. The host test does too,
 * so the decision can be walked a tick at a time without a captain or a window.
 *
 * keyUp stays true for every tick A is not held. The grab is finished only
 * when ThrowWait's KEY_Action0 sets the holding flag. Hanging the Pikmin on
 * the release tick threw it before that keyframe.
 *
 * Navi::update delivers the animation message before Navi::doAI runs the
 * state, so on the keyframe tick the holding flag is already set here.
 */

/// True when this tick leaves ThrowWait and enters Throw.
inline bool pc_throw_release_starts_throw(bool keyUp, bool grabKeyframeReached)
{
	return keyUp && grabKeyframeReached;
}

/**
 * @brief True when ThrowWait should drop back to Walk.
 *
 * A nearby Pikmin stays in the normal state until KEY_Action0. That wait is
 * the grab wind-up, so it stays in ThrowWait. Hanged and walking over
 * (GoHang) stay as well. Any other state, and a Pikmin that has died, ends
 * the grab.
 */
inline bool pc_throw_wait_lost_piki(bool alive, bool grabKeyframeReached, bool stateNormal, bool stateHanged,
                                    bool stateGoHang)
{
	const bool awaitingGrab = !grabKeyframeReached && stateNormal;
	return !alive || (!awaitingGrab && !stateHanged && !stateGoHang);
}

/**
 * @brief Remember a throw click that lands before the Pikmin leaves the hand.
 *
 * The click is stored and starts the next grab on the tick KEY_Action0 of
 * the throw animation releases the Pikmin. The stored click does not hang
 * anyone, and the grab it starts still waits for ThrowWait's own keyframe.
 *
 * @param queued NaviThrowState::mQueuedThrowPress. Cleared when the click is
 *               taken, including when the action button consumes the tick.
 * @return true when this tick should enter ThrowWait.
 */
inline bool pc_throw_consume_queued_press(bool hasThrownPiki, bool throwClick, bool* queued)
{
	if (!hasThrownPiki && throwClick) {
		*queued = true;
	}
	if (hasThrownPiki && (throwClick || *queued)) {
		*queued = false;
		return true;
	}
	return false;
}

#endif
