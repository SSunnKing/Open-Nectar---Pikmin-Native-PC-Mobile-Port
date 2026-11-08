// Write-then-read tests for the scenario sandbox's gate (#68): writeItemGenerator
// (gen.h), writePlatformMod (mod_writer.h) and the files of standin_sandbox.h.
//
// Oracles, in order of strength:
//  1. The game's own Stream (RamStream, readInt/readFloat/readByte/readString)
//     and Parameters::read (src/plugPikiColin/parameters.cpp), compiled in with
//     the stubs of tests/game_loader_stubs.cpp, driven in the order of
//     Generator::read, GenBase::read, GenObjectItem::doRead, GenArea::doRead
//     (src/plugPikiKando/generator.cpp, genItem.cpp). Those functions need the
//     whole engine (factories, ObjType, managers), so their call sequence is
//     transcribed in readItemGenerator below; the Parm objects carry the same
//     ids and defaults as GenObjectItem and GenTypeOne. If those change, update
//     the copy.
//  2. nectar::parseGen and nectar::parseMod (the lib's readers): the generator
//     round trip, and the platform .mod's 0x100 chunk and chunk alignment.
//  3. The game's real CmdStream over the gate model's collinfo text, with the
//     token loop of ObjCollInfo::loadini (shpObjColl.cpp:124) transcribed.
//  4. The scenario runs (throw_release on the stand-ins) load all of it through
//     BaseShape::read and GeneratorMgr::read.
#include "nectar_assets/anim.h"
#include "nectar_assets/bundle.h"
#include "nectar_assets/gen.h"
#include "nectar_assets/mod.h"
#include "nectar_assets/mod_writer.h"
#include "nectar_assets/standin_sandbox.h"

#include "CmdStream.h"
#include "Common/String.h"
#include "Parameters.h"
#include "Stream.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
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

constexpr std::uint32_t fourcc(const char (&s)[5])
{
	return (std::uint32_t(std::uint8_t(s[0])) << 24) | (std::uint32_t(std::uint8_t(s[1])) << 16) | (std::uint32_t(std::uint8_t(s[2])) << 8) | std::uint8_t(s[3]);
}

std::uint32_t bswap(std::uint32_t v) { return (v >> 24) | ((v >> 8) & 0xFF00u) | ((v << 8) & 0xFF0000u) | (v << 24); }

// readID on PC (generator.cpp): __builtin_bswap32(readInt()).
std::uint32_t readID(RandomAccessStream& s) { return bswap(std::uint32_t(s.readInt())); }

// GenObjectItem's and GenTypeOne's parameters (genItem.cpp, Generator.h), with
// their ids and defaults. Parameters::read fills the ones it finds.
struct ItemParms : public Parameters {
	ItemParms()
	    : Parameters("item")
	    , mType(this, 0, 0, 0, "p00", "type")
	    , mDay(this, 0, 0, 0, "p01", "day")
	    , mHour(this, 0, 0, 0, "p02", "hour")
	    , mStages(this, 3, 3, 3, "p03", "stages")
	{
	}
	Parm<int> mType, mDay, mHour, mStages;
};
struct AreaParms : public Parameters {
	AreaParms()
	    : Parameters("pint")
	{
	}
};
struct OneParms : public Parameters {
	OneParms()
	    : Parameters("1one")
	    , mResurrect(this, 0, 0, 0, "b00", "resurrect")
	    , mCarryOver(this, 0, 0, 0, "b01", "carry")
	    , mRotX(this, 0, 0, 0, "p00", "rx")
	    , mRotY(this, 0, 0, 0, "p01", "ry")
	    , mRotZ(this, 0, 0, 0, "p02", "rz")
	{
	}
	Parm<int> mResurrect, mCarryOver, mRotX, mRotY, mRotZ;
};

struct ItemRead {
	std::uint32_t name = 0, version = 0, objId = 0, objVersion = 0, areaId = 0, typeId = 0;
	int carry = 0;
	std::string memo, item;
	float pos[3] {}, offset[3] {}, areaOffset[3] {};
	ItemParms item_;
	OneParms one;
	int consumed = 0;
};

// Generator::read (non-RAM mode) with GenObjectItem, GenAreaPoint, GenTypeOne.
void readItemGenerator(const Bytes& b, ItemRead& r)
{
	RamStream s(const_cast<std::uint8_t*>(b.data()), int(b.size()));
	r.name    = readID(s);
	r.version = readID(s);
	readID(s); // _70
	r.carry = s.readInt();
	for (int i = 0; i < 32; i++) {
		const char c = char(s.readByte());
		if (c && r.memo.size() == std::size_t(i)) r.memo += c;
	}
	for (float& f : r.pos) f = s.readFloat();
	for (float& f : r.offset) f = s.readFloat();
	r.objId      = readID(s);
	r.objVersion = readID(s); // GenBase::read -> readVersion
	char buffer[256];
	String str(buffer, 0x100);
	s.readString(str); // GenObjectItem::doRead: readString(buffer, 0x100)
	r.item = str.mString;
	for (int i = 0; i < 64; i++) s.readByte(); // stage and print names (v0.1)
	r.item_.read(s);
	r.areaId = readID(s);
	readID(s); // area version
	for (float& f : r.areaOffset) f = s.readFloat(); // GenArea::doRead
	AreaParms area;
	area.read(s);
	r.typeId = readID(s);
	readID(s);
	r.one.read(s);
	r.consumed = s.getPosition();
}

