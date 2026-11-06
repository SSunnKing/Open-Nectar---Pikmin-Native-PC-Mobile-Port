// Write-then-read tests for the DCA/DCK payload writers, the AnimMgr table
// writer and the stand-in animations (#66).
//
// Oracles, per #62 (the game's loaders live in shapeBase.cpp and animMgr.cpp,
// which do not link into a host test: they need Shape, Graphics, CoreNode and a
// live StdSystem):
//  1. The game's real types and streams. DataChunk::read, AnimParam, AnimKey,
//     AnimInfo::Parms and AnimMgr::Parms come from Animator.h; the bytes go
//     through the real RamStream, and the parameter blocks through the real
//     Parameters::read (src/plugPikiColin/parameters.cpp, with the stubs of
//     tests/game_loader_stubs.cpp).
//  2. Transcribed loops, same calls in the same order: AnimDca::read and
//     AnimDck::read with checkMask (shapeBase.cpp:1235-1285, 1465-1520,
//     1181-1226), AnimMgr::read and AnimInfo::doread (animMgr.cpp:229-297,
//     452-466), the entry loop of StdSystem::loadBundle (stdSystem.cpp:631-714)
//     and PaniAnimator::animate / checkConstantKeys / checkConstantKey
//     (panianimator.cpp:191-295) for key timing. If those change, update the
//     copies here. The weakest part: the transcription is mine, so a mistake in
//     it would pass a writer that has the same mistake. The strongest check is
//     boot_smoke on the generated tree (#68), which runs the real loaders.
//  3. nectar::readBundle (#67) and the stand-in model specs (#65) for the
//     bundle entry names and the joint count and parents.
//
// Usage: nectar_anim_writer_test [boot-manifest.txt]. With the manifest, the
// stand-in file paths are checked against the disc-run list from #64.
#include "nectar_assets/anim.h"
#include "nectar_assets/bundle.h"
#include "nectar_assets/piki_rig.h"
#include "nectar_assets/standin_anims.h"
#include "nectar_assets/standin_models.h"

#include "Animator.h"
#include "Common/String.h"
#include "Parameters.h"
#include "Stream.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {
int failures = 0;

void check(bool ok, const std::string& what)
{
	if (!ok) {
		std::printf("FAIL: %s\n", what.c_str());
		failures++;
	}
}

using Bytes = std::vector<std::uint8_t>;

RamStream streamOf(const Bytes& b) { return RamStream(const_cast<std::uint8_t*>(b.data()), int(b.size())); }

// ------------------------------------------------------------------ DCA / DCK reader

struct LoadedAnim {
	bool dck = false;
	int jointCount = 0, frames = 0;
	DataChunk scale, rotate, translate;
	std::vector<AnimDataInfo> info;
	std::vector<int> group, parent;
	int activeJoints = 0;
	bool ok          = true;
};

// AnimDca::read / AnimDck::read followed by checkMask (shapeBase.cpp). The
// payload has no header: the stream starts at the joint count.
LoadedAnim readAnim(RamStream& in, bool dck)
{
	LoadedAnim a;
	a.dck         = dck;
	a.jointCount  = in.readInt();
	a.frames      = in.readInt();
	if (a.jointCount < 0 || a.jointCount > 4096) {
		a.ok = false;
		return a;
	}
	a.scale.read(in);
	a.rotate.read(in);
	a.translate.read(in);
	a.info.resize(std::size_t(a.jointCount));
	for (int i = 0; i < a.jointCount; i++) {
		a.group.push_back(in.readInt());
		a.parent.push_back(in.readInt());
		AnimParam* sets[3] = { a.info[std::size_t(i)].mScale, a.info[std::size_t(i)].mRotation, a.info[std::size_t(i)].mTranslation };
		for (AnimParam* set : sets) {
			for (int j = 0; j < 3; j++) {
				set[j].mEntryNum   = in.readInt();
				set[j].mDataOffset = in.readInt();
				set[j].mFlags      = dck ? in.readInt() : 0;
			}
		}
	}
	// checkMask: a component is static if it has exactly one entry; a joint is
	// static if all nine are.
	for (int i = 0; i < a.jointCount; i++) {
		AnimDataInfo& d = a.info[std::size_t(i)];
		d.mFlags        = 0;
		int bit         = 0;
		AnimParam* sets[3] = { d.mScale, d.mRotation, d.mTranslation };
		for (AnimParam* set : sets) {
			for (int j = 0; j < 3; j++, bit++) {
				if (set[j].mEntryNum == 1) d.mFlags |= (1 << bit);
			}
			bit++;
		}
		if ((d.mFlags & AnimDataFlags::AllComponentsStatic) != AnimDataFlags::AllComponentsStatic) a.activeJoints++;
	}
	return a;
}

// AnimDck::makeAnimSRT's switch on mEntryNum for the cases a static stand-in
// uses (shapeBase.cpp:1735-1840): 0 entries is the default, 1 is the one value.
// The defaults are scale 1, rotation 0 and, as written in the game, translation 1.
float evalStatic(const AnimParam& p, const DataChunk& data, float defaultValue)
{
	if (p.mEntryNum == 0) return defaultValue;
	if (p.mEntryNum == 1) return data.mData[p.mDataOffset];
	return std::nanf("");
}

