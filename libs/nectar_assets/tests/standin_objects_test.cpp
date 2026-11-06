// Write-then-read tests for the object stand-ins (standin_objects.h): the
// per-model animation bundles and AnimMgr tables of the other AnimMgrs, the
// extra models (naka1, naka4, naka7, chappy), the pellet tables and the .gen
// item and creature generators.
//
// Oracles, per #62 (the loaders in shapeBase.cpp, animMgr.cpp, pelletMgr.cpp and
// generator.cpp do not link into a host test: they need Shape, CoreNode, a live
// StdSystem):
//  1. The game's real types and streams: DataChunk::read, AnimParam, AnimKey,
//     AnimInfo::Parms and AnimMgr::Parms (Animator.h), RamStream, the real
//     Parameters::read (src/plugPikiColin/parameters.cpp) and CmdStream
//     (src/sysCommon/cmdStream.cpp). The pellet config and shape records are
//     read with Parm fields declared the way PelletConfig and PelletAnimInfo
//     declare them (pelletMgr.cpp:108-155, animPellet.cpp:27-80).
//  2. Transcribed loops, same calls in the same order: AnimDck::read with
//     checkMask (shapeBase.cpp:1465-1520, 1181-1226), AnimMgr::read and
//     AnimInfo::doread (animMgr.cpp:229-297, 452-466), the entry loop of
//     StdSystem::loadBundle (stdSystem.cpp:631-714), Generator::read and the
//     GenBase::read of each part (generator.cpp:727-822), GenObjectItem,
//     GenObjectPellet, GenObjectTeki, GenAreaPoint and GenTypeOne. The weak
//     spot is the same as in anim_writer_test.cpp: the transcription is mine.
//     The strong check is the scenario run on the generated tree.
//  3. nectar::readBundle, nectar::parseMod and nectar::parseGen (libs/nectar_assets).
//
// Usage: nectar_standin_objects_test [boot-manifest.txt]. With the manifest, every
// .anm bundle and AnimMgr table of the boot manifest is checked to be written.
#include "nectar_assets/anim.h"
#include "nectar_assets/bundle.h"
#include "nectar_assets/gen.h"
#include "nectar_assets/mod.h"
#include "nectar_assets/mod_writer.h"
#include "nectar_assets/standin_anims.h"
#include "nectar_assets/standin_art.h"
#include "nectar_assets/standin_models.h"
#include "nectar_assets/standin_objects.h"
#include "nectar_assets/standin_sandbox.h"
#include "nectar_assets/standin_world.h"

#include "Animator.h"
#include "CmdStream.h"
#include "Common/String.h"
#include "Parameters.h"
#include "Stream.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

extern std::string g_haltMessage;

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

// ------------------------------------------------------------------ AnimDck reader (shapeBase.cpp:1465-1520, 1181-1226)

struct LoadedAnim {
	int jointCount = 0, frames = 0;
	DataChunk scale, rotate, translate;
	std::vector<AnimDataInfo> info;
	std::vector<int> parent;
	int activeJoints = 0;
	bool ok          = true;
};

LoadedAnim readDck(RamStream& in)
{
	LoadedAnim a;
	a.jointCount = in.readInt();
	a.frames     = in.readInt();
	if (a.jointCount < 0 || a.jointCount > 4096) {
		a.ok = false;
		return a;
	}
	a.scale.read(in);
	a.rotate.read(in);
	a.translate.read(in);
	a.info.resize(std::size_t(a.jointCount));
	for (int i = 0; i < a.jointCount; i++) {
		in.readInt(); // group
		a.parent.push_back(in.readInt());
		AnimParam* sets[3] = { a.info[std::size_t(i)].mScale, a.info[std::size_t(i)].mRotation, a.info[std::size_t(i)].mTranslation };
		for (AnimParam* set : sets) {
			for (int j = 0; j < 3; j++) {
				set[j].mEntryNum   = in.readInt();
				set[j].mDataOffset = in.readInt();
				set[j].mFlags      = in.readInt();
			}
		}
	}
	for (int i = 0; i < a.jointCount; i++) { // checkMask
		AnimDataInfo& d    = a.info[std::size_t(i)];
		d.mFlags           = 0;
		int bit            = 0;
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

// ------------------------------------------------------------------ AnimMgr reader (animMgr.cpp:229-297, 452-466)

struct LoadedInfo {
	std::string name;
	int flags    = 0;
	float speed  = 0;
	std::vector<int> animKeys;
	std::vector<AnimKey> infoKeys, eventKeys;
};

struct LoadedMgr {
	int version = 0;
	std::string baseDir;
	std::vector<LoadedInfo> infos;
	int pending = -1;
};

LoadedMgr readAnimMgr(const Bytes& bytes)
{
	LoadedMgr m;
	RamStream in  = streamOf(bytes);
	m.version     = in.readInt();
	int infoCount = in.readInt();
	AnimMgr::Parms mgrParms;
	mgrParms.read(in);
	m.baseDir = mgrParms.mBasePath().mString;
	for (int i = 0; i < infoCount; i++) {
		LoadedInfo info;
		String str(0);
		in.readString(str);
		info.name = str.mString;
		AnimInfo::Parms parms;
		parms.read(in);
		info.flags     = parms.mFlags();
		info.speed     = parms.mSpeed();
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
			info.eventKeys.push_back(key);
		}
		m.infos.push_back(std::move(info));
	}
	m.pending = in.getPending();
	return m;
}

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
	RamStream fs  = streamOf(bytes);
	int fileCount = fs.readInt();
	for (int i = 0; i < fileCount; i++) {
		LoadedEntry e;
		e.type = fs.readInt();
		e.size = fs.readInt();
		String path(0);
		fs.readString(path);
		e.name      = path.mString;
		e.payloadAt = fs.getPosition();
		if (e.type == 3) {
			e.anim = readDck(fs);
		} else {
			e.anim.ok = false;
			fs.setPosition(fs.getPosition() + e.size);
		}
		e.consumedEnd = fs.getPosition();
		out.push_back(std::move(e));
	}
	pendingAfter = fs.getPending();
	return out;
}

// ------------------------------------------------------------------ collinfo (shapeBase.cpp:1901, shpObjColl.cpp:124)

struct CollRead {
	int joint = -1, depth = 0, type = -1;
	std::string id, code;
	float radius = 0.0f;
	nectar::Vec3 centre;
};

