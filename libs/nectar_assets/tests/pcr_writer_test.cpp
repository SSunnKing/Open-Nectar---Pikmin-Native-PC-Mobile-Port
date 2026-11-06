// Tests for nectar::writePcr (.pcr particle entries) and the stand-in particle
// set of #72: one inert payload under the 331 names EffectMgr loads.
//
// Oracle: particleGenerator::init and pmSetDDF need the whole engine (Graphics,
// GX, the particle pools), so they cannot be linked. The reader below transcribes
// the byte-consuming part of init, pmSetDDF (particleGenerator.cpp:147-232,
// 321-455) and bBoardColourAnimData::set (bBoardColourAnim.cpp:83-96), advancing
// by the same amounts in the same order. If those functions change, update the
// copy. Independent of it: tools/leer_pcr.py reads the same fields (run by hand,
// see the issue). The in-engine check, a generator created from each of the 332
// registrations, is a nectar self-test that is not part of this test.
//
// Names: the drift test compares standinPcrNames() with the literals of
// ptclLoadInfo in effectMgr.cpp (path from NECTAR_EFFECT_MGR_CPP), and the bundle
// test checks that the exact strings the game passes to particleLoader::load
// resolve in an effects.bun made with nectar::writeBundle.
// With NECTAR_DISC_DATA=<assets/dataDir> set, every disc .pcr is also run through
// the reader to check that the transcription walks real files to their end.
#include "nectar_assets/bundle.h"
#include "nectar_assets/pcr.h"
#include "nectar_assets/standin_ui.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#ifndef NECTAR_EFFECT_MGR2D_CPP
#error "NECTAR_EFFECT_MGR2D_CPP must name games/pikmin1/src/plugPikiYamashita/effectMgr2D.cpp"
#endif
#ifndef NECTAR_EFFECT_MGR_CPP
#error "NECTAR_EFFECT_MGR_CPP must name games/pikmin1/src/plugPikiYamashita/effectMgr.cpp"
#endif