// ------------------------------------------------------------------ AnimMgr reader

struct LoadedInfo {
	std::string name;
	int flags = 0;
	float speed = 0;
	std::vector<int> animKeys;
	std::vector<AnimKey> infoKeys, eventKeys;
};

struct LoadedMgr {
	int version = 0, flags = 0;
	std::string baseDir;
	std::vector<LoadedInfo> infos;
	int pending = -1;
};

// AnimMgr::read and AnimInfo::doread (animMgr.cpp:229-297, 452-466), with the real
// Parameters blocks. AnimInfo's key lists are intrusive; the values land in vectors.
LoadedMgr readAnimMgr(const Bytes& bytes)
{
	LoadedMgr m;
	RamStream in = streamOf(bytes);
	m.version    = in.readInt();
	int infoCount = in.readInt();
	AnimMgr::Parms mgrParms;
	mgrParms.read(in);
	m.flags   = mgrParms.mFlags();
	m.baseDir = mgrParms.mBasePath().mString;
	for (int i = 0; i < infoCount; i++) {
		LoadedInfo info;
		String str(0);
		in.readString(str);
		info.name = str.mString;
		AnimInfo::Parms parms;
		parms.read(in);
		info.flags = parms.mFlags();
		info.speed = parms.mSpeed();
		int animKeyNum = in.readInt();
		for (int k = 0; k < animKeyNum; k++) info.animKeys.push_back(in.readInt());
		if (m.version >= 1) {
			int infoKeyNum = in.readInt();
			for (int k = 0; k < infoKeyNum; k++) {
				AnimKey key;
				key.mFrameIndex = in.readInt();
				key.mEventType  = in.readShort();
				key.mEventCmdID = in.readByte();
				key.mKeyType    = in.readByte();
				info.infoKeys.push_back(key);
			}
		}
		int eventKeyNum = in.readInt();
		for (int k = 0; k < eventKeyNum; k++) {
			AnimKey key;
			key.mFrameIndex = in.readInt();
			key.mEventType  = in.readShort();
			key.mEventCmdID = in.readByte();
			key.mKeyType    = in.readByte();
			if (key.mEventType >= ANIMEVENT_COUNT) key.mEventType = ANIMEVENT_Notify; // the loader's silent rewrite
			info.eventKeys.push_back(key);
		}
		m.infos.push_back(std::move(info));
	}
	m.pending = in.getPending();
	return m;
}

// ------------------------------------------------------------------ bundle loop

struct LoadedEntry {
	int type, size, payloadAt, consumedEnd;
	std::string name;
	LoadedAnim anim;
};

// The entry loop of StdSystem::loadBundle: types 2 and 3 hand the stream to a
// nested reader that consumes exactly its own bytes and ignores `size`.
std::vector<LoadedEntry> loadBundleLoop(const Bytes& bytes, int& pendingAfter)
{
	std::vector<LoadedEntry> out;
	RamStream fs = streamOf(bytes);
	int fileCount = fs.readInt();
	for (int i = 0; i < fileCount; i++) {
		LoadedEntry e;
		e.type = fs.readInt();
		e.size = fs.readInt();
		String path(0);
		fs.readString(path);
		e.name      = path.mString;
		e.payloadAt = fs.getPosition();
		if (e.type == 2 || e.type == 3) {
			e.anim = readAnim(fs, e.type == 3);
		} else {
			fs.setPosition(fs.getPosition() + e.size);
		}
		e.consumedEnd = fs.getPosition();
		out.push_back(std::move(e));
	}
	pendingAfter = fs.getPending();
	return out;
}

// ------------------------------------------------------------------ key timing

struct KeyTimeline {
	int actionTick = -1;   // first tick on which KEY_Action0 reaches the listener
	int finishedTick = -1; // tick on which KEY_Finished does
};

// PaniAnimator::startMotion / animate / checkConstantKeys / checkConstantKey
// (panianimator.cpp:151-295) for a one-shot: start key 0, end key clamped to the
// last, counter += speed * dt per tick. `dt` is the sim step, 1/30 or 1/60.
KeyTimeline runMotion(const LoadedInfo& info, float speed, float dt, int maxTicks)
{
	KeyTimeline t;
	auto keyValue = [&](int idx) { return float(info.animKeys[std::size_t(idx)]); };
	const float endValue = keyValue(int(info.animKeys.size()) - 1);
	float counter        = keyValue(0);
	int current          = 0;
	for (int tick = 1; tick <= maxTicks && current >= 0; tick++) {
		counter += speed * dt;
		for (; current < int(info.infoKeys.size()); current++) {
			if (counter < keyValue(info.infoKeys[std::size_t(current)].mFrameIndex)) break;
			const int type = info.infoKeys[std::size_t(current)].mKeyType;
			if (type >= 2 && type <= 5 && type - 1 == KEY_Action0 && t.actionTick < 0) t.actionTick = tick;
		}
		if (counter >= endValue) {
			t.finishedTick = tick;
			current        = -1;
		}
	}
	return t;
}

// ------------------------------------------------------------------ tests

nectar::AnimJointTrack staticJoint(int index, int parent)
{
	nectar::AnimJointTrack t;
	t.group  = index;
	t.parent = parent;
	for (int i = 0; i < 3; i++) t.scale[i] = t.rotate[i] = t.translate[i] = { 1, 0, 0 };
	return t;
}