void loadini(CmdStream* cmd, std::vector<CollRead>& out, std::size_t self, int depth)
{
	while (!cmd->endOfCmds() && !cmd->endOfSection()) {
		cmd->getToken(true);
		if (cmd->isToken("id")) {
			cmd->getToken(true);
			out[self].id = std::string(cmd->mCurrentToken, strnlen(cmd->mCurrentToken, 4));
		} else if (cmd->isToken("code")) {
			cmd->getToken(true);
			out[self].code = std::string(cmd->mCurrentToken, strnlen(cmd->mCurrentToken, 4));
		} else if (cmd->isToken("type")) {
			sscanf(cmd->getToken(true), "%d", &out[self].type);
			if (out[self].type == 1) {
				cmd->getToken(true);
				sscanf(cmd->getToken(true), "%f", &out[self].radius);
				cmd->getToken(true);
				sscanf(cmd->getToken(true), "%f", &out[self].centre.x);
				sscanf(cmd->getToken(true), "%f", &out[self].centre.y);
				sscanf(cmd->getToken(true), "%f", &out[self].centre.z);
			}
		} else if (cmd->isToken("collinfo")) {
			int joint = -1;
			sscanf(cmd->getToken(true), "%d", &joint);
			out.emplace_back();
			const std::size_t child = out.size() - 1;
			out[child].joint        = joint;
			out[child].depth        = depth + 1;
			cmd->getToken(true);
			loadini(cmd, out, child, depth + 1);
		}
	}
	if (!cmd->endOfCmds()) cmd->getToken(true);
}

std::vector<CollRead> readColl(const std::string& text)
{
	std::vector<CollRead> out;
	Bytes tail(text.begin(), text.end());
	RamStream file(tail.data(), int(tail.size()));
	CmdStream* cmd = new CmdStream(&file);
	while (!cmd->endOfCmds() && !cmd->endOfSection()) {
		cmd->getToken(true);
		if (cmd->isToken("collinfo")) {
			int joint = -1;
			sscanf(cmd->getToken(true), "%d", &joint);
			out.emplace_back();
			const std::size_t self = out.size() - 1;
			out[self].joint        = joint;
			cmd->getToken(true);
			loadini(cmd, out, self, 0);
		}
	}
	return out;
}

// ------------------------------------------------------------------ tests: animation sets

nectar::Model parsedModel(const nectar::StandInModelSpec& spec)
{
	Bytes bytes;
	std::string err;
	const nectar::Model built = nectar::buildStandInModel(spec);
	check(spec.platform ? nectar::writePlatformMod(built, bytes, err) : nectar::writeMod(built, bytes, err), spec.path + ": writeMod: " + err);
	nectar::Model m;
	check(nectar::parseMod(bytes.data(), bytes.size(), m, err), spec.path + ": parseMod: " + err);
	return m;
}

void testProfiles()
{
	const auto& profiles = nectar::objectAnimProfiles();
	const auto& sets     = nectar::standInObjectAnims();
	check(profiles.size() == 45 && sets.size() == 45, "45 object AnimMgrs (the boot manifest's 46 .anm bundles but the Pikmin one and the soft gate's, which the world files write)");

	std::set<std::string> bundles;
	for (std::size_t s = 0; s < sets.size(); s++) {
		const nectar::StandInAnimSet& set = sets[s];
		const nectar::ObjectAnimProfile& p = profiles[s];
		std::string err;
		check(nectar::checkObjectAnimSet(set, err), set.tablePath + ": " + err);
		check(bundles.insert(set.bundlePath).second, set.bundlePath + ": bundle path is unique");
		check(int(set.parents.size()) == p.joints, set.bundlePath + ": joint count is the profile's");

		std::vector<nectar::StandInAnimFile> files;
		check(nectar::writeObjectAnimSet(set, files, err), set.bundlePath + ": write: " + err);
		if (files.size() != 2) continue;

		// Table, through the game's Parameters and key types.
		LoadedMgr m = readAnimMgr(files[0].bytes);
		check(files[0].path == set.tablePath && files[1].path == set.bundlePath, set.bundlePath + ": paths");
		check(m.pending == 0, set.tablePath + ": AnimMgr::read consumes every byte");
		check(m.version == 1 && m.baseDir == set.baseDir, set.tablePath + ": version 1 and the base directory");
		check(m.infos.size() == p.infos.size(), set.tablePath + ": info count");
		for (std::size_t i = 0; i < m.infos.size() && i < p.infos.size(); i++) {
			const LoadedInfo& l = m.infos[i];
			const std::string who = set.tablePath + " info " + std::to_string(i);
			check(l.name == set.anims[i].info.name, who + ": name");
			check(l.flags == p.infos[i].flags && l.speed == p.infos[i].speed, who + ": p00 and spd");
			check(std::vector<int>(set.anims[i].info.animKeys.begin(), set.anims[i].info.animKeys.end()) == l.animKeys && l.animKeys == p.infos[i].keys,
			      who + ": anim keys");
			check(l.infoKeys.size() == p.infos[i].infoKeys.size(), who + ": info key count");
			for (std::size_t k = 0; k < l.infoKeys.size() && k < p.infos[i].infoKeys.size(); k++) {
				check(l.infoKeys[k].mKeyType == p.infos[i].infoKeys[k].second, who + ": info key type");
				// The same animate call fires the key: the frame it points at is the profile's, or the one before for
				// the disc's one descending list.
				check(l.infoKeys[k].mFrameIndex >= 0 && l.infoKeys[k].mFrameIndex < int(l.animKeys.size()), who + ": info key points into the anim keys");
			}
			// Sound and effect cues are not stand-in data, except the profile's own effect events (the red Onion boot's).
			check(l.eventKeys.size() == p.infos[i].effectKeys.size(), who + ": event keys only where the profile has effect events");
			for (std::size_t k = 0; k < l.eventKeys.size() && k < p.infos[i].effectKeys.size(); k++) {
				check(l.eventKeys[k].mFrameIndex == p.infos[i].effectKeys[k].first && l.eventKeys[k].mEventType == 1
				          && l.eventKeys[k].mKeyType == p.infos[i].effectKeys[k].second,
				      who + ": an action event key with the profile's frame and value, as AnimInfo::doread reads it");
			}
			for (int f : l.animKeys) check(f >= 0 && f < p.infos[i].frames, who + ": anim key inside the frame count (checkAnimData clamps otherwise)");
			check(l.animKeys.size() >= 2 && l.animKeys.size() < 100, who + ": a start and an end anim key");
		}

		// Bundle, through the loader's entry loop and nectar::readBundle.
		int pending = -1;
		std::vector<LoadedEntry> loaded = loadBundleLoop(files[1].bytes, pending);
		check(loaded.size() == p.infos.size() && pending == 0, set.bundlePath + ": entry loop reads every entry and ends at the end of the file");
		std::vector<nectar::BundleEntry> viaLib;
		check(nectar::readBundle(files[1].bytes.data(), files[1].bytes.size(), viaLib, err), set.bundlePath + ": readBundle: " + err);
		check(viaLib.size() == loaded.size(), set.bundlePath + ": readBundle agrees on the entry count");
		for (std::size_t i = 0; i < loaded.size() && i < p.infos.size(); i++) {
			const LoadedEntry& e = loaded[i];
			const std::string who = set.bundlePath + " entry " + std::to_string(i);
			check(e.name == m.baseDir + "/" + m.infos[i].name, who + ": named <base>/<info name> (exact, case-sensitive match)");
			check(e.type == 3 && e.anim.ok, who + ": a DCK entry the loader accepts");
			check(e.consumedEnd - e.payloadAt == e.size, who + ": the reader consumed exactly `size` bytes");
			check(e.anim.jointCount == p.joints, who + ": joint count is the model's");
			check(e.anim.frames == p.infos[i].frames, who + ": frame count is the profile's");
			check(e.anim.activeJoints == 0, who + ": every joint static");
			check(e.anim.parent == set.parents, who + ": parents");
		}

		// The model, when one is written: same joint count and parents.
		std::string mod = set.bundlePath;
		mod.replace(mod.size() - 4, 4, ".mod");
		const nectar::StandInModelSpec* spec = nectar::findStandInModel(mod);
		if (!spec) {
			for (const nectar::StandInModelSpec& o : nectar::standInObjectModelSpecs()) {
				if (o.path == mod) spec = &o;
			}
		}
		if (spec) {
			nectar::Model model = parsedModel(*spec);
			check(int(model.joints.size()) == p.joints, set.bundlePath + ": the stand-in model has the animation's joint count");
			std::vector<int> parents;
			for (const auto& j : model.joints) parents.push_back(j.parent);
			check(parents == set.parents, set.bundlePath + ": the stand-in model has the animation's parents");
		}
	}

	// Specific disc-measured shapes the objects' code leans on.
	for (const auto& p : profiles) {
		if (std::string(p.id) == "goal") {
			check(p.joints == 13 && p.infos.size() == 13, "goal (the Onion): 13 joints, 13 infos");
			// The red boot (info 4) carries the disc's effect events; value 2 opens the Onion's way point. No other info has any.
			bool waypoint = false;
			for (std::size_t i = 0; i < p.infos.size(); i++) {
				if (i != 4) check(p.infos[i].effectKeys.empty(), "goal: only the red boot has event keys");
				for (const auto& e : p.infos[i].effectKeys) waypoint = waypoint || (i == 4 && e.first == 221 && e.second == 2 && e.first < p.infos[i].frames);
			}
			check(waypoint && p.infos[4].effectKeys.size() == 5, "goal info 4 (red boot): five effect events, value 2 (the way point) at frame 221");
			// Written as action event keys (type 1) with the value in mKeyType, as AnimInfo::doread reads them.
			const nectar::StandInAnimSet& goal = nectar::standInObjectAnims()[2];
			const auto& ev = goal.anims[4].info.eventKeys;
			check(ev.size() == 5 && ev[2].frame == 221 && ev[2].eventType == 1 && ev[2].keyType == 2, "goal info 4: event key 2 written as action, value 2, frame 221");
		}
		if (std::string(p.id) == "pikihead") check(p.joints == 4 && p.infos.size() == 7, "pikihead (the sprout): 4 joints, 7 infos");
		if (std::string(p.id) == "chappy") check(p.joints == 17 && p.infos.size() == 15, "chappy: 17 joints, 15 infos");
		if (std::string(p.id) == "ufo0705") check(p.joints >= nectar::kStandInShipMinJoints && p.infos.size() == 17, "ufo0705: the ship's 57 joints, 17 infos");
	}
}

