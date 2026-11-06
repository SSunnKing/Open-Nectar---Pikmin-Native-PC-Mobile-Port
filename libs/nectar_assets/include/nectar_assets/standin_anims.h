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
// Fidelity (#62): a stand-in matches the disc only where a scenario depends on
// it. That is the length and the grab/throw key frame of ThrowWait (info 63) and
// Throw (info 54), which #61's throw_release waits on. Those numbers ONLY THE
// DISC HAS; they were read from the disc install (read-only, a one-off parse of
// pikis/animMgr.bin and pikis/bluModel.anm) and each is named and sourced in
// standin_anims.cpp. No curves, names or other disc animation data are copied:
// every other info is a plain one-shot of kStandInDefaultFrames frames.

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

/// Frames of every info that no scenario measures. Counters are forced to 10
/// (Navi::enableMotionBlend, navi.cpp:1043, copied onto the desired lower motion),
/// 18 (aiPut.cpp:145,163,190,469) and 30 (aiBore.cpp:750) on motions whose
/// identity is not known without the disc, and `Animator::animate` halts on a
/// counter at or above the frame count, so every unmeasured info has more than 30.
constexpr int kStandInDefaultFrames = 40;
constexpr int kStandInMinForcedCounterFrames = 31;

// Measured on the disc (VERSION_GPIE01_01 install). Info 63 'trwwait': frame
// count 21, anim keys [0, 1, 20], one info key per Action0. Info 54 'throw':
// frame count 10, anim keys [0, 1, 9], one Action0 info key. Both p00 5 and spd 30.
// The stand-ins use those flags (5: constant speed, not the code default 2) and
// speed 30.0. That read did not record LoopStart, LoopEnd, Action1 or an event
// key, so none of those are written (throw charge still needs a fresh AnimMgr dump).
constexpr int kThrowWaitFrames    = 21;
constexpr int kThrowWaitGrabFrame = 1; ///< K: anim-key frame of ThrowWait's KEY_Action0
constexpr int kThrowFrames        = 10;
constexpr int kThrowReleaseFrame  = 1; ///< anim-key frame of Throw's KEY_Action0

// The motions of a Pikmin working a gate and giving up (#68 sandbox gate,
// standin_sandbox.h): ActBreakWall attacks on KEY_Action0 of Kuttuku (info 71)
// or Job2 (info 41, on a platform), and a Pikmin that gives up plays Gakkari
// (info 6) in PikiEmotionState until KEY_Finished. Their timing sets when the
// squad comes back from the gate, so it is measured on the disc
// (VERSION_GPIE01_01, pikis/animMgr.bin, read-only, 2026-10-28): anim keys,
// info keys (loop start, KEY_Action0/1, loop end), the effect event key, flags
// 5 (constant speed) and speed 30. No curves are copied.
constexpr int kAnimInfoGakkari = 6;  ///< PIKIANIM_Gakkari (73)
constexpr int kAnimInfoJob2    = 41; ///< PIKIANIM_Job2 (19)
constexpr int kAnimInfoKuttuku = 71; ///< PIKIANIM_Kuttuku (48)
// A Pikmin lifting a pellet loops PickLoop (info 68) until its KEY_Action0
// (ActTransport::animationKeyUpdated sets mIsLiftActionDone, which
// ActTransport::doLift waits for before the carry starts); without that key no
// pellet is ever carried. Measured the same way (2026-10-28): frame count 30,
// anim keys [0, 10, 13, 18, 20, 29], KEY_Action0 at 10, loop 13 to 18,
// KEY_Action1 at 20, flags 5, speed 30.
constexpr int kAnimInfoPickLoop = 68; ///< PIKIANIM_PickLoop (46)
// Drinking nectar (nectar_growth): PikiAbsorbState (PIKIANIM_Mizunomi, info 67) starts
// drinking on KEY_LoopStart and sets mHasAbsorbedNectar on its first exec there, so
// KEY_Finished then moves to PikiGrowupState, which plays GrowUp1 (info 39, the first
// flowering; GrowUp2, info 40, a second one) and turns the sprout into the next flower
// on its KEY_Action0 (PikiGrowupState::procAnimMsg). These three are NOT measured on
// the disc: the stand-in keeps the generic 40 frames and adds only the keys the states
// wait on (loop start 10 to loop end 20 for Mizunomi, Action0 at frame 10 for GrowUp).
// A Pikmin picking a bomb up (bomb_firsts): ActPick plays PIKIANIM_Pick (info 1) and
// calls InteractGrab on the bomb on its KEY_Action0, so without that key nothing is held.
// Not measured on the disc: the generic 40 frames with Action0 at frame 10.
constexpr int kAnimInfoPick     = 1;  ///< PIKIANIM_Pick (4)
constexpr int kAnimInfoGrowUp1  = 39; ///< PIKIANIM_GrowUp1 (17)
constexpr int kAnimInfoGrowUp2  = 40; ///< PIKIANIM_GrowUp2 (18)
constexpr int kAnimInfoMizunomi = 67; ///< PIKIANIM_Mizunomi (47)
// The captain's whistle (whistle_gather, whistle_release, #96): NaviGatherState grows the
// whistle circle, calls callPikis and runs Whistle Pluck only after PIKIANIM_Fue's
// KEY_LoopStart (mWhistleAnimPhase 1), and NaviReleaseState disbands the squad
// (releasePikis) on that same key. Measured on the disc (VERSION_GPIE01_01,
// pikis/animMgr.bin info 74, read-only): frame count 20, anim keys [0, 1, 3, 19],
// loop start at anim key 1 and loop end at anim key 2 (frames 1 to 3 loop until
// finishMotion), no event keys, flags 5, speed 30. No curves or names are copied.
constexpr int kAnimInfoFue    = 74; ///< PIKIANIM_Fue (49)
constexpr int kFueFrames      = 20;
constexpr int kFueLoopStart   = 1; ///< frame of KEY_LoopStart (anim key 1)
constexpr int kFueLoopEnd     = 3; ///< frame of KEY_LoopEnd (anim key 2)