void testStaticDck()
{
	std::vector<int> parents(nectar::kPikiRigParents, nectar::kPikiRigParents + nectar::kPikiRigJointCount);
	nectar::AnimPayload in = nectar::makeStaticAnim(parents, 21);
	Bytes bytes;
	std::string err;
	check(nectar::writeDckPayload(in, bytes, err), "write static DCK: " + err);
	// joint count, frames, 3 chunks of (count + 1 float), 12 joints of group, parent and 9 params of 3 ints
	check(bytes.size() == 4 + 4 + 3 * 8 + 12 * (8 + 9 * 12), "DCK size is exactly what the reader consumes");

	RamStream in2 = streamOf(bytes);
	LoadedAnim a  = readAnim(in2, true);
	check(in2.getPending() == 0, "AnimDck::read consumes every byte (self-delimiting payload)");
	check(a.jointCount == 12 && a.frames == 21, "joint count 12, frame count 21");
	check(a.scale.mDataSize == 1 && a.rotate.mDataSize == 1 && a.translate.mDataSize == 1, "one float per data chunk");
	for (int j = 0; j < 12; j++) {
		check(a.parent[std::size_t(j)] == parents[std::size_t(j)], "parent of joint " + std::to_string(j));
		check(a.group[std::size_t(j)] == j, "group index is the joint index");
		const AnimDataInfo& d = a.info[std::size_t(j)];
		for (int i = 0; i < 3; i++) {
			check(evalStatic(d.mScale[i], a.scale, 1.0f) == 1.0f, "static scale is 1");
			check(evalStatic(d.mRotation[i], a.rotate, 0.0f) == 0.0f, "static rotation is 0");
			// entryNum 1 with value 0.0, not entryNum 0, which would default the translation to 1.0
			check(d.mTranslation[i].mEntryNum == 1 && evalStatic(d.mTranslation[i], a.translate, 1.0f) == 0.0f, "static translation is 0");
			check(d.mScale[i].mFlags == 0 && d.mTranslation[i].mFlags == 0, "flags 0 (3 floats per key)");
		}
	}
	check(a.activeJoints == 0, "checkMask: every joint is static");

	// A bundle entry of this payload: the loader ignores `size`, so the size the
	// writer records must equal what the reader consumed.
	std::vector<nectar::BundleEntry> entries(2);
	entries[0].type = nectar::BUNDLE_DCK;
	entries[0].name = "pikis/motion/a.dck";
	entries[0].payload = bytes;
	entries[1].type = nectar::BUNDLE_DCK;
	entries[1].name = "pikis/motion/b.dck";
	entries[1].payload = bytes;
	Bytes bundle;
	check(nectar::writeBundle(entries, bundle, err), "bundle of two DCK entries");
	int pendingAfter = -1;
	std::vector<LoadedEntry> loaded = loadBundleLoop(bundle, pendingAfter);
	check(loaded.size() == 2 && pendingAfter == 0, "loadBundle loop reads both entries and ends at the end of the file");
	for (const LoadedEntry& e : loaded) {
		check(e.consumedEnd - e.payloadAt == e.size, e.name + ": the reader consumed exactly `size` bytes");
		check(e.anim.ok && e.anim.jointCount == 12, e.name + ": second entry starts where the first one ends");
	}
}

void testKeyedDckAndDca()
{
	std::string err;
	// DCK: translation x has 3 keys of 3 floats, rotation y has 2 keys of 4 floats.
	nectar::AnimPayload k;
	k.frameCount = 30;
	k.scaleData  = { 1.0f };
	k.rotateData = { 0.0f, 0, 0, 0, 29.0f, 1.5f, 0, 0, 9.0f }; // y keys with flags 1 start at offset 1: (t, v, ?, tangent) x 2
	k.translateData = { 0.0f, 1.0f, 0.5f, 10.0f, 2.0f, 0.5f, 29.0f, 3.0f, 0.5f };
	nectar::AnimJointTrack j = staticJoint(0, -1);
	j.translate[0]           = { 3, 0, 0 };
	j.rotate[1]              = { 2, 1, 1 };
	k.joints.push_back(j);
	Bytes bytes;
	check(nectar::writeDckPayload(k, bytes, err), "write keyed DCK: " + err);
	RamStream in = streamOf(bytes);
	LoadedAnim a = readAnim(in, true);
	check(in.getPending() == 0 && a.jointCount == 1 && a.frames == 30, "keyed DCK reads back");
	check(a.info[0].mTranslation[0].mEntryNum == 3 && a.info[0].mTranslation[0].mDataOffset == 0 && a.info[0].mTranslation[0].mFlags == 0, "translation x channel");
	check(a.info[0].mRotation[1].mEntryNum == 2 && a.info[0].mRotation[1].mDataOffset == 1 && a.info[0].mRotation[1].mFlags == 1, "rotation y channel with 4-float keys");
	check(a.translate.mDataSize == 9 && a.translate.mData[3] == 10.0f && a.translate.mData[7] == 3.0f, "translation chunk floats");
	check(a.activeJoints == 1, "checkMask: a joint with keyed channels is active");

	// DCA: 2-int params, baked samples, no flags.
	nectar::AnimPayload d;
	d.frameCount    = 4;
	d.scaleData     = { 1, 1, 1, 1 };
	d.rotateData    = { 0, 0.1f, 0.2f, 0.3f };
	d.translateData = { 5, 6, 7, 8 };
	nectar::AnimJointTrack dj = staticJoint(0, -1);
	for (int i = 0; i < 3; i++) dj.scale[i] = dj.rotate[i] = dj.translate[i] = { 4, 0, 0 };
	d.joints.push_back(dj);
	nectar::AnimJointTrack dj2 = staticJoint(1, 0);
	dj2.translate[2]           = { 1, 3, 0 };
	for (int i = 0; i < 3; i++) dj2.scale[i] = dj2.rotate[i] = { 1, 0, 0 };
	dj2.translate[0] = dj2.translate[1] = { 1, 0, 0 };
	d.joints.push_back(dj2);
	check(nectar::writeDcaPayload(d, bytes, err), "write DCA: " + err);
	check(bytes.size() == 4 + 4 + 3 * (4 + 16) + 2 * (8 + 9 * 8), "DCA size: 2 ints per param");
	in = streamOf(bytes);
	a  = readAnim(in, false);
	check(in.getPending() == 0 && a.jointCount == 2 && a.frames == 4, "DCA reads back");
	check(a.info[0].mTranslation[1].mEntryNum == 4 && a.translate.mData[2] == 7.0f && a.info[1].mTranslation[2].mDataOffset == 3, "DCA samples and offsets");
	check(a.parent[1] == 0, "DCA parent");
	check(a.activeJoints == 1, "DCA: joint 0 has 4 samples (active), joint 1 is single-entry (static)");
}

