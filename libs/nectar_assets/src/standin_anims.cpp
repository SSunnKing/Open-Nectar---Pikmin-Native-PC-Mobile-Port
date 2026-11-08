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

AnimKeyRecord loopKey(int animKeyIndex, int type)
{
	AnimKeyRecord k;
	k.frame   = animKeyIndex;
	k.keyType = std::uint8_t(type); // 0 KEY_LoopStart, 1 KEY_LoopEnd
	return k;
}

struct AuthoredTiming {
	std::vector<std::int32_t> animKeys;
	std::vector<std::pair<int, int>> infoKeys;
};

// The authored schedules of the motions the game's state machines wait on. Frame counts and key
// frames are chosen here (round numbers) from the key each consumer reads; see standin_anims.h.
const AuthoredTiming* authoredTiming(int info)
{
	// Hit cycle shared by gate work: LoopStart and Action0 at 10, Action1 at 15, LoopEnd at 30.
	static const AuthoredTiming kHitCycle { { 0, 10, 15, 30, 39 }, { { 1, 0 }, { 1, 2 }, { 2, 3 }, { 3, 1 } } };
	static const AuthoredTiming kPickLoop { { 0, 12, 16, 28, 39 }, { { 1, 2 }, { 2, 0 }, { 3, 1 } } };
	static const AuthoredTiming kKizuku { { 0, 3, 10, 18 }, { { 1, 2 }, { 2, 3 } } };
	static const AuthoredTiming kAttack { { 0, 6, 9, 23 }, { { 1, 2 }, { 2, 3 } } };
	static const AuthoredTiming kWait { { 0, kStandInDefaultFrames - 1 }, { { 0, 0 }, { 1, 1 } } };
	static const AuthoredTiming kFinite20 { { 0, 19 }, {} };
	static const AuthoredTiming kStillJump { { 0, 31 }, {} };
	static const AuthoredTiming kGetUp { { 0, 14 }, {} };
	static const AuthoredTiming kGakkari { { 0, 59 }, {} };
	switch (info) {
	case kAnimInfoKuttuku:
	case kAnimInfoJob2: return &kHitCycle;
	case kAnimInfoPickLoop: return &kPickLoop;
	case kAnimInfoKizuku: return &kKizuku;
	case kAnimInfoAttack: return &kAttack;
	case kAnimInfoWait: return &kWait;
	case kAnimInfoJHit:
	case kAnimInfoJKoke: return &kFinite20;
	case kAnimInfoStillJump: return &kStillJump;
	case kAnimInfoGetUp: return &kGetUp;
	case kAnimInfoGakkari: return &kGakkari;
	}
	return nullptr;
}

StandInAnimSet buildPikiSet()
{
	StandInAnimSet s;
	s.tablePath  = "dataDir/pikis/animMgr.bin";
	s.bundlePath = "dataDir/pikis/bluModel.anm";
	// AnimMgr 'a01': the base dir string only has to match the bundle entry names,
	// which this file chooses.
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
			// Flags 5 (constant speed) at 30 frames a second: without UseDynamicSpeed the
			// throw plays at the authored rate while the captain moves, not at his walk speed.
			a.frameCount     = kThrowWaitFrames;
			a.info.flags     = 5;
			a.info.speed     = 30.0f;
			a.info.animKeys  = { 0, kThrowWaitGrabFrame, kThrowWaitLoopEnd, kThrowWaitFrames - 1 };
			a.info.infoKeys  = { actionKey(1), loopKey(1, 0), loopKey(2, 1) }; // grab and loop start at key 1, loop end at key 2
		} else if (i == kAnimInfoThrow) {
			// NaviThrowState calls throwPiki on this key; KEY_Finished ends the state.
			a.frameCount     = kThrowFrames;
			a.info.flags     = 5;
			a.info.speed     = 30.0f;
			a.info.animKeys  = { 0, kThrowReleaseFrame, kThrowFrames - 1 };
			a.info.infoKeys  = { actionKey(1) };
		} else if (const AuthoredTiming* t = authoredTiming(i)) {
			// Authored schedule (standin_anims.h): anim keys, then info keys as (anim-key
			// index, type) with 0 loop start, 1 loop end, 2 KEY_Action0, 3 KEY_Action1.
			a.frameCount    = t->animKeys.back() + 1;
			a.scheduled     = true;
			a.info.flags    = 5;
			a.info.speed    = 30.0f;
			a.info.animKeys = t->animKeys;
			for (const auto& [key, type] : t->infoKeys) {
				AnimKeyRecord k;
				k.frame   = key;
				k.keyType = std::uint8_t(type);
				a.info.infoKeys.push_back(k);
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
			// Authored (standin_anims.h): loop start at anim key 1, loop end at anim key 2.
			a.frameCount    = kFueFrames;
			a.scheduled     = true;
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
		if (!a.scheduled && a.frameCount < kStandInMinForcedCounterFrames) {
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
			if (!a.info.flags || *a.info.flags != 5) return fail(i, "needs the authored constant speed (flags 5)");
			if (a.info.speed && *a.info.speed != 30.0f) return fail(i, "speed is not the authored 30");
			const int want = i == kAnimInfoThrowWait ? kThrowWaitGrabFrame : kThrowReleaseFrame;
			int actions    = 0;
			for (const AnimKeyRecord& k : a.info.infoKeys) {
				if (k.keyType != 2) continue;
				actions++;
				if (a.info.animKeys[std::size_t(k.frame)] != want) return fail(i, "KEY_Action0 is not at the authored frame");
				if (want >= a.frameCount) return fail(i, "KEY_Action0 frame is not below the frame count");
			}
			if (actions != 1) return fail(i, "needs exactly one KEY_Action0 info key");
		}
		if (i == kAnimInfoThrowWait) {
			// NaviThrowWaitState charges one level per KEY_LoopEnd: without it a held throw never charges.
			int starts = 0, ends = 0;
			for (const AnimKeyRecord& k : a.info.infoKeys) {
				const std::int32_t f = a.info.animKeys[std::size_t(k.frame)];
				if (k.keyType == 0 && f == kThrowWaitGrabFrame) starts++;
				if (k.keyType == 1 && f == kThrowWaitLoopEnd) ends++;
			}
			if (starts != 1 || ends != 1) return fail(i, "needs one KEY_LoopStart at the grab frame and one KEY_LoopEnd at the authored loop end");
		}
		if (i == kAnimInfoFue) {
			// NaviGatherState and NaviReleaseState wait on KEY_LoopStart: without it the whistle never grows and X never disbands.
			int starts = 0, ends = 0;
			for (const AnimKeyRecord& k : a.info.infoKeys) {
				const std::int32_t f = a.info.animKeys[std::size_t(k.frame)];
				if (k.keyType == 0 && f == kFueLoopStart) starts++;
				if (k.keyType == 1 && f == kFueLoopEnd) ends++;
			}
			if (starts != 1 || ends != 1) return fail(i, "needs one KEY_LoopStart at the authored frame and one KEY_LoopEnd after it");
		}
		if (i == kAnimInfoWait) {
			// Wait loops: a one-shot ends in KEY_Finished, which PikiLookAtState takes as Kizuku's end (#145).
			int starts = 0, ends = 0;
			for (const AnimKeyRecord& k : a.info.infoKeys) {
				if (k.keyType == 0 && k.frame == 0) starts++;
				if (k.keyType == 1 && k.frame == std::int32_t(a.info.animKeys.size()) - 1) ends++;
			}
			if (starts != 1 || ends != 1) return fail(i, "needs the loop: KEY_LoopStart at the first anim key, KEY_LoopEnd at the last");
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