void testCheckRejects()
{
	const nectar::StandInAnimSet& good = nectar::standInObjectAnims()[2]; // goal
	std::string err;
	check(nectar::checkObjectAnimSet(good, err), "baseline set passes: " + err);

	nectar::StandInAnimSet s = good;
	s.anims[1].info.name = s.anims[0].info.name;
	check(!nectar::checkObjectAnimSet(s, err), "duplicate names are rejected");
	s = good;
	s.anims[0].info.animKeys.back() = s.anims[0].frameCount;
	check(!nectar::checkObjectAnimSet(s, err), "an anim key at the frame count is rejected (the game clamps it silently)");
	s = good;
	s.anims[0].frameCount = 0;
	check(!nectar::checkObjectAnimSet(s, err), "a frame count below 1 is rejected");
	s = good;
	s.anims[0].info.infoKeys.assign(17, nectar::AnimKeyRecord {});
	check(!nectar::checkObjectAnimSet(s, err), "more than 16 info keys are rejected");
	s = good;
	s.anims[0].info.infoKeys = { { 2, 0, 0, 2 }, { 1, 0, 0, 2 } };
	check(!nectar::checkObjectAnimSet(s, err), "descending info keys are rejected");
	s = good;
	s.parents.pop_back();
	check(!nectar::checkObjectAnimSet(s, err), "a joint list that differs from the stand-in model is rejected");
	s = good;
	s.anims.clear();
	check(!nectar::checkObjectAnimSet(s, err), "an empty set is rejected");
}

// ------------------------------------------------------------------ tests: models

