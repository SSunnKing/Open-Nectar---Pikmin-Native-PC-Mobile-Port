#include "nectar_assets/standin_world.h"

#include "be_writer.h"
#include "nectar_assets/anim.h"
#include "nectar_assets/bundle.h"
#include "nectar_assets/mod_writer.h"
#include "nectar_assets/parms.h"
#include "nectar_assets/standin_sandbox.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace nectar {

namespace {

std::string partText(const WorldPart& p, int depth, bool open)
{
	const std::string ind(std::size_t(depth), '\t');
	char buf[512];
	if (!open) return ind + "\t}\n";
	if (p.type == 2) {
		std::snprintf(buf, sizeof buf, "%scollinfo %d {\n%s\tid\t\t%s\n%s\tcode\t\t%s\n%s\ttype\t2\n%s\tplatform\t%s\n", ind.c_str(), p.joint, ind.c_str(), p.id,
		              ind.c_str(), p.code, ind.c_str(), ind.c_str(), p.platform);
	} else {
		std::snprintf(buf, sizeof buf, "%scollinfo %d {\n%s\tid\t\t%s\n%s\tcode\t\t%s\n%s\ttype\t1\n%s\tradius\t%f\n%s\tcentre\t%f %f %f\n", ind.c_str(), p.joint,
		              ind.c_str(), p.id, ind.c_str(), p.code, ind.c_str(), ind.c_str(), double(p.radius), ind.c_str(), double(p.centre.x), double(p.centre.y),
		              double(p.centre.z));
	}
	return buf;
}

Rgba8 colourFor(const std::string& path)
{
	if (path.find("/plants/") != std::string::npos) return { 70, 160, 70, 255 };
	if (path.find("/tekis/") != std::string::npos) return { 200, 90, 70, 255 };
	if (path.find("/bosses/") != std::string::npos) return { 150, 70, 170, 255 };
	if (path.find("/bridge/") != std::string::npos) return { 150, 110, 70, 255 };
	if (path.find("/gate/") != std::string::npos) return { 170, 150, 110, 255 };
	return { 140, 140, 140, 255 };
}

/// Box size: a few units for the many-jointed models, else a share of the biggest sphere.
float halfFor(const WorldModelProfile& m)
{
	float r = 0.0f;
	for (const WorldPart& p : m.parts) r = std::max(r, p.radius);
	if (m.parents.size() > 30) return 5.0f;
	return std::clamp(r * 0.3f, 4.0f, 40.0f);
}

/// A platform model: one box on one joint whose triangles are also the collision.
Model platformModel()
{
	StandInModelSpec spec;
	spec.path     = "platform";
	spec.parents  = { -1 };
	spec.colour   = { 120, 120, 130, 255 };
	spec.halfSize = 10.0f;
	Model m       = buildStandInModel(spec);
	for (const ModMesh& mesh : m.meshes) {
		for (const ModMtxGroup& g : mesh.groups) {
			for (const ModTriangle& t : g.tris) {
				ModCollTri c;
				c.code = 0x20000000u; // rock, as the mapcore walls
				for (int k = 0; k < 3; k++) c.v[k] = t.v[k].pos;
				m.collision.push_back(c);
			}
		}
	}
	return m;
}

} // namespace

std::string worldCollInfo(const std::vector<WorldPart>& parts)
{
	std::string out = "// Collision info (nectar_standin_generator)\n";
	int open        = 0; // parts currently open
	for (const WorldPart& p : parts) {
		while (open > p.depth) {
			out += partText(p, open - 1, false);
			open--;
		}
		out += partText(p, p.depth, true);
		open = p.depth + 1;
	}
	while (open > 0) {
		out += partText(parts[0], open - 1, false);
		open--;
	}
	return out;
}

std::vector<StandInModelSpec> worldModelSpecs()
{
	std::vector<StandInModelSpec> out;
	for (const WorldModelProfile& m : worldModelProfiles()) {
		StandInModelSpec s;
		s.path     = m.path;
		s.parents  = m.parents;
		s.colour   = colourFor(m.path);
		s.halfSize = halfFor(m);
		if (!m.parts.empty()) s.collInfo = worldCollInfo(m.parts);
		if (s.path == "dataDir/objects/hinderrock/cube10.mod") {
			// The push box is solid: a 120-unit block whose side faces are the planes its four vtx parts (the disc's,
			// at x and z +-60) define (HinderRock::updatePlanes), so pushing Pikmin stop on the face and stay on its
			// side (HinderRock::getPlaneFlag, ActPush::exeGo). Without the collision they walk into the box.
			s.halfSize = 60.0f;
			s.platform = true;
		}
		out.push_back(std::move(s));
	}
	return out;
}