// A called Pikmin stops to notice the whistle: PikiLookAtState plays PIKIANIM_Kizuku
// (info 66), turns on its KEY_Action0, waits on KEY_Action1 and joins the squad on
// KEY_Finished, so the motion's length is most of the delay between a whistle and a
// Pikmin joining (whistle_delay, issue #133: the generic 40 frames gave about 1.5 s, the
// disc about 0.5 s). Measured on the disc (VERSION_GPIE01_01 install, read through the
// game's own AnimMgr, read-only, 2026-11-02): frame count 16, anim keys [0, 1, 11, 15],
// KEY_Action0 at anim key 1, KEY_Action1 at anim key 2, flags 5, speed 30. No curves.
constexpr int kAnimInfoKizuku = 66; ///< PIKIANIM_Kizuku (45)

// A Pikmin standing idle plays Wait (info 12), started with the Pikmin as its listener by
// ActFreeSelect and ActBoreSelect (aiBore.cpp). On the disc it loops and never ends; a
// one-shot Wait sends KEY_Finished, and one that lands in PikiLookAtState's random delay
// skips Kizuku and joins at once (whistle_delay at 30 Hz, issue #145: three of six stand-in
// Pikmin joined in 0.07 to 0.20 s). Measured on the disc (VERSION_GPIE01_01 install, read
// through the game's own AnimMgr, read-only, 2026-11-04): frame count 20, anim keys
// [0, 19], KEY_LoopStart at anim key 0 and KEY_LoopEnd at anim key 1, no event keys,
// flags 5, speed 30. No curves. The captain shares the table: NaviIdleState no longer
// re-rolls its idle motion every 40 frames once it picks Wait, and a captain leaving Idle
// ends Wait at frame 19, not 39, as on the disc.
constexpr int kAnimInfoWait = 12; ///< PIKIANIM_Wait (3)

// A Pikmin shaken off an enemy lies in PikiFlickState playing the Flick motion (info 58)
// until it ends (bulborb_fight, issue #136: the generic 40 frames kept stand-in Pikmin
// down for 4.6 s on average where the disc's 10 frames keep them down for 2.9 s, so a
// squad fighting a Dwarf Bulborb lost twice as many Pikmin and took 3.5 times longer to
// win). Measured on the disc (VERSION_GPIE01_01 install, read through the game's own
// AnimMgr, read-only, 2026-11-02): frame count 10, anim keys [0, 1, 9], KEY_Action0 at
// anim key 1, flags 6 (not 5), speed 30. No curves.
constexpr int kAnimInfoFlick = 58; ///< the Flick motion, shaking a Pikmin off an enemy

// The rest of a fight (bulborb_fight, issue #154). ActJumpAttack's ground attack plays
// PIKIANIM_Attack (info 18) and hits on its KEY_Action0 (mAttackState 1, cleared on KEY_Action1),
// so the generic 40 frames with no info keys never hit at all; its jump onto the enemy plays
// StillJump (info 13). PikiFlickState follows the Flick motion with JKoke (info 57, lying down
// until the knock-down timer) and GetUp (info 59). The generic infos also had flags 2
// (ANIMFLAG_UseDynamicSpeed), so PaniPikiAnimMgr::updateAnimation ran them at the Pikmin's
// motion speed instead of the info's 30 frames a second; the disc's Attack, JKoke and GetUp
// have flags 5 (constant speed). Measured on the disc (VERSION_GPIE01_01 install, files equal
// to Pikmin-USA-Rev1.iso's, read through the game's own AnimMgr, read-only, 2026-11-04), speed
// 30 for all four, no curves:
//   Attack    info 18: frame count 20, anim keys [0, 3, 5, 19], KEY_Action0 at anim key 1,
//                      KEY_Action1 at anim key 2, effect (action) event at frame 5, flags 5;
//   StillJump info 13: frame count 35, anim keys [0, 6, 26, 34], KEY_Action0 at anim key 1,
//                      KEY_Action1 at anim key 2, flags 6;
//   JKoke     info 57: frame count 16, anim keys [0, 15], no info keys, flags 5;
//   GetUp     info 59: frame count 11, anim keys [0, 10], no info keys, flags 5.
// The disc's sound (notify) event keys are left out, as on the other measured infos.
constexpr int kAnimInfoStillJump = 13; ///< PIKIANIM_StillJump (22)
constexpr int kAnimInfoAttack    = 18; ///< PIKIANIM_Attack (23)
constexpr int kAnimInfoJKoke     = 57; ///< PIKIANIM_JKoke (30)
constexpr int kAnimInfoGetUp     = 59; ///< PIKIANIM_GetUp (32)

/// One info: its AnimMgr record and the frame count of its payload.
struct StandInAnim {
	AnimInfoSpec info;
	int frameCount = 0;
	bool measured  = false; ///< the length and key frame come from the disc (see above), so the generic length rule does not apply
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
/// list; unmeasured infos are longer than the largest forced counter; ThrowWait
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