void testModels()
{
	const auto& specs = nectar::standInObjectModelSpecs();
	check(specs.size() == 4 + nectar::worldModelSpecs().size(), "naka1, naka4, naka7, chappy and the world models");
	std::vector<nectar::StandInModelFile> files;
	std::string err;
	check(nectar::writeStandInObjectModels(files, err), "write models: " + err);
	check(files.size() == specs.size(), "one file per spec");
	// Blender art (#115): every binding to this list names exported art made for the spec's rig,
	// and the model is then drawn from it (vertex colours), as mod_writer_test checks for the 45.
	check(nectar::checkStandInArtBindings(specs, err), "Blender art bindings: " + err);
	for (const nectar::StandInArtBinding& b : nectar::standInArtBindings()) {
		for (const nectar::StandInModelSpec& spec : specs) {
			if (spec.path != b.path) continue;
			check(!parsedModel(spec).colours.empty(), spec.path + ": drawn from its Blender art (vertex colours)");
		}
	}
	for (const nectar::StandInModelSpec& spec : specs) {
		check(nectar::findStandInModel(spec.path) == nullptr, spec.path + ": not also one of the 45 models");
		nectar::Model m = parsedModel(spec);
		check(int(m.joints.size()) == spec.jointCount() && m.joints[0].parent == -1, spec.path + ": joint count and root");
		for (int j = 0; j < spec.jointCount(); j++) check(m.joints[std::size_t(j)].parent == spec.parents[std::size_t(j)], spec.path + ": parent " + std::to_string(j));
		check(!m.meshes.empty(), spec.path + ": has geometry");
		if (spec.path == "dataDir/objects/hinderrock/cube10.mod") {
			// The push box is solid (standin_world.cpp): the 12 triangles of a 120-unit block on joint 0, its side faces on
			// the planes of the vtx parts at x and z +-60.
			bool onFaces = m.collision.size() == 12;
			for (const nectar::ModCollTri& t : m.collision) {
				for (int v : t.v) {
					const nectar::Vec3& p = m.positions[std::size_t(v)];
					onFaces = onFaces && std::fabs(std::fabs(p.x) - 60.0f) < 1e-4f && std::fabs(std::fabs(p.z) - 60.0f) < 1e-4f;
				}
			}
			check(spec.platform && onFaces, spec.path + ": a platform whose collision is the 120-unit block (x and z +-60)");
		} else {
			check(!spec.platform && m.collision.empty(), spec.path + ": no collision triangles (only the push box is a platform)");
		}

		g_haltMessage.clear();
		std::vector<CollRead> parts = readColl(m.trailingIni);
		check(g_haltMessage.empty(), spec.path + ": CmdStream raised an error: " + g_haltMessage);
		for (const CollRead& p : parts) {
			check(p.joint >= 0 && p.joint < spec.jointCount(), spec.path + ": collinfo " + p.id + " joint index is in range");
			check((p.type == 1 && p.radius > 0.0f) || p.type == 2, spec.path + ": collinfo " + p.id + " is a sphere or a platform");
			check(p.id.size() <= 4 && p.code.size() <= 4, spec.path + ": collinfo " + p.id + " has a four-character id and code");
		}
		if (spec.path.find("naka") != std::string::npos) {
			check(parts.size() == 1 && parts[0].id == "cent" && parts[0].radius == 35.0f && parts[0].centre.y == 3.0f, spec.path + ": one 'cent' sphere");
		} else if (spec.path.find("chappy") != std::string::npos) {
			// chappy: the parts the enemy code looks up by id, and the Pikmin sticking code.
			std::map<std::string, const CollRead*> byId;
			for (const CollRead& p : parts) byId[p.id] = &p;
			for (const char* id : { "cent", "head", "body", "slot", "lleg", "rleg" }) check(byId.count(id), spec.path + ": has '" + id + "'");
			check(byId.count("head") && byId["head"]->code == "stik" && byId["head"]->joint == 4, spec.path + ": head sticks, joint 4");
			check(byId.count("cent") && byId["cent"]->depth == 1, spec.path + ": 'cent' is a child of the body part");
			check(parts.size() == 8, spec.path + ": eight parts, two of them without an id");
			// The art hangs each part from the joint that means it, so real animations would move
			// the right part: the shell on the body (1), head and pincers on the head (4, 'head' and
			// 'slot'), the eye stalks on 5 and 6, the legs on the shins (8 'rleg', 11 'lleg'). The
			// skeleton's placeholder is a 12-triangle box. The stand-in poses are identity, so the
			// art must also stand on the ground: nothing below the model origin.
			const nectar::Model built = nectar::buildStandInModel(spec);
			for (int j : { 1, 4, 5, 6, 8, 11 }) {
				std::size_t tris = 0;
				for (const nectar::ModMtxGroup& g : built.meshes[std::size_t(j)].groups) tris += g.tris.size();
				check(tris > 12, spec.path + ": joint " + std::to_string(j) + " carries its part of the art");
			}
			float lowest = 0.0f;
			for (const nectar::Vec3& p : built.positions) lowest = std::min(lowest, p.y);
			check(lowest > -0.05f, spec.path + ": stands on the ground (lowest point " + std::to_string(lowest) + ")");
		}
	}
}

// ------------------------------------------------------------------ tests: pellet tables

// PelletConfig's Parm fields, declared as in pelletMgr.cpp:108-125.
struct TestPelletConfig : public Parameters {
	TestPelletConfig()
	    : Parameters("pellet")
	    , mPelletName(this, String("new", 0), String("", 0), String("", 0), "x99", "theName")
	    , mPelletType(this, 0, 0, 0, "p00", "type")
	    , mPelletColor(this, -1, 0, 0, "p09", "color")
	    , mCarryMin(this, 1, 0, 0, "p01", "min pikis")
	    , mCarryMax(this, 1, 0, 0, "p02", "max pikis")
	    , mScale(this, 1.0f, 0.0f, 0.0f, "p10", "scale")
	{
	}
	Parm<String> mPelletName;
	Parm<int> mPelletType, mPelletColor, mCarryMin, mCarryMax;
	Parm<f32> mScale;
	std::uint32_t model = 0, shape = 0, unused = 0;
	int joint = 0;
};

// PelletAnimInfo's, animPellet.cpp:27-35.
struct TestAnimInfo : public Parameters {
	TestAnimInfo()
	    : Parameters("pelletAnimInfo")
	    , mFolder(this, String("noname", 0), String("", 0), String("", 0), "x00", "folder")
	    , mFile(this, String("noname", 0), String("", 0), String("", 0), "x01", "theName")
	{
	}
	Parm<String> mFolder, mFile;
	std::uint32_t id = 0;
	int creation = 0, teki = 0, overrideJoint = 0;
};

std::uint32_t readId(RamStream& in) { return __builtin_bswap32(static_cast<std::uint32_t>(in.readInt())); } // ID32::read, then PelletConfig::read's swap

std::string idString(std::uint32_t id)
{
	std::string s;
	for (int i = 3; i >= 0; i--) s += char(id >> (8 * i));
	return s;
}