namespace {
int failures = 0;

void check(bool ok, const std::string& what)
{
	if (!ok) {
		std::printf("FAIL: %s\n", what.c_str());
		failures++;
	}
}

struct Cur {
	const std::uint8_t* d;
	std::size_t n, p = 0;
	bool ok = true;
	Cur(const std::vector<std::uint8_t>& v) : d(v.data()), n(v.size()) { }
	bool need(std::size_t k)
	{
		if (p + k > n) ok = false;
		return ok;
	}
	std::uint32_t u32at(std::size_t o) { return (std::uint32_t(d[o]) << 24) | (std::uint32_t(d[o + 1]) << 16) | (std::uint32_t(d[o + 2]) << 8) | d[o + 3]; }
	// readDDF_* helpers: read at p, advance by `adv`.
	std::uint32_t u32(std::size_t adv = 4)
	{
		if (!need(4)) return 0;
		std::uint32_t v = u32at(p);
		p += adv;
		return v;
	}
	float f32(std::size_t adv = 4)
	{
		std::uint32_t v = u32(adv);
		float f;
		std::memcpy(&f, &v, 4);
		return f;
	}
	std::int16_t s16(std::size_t adv = 2)
	{
		if (!need(2)) return 0;
		std::int16_t v = std::int16_t((d[p] << 8) | d[p + 1]);
		p += adv;
		return v;
	}
	std::uint8_t u8(std::size_t adv = 1)
	{
		if (!need(1)) return 0;
		std::uint8_t v = d[p];
		p += adv;
		return v;
	}
	void skip(std::size_t k)
	{
		if (need(k)) p += k;
	}
};

struct Parsed {
	bool ok = true;
	std::string version;
	int ddfOffset = 0, billboard = 0;
	float lengthScale = 0, pivot = 0;
	int blend = 0, duration = 0, animFlags = 0, maxFrameAnim = 0;
	std::vector<float> thresholds;
	std::size_t animEnd = 0, ddfEnd = 0;
	std::uint32_t flags = 0;
	float f[32] = {}; // every f32 of the DDF in read order
	int nf = 0;
	std::int16_t sh[5] = {};
	int u[4] = {}, zmode = 0;
};

// particleGenerator::init (the byte-consuming part) + pmSetDDF.
Parsed parse(const std::vector<std::uint8_t>& b)
{
	Parsed r;
	Cur c(b);
	if (b.size() < 8) {
		r.ok = false;
		return r;
	}
	r.version   = std::string(reinterpret_cast<const char*>(b.data()), 4);
	r.ddfOffset = (b[4] << 8) | b[5];
	r.billboard = b[6];
	std::size_t animAt = 8;
	if (!r.billboard) {
		c.p         = 8;
		r.lengthScale = c.f32();
		r.pivot       = c.f32();
		animAt        = 16;
	}
	c.p       = animAt;
	r.blend   = c.u8();
	r.duration = c.u8();
	r.animFlags = c.u8();
	r.maxFrameAnim = c.u8();
	for (int i = 0; i < r.maxFrameAnim; i++) r.thresholds.push_back(c.f32());
	c.skip(std::size_t(r.maxFrameAnim) * 4); // primary colours
	c.skip(std::size_t(r.maxFrameAnim) * 4); // environment colours
	r.animEnd = c.p;

	c.p = std::size_t(r.ddfOffset);
	r.flags = c.u32();
	auto vec = [&]() { for (int i = 0; i < 3; i++) r.f[r.nf++] = c.f32(); };
	vec(), vec(), vec();
	r.f[r.nf++] = c.f32(); // spread
	auto keyed = [&](std::uint32_t mask) {
		if (r.flags & mask) {
			int n = c.u8(4);
			c.skip(std::size_t(n) * 8);
		} else {
			r.f[r.nf++] = c.f32();
		}
	};
	keyed(0x40 | 0x80);
	r.f[r.nf++] = c.f32(); // rate jitter
	r.f[r.nf++] = c.f32(); // radius scale
	keyed(0x100 | 0x200);
	keyed(0x400 | 0x800);
	for (int i = 0; i < 14; i++) r.f[r.nf++] = c.f32();
	for (int i = 0; i < 5; i++) r.sh[i] = c.s16();
	r.u[0] = c.u8();
	r.u[1] = c.u8();
	r.u[2] = c.u8();
	r.u[3] = c.u8();
	r.zmode = c.u8(2);
	if (r.flags & 0x10000) { c.skip(12); }
	if (r.flags & 0x20000) { c.skip(12); }
	if (r.flags & 0x40000) { c.skip(12 + 16); }
	if (r.flags & 0x80000) { c.skip(12 + 4); }
	if (r.flags & 0x100000) { c.skip(12 + 4); }
	if (r.flags & 0x200000) { c.skip(12 + 4); }
	if (r.flags & 0x400000) { c.skip(4); }
	if (r.flags & 0x800000) { c.skip(12 + 8); }
	if (r.flags & 0x2000) { c.skip(4 + 4 + 4 + 4 + 1 + 3); }
	r.ddfEnd = c.p;
	r.ok     = c.ok;
	return r;
}

bool readFile(const std::string& path, std::vector<std::uint8_t>& out)
{
	std::ifstream f(path, std::ios::binary);
	if (!f) return false;
	out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
	return true;
}

// The registrations of ptclLoadInfo, in source order (332 strings).
std::vector<std::string> registrations()
{
	std::ifstream f(NECTAR_EFFECT_MGR_CPP);
	std::stringstream ss;
	ss << f.rdbuf();
	std::string src = ss.str();
	std::size_t a   = src.find("PtclLoadInfo ptclLoadInfo[");
	std::size_t b   = a == std::string::npos ? a : src.find("};", a);
	std::vector<std::string> out;
	if (a == std::string::npos || b == std::string::npos) return out;
	std::size_t pos = a;
	while (true) {
		std::size_t o = src.find("{ \"effects/pcr/", pos);
		if (o == std::string::npos || o > b) break;
		std::size_t s = o + 3, e = src.find('"', s);
		out.push_back(src.substr(s, e - s));
		pos = e;
	}
	return out;
}

// The registrations of ptclInfo in effectMgr2D.cpp (EffectMgr2D), in source order.
std::vector<std::string> registrations2d()
{
	std::ifstream f(NECTAR_EFFECT_MGR2D_CPP);
	std::stringstream ss;
	ss << f.rdbuf();
	const std::string src = ss.str();
	std::vector<std::string> out;
	std::size_t pos = src.find("PtclLoadInfo ptclInfo[");
	while (pos != std::string::npos) {
		std::size_t o = src.find("{ \"effects/pcr/", pos);
		if (o == std::string::npos) break;
		std::size_t s = o + 3, e = src.find('"', s);
		out.push_back(src.substr(s, e - s));
		pos = e;
	}
	return out;
}
} // namespace

