#include "nectar_assets/standin_objects.h"

#include "be_writer.h"
#include "nectar_assets/anim.h"
#include "nectar_assets/bundle.h"
#include "nectar_assets/gen.h"
#include "nectar_assets/mod_writer.h"
#include "nectar_assets/parms.h"
#include "nectar_assets/standin_world.h"

#include <cstdio>
#include <iterator>
#include <set>

namespace nectar {

namespace {

constexpr int kMaxInfoKeys = 16; // PaniAnimator::checkConstantKeys errors above 16

std::string modPathOf(const std::string& bundlePath)
{
	// AnimMgr::loadAnims derives the bundle from the model by swapping the
	// extension (animMgr.cpp:353-367); this is the same rule backwards.
	const std::string ext = ".anm";
	if (bundlePath.size() <= ext.size() || bundlePath.compare(bundlePath.size() - ext.size(), ext.size(), ext) != 0) return {};
	return bundlePath.substr(0, bundlePath.size() - ext.size()) + ".mod";
}

const StandInModelSpec* findAnyModel(const std::string& path)
{
	if (const StandInModelSpec* s = findStandInModel(path)) return s;
	for (const StandInModelSpec& s : standInObjectModelSpecs()) {
		if (s.path == path) return &s;
	}
	return nullptr;
}

/// A joint list for a set whose model is not written: the root and a flat fan.
/// Only the count is the disc's. The parents are never read back by the game
/// (AnimDck::read stores them), and a model written later brings its own.
std::vector<int> flatParents(int joints)
{
	std::vector<int> p(std::size_t(joints), 0);
	p[0] = -1;
	return p;
}

StandInAnimSet buildSet(const ObjectAnimProfile& p)
{
	StandInAnimSet s;
	s.tablePath  = p.tablePath;
	s.bundlePath = p.bundlePath;
	s.baseDir    = p.baseDir;
	const StandInModelSpec* model = findAnyModel(modPathOf(p.bundlePath));
	s.parents = model ? model->parents : flatParents(p.joints);
	for (std::size_t i = 0; i < p.infos.size(); i++) {
		const ObjectAnimInfoProfile& in = p.infos[i];
		StandInAnim a;
		char name[64];
		std::snprintf(name, sizeof name, "%s_%02d.dck", p.id, int(i)); // unique: the bundle lookup is by name
		a.info.name     = name;
		a.info.flags    = in.flags;
		a.info.speed    = in.speed;
		a.info.animKeys.assign(in.keys.begin(), in.keys.end());
		// checkConstantKeys walks the info keys in order and stops at the first
		// one not yet reached. The disc has one info whose list is not ascending
		// by frame (goal info 0: loop start, loop end, then an Action0 on an
		// earlier frame), so that key fires on the same animate call as the one
		// before it. The writer wants ascending lists, so such a key is pointed at
		// the previous key's frame, which fires on the same tick.
		int previousKey = -1;
		for (const auto& k : in.infoKeys) {
			AnimKeyRecord r;
			r.frame   = k.first;
			r.keyType = std::uint8_t(k.second);
			if (previousKey >= 0 && in.keys[std::size_t(r.frame)] < in.keys[std::size_t(previousKey)]) r.frame = previousKey;
			previousKey = r.frame;
			a.info.infoKeys.push_back(r);
		}
		for (const auto& e : in.effectKeys) {
			AnimKeyRecord r;
			r.frame     = e.first;
			r.eventType = 1; // ANIMEVENT_Action: PaniAnimator::checkEventKeys sends KEY_PlayEffect with mKeyType as its value
			r.keyType   = std::uint8_t(e.second);
			a.info.eventKeys.push_back(r);
		}
		a.frameCount = in.frames;
		a.measured   = true; // length and keys come from the disc
		s.anims.push_back(std::move(a));
	}
	return s;
}

} // namespace

const std::vector<StandInAnimSet>& standInObjectAnims()
{
	static const std::vector<StandInAnimSet> sets = [] {
		std::vector<StandInAnimSet> v;
		for (const ObjectAnimProfile& p : objectAnimProfiles()) v.push_back(buildSet(p));
		return v;
	}();
	return sets;
}

bool checkObjectAnimSet(const StandInAnimSet& set, std::string& error)
{
	auto fail = [&](int i, const std::string& why) {
		error = set.tablePath + " info " + std::to_string(i) + (i >= 0 ? " '" + set.anims[std::size_t(i)].info.name + "'" : std::string()) + ": " + why;
		return false;
	};
	if (set.anims.empty()) return fail(-1, "no infos");
	if (set.parents.empty() || set.parents[0] != -1) return fail(-1, "the first joint is not the root");
	for (std::size_t j = 1; j < set.parents.size(); j++) {
		if (set.parents[j] < 0 || set.parents[j] >= int(j)) return fail(-1, "joint " + std::to_string(j) + " has no earlier parent");
	}
	if (const StandInModelSpec* m = findAnyModel(modPathOf(set.bundlePath))) {
		if (m->parents != set.parents) return fail(-1, "the joints differ from the stand-in model " + m->path);
	}
	std::set<std::string> names;
	for (int i = 0; i < int(set.anims.size()); i++) {
		const StandInAnim& a = set.anims[std::size_t(i)];
		if (!names.insert(a.info.name).second) return fail(i, "name is not unique");
		if (a.frameCount < 1) return fail(i, "frame count below 1 (startAnim errors on counter >= N)");
		if (a.info.animKeys.size() < 2) return fail(i, "needs a start and an end anim key");
		for (std::int32_t f : a.info.animKeys) {
			if (f < 0 || f >= a.frameCount) return fail(i, "anim key frame " + std::to_string(f) + " is not inside the frame count (checkAnimData would clamp it)");
		}
		if (a.info.infoKeys.size() > std::size_t(kMaxInfoKeys)) return fail(i, "more than 16 info keys");
		std::int32_t previous = -1;
		for (const AnimKeyRecord& k : a.info.infoKeys) {
			if (k.frame < 0 || k.frame >= std::int32_t(a.info.animKeys.size())) return fail(i, "info key points past the anim-key list");
			const std::int32_t at = a.info.animKeys[std::size_t(k.frame)];
			if (at < previous) return fail(i, "info keys are not ascending by frame");
			previous = at;
		}
	}
	return true;
}

bool writeObjectAnimSet(const StandInAnimSet& set, std::vector<StandInAnimFile>& out, std::string& error)
{
	if (!checkObjectAnimSet(set, error)) return false;

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

bool writeStandInObjectAnimations(std::vector<StandInAnimFile>& out, std::string& error)
{
	out.clear();
	for (const StandInAnimSet& s : standInObjectAnims()) {
		if (!writeObjectAnimSet(s, out, error)) return false;
	}
	return true;
}

// ---------------------------------------------------------------- extra models

namespace {

std::string objCollText(const char* id, const char* code, int joint, float radius, Vec3 c, const char* comment, int depth)
{
	const std::string ind(std::size_t(depth), '\t');
	char buf[400];
	std::snprintf(buf, sizeof buf, "%scollinfo %d {\t\t// %s\n%s\tid\t\t%s\n%s\tcode\t\t%s\n%s\ttype\t1\n%s\tradius\t%f\n%s\tcentre\t%f %f %f\n", ind.c_str(), joint, comment,
	              ind.c_str(), id, ind.c_str(), code, ind.c_str(), ind.c_str(), double(radius), ind.c_str(), double(c.x), double(c.y), double(c.z));
	return buf;
}

std::vector<StandInModelSpec> buildObjectModelSpecs()
{
	std::vector<StandInModelSpec> s;
	const Rgba8 grey { 160, 160, 160, 255 };

	// The three ship parts the pellet tables load by id on the disc (un01, un04,
	// un07): one joint, one collision sphere 'cent' at the part's centre. Radius
	// 35 and centre (0, 3, 0) are the disc's, the same on all three.
	for (const char* n : { "naka1", "naka4", "naka7" }) {
		StandInModelSpec m;
		m.path     = std::string("dataDir/objects/ufoparts/") + n + ".mod";
		m.parents  = { -1 };
		m.colour   = grey;
		m.halfSize = 8.0f;
		m.collInfo = objCollText("cent", "none", 0, 35.0f, { 0.0f, 3.0f, 0.0f }, n, 0) + "\t}\n\n";
		s.push_back(std::move(m));
	}

	// chappy (the Bulborb of the Forest of Hope): 17 joints and these parents
	// are the disc's, as are the collision parts (id, code, joint, radius,
	// centre). The code 'stik' marks where a Pikmin sticks while it attacks.
	{
		StandInModelSpec m;
		m.path     = "dataDir/tekis/chappy/chappy.mod";
		m.parents  = { -1, 0, 1, 0, 3, 3, 3, 0, 7, 8, 0, 10, 11, 0, 13, 14, 15 };
		m.colour   = { 200, 70, 60, 255 };
		m.halfSize = 12.0f;
		std::string t;
		t += objCollText("none", "none", 1, 20.0f, { -1.0f, -5.0f, 0.0f }, "body", 0);
		t += "\n" + objCollText("cent", "none", 0, 10.0f, { 0.0f, 0.0f, 0.0f }, "kosi", 1) + "\t\t}\n";
		t += "\n" + objCollText("head", "stik", 4, 9.0f, { 2.0f, 0.0f, 0.0f }, "hana", 1) + "\t\t}\n";
		t += "\n" + objCollText("body", "stik", 1, 10.0f, { 1.0f, 0.0f, 0.0f }, "body", 1) + "\t\t}\n\t}\n\n";
		t += objCollText("slot", "none", 4, 10.0f, { 0.0f, 0.0f, 0.0f }, "hana", 0);
		t += "\n" + objCollText("none", "none", 16, 10.0f, { 0.0f, 0.0f, 0.0f }, "kamu_eff", 1) + "\t\t}\n\t}\n\n";
		t += objCollText("lleg", "none", 11, 5.0f, { 0.0f, 0.0f, 0.0f }, "regL_sune", 0) + "\t}\n\n";
		t += objCollText("rleg", "none", 8, 5.0f, { 0.0f, 0.0f, 0.0f }, "regR_sune", 0) + "\t}\n";
		m.collInfo = t;
		s.push_back(std::move(m));
	}
	for (StandInModelSpec& w : worldModelSpecs()) s.push_back(std::move(w));
	return s;
}

} // namespace

const std::vector<StandInModelSpec>& standInObjectModelSpecs()
{
	static const std::vector<StandInModelSpec> specs = buildObjectModelSpecs();
	return specs;
}

bool writeStandInObjectModels(std::vector<StandInModelFile>& out, std::string& error)
{
	out.clear();
	for (const StandInModelSpec& spec : standInObjectModelSpecs()) {
		StandInModelFile f;
		f.path = spec.path;
		std::string err;
		const Model model = buildStandInModel(spec);
		if (!(spec.platform ? writePlatformMod(model, f.bytes, err) : writeMod(model, f.bytes, err))) {
			error = spec.path + ": " + err;
			return false;
		}
		out.push_back(std::move(f));
	}
	return true;
}

// ---------------------------------------------------------------- pellet tables

namespace {

/// ID32::read keeps the byte layout of the PowerPC build and PelletConfig::read
/// byte-swaps it back, so the file holds the four characters reversed.
void putReversedId(BeWriter& w, const char (&id)[5])
{
	for (int i = 3; i >= 0; i--) w.u8(std::uint8_t(id[i]));
}

bool putParams(BeWriter& w, std::vector<ParmEntry> list, std::string& error)
{
	ParmFile f;
	f.lists.push_back(std::move(list));
	std::vector<std::uint8_t> bytes;
	if (!writeParmFile(f, bytes, error)) return false;
	w.bytes(bytes);
	return true;
}

} // namespace

bool writeStandInPelletTables(std::vector<StandInAnimFile>& out, std::string& error)
{
	// parms/pelMgr.bin: int count, then per config (PelletConfig::read) its
	// Parameters block, the model id, the shape id, the split id and the ship
	// joint to repair (-1). The twelve coloured pellets of the game: blue, red and
	// yellow at 1, 5, 10 and 20. The carry numbers below are this file's own
	// (the fewest carriers are the pellet's number, the most twice that), not
	// the disc's: a scenario that needs the disc's balance reads the disc.
	// 'p00' is the type (0 blue, 1 red, 2 yellow) and 'p09' the colour, the same.
	struct Kind {
		const char* name;
		int colour;
		char letter;
	};
	const Kind kinds[] = { { "blue", 0, 'b' }, { "red", 1, 'r' }, { "yellow", 2, 'y' } };
	struct Size {
		int number;
		const char* digits;
		const char* shape;
		int matchingSeeds;
		int otherSeeds;
	};
	const Size sizes[] = { { 1, "01", "pe01", 1, 1 }, { 5, "05", "pe05", 3, 1 }, { 10, "10", "pe10", 5, 2 }, { 20, "20", "pe20", 8, 3 } };

	BeWriter cfg;
	// Enemy corpses (below): the stand-in enemies that leave a corpse (TPI_CorpseType
	// TEKICORPSE_LeaveCorpse), whose death (BTeki::dieSoon) halts the game without a
	// config for their type id.
	struct Corpse {
		const char* id;
		const char* name;
		int carryMin;
		int seeds;
	};
	const Corpse corpses[] = { { "tkch", "teki : chappy", 3, 5 },  { "tksw", "teki : swallow", 10, 10 }, { "tkbe", "teki : beatle", 20, 20 },
		                       { "tkka", "teki : kabekuiA", 2, 2 }, { "tkkb", "teki : kabekuiB", 2, 2 },   { "tkot", "teki : otama", 1, 1 } };

	cfg.s32(int(std::size(kinds) * std::size(sizes)) + 2
	        + int(std::size(corpses))); // + the Main Engine, the Whimsical Radar and the corpses, below
	for (const Kind& k : kinds) {
		for (const Size& z : sizes) {
			const std::string name = std::string(k.name) + " pellet " + std::to_string(z.number);
			const int carryMax     = z.number == 1 ? 2 : z.number * 2;
			// Onion seeds (p06 same colour, p07 another colour): the 1-pellets keep the code default 1 and 1 (the tutorial
			// scenarios count one seed); the larger ones differ so a scenario can tell the two rules apart (pellet_carry).
			// This file's own numbers, not the disc's.
			std::vector<ParmEntry> params = { parmString("x99", name), parmInt("p00", k.colour), parmInt("p09", k.colour), parmInt("p01", z.number),
				                              parmInt("p02", carryMax) };
			if (z.number > 1) {
				params.push_back(parmInt("p06", z.matchingSeeds));
				params.push_back(parmInt("p07", z.otherSeeds));
			}
			if (!putParams(cfg, params, error)) return false;
			const std::string model = std::string("p") + k.letter + z.digits;
			char id[5]              = { model[0], model[1], model[2], model[3], 0 };
			putReversedId(cfg, id);
			char shape[5] = { z.shape[0], z.shape[1], z.shape[2], z.shape[3], 0 };
			putReversedId(cfg, shape);
			putReversedId(cfg, "none");
			cfg.s32(-1);
		}
	}
	// The Main Engine ('ust5', the shape soto5 above), the one ship part the
	// tutorial stand-in stage places (tools/scenarios/standin_practice_stage.h).
	// Unlike the pellets above, every value is the disc's: the VERSION_GPIE01_01
	// install's parms/pelMgr.bin, config 23 of 62, read once (read-only,
	// 2026-10-28): type 3 (ship part), colour -1, 20 to 40 carriers, dynamics on
	// with 10 / 4 / 4, seeds 1 / 1, scale 1.65, carry-info height 30, sounds 1 and
	// 1, and ship joint 3 to repair. The carriers and the scale decide how many
	// Pikmin carry it and how fast, which the tutorial scenarios depend on.
	if (!putParams(cfg,
	               { parmString("x99", "ufo : soto5"), parmInt("p00", 3), parmInt("p09", -1), parmInt("p01", 20), parmInt("p02", 40),
	                 parmInt("p03", 1), parmFloat("p04", 10.0f), parmFloat("p05", 4.0f), parmInt("p08", 4), parmInt("p06", 1), parmInt("p07", 1),
	                 parmFloat("p10", 1.65f), parmFloat("p11", 30.0f), parmInt("p12", 1), parmInt("p13", 1) },
	               error)) {
		return false;
	}
	putReversedId(cfg, "ust5");
	putReversedId(cfg, "ust5");
	putReversedId(cfg, "none");
	cfg.s32(3);
	// The Whimsical Radar ('uf01', shape fuzoku1), a second ship part for the carry-to-ship
	// scenario (ship_part_carry) and the shape the part-discovery scenario borrows for the
	// other parts. This file's own numbers: 10 to 20 carriers, no dynamics, no repair joint
	// (-1: PlayerState::renderParts skips it, so the stand-in ship model needs no joint for it).
	if (!putParams(cfg,
	               { parmString("x99", "ufo : fuzoku1"), parmInt("p00", 3), parmInt("p09", -1), parmInt("p01", 10), parmInt("p02", 20),
	                 parmInt("p12", 1), parmInt("p13", 1) },
	               error)) {
		return false;
	}
	putReversedId(cfg, "uf01");
	putReversedId(cfg, "uf01");
	putReversedId(cfg, "none");
	cfg.s32(-1);
	// The corpses: type 0 (PELTYPE_Corpse), colour -1, carriers from the number to twice
	// it, and the same seed count for every Onion colour. This file's own numbers, not the
	// disc's. Model and shape ids are the enemy's type id ('tk**'); the game only ever makes
	// these through BTeki::dieSoon's becomePellet, which draws the enemy itself.
	for (const Corpse& c : corpses) {
		if (!putParams(cfg,
		               { parmString("x99", c.name), parmInt("p00", 0), parmInt("p09", -1), parmInt("p01", c.carryMin), parmInt("p02", c.carryMin * 2),
		                 parmInt("p06", c.seeds), parmInt("p07", c.seeds) },
		               error)) {
			return false;
		}
		const char id[5] = { c.id[0], c.id[1], c.id[2], c.id[3], 0 };
		putReversedId(cfg, id);
		putReversedId(cfg, id);
		putReversedId(cfg, "none");
		cfg.s32(-1);
	}
	out.push_back({ "dataDir/parms/pelMgr.bin", cfg.b });

	// objects/pelAnim.bin: int count, then per info (PelletAnimInfo::read) the
	// id, creation type (0 resident, 1 loaded when used), teki type (-1), the
	// joint to override (-1 or a joint index of the model) and the Parameters
	// 'x00' folder and 'x01' file (objects/<folder>/<file>.mod). Only the shapes
	// this tree has models for: the four pellets, the 16 ship parts the game
	// always loads, and naka1, naka4 and naka7. The override joints are the
	// disc's (they index the model's joints; 1 or none).
	struct Shape {
		const char* id;
		int creation;
		int overrideJoint;
		const char* folder;
		const char* file;
	};
	const Shape shapes[] = {
		{ "ust1", 0, 1, "ufoparts", "soto1" },   { "ust2", 0, 1, "ufoparts", "soto2" },   { "ust3", 0, -1, "ufoparts", "soto3" },
		{ "ust4", 0, -1, "ufoparts", "soto4" },  { "ust5", 0, 1, "ufoparts", "soto5" },   { "pe01", 0, -1, "pellets", "white1" },
		{ "pe05", 0, -1, "pellets", "white2" },  { "pe10", 0, -1, "pellets", "white3" },  { "pe20", 0, -1, "pellets", "white4" },
		{ "uf01", 0, 1, "ufoparts", "fuzoku1" }, { "uf02", 0, 1, "ufoparts", "fuzoku2" }, { "uf03", 0, 1, "ufoparts", "fuzoku3" },
		{ "uf04", 0, -1, "ufoparts", "fuzoku4" }, { "uf05", 0, -1, "ufoparts", "fuzoku5" }, { "uf06", 0, 1, "ufoparts", "fuzoku6" },
		{ "uf07", 0, -1, "ufoparts", "fuzoku7" }, { "uf08", 0, -1, "ufoparts", "fuzoku8" }, { "uf09", 0, -1, "ufoparts", "fuzoku9" },
		{ "un01", 1, -1, "ufoparts", "naka1" },  { "uf11", 0, -1, "ufoparts", "fuzoku11" }, { "uf10", 0, -1, "ufoparts", "fuzoku10" },
		{ "un04", 1, -1, "ufoparts", "naka4" },  { "un07", 1, -1, "ufoparts", "naka7" },
	};
	BeWriter anim;
	anim.s32(int(std::size(shapes)));
	for (const Shape& s : shapes) {
		char id[5] = { s.id[0], s.id[1], s.id[2], s.id[3], 0 };
		putReversedId(anim, id);
		anim.s32(s.creation);
		anim.s32(-1);
		anim.s32(s.overrideJoint);
		if (!putParams(anim, { parmString("x00", s.folder), parmString("x01", s.file) }, error)) return false;
	}
	out.push_back({ "dataDir/objects/pelAnim.bin", anim.b });
	return true;
}

// ---------------------------------------------------------------- .gen object generators

namespace {

/// Ids in a .gen are stored reversed ("1.0v" is 'v0.1'): Generator::read's readID.
void putGenId(BeWriter& w, const std::string& id)
{
	for (int i = 3; i >= 0; i--) w.u8(i < int(id.size()) ? std::uint8_t(id[std::size_t(i)]) : std::uint8_t(' '));
}

/// A version stored the same way: Factory::registerMember's number (the Teki
/// generator is version 10), read with readID, so its bytes are little-endian.
void putGenVersionNumber(BeWriter& w, std::uint32_t v)
{
	for (int i = 0; i < 4; i++) w.u8(std::uint8_t(v >> (8 * i)));
}

/// Stream::writeString: the length, then the bytes, NUL-padded to a multiple of four.
void putGenString(BeWriter& w, const std::string& s)
{
	const std::size_t padded = (s.size() + 3) / 4 * 4;
	w.u32(std::uint32_t(padded));
	w.b.insert(w.b.end(), s.begin(), s.end());
	for (std::size_t i = s.size(); i < padded; i++) w.u8(0);
}

/// A Parameters block: tagged entries, then -1 (Parameters::write).
void putGenParams(BeWriter& w, std::vector<ParmEntry> list)
{
	ParmFile f;
	f.lists.push_back(std::move(list));
	std::vector<std::uint8_t> bytes;
	std::string error;
	writeParmFile(f, bytes, error); // three-letter ids and four-byte values: cannot fail
	w.bytes(bytes);
}

} // namespace

std::vector<std::uint8_t> writeObjectGen(const ObjectGenSpec& g)
{
	BeWriter w;
	putGenId(w, g.generatorName);
	putGenId(w, "v0.0"); // the generator's version
	putGenId(w, "    "); // _70, read and kept
	w.s32(g.carryOver);
	for (int i = 0; i < 32; i++) w.u8(i < int(g.memo.size()) && i < 31 ? std::uint8_t(g.memo[std::size_t(i)]) : std::uint8_t(0));
	w.f32(g.pos.x);
	w.f32(g.pos.y);
	w.f32(g.pos.z);
	w.f32(0.0f); // the offset
	w.f32(0.0f);
	w.f32(0.0f);

	switch (g.kind) {
	case ObjectGenSpec::Item: { // GenObjectItem::doRead, version v0.1: the name, then the stage and print names (doors only)
		putGenId(w, "item");
		putGenId(w, "v0.1");
		putGenString(w, g.name);
		for (int i = 0; i < 64; i++) w.u8(0);
		std::vector<ParmEntry> params = { parmInt("p00", g.itemParam) };
		if (g.itemDays >= 0) params.push_back(parmInt("p01", g.itemDays));
		if (g.itemHours >= 0) params.push_back(parmInt("p02", g.itemHours));
		putGenParams(w, params);
		break;
	}
	case ObjectGenSpec::Pellet: // GenObjectPellet::doRead: the pellet id
		putGenId(w, "pelt");
		putGenId(w, "v0.0");
		putGenId(w, g.name);
		putGenParams(w, {});
		break;
	case ObjectGenSpec::Piki: // GenObjectPiki: no data of its own, 'p00' state and 'p01' colour
		putGenId(w, "piki");
		putGenId(w, "v0.0");
		putGenParams(w, { parmInt("p00", g.pikiState), parmInt("p01", g.pikiColour) });
		break;
	case ObjectGenSpec::Teki: { // GenObjectTeki::doRead and TekiPersonality::read, version 10
		putGenId(w, "teki");
		putGenVersionNumber(w, 10);
		w.u8(std::uint8_t(g.tekiType));
		w.u8(0); // pellet kind
		w.u8(0); // pellet colour
		putGenId(w, "none"); // the pellet it drops
		for (int i = 0; i < 5; i++) w.s32(0); // pellet and nectar counts, parameter 0
		const float floats[5] = { 1.0f, 1.0f, 0.0f, 0.0f, 0.0f }; // size, strength, territory, pellet chance, nectar chance (TekiPersonality::reset)
		for (float f : floats) w.f32(f);
		putGenParams(w, {});
		break;
	}
	case ObjectGenSpec::Plant: // GenObjectPlant::doRead: the type
		putGenId(w, "plnt");
		putGenId(w, "v0.0");
		w.s32(g.plantType);
		putGenParams(w, {});
		break;
	case ObjectGenSpec::Work: // GenObjectWorkObject::doRead, version v0.3: name, shape, and a move stone's destination
		putGenId(w, "work");
		putGenId(w, "v0.3");
		putGenString(w, g.name);
		putGenString(w, g.shape);
		if (g.name == "move stone") {
			w.f32(g.stoneTo.x);
			w.f32(g.stoneTo.y);
			w.f32(g.stoneTo.z);
		}
		if (g.stonePushers >= 0) {
			// GenObjectWorkObject 'p02' (Pikmin needed to move it) and 'p03' (speed).
			putGenParams(w, { parmInt("p02", g.stonePushers), parmFloat("p03", g.stoneSpeed) });
		} else {
			putGenParams(w, {});
		}
		break;
	case ObjectGenSpec::Boss: // GenObjectBoss::readParameters, version 2: one word of flags (the boss in the low four bits)
		putGenId(w, "boss");
		putGenVersionNumber(w, 2);
		w.u32(std::uint32_t(g.bossType) & 0xFu);
		putGenParams(w, {});
		break;
	}

	// GenAreaPoint ('pint'): version, an offset, parameters.
	putGenId(w, "pint");
	putGenId(w, "v0.0");
	w.f32(0.0f);
	w.f32(0.0f);
	w.f32(0.0f);
	putGenParams(w, {});

	// GenTypeOne ('1one'): version, parameters. 'p01' is the rotation about y.
	putGenId(w, "1one");
	putGenId(w, "v0.0");
	putGenParams(w, { parmInt("p01", g.faceDegrees) });
	return w.b;
}

std::vector<std::uint8_t> writeObjectGenFile(const std::vector<ObjectGenSpec>& specs)
{
	std::vector<std::vector<std::uint8_t>> raw;
	for (const ObjectGenSpec& s : specs) raw.push_back(writeObjectGen(s));
	return writeGen(raw);
}

bool writeStandInObjectFiles(std::vector<StandInAnimFile>& out, std::string& error)
{
	out.clear();
	std::vector<StandInAnimFile> anims;
	if (!writeStandInObjectAnimations(anims, error)) return false;
	for (StandInAnimFile& f : anims) out.push_back(std::move(f));
	std::vector<StandInModelFile> models;
	if (!writeStandInObjectModels(models, error)) return false;
	for (StandInModelFile& f : models) out.push_back({ f.path, std::move(f.bytes) });
	return writeStandInPelletTables(out, error) && writeStandInWorldFiles(out, error);
}

} // namespace nectar