void testPelletTables()
{
	std::vector<nectar::StandInAnimFile> files;
	std::string err;
	check(nectar::writeStandInPelletTables(files, err), "write pellet tables: " + err);
	check(files.size() == 2, "two pellet files");
	if (files.size() != 2) return;

	check(files[0].path == "dataDir/parms/pelMgr.bin", "pelMgr.bin path");
	RamStream cfg = streamOf(files[0].bytes);
	const int configs = cfg.readInt(); // PelletMgr::readConfigs
	check(configs == 20, "twelve coloured pellet configs, the Main Engine, the Whimsical Radar and six enemy corpses");
	std::set<std::string> ids, configShapes, corpseShapes;
	for (int i = 0; i < configs; i++) {
		TestPelletConfig c;
		c.read(cfg); // PelletConfig::read
		c.model  = readId(cfg);
		c.shape  = readId(cfg);
		c.unused = readId(cfg);
		c.joint  = cfg.readInt();
		const std::string model = idString(c.model);
		check(ids.insert(model).second, "config id " + model + " is unique");
		configShapes.insert(idString(c.shape));
		if (model == "ust5") {
			// The disc's Main Engine config (standin_objects.cpp): a ship part (type 3), no colour, 20 to 40 carriers,
			// scale 1.65, ship joint 3, its own shape.
			check(i == 12, "the Main Engine follows the coloured configs");
			check(idString(c.shape) == "ust5" && idString(c.unused) == "none" && c.joint == 3, "Main Engine: shape ust5, no split, repair joint 3");
			check(c.mPelletType() == 3 && c.mPelletColor() == -1 && c.mCarryMin() == 20 && c.mCarryMax() == 40 && c.mScale() == 1.65f,
			      "Main Engine: type 3, colour -1, 20 to 40 carriers, scale 1.65 (the disc's)");
			continue;
		}
		if (model == "uf01") {
			check(i == 13, "the Whimsical Radar follows the Main Engine");
			check(idString(c.shape) == "uf01" && idString(c.unused) == "none" && c.joint == -1, "Whimsical Radar: shape uf01, no split, no repair joint");
			check(c.mPelletType() == 3 && c.mPelletColor() == -1 && c.mCarryMin() == 10 && c.mCarryMax() == 20, "Whimsical Radar: type 3, colour -1, 10 to 20 carriers");
			continue;
		}
		if (model.rfind("tk", 0) == 0) {
			// An enemy corpse (W7 #98): only BTeki::dieSoon's becomePellet makes one, with the
			// enemy as its view, so PelletMgr::newPellet never looks up a shape info for it.
			check(i >= 14, "the enemy corpses follow the Radar");
			check(idString(c.shape) == model && idString(c.unused) == "none" && c.joint == -1, "corpse " + model + ": shape is its own id, no split, no repair joint");
			check(c.mPelletType() == 0 && c.mPelletColor() == -1, "corpse " + model + ": type 0, colour -1");
			check(c.mCarryMin() >= 1 && c.mCarryMax() == 2 * c.mCarryMin(), "corpse " + model + ": carry range");
			corpseShapes.insert(model);
			continue;
		}
		check(model.size() == 4 && (model[1] == 'b' || model[1] == 'r' || model[1] == 'y') && model[0] == 'p', "config id " + model + " is p<colour><number>");
		check(idString(c.shape).rfind("pe", 0) == 0 && idString(c.unused) == "none" && c.joint == -1, "config " + model + ": shape pe**, no split, no repair joint");
		const int want = model[1] == 'b' ? 0 : model[1] == 'r' ? 1 : 2;
		check(c.mPelletType() == want && c.mPelletColor() == want, "config " + model + ": type and colour");
		check(c.mCarryMin() >= 1 && c.mCarryMax() >= c.mCarryMin(), "config " + model + ": carry range");
	}
	check(cfg.getPending() == 0, "PelletMgr::readConfigs consumes every byte");
	for (const char* id : { "pb01", "pr01", "py01", "pb20", "pr20", "py20" }) check(ids.count(id), std::string("config ") + id + " exists");
	for (const char* id : { "tkch", "tksw", "tkbe", "tkka", "tkkb", "tkot" }) check(corpseShapes.count(id), std::string("corpse config ") + id + " exists");

	check(files[1].path == "dataDir/objects/pelAnim.bin", "pelAnim.bin path");
	RamStream sh = streamOf(files[1].bytes);
	const int infos = sh.readInt(); // PelletMgr::readAnimInfos
	check(infos == 23, "23 shape infos: 4 pellets, 16 ship parts, 3 naka parts");
	std::set<std::string> shapeIds;
	int resident = 0;
	for (int i = 0; i < infos; i++) {
		TestAnimInfo a;
		a.id            = readId(sh); // PelletAnimInfo::read
		a.creation      = sh.readInt();
		a.teki          = sh.readInt();
		a.overrideJoint = sh.readInt();
		a.read(sh);
		const std::string id = idString(a.id);
		check(shapeIds.insert(id).second, "shape id " + id + " is unique");
		check(a.creation == 0 || a.creation == 1, "shape " + id + ": creation type");
		check(a.teki == -1, "shape " + id + ": no teki type");
		resident += a.creation == 0;
		const std::string path = std::string("dataDir/objects/") + a.mFolder().mString + "/" + a.mFile().mString + ".mod";
		const nectar::StandInModelSpec* spec = nectar::findStandInModel(path);
		if (!spec) {
			for (const nectar::StandInModelSpec& o : nectar::standInObjectModelSpecs()) {
				if (o.path == path) spec = &o;
			}
		}
		check(spec != nullptr, "shape " + id + ": " + path + " is a stand-in model");
		if (spec) check(a.overrideJoint < spec->jointCount(), "shape " + id + ": override joint is inside the model");
		const std::string table = std::string("dataDir/objects/") + a.mFolder().mString + "/" + a.mFile().mString + ".bin";
		bool hasTable = false;
		for (const nectar::StandInAnimSet& s : nectar::standInObjectAnims()) hasTable = hasTable || s.tablePath == table;
		check(hasTable, "shape " + id + ": " + table + " is a written AnimMgr table");
	}
	check(sh.getPending() == 0, "PelletMgr::readAnimInfos consumes every byte");
	check(resident == 20, "20 resident shapes (the ones the disc boot loads), three loaded on use");
	for (const char* id : { "pe01", "pe05", "pe10", "pe20", "ust1", "uf01", "un01", "un04", "un07" }) check(shapeIds.count(id), std::string("shape ") + id + " exists");
	for (const std::string& shape : configShapes) {
		if (corpseShapes.count(shape)) continue; // drawn by the enemy itself (above)
		check(shapeIds.count(shape) == 1, "config shape " + shape + " has a shape info");
	}
}

// ------------------------------------------------------------------ tests: generators

std::uint32_t readGenId(RamStream& in) { return __builtin_bswap32(static_cast<std::uint32_t>(in.readInt())); } // generator.cpp readID (PC port)

std::string genIdText(std::uint32_t id)
{
	std::string s;
	for (int i = 3; i >= 0; i--) s += char(id >> (8 * i));
	return s;
}

// The Parm fields GenObjectItem, GenObjectPiki and GenTypeOne declare (genItem.cpp:33, generator.h:616, 811).
struct GenParms : public Parameters {
	GenParms()
	    : Parameters("gen")
	    , p00(this, 0, 0, 0, "p00", "a")
	    , p01(this, 0, 0, 0, "p01", "b")
	{
	}
	Parm<int> p00, p01;
};

// GenObjectWorkObject's Parm fields (workObject.cpp:305-310), with its code defaults.
struct WorkGenParms : public Parameters {
	WorkGenParms()
	    : Parameters("work")
	    , p02(this, 10, 0, 100, "p02", "pushers")
	    , p03(this, 30.0f, 0.0f, 400.0f, "p03", "speed")
	{
	}
	Parm<int> p02;
	Parm<f32> p03;
};

struct ReadGen {
	std::string name, kind, itemName;
	std::uint32_t version = 0, objVersion = 0;
	int carry = 0, tekiType = -1, p00 = 0, p01 = 0, typeP01 = 0;
	std::string memo, pellet;
	float pos[3] = {}, offset[3] = {}, areaOffset[3] = {};
};