std::uint32_t be32(const Bytes& b, std::size_t at)
{
	return (std::uint32_t(b[at]) << 24) | (std::uint32_t(b[at + 1]) << 16) | (std::uint32_t(b[at + 2]) << 8) | b[at + 3];
}

// Chunk walk as BaseShape::read: every chunk 32-aligned, its length covering
// the padding. Returns the chunk types in order and where the end chunk starts.
std::vector<std::uint32_t> walk(const Bytes& b, std::size_t& endAt, const std::string& who)
{
	std::vector<std::uint32_t> types;
	std::size_t pos = 0;
	endAt           = 0;
	while (pos + 8 <= b.size()) {
		check(pos % 32 == 0, who + ": chunk not 32-aligned");
		const std::uint32_t t = be32(b, pos), len = be32(b, pos + 4);
		types.push_back(t);
		if (t == 0xFFFF) {
			endAt = pos;
			break;
		}
		check((pos + 8 + len) % 32 == 0, who + ": chunk length does not reach the next boundary");
		pos += 8 + len;
	}
	check(!types.empty() && types.back() == 0xFFFF, who + ": no end chunk");
	return types;
}

struct CollRead {
	int joint = -1, type = -1;
	std::string id, platform;
	float radius = 0.0f;
};

// ObjCollInfo::loadini's token loop for one level (no nesting in the gate).
void loadini(CmdStream* cmd, CollRead& out)
{
	while (!cmd->endOfCmds() && !cmd->endOfSection()) {
		cmd->getToken(true);
		if (cmd->isToken("id")) {
			cmd->getToken(true);
			out.id = std::string(cmd->mCurrentToken, strnlen(cmd->mCurrentToken, 4));
		} else if (cmd->isToken("code")) {
			cmd->getToken(true);
		} else if (cmd->isToken("type")) {
			sscanf(cmd->getToken(true), "%d", &out.type);
			if (out.type == 1) {
				cmd->getToken(true);
				sscanf(cmd->getToken(true), "%f", &out.radius);
				cmd->getToken(true);
				for (int k = 0; k < 3; k++) cmd->getToken(true);
			} else if (out.type == 2) {
				cmd->getToken(true);
				out.platform = cmd->getToken(true);
			}
		}
	}
	if (!cmd->endOfCmds()) cmd->getToken(true);
}

std::vector<CollRead> readColl(const Bytes& b, std::size_t from)
{
	std::vector<CollRead> out;
	Bytes tail(b.begin() + long(from), b.end());
	RamStream file(tail.data(), int(tail.size()));
	CmdStream* cmd = new CmdStream(&file);
	while (!cmd->endOfCmds() && !cmd->endOfSection()) {
		cmd->getToken(true);
		if (cmd->isToken("collinfo")) {
			CollRead c;
			sscanf(cmd->getToken(true), "%d", &c.joint);
			cmd->getToken(true);
			loadini(cmd, c);
			out.push_back(c);
		}
	}
	return out;
}