bool writeStandInWorldFiles(std::vector<StandInAnimFile>& out, std::string& error)
{
	// Platform models (the gate's two come from the sandbox gate).
	for (const std::string& path : worldPlatformPaths()) {
		std::vector<std::uint8_t> bytes;
		if (!writePlatformMod(platformModel(), bytes, error)) {
			error = path + ": " + error;
			return false;
		}
		out.push_back({ path, std::move(bytes) });
	}

	// gt_soft.anm. The soft gate shares objects/gate/gate.bin with the sandbox's
	// bomb gate (standin_sandbox.cpp), whose table has six infos named gate0 to
	// gate5 under the base directory "gate", 30 frames each. The soft gate's model
	// has four joints (the disc's), so its bundle carries those entries for four
	// joints. The names and length are read back from the sandbox's table so the
	// two cannot drift apart.
	{
		std::vector<SandboxFile> gate;
		if (!writeSandboxGateFiles(gate, error)) return false;
		const SandboxFile* table = nullptr;
		for (const SandboxFile& f : gate) {
			if (f.path == "dataDir/objects/gate/gate.bin") table = &f;
		}
		if (!table) {
			error = "the sandbox gate wrote no objects/gate/gate.bin";
			return false;
		}
		// AnimMgr: version, count, a Parameters block ('a00', 'a01'), then per info a name...
		// Only the info names are needed: read them with a minimal walk of the known layout.
		std::vector<BundleEntry> sandboxBundle;
		for (const SandboxFile& f : gate) {
			if (f.path == "dataDir/objects/gate/gt_hard.anm" && !readBundle(f.bytes.data(), f.bytes.size(), sandboxBundle, error)) return false;
		}
		const WorldModelProfile* soft = nullptr;
		for (const WorldModelProfile& m : worldModelProfiles()) {
			if (std::strcmp(m.path, "dataDir/objects/gate/gt_soft.mod") == 0) soft = &m;
		}
		if (!soft || sandboxBundle.empty()) {
			error = "no soft gate profile or no sandbox gate bundle";
			return false;
		}
		std::vector<BundleEntry> entries;
		for (const BundleEntry& e : sandboxBundle) {
			// The payload starts with the joint count and the frame count.
			const int frames = (int(e.payload[4]) << 24) | (int(e.payload[5]) << 16) | (int(e.payload[6]) << 8) | int(e.payload[7]);
			BundleEntry n;
			n.type = BUNDLE_DCK;
			n.name = e.name;
			if (!writeDckPayload(makeStaticAnim(soft->parents, frames), n.payload, error)) return false;
			entries.push_back(std::move(n));
		}
		StandInAnimFile bundle;
		bundle.path = "dataDir/objects/gate/gt_soft.anm";
		if (!writeBundle(entries, bundle.bytes, error)) return false;
		out.push_back(std::move(bundle));
	}

	// tekipara/*.bin: the words of the disc's files.
	for (const TekiParaProfile& t : tekiParaProfiles()) {
		BeWriter w;
		for (std::uint32_t v : t.words) w.u32(v);
		out.push_back({ std::string("dataDir/tekipara/") + t.name + ".bin", std::move(w.b) });
	}

	// bosses/*/parms.bin: tagged lists.
	for (const BossParmProfile& b : bossParmProfiles()) {
		ParmFile f;
		for (const auto& list : b.lists) {
			std::vector<ParmEntry> entries;
			for (const auto& kv : list) {
				ParmEntry e;
				e.id = kv.first;
				BeWriter w;
				w.u32(kv.second);
				e.payload = std::move(w.b);
				entries.push_back(std::move(e));
			}
			f.lists.push_back(std::move(entries));
		}
		std::vector<std::uint8_t> bytes;
		if (!writeParmFile(f, bytes, error)) {
			error = std::string(b.path) + ": " + error;
			return false;
		}
		out.push_back({ b.path, std::move(bytes) });
	}
	return true;
}

} // namespace nectar