// Generator::read (generator.cpp:727-822) with the part reads of GenBase::read (GenBase::readVersion,
// doRead, Parameters::read), for the four object kinds and the point and one-shot parts.
ReadGen readGenerator(RamStream& in)
{
	ReadGen g;
	g.name    = genIdText(readGenId(in));
	g.version = readGenId(in);
	readGenId(in); // _70
	g.carry = in.readInt();
	char memo[32];
	for (int i = 0; i < 32; i++) memo[i] = char(in.readByte());
	g.memo = memo;
	for (float& f : g.pos) f = in.readFloat();
	for (float& f : g.offset) f = in.readFloat();

	const std::uint32_t kindId = readGenId(in);
	g.kind                     = genIdText(kindId);
	g.objVersion               = readGenId(in);
	GenParms objParms;
	if (g.kind == "item") { // GenObjectItem::doRead
		String s(0);
		in.readString(s);
		g.itemName = s.mString;
		if (genIdText(g.objVersion) != "v0.0") {
			for (int i = 0; i < 64; i++) in.readByte();
		}
	} else if (g.kind == "pelt") { // GenObjectPellet::doRead
		g.pellet = genIdText(readGenId(in));
	} else if (g.kind == "teki") { // GenObjectTeki::doRead, TekiPersonality::read (version 10)
		g.tekiType = in.readByte();
		in.readByte();
		in.readByte();
		readGenId(in);
		for (int i = 0; i < 5; i++) in.readInt();
		for (int i = 0; i < 5; i++) in.readFloat();
	}
	objParms.read(in);
	g.p00 = objParms.p00();
	g.p01 = objParms.p01();

	check(genIdText(readGenId(in)) == "pint", "the area is a point");
	readGenId(in); // version
	for (float& f : g.areaOffset) f = in.readFloat(); // GenArea::doRead
	GenParms areaParms;
	areaParms.read(in);

	check(genIdText(readGenId(in)) == "1one", "the type is one thing");
	readGenId(in); // version
	GenParms typeParms;
	typeParms.read(in);
	g.typeP01 = typeParms.p01();
	return g;
}

void testGenerators()
{
	std::vector<nectar::ObjectGenSpec> specs(5);
	specs[0].kind          = nectar::ObjectGenSpec::Item;
	specs[0].generatorName = "next";
	specs[0].memo          = "onion";
	specs[0].name          = "goal";
	specs[0].itemParam     = 1;
	specs[0].pos           = { -150.0f, 0.0f, 80.0f };
	specs[1].kind = nectar::ObjectGenSpec::Pellet;
	specs[1].name = "pr01";
	specs[1].pos  = { 10.0f, 2.0f, 30.0f };
	specs[2].kind        = nectar::ObjectGenSpec::Piki;
	specs[2].pikiState   = 0;
	specs[2].pikiColour  = 2;
	specs[2].faceDegrees = 90;
	specs[2].pos         = { 1.0f, 2.0f, 3.0f };
	specs[3].kind     = nectar::ObjectGenSpec::Teki;
	specs[3].tekiType = 3;
	specs[3].pos      = { 200.0f, 0.0f, -100.0f };
	specs[4].kind = nectar::ObjectGenSpec::Item;
	specs[4].name = "pikihead";

	const Bytes file = nectar::writeObjectGenFile(specs);

	// The game's side: header, then Generator::read per generator, ending exactly at the end of the file.
	RamStream in = streamOf(file);
	check(genIdText(readGenId(in)) == "v0.1", "GeneratorMgr::read: header version");
	for (int i = 0; i < 3; i++) in.readFloat(); // the captain's position
	in.readFloat();                              // his direction
	const int count = in.readInt();
	check(count == 5, "generator count");
	std::vector<ReadGen> read;
	for (int i = 0; i < count; i++) read.push_back(readGenerator(in));
	check(in.getPending() == 0, "GeneratorMgr::read consumes every byte");
	check(read.size() == 5 && read[0].name == "next" && read[0].kind == "item" && read[0].itemName == "goal" && read[0].p00 == 1, "Onion: item 'goal' colour 1, generator 'next'");
	check(read[0].memo == "onion" && read[0].pos[0] == -150.0f && read[0].pos[2] == 80.0f, "Onion: memo and position");
	check(read[1].kind == "pelt" && read[1].pellet == "pr01" && read[1].pos[0] == 10.0f, "pellet pr01");
	check(read[2].kind == "piki" && read[2].p00 == 0 && read[2].p01 == 2 && read[2].typeP01 == 90, "sprout: buried, colour 2, facing 90");
	check(read[3].kind == "teki" && read[3].tekiType == 3 && read[3].pos[0] == 200.0f && read[3].name == "    ", "Dwarf Bulborb");
	check(read[4].kind == "item" && read[4].itemName == "pikihead", "item pikihead");

	// nectar::parseGen agrees.
	nectar::GenFile parsed;
	std::string err;
	check(nectar::parseGen(file.data(), file.size(), parsed, err), "parseGen: " + err);
	check(parsed.generators.size() == 5, "parseGen: five generators");
	if (parsed.generators.size() == 5) {
		check(nectar::genIdString(parsed.generators[0].object.kind) == "item" && parsed.generators[0].object.name == "goal", "parseGen: Onion item");
		check(parsed.generators[1].object.pellet == 0x70723031u, "parseGen: pellet id 'pr01'");
		check(nectar::genIdString(parsed.generators[3].object.kind) == "teki" && parsed.generators[3].object.type == 3, "parseGen: teki 3");
		check(parsed.generators[0].pos.x == -150.0f && parsed.generators[0].pos.z == 80.0f, "parseGen: position");
		check(nectar::genIdString(parsed.generators[2].type.kind) == "1one" && parsed.generators[2].type.param(0x70303100u) && parsed.generators[2].type.param(0x70303100u)->asInt() == 90,
		      "parseGen: one-shot facing");
	}
	check(nectar::writeObjectGenFile({}).size() == nectar::writeGen({}).size(), "an empty file is the empty .gen");
}

// ------------------------------------------------------------------ tests: world models and parameter files