void testItemGenerator()
{
	nectar::GenItemSpec spec;
	spec.memo   = "test gate";
	spec.pos    = { -34.5f, 2.0f, -203.25f };
	spec.item   = "sluice bomb";
	spec.type   = 1;
	spec.day    = 2;
	spec.hour   = 7;
	spec.stages = 4;
	spec.rotX   = 3;
	spec.rotY   = 36;
	spec.rotZ   = -5;
	spec.resurrectDays  = 6;
	spec.carryOver      = 1;
	spec.carryOverFlags = 11;
	Bytes raw;
	std::string err;
	check(nectar::writeItemGenerator(spec, raw, err), "writeItemGenerator: " + err);

	// Oracle 1: the game's stream and Parameters::read.
	ItemRead r;
	readItemGenerator(raw, r);
	check(g_haltMessage.empty(), "game reader raised an error: " + g_haltMessage);
	check(r.version == fourcc("v0.0"), "generator version");
	check(r.carry == 11, "carry-over flags");
	check(r.memo == "test gate", "memo");
	check(r.pos[0] == -34.5f && r.pos[1] == 2.0f && r.pos[2] == -203.25f, "position");
	check(r.offset[0] == 0.0f && r.offset[1] == 0.0f && r.offset[2] == 0.0f, "offset");
	check(r.objId == fourcc("item"), "object id reads back as 'item' through readID");
	check(r.objVersion == fourcc("v0.1"), "GenObjectItem version v0.1 (reads stage and print names)");
	check(r.item == "sluice bomb", "item name through Stream::readString: '" + r.item + "'");
	check(r.item_.mType() == 1 && r.item_.mDay() == 2 && r.item_.mHour() == 7 && r.item_.mStages() == 4, "GenObjectItem p00-p03 through Parameters::read");
	check(r.areaId == fourcc("pint"), "area id 'pint'");
	check(r.areaOffset[0] == 0.0f && r.areaOffset[1] == 0.0f && r.areaOffset[2] == 0.0f, "area offset");
	check(r.typeId == fourcc("1one"), "type id '1one'");
	check(r.one.mResurrect() == 6 && r.one.mCarryOver() == 1, "GenType b00/b01");
	check(r.one.mRotX() == 3 && r.one.mRotY() == 36 && r.one.mRotZ() == -5, "GenTypeOne rotation p00-p02");
	check(r.consumed == int(raw.size()), "the game reader consumes exactly the generator");

	// Oracle 2: parseGen over a whole .gen.
	const Bytes gen = nectar::writeGen({ raw, raw });
	nectar::GenFile f;
	check(nectar::parseGen(gen.data(), gen.size(), f, err), "parseGen: " + err);
	check(f.generators.size() == 2, "two generators");
	if (!f.generators.empty()) {
		const nectar::Generator& g = f.generators[0];
		check(g.raw == raw, "Generator::raw is the written bytes");
		check(g.object.kind == fourcc("item") && g.object.name == "sluice bomb", "parseGen object");
		check(g.memo == "test gate", "parseGen memo");
		const nectar::GenParam* ry = g.type.param(fourcc("p01 "));
		check(ry && ry->asInt() == 36, "parseGen rotation y");
		const nectar::GenParam* st = g.object.param(fourcc("p03 "));
		check(st && st->asInt() == 4, "parseGen stages");
	}

	// Errors.
	spec.memo = std::string(32, 'm');
	check(!nectar::writeItemGenerator(spec, raw, err), "a 32-byte memo is refused (no room for the NUL)");
	spec.memo = "ok";
	spec.item.clear();
	check(!nectar::writeItemGenerator(spec, raw, err), "an empty item name is refused");
}

void testPlatformMod()
{
	const nectar::Model m = nectar::buildSandboxGatePlatform(true);
	Bytes bytes;
	std::string err;
	check(nectar::writePlatformMod(m, bytes, err), "writePlatformMod: " + err);
	std::size_t endAt = 0;
	const std::vector<std::uint32_t> types = walk(bytes, endAt, "platform");
	check(types.size() >= 2 && types[types.size() - 2] == 0x100, "the 0x100 chunk sits right before the end chunk");

	nectar::Model back;
	check(nectar::parseMod(bytes.data(), bytes.size(), back, err), "parseMod: " + err);
	check(back.collision.size() == m.collision.size() && !m.collision.empty(), "collision triangle count");
	check(back.collRooms == 1, "one room");
	check(back.positions.size() == m.positions.size(), "positions");
	for (std::size_t i = 0; i < back.collision.size() && i < m.collision.size(); i++) {
		const nectar::ModCollTri &t = back.collision[i], &w = m.collision[i];
		check(t.code == w.code && t.v[0] == w.v[0] && t.v[1] == w.v[1] && t.v[2] == w.v[2] && t.room == 0, "triangle " + std::to_string(i));
		// The plane is the game's rule: n = (C - A) x (B - A) normalised, offset n . A.
		const nectar::Vec3 &a = back.positions[std::size_t(t.v[0])], &b = back.positions[std::size_t(t.v[1])], &c = back.positions[std::size_t(t.v[2])];
		const nectar::Vec3 u { c.x - a.x, c.y - a.y, c.z - a.z }, v { b.x - a.x, b.y - a.y, b.z - a.z };
		nectar::Vec3 n { u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x };
		const float len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
		check(std::fabs(t.normal.x - n.x / len) < 1e-5f && std::fabs(t.normal.y - n.y / len) < 1e-5f && std::fabs(t.normal.z - n.z / len) < 1e-5f,
		      "triangle " + std::to_string(i) + " normal");
		check(std::fabs(t.offset - (t.normal.x * a.x + t.normal.y * a.y + t.normal.z * a.z)) < 1e-3f, "triangle " + std::to_string(i) + " offset");
		// Walls of a box face outwards: the normal points away from the box centre.
		check(t.normal.y > -0.5f, "no downward-facing collision (the bottom is left open)");
	}

	nectar::Model empty = m;
	empty.collision.clear();
	check(!nectar::writePlatformMod(empty, bytes, err), "a platform without triangles is refused");
	nectar::Model bad = m;
	bad.collision[0].v[1] = int(bad.positions.size());
	check(!nectar::writePlatformMod(bad, bytes, err), "an out-of-range vertex index is refused");
}