void testPayloadRejects()
{
	std::string err;
	Bytes out;
	std::vector<int> parents = { -1, 0 };
	auto good                = [&]() { return nectar::makeStaticAnim(parents, 5); };

	check(nectar::writeDckPayload(good(), out, err) && nectar::writeDcaPayload(good(), out, err), "the static anim is valid as DCK and DCA");

	nectar::AnimPayload a = good();
	a.frameCount          = 0;
	check(!nectar::writeDckPayload(a, out, err), "zero frames rejected");
	a = good();
	a.joints.clear();
	check(!nectar::writeDckPayload(a, out, err), "no joints rejected");
	a                 = good();
	a.joints[0].parent = 1; // a parent after its child
	check(!nectar::writeDckPayload(a, out, err), "parent that is not an earlier joint rejected");
	a                  = good();
	a.joints[1].parent = -2;
	check(!nectar::writeDckPayload(a, out, err), "parent below -1 rejected");
	a                          = good();
	a.joints[1].translate[0]   = { 1, 1, 0 }; // static value one past the 1-float chunk
	check(!nectar::writeDckPayload(a, out, err), "DCK static value past the data block rejected");
	a                        = good();
	a.joints[1].scale[2]     = { 2, 0, 0 }; // 2 keys of 3 floats from a 1-float chunk
	check(!nectar::writeDckPayload(a, out, err), "DCK keys past the data block rejected");
	a                    = good();
	a.scaleData          = { 0, 1, 1, 0, 2, 1 }; // 2 keys of 3 fit exactly
	a.joints[1].scale[2] = { 2, 0, 0 };
	check(nectar::writeDckPayload(a, out, err), "DCK keys that fit exactly are accepted");
	a.joints[1].scale[2] = { 2, 0, 1 }; // flags 1 means 4 floats per key: 8 > 6
	check(!nectar::writeDckPayload(a, out, err), "DCK 4-float keys past the data block rejected");
	a                       = good();
	a.joints[1].rotate[0]   = { -1, 0, 0 };
	check(!nectar::writeDckPayload(a, out, err), "negative entry count rejected");
	a                      = good();
	a.joints[1].rotate[0]  = { 0, 0, 0 };
	check(nectar::writeDckPayload(a, out, err) && !nectar::writeDcaPayload(a, out, err), "entryNum 0 is a DCK default but not a DCA sample");
	a                       = good();
	a.joints[1].translate[1] = { 1, 0, 3 };
	check(nectar::writeDckPayload(a, out, err) && !nectar::writeDcaPayload(a, out, err), "DCA channel with flags rejected (no field for them)");
}