int main()
{
	std::string err;

	// ---- the inert payload -------------------------------------------------------------
	std::vector<std::uint8_t> payload = nectar::inertPcrPayload();
	{
		check(payload.size() == 172, "inert payload is 172 bytes, got " + std::to_string(payload.size()));
		Parsed r = parse(payload);
		check(r.ok, "reader walks the payload without leaving it");
		check(r.version == "1-02", "version tag");
		check(r.ddfOffset == 36 && r.billboard == 1 && payload[7] == 0, "header: DDF at 36, billboard, byte 7 unused");
		check(r.maxFrameAnim == 2 && r.duration >= 2 && r.thresholds.size() == 2 && r.thresholds[0] == 0.0f && r.thresholds[1] == 1.0f,
		      "colour block: 2 frames, duration >= 2, thresholds 0 and 1");
		check(r.animEnd == 36, "colour block ends where the DDF starts");
		check(r.flags == 0x1000, "DDF flags are DisableEmission only");
		check(r.ddfEnd == payload.size(), "DDF ends exactly at the end of the payload");
		check(r.ddfEnd - 36 == nectar::kPcrDdfSize, "DDF is 136 bytes");
		check(r.sh[4] == 1 && r.u[2] == 1, "mMaxFrame is 1 and mMaxPasses is 1 (the generator finishes)");
		bool zero = r.nf == 3 * 3 + 1 + 1 + 1 + 1 + 1 + 1 + 14;
		for (int i = 0; i < r.nf; i++) zero = zero && r.f[i] == 0.0f;
		check(zero, "every float field of the DDF is 0");
		check(r.sh[0] == 0 && r.sh[1] == 0 && r.sh[2] == 0 && r.sh[3] == 0 && r.u[0] == 0 && r.u[1] == 0 && r.u[3] == 0 && r.zmode == 0,
		      "every other DDF field is 0");
		std::vector<std::uint8_t> again;
		check(nectar::writePcr(nectar::inertPcrParticle(), again, err) && again == payload, "payload is deterministic");
	}

	// ---- writer: layouts and rules ----------------------------------------------------------
	{
		nectar::PcrParticle p = nectar::inertPcrParticle();
		p.billboard           = false;
		p.lengthScale         = 2.5f;
		p.pivotOffsetY        = -1.0f;
		p.flags               = nectar::kPcrDisableEmission | 0x1;
		p.rotAngle            = -7;
		p.zMode               = 0x0B;
		std::vector<std::uint8_t> out;
		check(nectar::writePcr(p, out, err), "write a non-billboard entry");
		Parsed r = parse(out);
		check(r.ok && r.billboard == 0 && r.lengthScale == 2.5f && r.pivot == -1.0f, "non-billboard: scale and pivot at 8 and 12");
		check(r.ddfOffset == 16 + 28 && r.animEnd == 16 + 28 && r.ddfEnd == out.size() && out.size() == 44 + 136, "non-billboard: colour block at 16, DDF at 44");
		check(r.flags == 0x1001 && r.sh[2] == -7 && r.zmode == 0x0B, "non-billboard: flags, rot angle and z mode");

		nectar::PcrParticle q = nectar::inertPcrParticle();
		q.colour.thresholds   = { 0, 0.5f, 1 };
		q.colour.primary.resize(3);
		q.colour.env.resize(3);
		check(nectar::writePcr(q, out, err), "three colour frames");
		r = parse(out);
		check(r.ok && r.maxFrameAnim == 3 && r.ddfOffset == 8 + 4 + 36 && r.ddfEnd == out.size(), "three frames: DDF offset moves by 12");

		auto rejects = [&](nectar::PcrParticle x, const char* what) {
			std::vector<std::uint8_t> o;
			std::string e;
			check(!nectar::writePcr(x, o, e) && !e.empty(), std::string("writer rejects ") + what);
		};
		for (std::uint32_t bit : { 6u, 7u, 8u, 9u, 10u, 11u, 13u, 16u, 17u, 18u, 19u, 20u, 21u, 22u, 23u }) {
			nectar::PcrParticle x = nectar::inertPcrParticle();
			x.flags |= 1u << bit;
			rejects(x, ("flag bit " + std::to_string(bit) + " (keyed or optional DDF block)").c_str());
		}
		nectar::PcrParticle x = nectar::inertPcrParticle();
		x.colour.thresholds   = { 0.0f };
		x.colour.primary.resize(1);
		x.colour.env.resize(1);
		rejects(x, "one colour frame (update reads the next threshold)");
		x = nectar::inertPcrParticle();
		x.colour.duration = 1;
		rejects(x, "duration 1 (update divides by duration - 1)");
		x = nectar::inertPcrParticle();
		x.colour.env.resize(1);
		rejects(x, "colour arrays of different lengths");
		x = nectar::inertPcrParticle();
		x.version = "1-0";
		rejects(x, "a version tag that is not 4 bytes");
	}

	// ---- names: 331 distinct, in step with effectMgr.cpp ---------------------------------------------
	const std::vector<std::string>& names = nectar::standinPcrNames();
	std::vector<std::string> regs         = registrations();
	{
		check(regs.size() == 332, "ptclLoadInfo has 332 registrations (found " + std::to_string(regs.size()) + ")");
		std::vector<std::string> distinct;
		std::set<std::string> seen;
		for (const std::string& s : regs)
			if (seen.insert(s).second) distinct.push_back(s);
		check(distinct.size() == 331, "331 distinct names in ptclLoadInfo");
		check(names == distinct, "standinPcrNames() equals the distinct literals of ptclLoadInfo, in order (drift test)");
		std::set<std::string> uniq(names.begin(), names.end());
		check(names.size() == 331 && uniq.size() == 331, "table has 331 unique names");
		for (const std::string& s : names)
			check(s.rfind("effects/pcr/", 0) == 0 && s.size() > 16 && s.compare(s.size() - 4, 4, ".pcr") == 0, "name shape: " + s);
		check(std::count(regs.begin(), regs.end(), std::string("effects/pcr/r7meteor.pcr")) == 2, "r7meteor.pcr is registered twice");
	}

	// ---- the 2D names: 52, in step with effectMgr2D.cpp, none shared with the 331 -------------------------
	{
		const std::vector<std::string>& n2 = nectar::standinPcr2dNames();
		check(registrations2d().size() == 52, "ptclInfo has 52 registrations (found " + std::to_string(registrations2d().size()) + ")");
		check(n2 == registrations2d(), "standinPcr2dNames() equals the literals of ptclInfo in effectMgr2D.cpp, in order (drift test)");
		std::set<std::string> uniq(n2.begin(), n2.end()), main331(names.begin(), names.end());
		check(uniq.size() == n2.size(), "the 2D names are distinct");
		int shared = 0;
		for (const std::string& s : n2) shared += main331.count(s) ? 1 : 0;
		check(shared == 0, "no 2D name is also one of the 331");
	}

	// ---- effects.bun: type-0 entries under every name ------------------------------------------
	{
		std::vector<nectar::BundleEntry> entries;
		for (const std::string& n : names) {
			nectar::BundleEntry e;
			e.type    = nectar::BUNDLE_BINARY;
			e.name    = n;
			e.payload = payload;
			entries.push_back(e);
		}
		std::vector<std::uint8_t> bun;
		check(nectar::writeBundle(entries, bun, err), "write effects.bun: " + err);
		std::vector<nectar::BundleEntry> back;
		check(nectar::readBundle(bun.data(), bun.size(), back, err), "read effects.bun back: " + err);
		check(back.size() == 331, "331 entries");
		std::map<std::string, const nectar::BundleEntry*> byName;
		for (const nectar::BundleEntry& e : back) byName[e.name] = &e;
		// particleLoader::load: findGfxObject(name, '_bin') is an exact strcmp.
		int missing = 0, wrongType = 0, wrongBytes = 0;
		for (const std::string& n : regs) {
			auto it = byName.find(n);
			if (it == byName.end()) missing++;
			else {
				wrongType += it->second->type != nectar::BUNDLE_BINARY;
				wrongBytes += it->second->payload != payload;
			}
		}
		check(missing == 0 && wrongType == 0 && wrongBytes == 0, "every one of the 332 registrations resolves to a type-0 entry with the payload");
		// A type-0 entry is read as exactly `size` bytes: the payload length is the size.
		check(back[0].payload.size() == 172, "entry size is the payload length");
	}

	// ---- optional: the reader against real .pcr files -----------------------------------------------------
	if (const char* dir = std::getenv("NECTAR_DISC_DATA")) {
		int files = 0, bad = 0;
		for (const std::string& n : names) {
			std::vector<std::uint8_t> raw;
			if (!readFile(std::string(dir) + "/" + n, raw)) continue;
			files++;
			Parsed r = parse(raw);
			if (!r.ok || r.ddfEnd > raw.size()) {
				std::printf("  disc: %s runs past its end (reader %s, DDF end %zu of %zu)\n", n.c_str(), r.ok ? "ok" : "stopped", r.ddfEnd, raw.size());
				bad++;
			}
		}
		std::printf("disc check: %d .pcr files, %d problems\n", files, bad);
		check(bad == 0, "disc .pcr files walk to their end");
	}

	if (failures) {
		std::printf("%d failure(s)\n", failures);
		return 1;
	}
	std::printf("pcr_writer_test: ok\n");
	return 0;
}
