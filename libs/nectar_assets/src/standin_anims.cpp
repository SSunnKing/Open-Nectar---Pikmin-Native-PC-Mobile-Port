#include "nectar_assets/standin_anims.h"

#include "nectar_assets/piki_rig.h"

#include <cstdio>
#include <set>

namespace nectar {

namespace {

AnimKeyRecord actionKey(int animKeyIndex)
{
	AnimKeyRecord k;
	k.frame   = animKeyIndex;
	k.keyType = 2; // KEY_Action0 (PaniAnimator::checkConstantKey: type - 1)
	return k;
}

StandInAnimSet buildPikiSet()
{
	StandInAnimSet s;
	s.tablePath  = "dataDir/pikis/animMgr.bin";
	s.bundlePath = "dataDir/pikis/bluModel.anm";
	// AnimMgr 'a01' on the disc is also pikis/motion; the string only has to
	// match the bundle entry names, which this file chooses.
	s.baseDir = "pikis/motion";
	s.parents.assign(kPikiRigParents, kPikiRigParents + kPikiRigJointCount);

	for (int i = 0; i < kStandInAnimCount; i++) {
		StandInAnim a;
		char name[32];
		if (i == kAnimInfoThrow) {
			std::snprintf(name, sizeof name, "throw.dck");
		} else if (i == kAnimInfoThrowWait) {
			std::snprintf(name, sizeof name, "trwwait.dck");
		} else {
			std::snprintf(name, sizeof name, "motion%02d.dck", i); // unique: the bundle lookup is by name
		}
		a.info.name = name;
		if (i == kAnimInfoThrowWait) {
			// Anim keys [0, K, N-1] and one info key pointing at the middle one.
			// The first tick the counter reaches K, NaviThrowWaitState hangs the Pikmin.
			a.frameCount     = kThrowWaitFrames;
			a.measured       = true;
			a.info.animKeys  = { 0, kThrowWaitGrabFrame, kThrowWaitFrames - 1 };
			a.info.infoKeys  = { actionKey(1) };
		} else if (i == kAnimInfoThrow) {
			// NaviThrowState calls throwPiki on this key; KEY_Finished ends the state.
			a.frameCount     = kThrowFrames;
			a.measured       = true;
			a.info.animKeys  = { 0, kThrowReleaseFrame, kThrowFrames - 1 };
			a.info.infoKeys  = { actionKey(1) };
		} else if (i == kAnimInfoKuttuku || i == kAnimInfoJob2 || i == kAnimInfoGakkari || i == kAnimInfoPickLoop || i == kAnimInfoKizuku || i == kAnimInfoFlick
		           || i == kAnimInfoWait || i == kAnimInfoAttack || i == kAnimInfoStillJump || i == kAnimInfoJKoke || i == kAnimInfoGetUp) {
			// Disc timing (standin_anims.h): anim keys, then info keys as (anim-key
			// index, type) with 0 loop start, 1 loop end, 2 KEY_Action0, 3 KEY_Action1.
			struct Timing {
				std::vector<std::int32_t> animKeys;
				std::vector<std::pair<int, int>> infoKeys;
				int effectFrame; ///< event key of type 1 (action: ActBreakWall's hit effect), or -1
				int flags = 5;
			};
			const Timing t = i == kAnimInfoKuttuku  ? Timing { { 0, 11, 17, 30, 34 }, { { 1, 0 }, { 1, 2 }, { 2, 3 }, { 3, 1 } }, 11 }
			               : i == kAnimInfoJob2     ? Timing { { 0, 10, 12, 24 }, { { 0, 0 }, { 1, 2 }, { 2, 3 }, { 3, 1 } }, 11 }
			               : i == kAnimInfoPickLoop ? Timing { { 0, 10, 13, 18, 20, 29 }, { { 1, 2 }, { 2, 0 }, { 3, 1 }, { 4, 3 } }, -1 }
			               : i == kAnimInfoKizuku   ? Timing { { 0, 1, 11, 15 }, { { 1, 2 }, { 2, 3 } }, -1 }
			               : i == kAnimInfoFlick    ? Timing { { 0, 1, 9 }, { { 1, 2 } }, -1, 6 }
			               : i == kAnimInfoWait     ? Timing { { 0, 19 }, { { 0, 0 }, { 1, 1 } }, -1 }
			               : i == kAnimInfoAttack   ? Timing { { 0, 3, 5, 19 }, { { 1, 2 }, { 2, 3 } }, 5 }
			               : i == kAnimInfoStillJump ? Timing { { 0, 6, 26, 34 }, { { 1, 2 }, { 2, 3 } }, -1, 6 }
			               : i == kAnimInfoJKoke    ? Timing { { 0, 15 }, {}, -1 }
			               : i == kAnimInfoGetUp    ? Timing { { 0, 10 }, {}, -1 }
			                                        : Timing { { 0, 21, 38, 69 }, { { 1, 0 }, { 2, 1 } }, -1 };
			a.frameCount    = t.animKeys.back() + 1;
			a.measured      = true;
			a.info.flags    = t.flags;
			a.info.speed    = 30.0f;
			a.info.animKeys = t.animKeys;
			for (const auto& [key, type] : t.infoKeys) {
				AnimKeyRecord k;
				k.frame   = key;
				k.keyType = std::uint8_t(type);
				a.info.infoKeys.push_back(k);
			}
			if (t.effectFrame >= 0) {
				AnimKeyRecord e;
				e.frame     = t.effectFrame;
				e.eventType = 1;
				a.info.eventKeys.push_back(e);
			}
		} else if (i == kAnimInfoMizunomi) {
			// Loop window frames 10 to 20 (anim keys 1 and 2): info keys loop start, loop end.
			a.frameCount    = kStandInDefaultFrames;
			a.info.animKeys = { 0, 10, 20, kStandInDefaultFrames - 1 };
			a.info.flags    = 5;
			a.info.speed    = 30.0f;
			for (const auto& [key, type] : { std::pair<int, int> { 1, 0 }, { 2, 1 } }) {
				AnimKeyRecord k;
				k.frame   = key;
				k.keyType = std::uint8_t(type);
				a.info.infoKeys.push_back(k);
			}
		} else if (i == kAnimInfoFue) {
			// Disc timing (standin_anims.h): loop start at anim key 1, loop end at anim key 2.
			a.frameCount    = kFueFrames;
			a.measured      = true;
			a.info.animKeys = { 0, kFueLoopStart, kFueLoopEnd, kFueFrames - 1 };
			a.info.flags    = 5;
			a.info.speed    = 30.0f;
			for (const auto& [key, type] : { std::pair<int, int> { 1, 0 }, { 2, 1 } }) {
				AnimKeyRecord k;
				k.frame   = key;
				k.keyType = std::uint8_t(type);
				a.info.infoKeys.push_back(k);
			}
		} else if (i == kAnimInfoPick) {
			// ActPick grabs the bomb on KEY_Action0 (frame 10) and ends on KEY_Finished.
			a.frameCount    = kStandInDefaultFrames;
			a.info.animKeys = { 0, 10, kStandInDefaultFrames - 1 };
			a.info.flags    = 5;
			a.info.speed    = 30.0f;
			a.info.infoKeys = { actionKey(1) };
		} else if (i == kAnimInfoGrowUp1 || i == kAnimInfoGrowUp2) {
			a.frameCount    = kStandInDefaultFrames;
			a.info.animKeys = { 0, 10, kStandInDefaultFrames - 1 };
			a.info.flags    = 5;
			a.info.speed    = 30.0f;
			a.info.infoKeys = { actionKey(1) };
		} else {
			a.frameCount    = kStandInDefaultFrames;
			a.info.animKeys = { 0, kStandInDefaultFrames - 1 };
		}
		s.anims.push_back(std::move(a));
	}
	return s;
}

} // namespace

AnimMgrTable StandInAnimSet::table() const
{
	AnimMgrTable t;
	t.baseDir = baseDir;
	for (const StandInAnim& a : anims) t.infos.push_back(a.info);
	return t;
}

const StandInAnimSet& standInPikiAnims()
{
	static const StandInAnimSet set = buildPikiSet();
	return set;
}

bool checkStandInAnims(const StandInAnimSet& set, std::string& error)
{
	auto fail = [&](int i, const std::string& why) {
		error = "animation info " + std::to_string(i) + " '" + set.anims[std::size_t(i)].info.name + "': " + why;
		return false;
	};
	if (int(set.parents.size()) != kPikiRigJointCount) {
		error = "animation joint count " + std::to_string(set.parents.size()) + " is not the rig's " + std::to_string(kPikiRigJointCount);
		return false;
	}
	for (int j = 0; j < kPikiRigJointCount; j++) {
		if (set.parents[std::size_t(j)] != kPikiRigParents[j]) {
			error = "animation parent of joint " + std::to_string(j) + " differs from the rig";
			return false;
		}
	}
	if (int(set.anims.size()) != kStandInAnimCount) {
		error = "the AnimMgr table needs " + std::to_string(kStandInAnimCount) + " infos, has " + std::to_string(set.anims.size());
		return false;
	}

	std::set<std::string> names;
	for (int i = 0; i < kStandInAnimCount; i++) {
		const StandInAnim& a = set.anims[std::size_t(i)];
		if (!names.insert(a.info.name).second) return fail(i, "name is not unique");
		if (a.frameCount < 1) return fail(i, "frame count below 1 (startAnim errors on counter >= N)");
		if (!a.measured && a.frameCount < kStandInMinForcedCounterFrames) {
			return fail(i, "shorter than the largest counter the game forces (30)");
		}
		if (i == kAnimInfoNigeru && a.frameCount < 11) return fail(i, "Navi::enableMotionBlend forces counter 10.0 on Nigeru");
		if (a.info.animKeys.size() < 2) return fail(i, "needs a start and an end anim key");
		for (std::int32_t f : a.info.animKeys) {
			if (f >= a.frameCount) return fail(i, "anim key frame " + std::to_string(f) + " is not below the frame count (checkAnimData would clamp it)");
		}
		if (a.info.animKeys.back() != a.frameCount - 1) return fail(i, "last anim key is not the last frame");
		if (a.info.infoKeys.size() > 16) return fail(i, "more than 16 info keys");
		for (const AnimKeyRecord& k : a.info.infoKeys) {
			if (k.frame >= std::int32_t(a.info.animKeys.size())) return fail(i, "info key points past the anim-key list");
		}

		if (i == kAnimInfoThrowWait || i == kAnimInfoThrow) {
			const int want = i == kAnimInfoThrowWait ? kThrowWaitGrabFrame : kThrowReleaseFrame;
			int actions    = 0;
			for (const AnimKeyRecord& k : a.info.infoKeys) {
				if (k.keyType != 2) continue;
				actions++;
				if (a.info.animKeys[std::size_t(k.frame)] != want) return fail(i, "KEY_Action0 is not at the recorded frame");
				if (want >= a.frameCount) return fail(i, "KEY_Action0 frame is not below the frame count");
			}
			if (actions != 1) return fail(i, "needs exactly one KEY_Action0 info key");
		}
		if (i == kAnimInfoFue) {
			// NaviGatherState and NaviReleaseState wait on KEY_LoopStart: without it the whistle never grows and X never disbands.
			int starts = 0, ends = 0;
			for (const AnimKeyRecord& k : a.info.infoKeys) {
				const std::int32_t f = a.info.animKeys[std::size_t(k.frame)];
				if (k.keyType == 0 && f == kFueLoopStart) starts++;
				if (k.keyType == 1 && f == kFueLoopEnd) ends++;
			}
			if (starts != 1 || ends != 1) return fail(i, "needs one KEY_LoopStart at the recorded frame and one KEY_LoopEnd after it");
		}
		if (i == kAnimInfoWait) {
			// The disc's Wait loops: a one-shot ends in KEY_Finished, which PikiLookAtState takes as Kizuku's end (#145).
			int starts = 0, ends = 0;
			for (const AnimKeyRecord& k : a.info.infoKeys) {
				if (k.keyType == 0 && k.frame == 0) starts++;
				if (k.keyType == 1 && k.frame == std::int32_t(a.info.animKeys.size()) - 1) ends++;
			}
			if (starts != 1 || ends != 1) return fail(i, "needs the disc's loop: KEY_LoopStart at the first anim key, KEY_LoopEnd at the last");
		}
	}
	return true;
}

bool writeStandInAnimSet(const StandInAnimSet& set, std::vector<StandInAnimFile>& out, std::string& error)
{
	out.clear();
	if (!checkStandInAnims(set, error)) return false;

	StandInAnimFile table;
	table.path = set.tablePath;
	if (!writeAnimMgrTable(set.table(), table.bytes, error)) return false;

	std::vector<BundleEntry> entries;
	for (std::size_t i = 0; i < set.anims.size(); i++) {
		BundleEntry e;
		e.type = BUNDLE_DCK;
		e.name = set.entryName(int(i));
		if (!writeDckPayload(makeStaticAnim(set.parents, set.anims[i].frameCount), e.payload, error)) {
			error = e.name + ": " + error;
			return false;
		}
		entries.push_back(std::move(e));
	}
	StandInAnimFile bundle;
	bundle.path = set.bundlePath;
	if (!writeBundle(entries, bundle.bytes, error)) return false;

	out.push_back(std::move(table));
	out.push_back(std::move(bundle));
	return true;
}

bool writeStandInAnimations(std::vector<StandInAnimFile>& out, std::string& error) { return writeStandInAnimSet(standInPikiAnims(), out, error); }

} // namespace nectar