void testAnimMgrTable()
{
	std::string err;
	nectar::AnimMgrTable t;
	t.baseDir = "pikis/motion";
	t.flags   = 2;
	nectar::AnimInfoSpec a;
	a.name     = "one.dck";
	a.animKeys = { 0, 5, 19 };
	nectar::AnimInfoSpec b;
	b.name      = "two.dck";
	b.flags     = 5;
	b.speed     = 24.5f;
	b.animKeys  = { 0, 3, 10, 12 };
	// cmdId 7 and keyType 3 differ so a swapped order shows: the reader takes
	// event type (short), command id (byte), key type (byte).
	b.infoKeys  = { { 1, 0, 7, 2 }, { 1, 0, 0, 0 }, { 2, 0, 0, 3 }, { 3, 0, 0, 1 } };
	b.eventKeys = { { 9, 1, 4, 6 } };
	t.infos     = { a, b };
	Bytes bytes;
	check(nectar::writeAnimMgrTable(t, bytes, err), "write table: " + err);

	LoadedMgr m = readAnimMgr(bytes);
	check(m.pending == 0, "AnimMgr::read consumes every byte");
	check(m.version == 1 && m.infos.size() == 2, "version 1, two infos");
	check(m.baseDir == "pikis/motion" && m.flags == 2, "a00 and a01 read back through Parameters::read");
	check(m.infos[0].name == "one.dck" && m.infos[0].flags == 2 && m.infos[0].speed == 30.0f, "an empty parameter block keeps the code defaults (p00 2, spd 30)");
	check(m.infos[0].animKeys == std::vector<int>({ 0, 5, 19 }) && m.infos[0].infoKeys.empty() && m.infos[0].eventKeys.empty(), "info 0 keys");
	check(m.infos[1].flags == 5 && m.infos[1].speed == 24.5f, "explicit p00 and spd");
	check(m.infos[1].animKeys == std::vector<int>({ 0, 3, 10, 12 }), "info 1 anim keys");
	check(m.infos[1].infoKeys.size() == 4 && m.infos[1].infoKeys[0].mFrameIndex == 1 && m.infos[1].infoKeys[0].mEventCmdID == 7 && m.infos[1].infoKeys[0].mKeyType == 2
	          && m.infos[1].infoKeys[2].mKeyType == 3 && m.infos[1].infoKeys[3].mFrameIndex == 3 && m.infos[1].infoKeys[3].mKeyType == 1,
	      "info keys: frame, command id and key type land in the right fields");
	check(m.infos[1].eventKeys.size() == 1 && m.infos[1].eventKeys[0].mFrameIndex == 9 && m.infos[1].eventKeys[0].mEventType == 1 && m.infos[1].eventKeys[0].mEventCmdID == 4
	          && m.infos[1].eventKeys[0].mKeyType == 6,
	      "event key fields");

	// An absent a00 keeps the default 2.
	t.flags.reset();
	check(nectar::writeAnimMgrTable(t, bytes, err), "write table without a00");
	m = readAnimMgr(bytes);
	check(m.pending == 0 && m.flags == 2 && m.baseDir == "pikis/motion", "absent a00 keeps the default");

	// The record layout is fixed: spot-check the first bytes (version, count, 'a01' header).
	check(bytes.size() > 16 && bytes[3] == 1 && bytes[7] == 2 && bytes[8] == 'a' && bytes[9] == '0' && bytes[10] == '1' && bytes[11] == 16, "version, count and the a01 header bytes");

	// Rejects.
	auto bad = [&](const char* what, auto mutate) {
		nectar::AnimMgrTable x = t;
		mutate(x);
		Bytes o;
		check(!nectar::writeAnimMgrTable(x, o, err), what);
	};
	bad("empty base dir rejected", [](nectar::AnimMgrTable& x) { x.baseDir.clear(); });
	bad("empty info name rejected", [](nectar::AnimMgrTable& x) { x.infos[0].name.clear(); });
	bad("single anim key rejected", [](nectar::AnimMgrTable& x) { x.infos[0].animKeys = { 0 }; });
	bad("descending anim keys rejected", [](nectar::AnimMgrTable& x) { x.infos[0].animKeys = { 0, 9, 4 }; });
	bad("negative anim key rejected", [](nectar::AnimMgrTable& x) { x.infos[0].animKeys = { -1, 4 }; });
	bad("info key past the anim-key list rejected", [](nectar::AnimMgrTable& x) { x.infos[0].infoKeys = { { 3, 0, 0, 2 } }; });
	bad("info keys going backwards rejected", [](nectar::AnimMgrTable& x) { x.infos[1].infoKeys = { { 2, 0, 0, 3 }, { 1, 0, 0, 2 } }; });
	bad("17 info keys rejected", [](nectar::AnimMgrTable& x) { x.infos[0].infoKeys.assign(17, { 1, 0, 0, 2 }); });
	bad("key type 6 rejected", [](nectar::AnimMgrTable& x) { x.infos[0].infoKeys = { { 1, 0, 0, 6 } }; });
	bad("event type 3 rejected (the loader would clamp it)", [](nectar::AnimMgrTable& x) { x.infos[0].eventKeys = { { 4, 3, 0, 0 } }; });
	bad("non-finite speed rejected", [](nectar::AnimMgrTable& x) { x.infos[0].speed = std::nanf(""); });
	{
		nectar::AnimMgrTable x = t;
		x.infos[0].infoKeys    = { { 1, 0, 0, 2 }, { 1, 0, 0, 0 }, { 2, 0, 0, 3 }, { 2, 0, 0, 1 } }; // equal frames in a row are fine (disc ThrowWait does this)
		Bytes o;
		check(nectar::writeAnimMgrTable(x, o, err), "info keys at equal frames accepted");
	}
}

// ThrowWait calibration: #61's throw_release measured, on the disc install,
// K = 13 ticks at 30 Hz and 14 at 60 Hz between pressing A and the grab
// completing. The animation contributes ceil(K_frame / (30 * dt)) ticks and the
// rest is the same at both rates (the Pikmin reaching the hand): 1 + 12 and 2 + 12.
constexpr int kMeasuredTicks30 = 13;
constexpr int kMeasuredTicks60 = 14;