void testWorld()
{
	std::vector<nectar::StandInAnimFile> files;
	std::string err;
	check(nectar::writeStandInWorldFiles(files, err), "write world files: " + err);
	std::map<std::string, const nectar::StandInAnimFile*> byPath;
	for (const auto& f : files) byPath[f.path] = &f;
	check(byPath.size() == files.size(), "world file paths are unique");

	// Platform models: parseMod, one joint, collision triangles, no grid.
	check(nectar::worldPlatformPaths().size() == 6, "six platform models (beatle's four, swallow's two)");
	for (const std::string& path : nectar::worldPlatformPaths()) {
		check(byPath.count(path) == 1, path + ": written");
		if (!byPath.count(path)) continue;
		nectar::Model m;
		check(nectar::parseMod(byPath[path]->bytes.data(), byPath[path]->bytes.size(), m, err), path + ": parseMod: " + err);
		check(m.joints.size() == 1 && !m.collision.empty(), path + ": one joint and collision triangles");
	}

	// Every model's collision platforms are written (or are the sandbox gate's), and its parts are in range.
	std::set<std::string> gatePaths = { "dataDir/objects/gate/centcoll.mod", "dataDir/objects/gate/sidecoll.mod" };
	int platformRefs = 0;
	for (const nectar::WorldModelProfile& m : nectar::worldModelProfiles()) {
		for (const nectar::WorldPart& part : m.parts) {
			check(part.joint >= 0 && part.joint < int(m.parents.size()), std::string(m.path) + ": part " + part.id + " joint in range");
			if (part.type == 2) {
				platformRefs++;
				const std::string ref = std::string("dataDir/") + part.platform;
				check(byPath.count(ref) == 1 || gatePaths.count(ref) == 1, std::string(m.path) + ": platform " + part.platform + " is written");
			}
		}
	}
	check(platformRefs >= 8, "the models name their platforms, " + std::to_string(platformRefs));

	// The soft gate's bundle: the sandbox gate's table, four joints.
	std::vector<nectar::SandboxFile> gate;
	check(nectar::writeSandboxGateFiles(gate, err), "sandbox gate files: " + err);
	const nectar::SandboxFile* table = nullptr;
	for (const auto& f : gate) {
		if (f.path == "dataDir/objects/gate/gate.bin") table = &f;
	}
	check(table != nullptr && byPath.count("dataDir/objects/gate/gt_soft.anm") == 1, "gate.bin and gt_soft.anm exist");
	if (table && byPath.count("dataDir/objects/gate/gt_soft.anm")) {
		LoadedMgr mgr = readAnimMgr(table->bytes);
		int pending = -1;
		std::vector<LoadedEntry> loaded = loadBundleLoop(byPath["dataDir/objects/gate/gt_soft.anm"]->bytes, pending);
		check(pending == 0 && loaded.size() == mgr.infos.size(), "gt_soft.anm: one entry per info of gate.bin");
		for (std::size_t i = 0; i < loaded.size() && i < mgr.infos.size(); i++) {
			check(loaded[i].name == mgr.baseDir + "/" + mgr.infos[i].name, "gt_soft.anm entry " + std::to_string(i) + " is named as gate.bin's info");
			check(loaded[i].anim.ok && loaded[i].anim.jointCount == 4, "gt_soft.anm entry " + std::to_string(i) + ": four joints");
			check(loaded[i].anim.parent == std::vector<int>({ -1, 0, 1, 0 }), "gt_soft.anm entry: the soft gate's parents");
		}
	}

	// tekipara/*.bin: version 8 to 11, eight ids, then the parameters; size a multiple of four.
	check(nectar::tekiParaProfiles().size() == 31, "31 tekipara files");
	for (const nectar::TekiParaProfile& t : nectar::tekiParaProfiles()) {
		const std::string path = std::string("dataDir/tekipara/") + t.name + ".bin";
		check(byPath.count(path) == 1, path + ": written");
		if (!byPath.count(path)) continue;
		const Bytes& b = byPath[path]->bytes;
		check(b.size() == t.words.size() * 4 && b.size() >= 4 * (1 + 8 + 20), path + ": size matches the words and holds the shared parameters");
		RamStream in      = streamOf(b);
		const int version = in.readInt();
		check(version >= 8 && version <= 11, path + ": version " + std::to_string(version));
		for (int i = 0; i < 8; i++) in.readInt(); // ParaIDs
		int left = 0;
		while (in.getPending() > 0) {
			in.readInt();
			left++;
		}
		check(left == int(t.words.size()) - 9, path + ": the parameters fill the rest");
	}

	// Boss parameters: tagged lists, read with the game's Parameters::read until every byte is used.
	check(nectar::bossParmProfiles().size() == 2, "two boss parameter files");
	for (const nectar::BossParmProfile& b : nectar::bossParmProfiles()) {
		check(byPath.count(b.path) == 1, std::string(b.path) + ": written");
		if (!byPath.count(b.path)) continue;
		RamStream in = streamOf(byPath[b.path]->bytes);
		for (std::size_t l = 0; l < b.lists.size(); l++) {
			GenParms parms; // any Parm set: unknown tags are skipped by size
			parms.read(in);
		}
		check(in.getPending() == 0, std::string(b.path) + ": Parameters::read consumes every byte, one list at a time");
	}
}

// ------------------------------------------------------------------ tests: world generators