void testSandboxFiles()
{
	std::vector<nectar::SandboxFile> files;
	std::string err;
	check(nectar::writeSandboxGateFiles(files, err), "writeSandboxGateFiles: " + err);
	std::map<std::string, Bytes> byPath;
	for (const nectar::SandboxFile& f : files) byPath[f.path] = f.bytes;
	const char* const want[] = { "dataDir/objects/gate/gt_hard.mod", "dataDir/objects/gate/centcoll.mod", "dataDir/objects/gate/sidecoll.mod",
		                         "dataDir/objects/gate/gate.bin", "dataDir/objects/gate/gt_hard.anm" };
	for (const char* p : want) check(byPath.count(p) == 1, std::string("gate file ") + p);

	// The gate model: two joints (1 is a child of 0) and the collinfo the game needs.
	const Bytes& gate = byPath["dataDir/objects/gate/gt_hard.mod"];
	nectar::Model m;
	check(nectar::parseMod(gate.data(), gate.size(), m, err), "gt_hard.mod parseMod: " + err);
	check(m.joints.size() == 2 && m.joints[1].parent == 0, "gt_hard.mod joints");
	std::size_t endAt = 0;
	walk(gate, endAt, "gt_hard.mod");
	g_haltMessage.clear();
	const std::vector<CollRead> coll = readColl(gate, endAt + 8);
	check(g_haltMessage.empty(), "CmdStream raised an error: " + g_haltMessage);
	bool sawGate = false, sawSide = false, sawFlag = false;
	for (const CollRead& c : coll) {
		if (c.id == "gate") {
			sawGate = true;
			check(c.joint == 1 && c.type == 2, "'gate' is a platform on joint 1");
			check(byPath.count("dataDir/" + c.platform) == 1, "'gate' platform file is written: " + c.platform);
		} else if (c.id == "side") {
			sawSide = true;
			check(c.joint == 0 && c.type == 2, "'side' is a platform on joint 0");
			check(byPath.count("dataDir/" + c.platform) == 1, "'side' platform file is written: " + c.platform);
		} else if (c.id == "flag") {
			sawFlag = true;
			check(c.type == 1 && c.radius > 0.0f, "'flag' is a sphere with a radius");
		}
	}
	check(sawGate && sawSide && sawFlag, "collinfo has gate, side and flag");
	for (const char* p : { "dataDir/objects/gate/centcoll.mod", "dataDir/objects/gate/sidecoll.mod" }) {
		nectar::Model pm;
		check(nectar::parseMod(byPath[p].data(), byPath[p].size(), pm, err) && !pm.collision.empty(), std::string(p) + " has collision: " + err);
	}

	// The animation bundle holds an entry for every table info (BuildingItem
	// starts motion 0; without it BaseShape panics "no joint anim!!").
	const Bytes& bun = byPath["dataDir/objects/gate/gt_hard.anm"];
	check(bun.size() >= 4 && be32(bun, 0) == 6, "six gate animations");

	// default.gen: one 'sluice bomb' item generator at the placement.
	nectar::SandboxFile gen;
	check(nectar::writeSandboxDefaultGen("stages/sandbox/", gen, err), "writeSandboxDefaultGen: " + err);
	check(gen.path == "dataDir/stages/sandbox/default.gen", "default.gen path");
	nectar::GenFile f;
	check(nectar::parseGen(gen.bytes.data(), gen.bytes.size(), f, err), "default.gen parseGen: " + err);
	check(f.naviPos.x == 0.0f && f.naviPos.z == 0.0f, "captain at the origin");
	check(f.generators.size() == 1, "one generator");
	if (f.generators.size() == 1) {
		const nectar::Generator& g = f.generators[0];
		const nectar::SandboxGatePlacement p = nectar::sandboxGatePlacement();
		check(g.object.name == "sluice bomb", "the gate is a 'sluice bomb'");
		check(g.pos.x == p.x && g.pos.z == p.z, "gate position");
		const nectar::GenParam* ry = g.type.param(fourcc("p01 "));
		check(ry && ry->asInt() == p.rotY, "gate rotation");
		ItemRead r;
		readItemGenerator(g.raw, r);
		check(r.item == "sluice bomb" && r.consumed == int(g.raw.size()), "the game reader reads the sandbox generator");
	}
}

} // namespace

int main()
{
	testItemGenerator();
	testPlatformMod();
	testSandboxFiles();
	if (failures) {
		std::printf("%d failure(s)\n", failures);
		return 1;
	}
	std::printf("sandbox writer tests passed\n");
	return 0;
}