void testStandInSet()
{
	const nectar::StandInAnimSet& set = nectar::standInPikiAnims();
	std::string err;
	check(nectar::checkStandInAnims(set, err), "the stand-in set passes the generator assertions: " + err);

	// Each assertion fires on its own mutation.
	auto breaks = [&](const char* what, auto mutate) {
		nectar::StandInAnimSet s = set;
		mutate(s);
		std::string e;
		check(!nectar::checkStandInAnims(s, e), what);
	};
	breaks("K at or above N", [](nectar::StandInAnimSet& s) {
		s.anims[nectar::kAnimInfoThrowWait].info.animKeys = { 0, nectar::kThrowWaitFrames, nectar::kThrowWaitFrames };
	});
	breaks("an anim key at N", [](nectar::StandInAnimSet& s) { s.anims[7].info.animKeys = { 0, nectar::kStandInDefaultFrames }; });
	breaks("N below 1", [](nectar::StandInAnimSet& s) { s.anims[nectar::kAnimInfoThrow].frameCount = 0; });
	breaks("N too short for the forced counters", [](nectar::StandInAnimSet& s) {
		s.anims[9].frameCount = 20;
		s.anims[9].info.animKeys = { 0, 19 };
	});
	breaks("Nigeru shorter than 11", [](nectar::StandInAnimSet& s) {
		s.anims[nectar::kAnimInfoNigeru].measured = true;
		s.anims[nectar::kAnimInfoNigeru].frameCount = 10;
		s.anims[nectar::kAnimInfoNigeru].info.animKeys = { 0, 9 };
	});
	breaks("no KEY_Action0 on ThrowWait", [](nectar::StandInAnimSet& s) { s.anims[nectar::kAnimInfoThrowWait].info.infoKeys.clear(); });
	breaks("no KEY_Action0 on Throw", [](nectar::StandInAnimSet& s) { s.anims[nectar::kAnimInfoThrow].info.infoKeys.clear(); });
	breaks("KEY_Action0 at the wrong frame", [](nectar::StandInAnimSet& s) { s.anims[nectar::kAnimInfoThrowWait].info.infoKeys[0].frame = 2; });
	breaks("two KEY_Action0 on ThrowWait", [](nectar::StandInAnimSet& s) {
		s.anims[nectar::kAnimInfoThrowWait].info.infoKeys.push_back(s.anims[nectar::kAnimInfoThrowWait].info.infoKeys[0]);
	});
	breaks("no KEY_LoopStart on Fue", [](nectar::StandInAnimSet& s) { s.anims[nectar::kAnimInfoFue].info.infoKeys.clear(); });
	breaks("Wait does not loop", [](nectar::StandInAnimSet& s) { s.anims[nectar::kAnimInfoWait].info.infoKeys.clear(); });
	breaks("Fue's KEY_LoopStart at the wrong frame", [](nectar::StandInAnimSet& s) { s.anims[nectar::kAnimInfoFue].info.infoKeys[0].frame = 2; });
	check(set.anims[nectar::kAnimInfoFue].frameCount == nectar::kFueFrames
	          && set.anims[nectar::kAnimInfoFue].info.animKeys == std::vector<std::int32_t>({ 0, nectar::kFueLoopStart, nectar::kFueLoopEnd, nectar::kFueFrames - 1 })
	          && set.anims[nectar::kAnimInfoFue].info.eventKeys.empty(),
	      "Fue has the disc's 20 frames, anim keys [0, 1, 3, 19] and no event keys");
	breaks("info key past the anim-key list", [](nectar::StandInAnimSet& s) { s.anims[3].info.infoKeys = { { 2, 0, 0, 2 } }; });
	breaks("17 info keys", [](nectar::StandInAnimSet& s) { s.anims[3].info.infoKeys.assign(17, { 1, 0, 0, 2 }); s.anims[3].info.animKeys = { 0, 5, 39 }; });
	breaks("duplicate names", [](nectar::StandInAnimSet& s) { s.anims[8].info.name = s.anims[9].info.name; });
	breaks("a missing info", [](nectar::StandInAnimSet& s) { s.anims.pop_back(); });
	breaks("joint count differs from the rig", [](nectar::StandInAnimSet& s) { s.parents.pop_back(); });
	breaks("a parent differs from the rig", [](nectar::StandInAnimSet& s) { s.parents[5] = 1; });

	// Write and read back.
	std::vector<nectar::StandInAnimFile> files;
	check(nectar::writeStandInAnimations(files, err), "write the stand-in animations: " + err);
	check(files.size() == 2 && files[0].path == "dataDir/pikis/animMgr.bin" && files[1].path == "dataDir/pikis/bluModel.anm", "two files at the paths the game opens");
	if (files.size() != 2) return;

	LoadedMgr m = readAnimMgr(files[0].bytes);
	check(m.pending == 0 && m.version == 1, "animMgr.bin: AnimMgr::read consumes every byte");
	check(int(m.infos.size()) == nectar::kStandInAnimCount, "animMgr.bin has infos 0 to 99");
	check(m.baseDir == set.baseDir && m.flags == 2, "base directory and a00");

	int pendingAfter = -1;
	std::vector<LoadedEntry> loaded = loadBundleLoop(files[1].bytes, pendingAfter);
	check(pendingAfter == 0 && int(loaded.size()) == nectar::kStandInAnimCount, "bluModel.anm: 100 entries, loop ends at the end of the file");
	std::vector<nectar::BundleEntry> viaLib;
	check(nectar::readBundle(files[1].bytes.data(), files[1].bytes.size(), viaLib, err) && viaLib.size() == loaded.size(), "nectar::readBundle agrees on the entry count: " + err);

	// The standard rig: every shape that shares the AnimMgr.
	const char* shapes[] = { "dataDir/pikis/bluModel.mod", "dataDir/pikis/redModel.mod", "dataDir/pikis/yelModel.mod", "dataDir/pikis/kinModel.mod", "dataDir/pikis/nv3Model.mod" };
	for (const char* path : shapes) {
		const nectar::StandInModelSpec* spec = nectar::findStandInModel(path);
		check(spec != nullptr && spec->jointCount() == nectar::kPikiRigJointCount, std::string(path) + ": 12 joints");
		check(spec != nullptr && spec->parents == set.parents, std::string(path) + ": parents match the animations'");
	}

	std::set<std::string> names;
	for (int i = 0; i < nectar::kStandInAnimCount && i < int(loaded.size()) && i < int(m.infos.size()); i++) {
		const LoadedInfo& info = m.infos[std::size_t(i)];
		// AnimMgr::loadAnims: sprintf("%s/%s", basePath, info->mName), then an exact strcmp in the bundle.
		const std::string want = m.baseDir + "/" + info.name;
		const LoadedEntry* hit = nullptr;
		for (const LoadedEntry& e : loaded) {
			if (std::strcmp(e.name.c_str(), want.c_str()) == 0) {
				hit = &e;
				break;
			}
		}
		const std::string label = "info " + std::to_string(i) + " (" + info.name + ")";
		check(hit != nullptr, label + ": bundle entry " + want + " exists (a miss is a Null Anim with zero frames)");
		check(names.insert(info.name).second, label + ": unique name");
		if (hit == nullptr) continue;
		// The scenario preconditions: data present, more than zero frames, joint count equal to the shape's.
		check(hit->type == nectar::BUNDLE_DCK && hit->anim.ok, label + ": a readable DCK");
		check(hit->anim.frames > 0 && hit->anim.frames == set.anims[std::size_t(i)].frameCount, label + ": totalFrameCount > 0 and equal to N");
		check(hit->anim.jointCount == nectar::kPikiRigJointCount, label + ": joint count equals the shape's");
		check(hit->consumedEnd - hit->payloadAt == hit->size, label + ": reader consumes exactly `size`");
		for (int j = 0; j < hit->anim.jointCount; j++) check(hit->anim.parent[std::size_t(j)] == set.parents[std::size_t(j)], label + ": joint parents");
		check(hit->anim.activeJoints == 0, label + ": all joints static");

		// AnimInfo::checkAnimData must not have anything to clamp, so K and the
		// other key frames really are the written ones.
		for (int f : info.animKeys) check(f >= 0 && f <= hit->anim.frames - 1, label + ": anim key within the frame count");
		check(info.animKeys.front() == 0 && info.animKeys.back() == hit->anim.frames - 1, label + ": anim keys span the animation");
		for (const AnimKey& k : info.infoKeys) check(k.mFrameIndex < int(info.animKeys.size()), label + ": info key points into the anim-key list");
		check(info.infoKeys.size() <= 16, label + ": at most 16 info keys");
		if (i == nectar::kAnimInfoNigeru) check(hit->anim.frames >= 11, label + ": long enough for the forced counter 10.0");
		// Measured infos have the disc's length (checkStandInAnims exempts them too).
		if (!set.anims[std::size_t(i)].measured) check(hit->anim.frames > 30, label + ": longer than the largest forced counter");
	}

	// ThrowWait and Throw: the type-2 info key and its frame.
	struct Want {
		int info, k, n;
		const char* label;
	};
	const Want wants[] = { { nectar::kAnimInfoThrowWait, nectar::kThrowWaitGrabFrame, nectar::kThrowWaitFrames, "ThrowWait (63)" },
		                   { nectar::kAnimInfoThrow, nectar::kThrowReleaseFrame, nectar::kThrowFrames, "Throw (54)" } };
	for (const Want& w : wants) {
		const LoadedInfo& info = m.infos[std::size_t(w.info)];
		int found = 0, kFrame = -1;
		for (const AnimKey& k : info.infoKeys) {
			if (k.mKeyType == 2) { // the existence check: a scan for mKeyType == 2, not getKeyValueByKeyType (0.0 when absent)
				found++;
				kFrame = info.animKeys[std::size_t(k.mFrameIndex)];
			}
		}
		check(found == 1 && kFrame == w.k, std::string(w.label) + ": one type-2 info key whose anim-key frame is K");
		check(w.k < w.n, std::string(w.label) + ": K < N");
		check(info.animKeys == std::vector<int>({ 0, w.k, w.n - 1 }), std::string(w.label) + ": anim keys [0, K, N-1]");
		const LoadedEntry& e = loaded[std::size_t(w.info)];
		check(e.anim.frames == w.n, std::string(w.label) + ": totalFrameCount == N");
		check(info.flags == 5 && info.speed == 30.0f, std::string(w.label) + ": disc flags 5 (constant speed) and speed 30");
	}

	// PickLoop (info 68): the disc's timing, read back as the game reads it. ActTransport lifts on its KEY_Action0.
	{
		const LoadedInfo& info = m.infos[std::size_t(nectar::kAnimInfoPickLoop)];
		int action0 = -1, action1 = -1, loopStart = -1, loopEnd = -1;
		for (const AnimKey& k : info.infoKeys) {
			const int frame = info.animKeys[std::size_t(k.mFrameIndex)];
			if (k.mKeyType == 2) action0 = frame;
			if (k.mKeyType == 3) action1 = frame;
			if (k.mKeyType == 0) loopStart = frame;
			if (k.mKeyType == 1) loopEnd = frame;
		}
		check(loaded[std::size_t(nectar::kAnimInfoPickLoop)].anim.frames == 30 && info.animKeys == std::vector<int>({ 0, 10, 13, 18, 20, 29 }),
		      "PickLoop (68): 30 frames, anim keys [0, 10, 13, 18, 20, 29] (the disc's)");
		check(action0 == 10 && loopStart == 13 && loopEnd == 18 && action1 == 20 && info.flags == 5 && info.speed == 30.0f,
		      "PickLoop (68): KEY_Action0 at 10, loop 13 to 18, KEY_Action1 at 20, flags 5, speed 30 (the disc's)");
	}

	// Key timing: PaniAnimator at speed 30 (navi->mMotionSpeed with throwSpeedPct 100).
	const float dt30 = 1.0f / 30.0f, dt60 = 1.0f / 60.0f;
	KeyTimeline w30 = runMotion(m.infos[nectar::kAnimInfoThrowWait], 30.0f, dt30, 100), w60 = runMotion(m.infos[nectar::kAnimInfoThrowWait], 30.0f, dt60, 200);
	KeyTimeline t30 = runMotion(m.infos[nectar::kAnimInfoThrow], 30.0f, dt30, 100), t60 = runMotion(m.infos[nectar::kAnimInfoThrow], 30.0f, dt60, 200);
	std::printf("ThrowWait KEY_Action0 at tick %d (30 Hz), %d (60 Hz); Throw KEY_Action0 at %d and %d, KEY_Finished at %d and %d\n", w30.actionTick, w60.actionTick, t30.actionTick,
	            t60.actionTick, t30.finishedTick, t60.finishedTick);
	check(w30.actionTick == nectar::kThrowWaitGrabFrame, "ThrowWait KEY_Action0 lands K ticks after the motion starts at 30 Hz");
	check(w60.actionTick == 2 * nectar::kThrowWaitGrabFrame || w60.actionTick == 2 * nectar::kThrowWaitGrabFrame + 1, "ThrowWait KEY_Action0 lands about 2K ticks after at 60 Hz (a float edge can add one)");
	check(t30.actionTick > 0 && t60.actionTick > 0, "Throw's KEY_Action0 fires (the Pikmin leaves the hand)");
	check(t30.finishedTick > t30.actionTick && t60.finishedTick > t60.actionTick, "Throw finishes after it (KEY_Finished ends the state)");
	check(w30.finishedTick > w30.actionTick, "ThrowWait is longer than K");
	// The measured calibration: the same fixed part (kMeasuredTicks30 - ticks) at both rates.
	const int fixed30 = kMeasuredTicks30 - w30.actionTick, fixed60 = kMeasuredTicks60 - w60.actionTick;
	check(fixed30 == fixed60, "the disc's 13 and 14 ticks (calibration K) differ by the animation's own ticks at 30 and 60 Hz: the same " + std::to_string(fixed30) + " fixed ticks both times");
}