void testWorldGenerators()
{
	using Spec = nectar::ObjectGenSpec;
	std::vector<Spec> specs(5);
	specs[0].kind      = Spec::Plant;
	specs[0].plantType = 11;
	specs[1].kind      = Spec::Work;
	specs[1].name      = "move stone";
	specs[1].shape     = "stone 10";
	specs[1].stoneTo   = { 1.0f, 2.0f, 3.0f };
	specs[2].kind      = Spec::Work;
	specs[2].name      = "bridge test";
	specs[2].shape     = "bridge 13";
	specs[3].kind      = Spec::Boss;
	specs[3].bossType  = 8;
	// A move stone with its own parameters, as the tutorial stand-in stage writes the disc's (10 pushers, speed 50).
	specs[4].kind         = Spec::Work;
	specs[4].name         = "move stone";
	specs[4].shape        = "stone 10";
	specs[4].stoneTo      = { 4.0f, 5.0f, 6.0f };
	specs[4].stonePushers = 10;
	specs[4].stoneSpeed   = 50.0f;
	const Bytes file      = nectar::writeObjectGenFile(specs);

	// Without stonePushers the bytes are what they were before the field existed (no object parameters).
	{
		Spec plain       = specs[1];
		Spec withDefault = specs[1];
		withDefault.stoneSpeed = 99.0f; // ignored while stonePushers < 0
		check(nectar::writeObjectGen(plain) == nectar::writeObjectGen(withDefault), "a move stone without stonePushers writes no parameters");
	}

	nectar::GenFile parsed;
	std::string err;
	check(nectar::parseGen(file.data(), file.size(), parsed, err), "parseGen of plant, work and boss generators: " + err);
	check(parsed.generators.size() == 5, "five generators");
	if (parsed.generators.size() != 5) return;
	const nectar::GenParam* pushers = parsed.generators[4].object.param(0x70303200u); // 'p02'
	const nectar::GenParam* speed   = parsed.generators[4].object.param(0x70303300u); // 'p03'
	check(pushers && pushers->asInt() == 10 && speed && speed->asFloat() == 50.0f, "parseGen: the move stone's p02 10 and p03 50");
	check(parsed.generators[1].object.params.empty(), "parseGen: the plain move stone has no object parameters");
	check(nectar::genIdString(parsed.generators[0].object.kind) == "plnt" && parsed.generators[0].object.type == 11, "plant type 11");
	check(nectar::genIdString(parsed.generators[1].object.kind) == "work" && parsed.generators[1].object.name == "move stone", "move stone");
	check(parsed.generators[2].object.name == "bridge test", "bridge");
	check(nectar::genIdString(parsed.generators[3].object.kind) == "boss" && parsed.generators[3].object.type == 8, "boss 8 (mizu)");

	// The game's read of each part: GenObjectPlant (an int), GenObjectWorkObject v0.3 (two strings, a stone's three floats), GenObjectBoss v2 (one word).
	RamStream in = streamOf(file);
	readGenId(in);
	for (int i = 0; i < 4; i++) in.readFloat();
	check(in.readInt() == 5, "count");
	for (int g = 0; g < 5; g++) {
		bool objParmsRead = false;
		readGenId(in);
		readGenId(in);
		readGenId(in);
		in.readInt();
		for (int i = 0; i < 32; i++) in.readByte();
		for (int i = 0; i < 6; i++) in.readFloat();
		const std::string kind = genIdText(readGenId(in));
		const std::uint32_t version = readGenId(in);
		if (kind == "plnt") {
			check(version == 0x76302e30u, "plnt is v0.0");
			check(in.readInt() == 11, "plnt type");
		} else if (kind == "work") {
			check(genIdText(version) == "v0.3", "work is v0.3");
			String name(0), shape(0);
			in.readString(name);
			in.readString(shape);
			if (std::string(name.mString) == "move stone") {
				const float x = in.readFloat(), y = in.readFloat(), z = in.readFloat();
				check((x == 1.0f && y == 2.0f && z == 3.0f) || (x == 4.0f && y == 5.0f && z == 6.0f), "a move stone's destination");
				// GenObjectWorkObject's own Parms: the code defaults unless the generator carries them.
				WorkGenParms w;
				w.read(in);
				const bool own = x == 4.0f;
				check(own ? (w.p02() == 10 && w.p03() == 50.0f) : (w.p02() == 10 && w.p03() == 30.0f),
				      own ? "the game reads the move stone's 10 pushers and speed 50" : "a plain move stone keeps the defaults 10 and 30");
				objParmsRead = true;
			}
		} else if (kind == "boss") {
			check(version == 2, "boss is version 2");
			check((static_cast<std::uint32_t>(in.readInt()) & 0xF) == 8u, "boss type in the low four bits");
		}
		if (!objParmsRead) {
			GenParms objParms;
			objParms.read(in);
		}
		readGenId(in); // pint
		readGenId(in);
		for (int i = 0; i < 3; i++) in.readFloat();
		GenParms a;
		a.read(in);
		readGenId(in); // 1one
		readGenId(in);
		GenParms t;
		t.read(in);
	}
	check(in.getPending() == 0, "Generator::read consumes every byte of the plant, work and boss generators");
}

// ------------------------------------------------------------------ tests: boot manifest

void testManifest(const std::string& path)
{
	if (path.empty()) return;
	std::ifstream in(path);
	check(bool(in), "cannot read " + path);
	std::set<std::string> written;
	std::vector<nectar::StandInAnimFile> files;
	std::string err;
	check(nectar::writeStandInObjectFiles(files, err), "write all object files: " + err);
	for (const auto& f : files) written.insert(f.path);
	std::vector<nectar::SandboxFile> gate;
	check(nectar::writeSandboxGateFiles(gate, err), "write the sandbox gate files: " + err);
	for (const auto& f : gate) written.insert(f.path); // gt_hard.mod, centcoll, sidecoll, gate.bin, gt_hard.anm

	int anm = 0, tables = 0, worldFiles = 0;
	std::string line;
	while (std::getline(in, line)) {
		if (line.empty() || line[0] == '#') continue;
		const std::string p = line.substr(0, line.find('\t'));
		auto ends = [&](const char* s) { return p.size() >= std::strlen(s) && p.compare(p.size() - std::strlen(s), std::string::npos, s) == 0; };
		if (p == "dataDir/pikis/bluModel.anm" || p == "dataDir/pikis/animMgr.bin") continue;
		if (ends(".anm")) {
			anm++;
			check(written.count(p) == 1, "manifest bundle " + p + " is written");
		}
		const bool isTable = ends("/anims.bin") || ends(".key") || p == "dataDir/objects/gate/gate.bin" || p.find("dataDir/objects/pellets/") == 0 && ends(".bin")
		                  || p.find("dataDir/objects/ufoparts/") == 0 && ends(".bin") || p == "dataDir/objects/ufo/ufo0705.bin" || p == "dataDir/objects/pelAnim.bin"
		                  || p == "dataDir/parms/pelMgr.bin" || p.find("dataDir/objects/plants/") == 0 && ends(".bin");
		if (isTable) {
			tables++;
			check(written.count(p) == 1, "manifest table " + p + " is written");
		}
		if (p.find("dataDir/objects/ufoparts/naka") == 0 && ends(".mod")) check(written.count(p) == 1, "manifest model " + p + " is written");
		if (p == "dataDir/tekis/chappy/chappy.mod") check(written.count(p) == 1, "manifest model " + p + " is written");
		// Every model, platform and parameter file of the objects, plants, gates, bosses and enemies.
		const bool world = (ends(".mod") && (p.find("dataDir/tekis/") == 0 || p.find("dataDir/bosses/") == 0 || p.find("dataDir/objects/plants/") == 0
		                                     || p.find("dataDir/objects/bridge/") == 0 || p.find("dataDir/objects/gate/") == 0 || p.find("dataDir/objects/hinderrock/") == 0))
		                || (p.find("dataDir/tekipara/") == 0 && ends(".bin")) || (p.find("dataDir/bosses/") == 0 && ends("parms.bin"));
		if (world) {
			worldFiles++;
			check(written.count(p) == 1, "manifest file " + p + " is written");
		}
	}
	check(anm == 46, "the manifest lists 46 .anm bundles besides the Pikmin one, found " + std::to_string(anm));
	check(worldFiles >= 60, "found the manifest's model, tekipara and boss parameter files, " + std::to_string(worldFiles));
	check(tables == 48, "found the manifest's 48 AnimMgr and pellet tables, " + std::to_string(tables));
}

} // namespace

int main(int argc, char** argv)
{
	testProfiles();
	testCheckRejects();
	testModels();
	testPelletTables();
	testGenerators();
	testWorld();
	testWorldGenerators();
	testManifest(argc > 1 ? argv[1] : "");
	if (failures) {
		std::printf("%d failure(s)\n", failures);
		return 1;
	}
	std::printf("standin objects tests passed\n");
	return 0;
}
