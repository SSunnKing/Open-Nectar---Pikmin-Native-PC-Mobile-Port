#pragma once

// Stand-in animations for the scenario sandbox (issue #66, under #62 and #59):
// the captain and every Pikmin colour share one AnimMgr (pikis/animMgr.bin) and
// one bundle (pikis/bluModel.anm). The table has the 100 infos the motion table
// can reach (PaniPikiAnimator maps motions up to AnimID 99, and startAnim
// dereferences a null info past the end of the list). Each info is a static
// identity DCK on the shared 12-joint rig (piki_rig.h, issue #65): motion is
// trivial, and the game's only check on the joint count is a log line followed by
// out-of-range reads.
//
// ThrowWait, Throw and every other motion a state machine waits on use an authored schedule.

#include "nectar_assets/anim.h"
#include "nectar_assets/bundle.h"

#include <cstdint>
#include <string>
#include <vector>

namespace nectar {

/// Infos 0 to 99, in index order.
constexpr int kStandInAnimCount = 100;

// Info indices (PaniPikiAnimator::init, panipikianimator.cpp: setMotion(PIKIANIM_x, info)).
constexpr int kAnimInfoNigeru    = 4;  ///< PIKIANIM_Nigeru: Navi::enableMotionBlend starts the lower animator on it at counter 10.0
constexpr int kAnimInfoThrow     = 54; ///< PIKIANIM_Throw (27): throws on its KEY_Action0
constexpr int kAnimInfoThrowWait = 63; ///< PIKIANIM_ThrowWait (36): hangs the Pikmin on its KEY_Action0

/// Frames of every info that has no authored schedule. Counters are forced to 10
/// (Navi::enableMotionBlend, navi.cpp:1043, copied onto the desired lower motion),
/// 18 (aiPut.cpp:145,163,190,469) and 30 (aiBore.cpp:750) on motions whose
/// identity is not known without the disc, and `Animator::animate` halts on a
/// counter at or above the frame count, so every info without an authored schedule has more than 30.
constexpr int kStandInDefaultFrames = 40;
constexpr int kStandInMinForcedCounterFrames = 31;

// Authored test schedule (not retail timing): Throw releases at frame 10 of 40. ThrowWait
// hangs the Pikmin at frame 10, then loops frames 10 to 34 while A is held. NaviThrowWaitState
// raises the charge level by one on every KEY_LoopEnd (to a maximum of 3), so a held throw
// charges naturally. The loop and the 40-frame length are chosen here, not copied.
constexpr int kThrowWaitFrames    = 40;
constexpr int kThrowWaitGrabFrame = 10; ///< K: anim-key frame of ThrowWait's KEY_Action0 and KEY_LoopStart
constexpr int kThrowWaitLoopEnd   = 34; ///< anim-key frame of ThrowWait's KEY_LoopEnd
constexpr int kThrowFrames        = 40;
constexpr int kThrowReleaseFrame  = 10; ///< anim-key frame of Throw's KEY_Action0

// Every motion below that a state machine waits on has an authored schedule: the frame
// count, anim keys and info keys are chosen here from the key the game code reads, with
// round numbers, and the constant-speed flags (5) and 30 frames a second are the source
// defaults of AnimInfo. No retail timing is used. standin_anims.cpp holds the table.
//
// Gate work and giving up. ActBreakWall (and ActJumpAttack's work state) hit on KEY_Action0
// of Kuttuku (info 71) or, on a platform, Job2 (info 41), and clear it on KEY_Action1;
// KEY_LoopStart sits before KEY_Action0 so every pass of the loop hits again, and
// KEY_Finished re-rolls the work timer. A Pikmin that gives up plays Gakkari (info 6) in
// PikiEmotionState until KEY_Finished (its loop keys are never used).
constexpr int kAnimInfoGakkari = 6;  ///< PIKIANIM_Gakkari (73)
constexpr int kAnimInfoJob2    = 41; ///< PIKIANIM_Job2 (19)
constexpr int kAnimInfoKuttuku = 71; ///< PIKIANIM_Kuttuku (48)
// A Pikmin lifting a pellet loops PickLoop (info 68) until its KEY_Action0
// (ActTransport::animationKeyUpdated sets mIsLiftActionDone, which
// ActTransport::doLift waits for before the carry starts); without that key no
// pellet is ever carried. A LoopStart/LoopEnd pair after it counts the lift retries
// while too few carriers are present.
constexpr int kAnimInfoPickLoop = 68; ///< PIKIANIM_PickLoop (46)
// Drinking nectar (nectar_growth): PikiAbsorbState (PIKIANIM_Mizunomi, info 67) starts
// drinking on KEY_LoopStart and sets mHasAbsorbedNectar on its first exec there, so
// KEY_Finished then moves to PikiGrowupState, which plays GrowUp1 (info 39, the first
// flowering; GrowUp2, info 40, a second one) and turns the sprout into the next flower
// on its KEY_Action0 (PikiGrowupState::procAnimMsg). The stand-in keeps the generic 40
// frames and adds only the keys the states wait on (loop start 10 to loop end 20 for
// Mizunomi, Action0 at frame 10 for GrowUp).
// A Pikmin picking a bomb up (bomb_firsts): ActPick plays PIKIANIM_Pick (info 1) and
// calls InteractGrab on the bomb on its KEY_Action0, so without that key nothing is held.
constexpr int kAnimInfoPick     = 1;  ///< PIKIANIM_Pick (4)
constexpr int kAnimInfoGrowUp1  = 39; ///< PIKIANIM_GrowUp1 (17)
constexpr int kAnimInfoGrowUp2  = 40; ///< PIKIANIM_GrowUp2 (18)
constexpr int kAnimInfoMizunomi = 67; ///< PIKIANIM_Mizunomi (47)
// The captain's whistle (whistle_gather, whistle_release, #96): NaviGatherState grows the
// whistle circle, calls callPikis and runs Whistle Pluck only after PIKIANIM_Fue's
// KEY_LoopStart (mWhistleAnimPhase 1), and NaviReleaseState disbands the squad
// (releasePikis) on that same key; KEY_Finished returns the captain to Walk. Authored:
// 24 frames with the loop from frame 2 to frame 6, no event keys.
constexpr int kAnimInfoFue    = 74; ///< PIKIANIM_Fue (49)
constexpr int kFueFrames      = 24;
constexpr int kFueLoopStart   = 2; ///< frame of KEY_LoopStart (anim key 1)
constexpr int kFueLoopEnd     = 6; ///< frame of KEY_LoopEnd (anim key 2)

// A called Pikmin stops to notice the whistle: PikiLookAtState plays PIKIANIM_Kizuku
// (info 66), turns on its KEY_Action0 (a 7-tick turn, so KEY_Action1 must come at least
// 7 ticks later), waits on KEY_Action1 and joins the squad on KEY_Finished, so the
// motion's length is most of the delay between a whistle and a Pikmin joining
// (whistle_delay). Authored: 19 frames, Action0 at 3, Action1 at 10.
constexpr int kAnimInfoKizuku = 66; ///< PIKIANIM_Kizuku (45)

// A Pikmin standing idle plays Wait (info 12), started with the Pikmin as its listener by
// ActFreeSelect and ActBoreSelect (aiBore.cpp). It must loop: a one-shot Wait sends
// KEY_Finished, which PikiLookAtState takes as Kizuku's end, so a Pikmin that lands in its
// random delay would skip Kizuku and join at once. The captain shares the table:
// NaviIdleState leaves Idle only when the motion ends after finishMotion. Authored: the
// shared 40 frames, KEY_LoopStart at the first anim key and KEY_LoopEnd at the last.
constexpr int kAnimInfoWait = 12; ///< PIKIANIM_Wait (3)

// A Pikmin shaken off an enemy lies in PikiFlickState playing JHit (info 58) until it
// ends (the flight distance is the strength times the motion's length), then JKoke
// (info 57, lying down until the knock-down timer) and GetUp (info 59); each only needs
// a finite length. ActJumpAttack's ground attack plays PIKIANIM_Attack (info 18) and hits
// on its KEY_Action0 (mAttackState 1, cleared on KEY_Action1, which must come at least
// 3 frames later so exec sees the hit), and its jump onto the enemy plays StillJump
// (info 13), whose KEY_Finished ends the jump. The generic infos have flags 2
// (ANIMFLAG_UseDynamicSpeed), so PaniPikiAnimMgr::updateAnimation would run them at the
// Pikmin's motion speed; these authored infos use flags 5 (constant speed).
constexpr int kAnimInfoStillJump = 13; ///< PIKIANIM_StillJump (22)
constexpr int kAnimInfoAttack    = 18; ///< PIKIANIM_Attack (23)
constexpr int kAnimInfoJKoke     = 57; ///< PIKIANIM_JKoke (30)
constexpr int kAnimInfoJHit      = 58; ///< PIKIANIM_JHit (31): shakes a Pikmin off an enemy
constexpr int kAnimInfoGetUp     = 59; ///< PIKIANIM_GetUp (32)

/// One info: its AnimMgr record and the frame count of its payload.
struct StandInAnim {
	AnimInfoSpec info;
	int frameCount = 0;
	bool scheduled = false; ///< the length and keys are an authored schedule (see above), so the generic length rule does not apply
};

struct StandInAnimSet {
	std::string tablePath;  ///< relative to assets/, with dataDir/ as in the boot manifest
	std::string bundlePath;
	std::string baseDir;    ///< 'a01'; every bundle entry is named `<baseDir>/<info name>`
	std::vector<int> parents; ///< skeleton the payloads are written for (joint count = size)
	std::vector<StandInAnim> anims;

	AnimMgrTable table() const;
	std::string entryName(int info) const { return baseDir + "/" + anims[std::size_t(info)].info.name; }
};

/// The Pikmin and captain set: pikis/animMgr.bin and pikis/bluModel.anm.
const StandInAnimSet& standInPikiAnims();

/// The generator assertions, written from the same constants as the data: unique
/// names; the joint list matches the rig; every anim key is below the frame count
/// (the game clamps it silently otherwise); info keys point into the anim-key
/// list; infos without an authored schedule are longer than the largest forced counter; ThrowWait
/// and Throw each have exactly one KEY_Action0 info key at the recorded frame.
bool checkStandInAnims(const StandInAnimSet& set, std::string& error);

struct StandInAnimFile {
	std::string path; ///< relative to assets/
	std::vector<std::uint8_t> bytes;
};

/// Writes the table and the bundle of `set`. The generator (#68) decides where the files go.
bool writeStandInAnimSet(const StandInAnimSet& set, std::vector<StandInAnimFile>& out, std::string& error);

/// writeStandInAnimSet(standInPikiAnims()).
bool writeStandInAnimations(std::vector<StandInAnimFile>& out, std::string& error);

} // namespace nectar