void testManifest(const std::string& path)
{
	if (path.empty()) {
		std::printf("note: no manifest argument, skipping the boot-manifest cross-check\n");
		return;
	}
	std::ifstream in(path);
	check(bool(in), "boot manifest opens: " + path);
	std::set<std::string> listed;
	std::string line;
	while (std::getline(in, line)) {
		if (line.empty() || line[0] == '#') continue;
		listed.insert(line.substr(0, line.find('\t')));
	}
	for (const char* p : { "dataDir/pikis/animMgr.bin", "dataDir/pikis/bluModel.anm" }) check(listed.count(p) == 1, std::string(p) + ": in the boot manifest");
	// Other AnimMgrs and bundles in the manifest belong to other owners (#67 for the
	// object .anm bundles, #68 for the generator); reported, not written here.
	int others = 0;
	for (const std::string& p : listed) {
		const bool anim = p.size() > 4 && (p.compare(p.size() - 4, 4, ".anm") == 0 || p.compare(p.size() - 4, 4, ".dck") == 0 || p.compare(p.size() - 4, 4, ".dca") == 0);
		if (anim && p != "dataDir/pikis/bluModel.anm") others++;
	}
	std::printf("manifest: %zu entries, %d animation bundles or loose files besides the Pikmin one (not written by #66)\n", listed.size(), others);
}
} // namespace

int main(int argc, char** argv)
{
	testStaticDck();
	testKeyedDckAndDca();
	testPayloadRejects();
	testAnimMgrTable();
	testStandInSet();
	testManifest(argc > 1 ? argv[1] : "");
	if (failures) {
		std::printf("%d failure(s)\n", failures);
		return 1;
	}
	std::printf("anim writer tests passed\n");
	return 0;
}
