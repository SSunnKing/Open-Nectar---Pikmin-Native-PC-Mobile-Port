// .cin and .dsk writers (cinema.h). The token layout follows the loaders:
// CinematicPlayer::loadCin (cinePlayer.cpp) and SceneData::parse, getAnimInfo
// and DataChunk::getData (shapeBase.cpp, Animator.h). All three read with
// CmdStream, which splits on whitespace and commas, and treats '#' or "//" as a
// comment only where a token starts. Comments are written only before the first
// command, because inside a section the loaders test for '}' before skipping
// comments.
#include "nectar_assets/cinema.h"

#include <cmath>
#include <cstdio>
#include <set>

namespace nectar {
namespace {

bool fail(std::string& error, const std::string& what)
{
	error = what;
	return false;
}

/// A token the CmdStream reads back whole: non-empty, no separator, no quote,
/// no brace, no comment start.
bool plainToken(const std::string& s)
{
	if (s.empty() || s[0] == '#' || s.rfind("//", 0) == 0) return false;
	for (char c : s) {
		if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == ',' || c == '"' || c == '\'' || c == '{' || c == '}' || c == '\0') return false;
	}
	return true;
}

std::string num(float v)
{
	char buf[32];
	std::snprintf(buf, sizeof buf, "%.9g", double(v));
	return buf;
}

std::string num(long long v) { return std::to_string(v); }

} // namespace

bool writeCin(const Cinema& cin, std::string& out, std::string& error)
{
	out.clear();
	if (!cin.cuts.empty() && cin.scenes.empty()) return fail(error, "a cut needs at least one scene (addSceneCut reads scene 0)");
	std::set<std::string> shapes;
	for (const std::string& s : cin.scenes) {
		if (!plainToken(s)) return fail(error, "scene path is not a plain token: '" + s + "'");
	}
	for (const CinActorFiles& a : cin.actors) {
		if (!plainToken(a.shape)) return fail(error, "actor shape is not a plain token: '" + a.shape + "'");
		if (!a.bundle.empty() && !plainToken(a.bundle)) return fail(error, "actor bundle is not a plain token: '" + a.bundle + "'");
		if (!a.anims.empty() && !plainToken(a.anims)) return fail(error, "actor anims is not a plain token: '" + a.anims + "'");
		if (!shapes.insert(a.shape).second) return fail(error, "actor listed twice: " + a.shape);
	}
	for (std::size_t i = 0; i < cin.cuts.size(); i++) {
		const CinCut& c = cin.cuts[i];
		const std::string at = "cut " + std::to_string(i) + ": ";
		if (c.scene < 0 || std::size_t(c.scene) >= cin.scenes.size()) return fail(error, at + "scene index out of range");
		if (c.startFrame < 0 || c.endFrame < 0) return fail(error, at + "negative frame");
		for (const CinCutActor& a : c.actors) {
			if (!shapes.count(a.shape)) return fail(error, at + "actor not in the actor list: " + a.shape);
		}
		for (const CinKey& k : c.keys) {
			if (k.eventType < 0 || k.eventType > 2) return fail(error, at + "key event type must be 0, 1 or 2");
		}
	}

	out += "// Movie flags\n";
	out += "type\t" + num((long long)cin.type) + "\n";
	out += "flags\t" + num((long long)cin.flags) + "\n\n";
	for (const std::string& s : cin.scenes) out += "addScene {\tscene\t" + s + "\n\t}\n\n";
	for (const CinActorFiles& a : cin.actors) {
		out += "addActor {\tshape\t" + a.shape + "\n";
		if (!a.bundle.empty()) out += "\tbundle\t" + a.bundle + "\n";
		if (!a.anims.empty()) out += "\tanims\t" + a.anims + "\n";
		out += "\t}\n\n";
	}
	for (const CinCut& c : cin.cuts) {
		out += "addCut {\tcut\t" + num((long long)c.scene) + "\t" + num((long long)c.startFrame) + "\t" + num((long long)c.endFrame) + "\n";
		out += "\tflags\t" + num((long long)c.flags) + "\n";
		for (const CinCutActor& a : c.actors) {
			out += "\tactor\t" + a.shape + "\n";
			out += "\tacflags\t" + num((long long)a.acflags) + "\n";
			out += "\tanim\t" + num((long long)a.animState) + "\t" + num((long long)a.colourAnim) + "\n";
		}
		if (!c.keys.empty()) {
			out += "\tkeys\t" + num((long long)c.keys.size()) + "\t{\n";
			for (const CinKey& k : c.keys) {
				out += "\t\t" + num((long long)k.eventType) + "\t" + num((long long)k.cmd) + "\t" + num((long long)k.value) + "\t" + num((long long)k.frame) + "\n";
			}
			out += "\t\t}\n";
		}
		out += "\t}\n\n";
	}
	return true;
}

namespace {

/// Appends a track's floats to `data` and returns its table line, or "" when
/// the track is empty (the field is then left out and keeps the game default).
bool trackLine(const char* field, const DskTrack& t, std::vector<float>& data, std::string& line, std::string& error)
{
	line.clear();
	if (t.empty()) return true;
	for (const DskKey& k : t) {
		if (!std::isfinite(k.frame) || !std::isfinite(k.value) || !std::isfinite(k.tangent)) return fail(error, std::string(field) + ": non-finite key");
	}
	const std::size_t offset = data.size();
	if (t.size() == 1) {
		data.push_back(t[0].value);
	} else {
		for (std::size_t i = 0; i < t.size(); i++) {
			if (i > 0 && !(t[i].frame > t[i - 1].frame)) return fail(error, std::string(field) + ": key frames must increase");
			data.push_back(t[i].frame);
			data.push_back(t[i].value);
			data.push_back(t[i].tangent);
		}
	}
	// Flags 0: three floats per key (extract's dataSize).
	line = std::string("\t") + field + "\t" + std::to_string(t.size()) + "\t" + std::to_string(offset) + "\t0\n";
	return true;
}

/// DataChunk::getData: "size N", then "float" before every run of 8 values.
std::string chunk(const char* tag, const std::vector<float>& data)
{
	std::string s = std::string(tag) + "\n{\n\tsize\t" + std::to_string(data.size()) + "\n";
	for (std::size_t i = 0; i < data.size(); i += 8) {
		s += "\tfloat";
		for (std::size_t j = i; j < data.size() && j < i + 8; j++) s += "\t" + num(data[j]);
		s += "\n";
	}
	return s + "}\n\n";
}

} // namespace

bool writeDsk(const DskScene& scene, std::string& out, std::string& error)
{
	out.clear();
	if (scene.numFrames <= 0) return fail(error, "numFrames must be positive");
	if (scene.cameras.empty()) return fail(error, "a scene needs a camera (MovieInfo::setCamera reads camera 0)");

	std::vector<float> camData, lightData;
	std::string camTables, lightTables, line;
	for (std::size_t i = 0; i < scene.cameras.size(); i++) {
		const DskCamera& c = scene.cameras[i];
		if (!plainToken(c.name)) return fail(error, "camera name is not a plain token");
		if (c.fovy.empty()) return fail(error, "camera " + c.name + ": fovy must be set (the game default is 1 degree)");
		camTables += "<KEY_CAMERA_TABLE>\n{\n\tindex\t" + std::to_string(i) + "\n\tname\t" + c.name + "\n";
		const std::pair<const char*, const DskTrack*> fields[] = {
			{ "cam_pos_x", &c.posX }, { "cam_pos_y", &c.posY }, { "cam_pos_z", &c.posZ }, { "cam_lat_x", &c.latX },
			{ "cam_lat_y", &c.latY }, { "cam_lat_z", &c.latZ }, { "cam_twist", &c.twist }, { "cam_fovy", &c.fovy },
			{ "cam_near", &c.nearZ }, { "cam_far", &c.farZ },
		};
		for (const auto& f : fields) {
			if (!trackLine(f.first, *f.second, camData, line, error)) return fail(error, "camera " + c.name + ": " + error);
			camTables += line;
		}
		camTables += "}\n\n";
	}
	for (std::size_t i = 0; i < scene.lights.size(); i++) {
		const DskLight& l = scene.lights[i];
		if (!plainToken(l.name)) return fail(error, "light name is not a plain token");
		lightTables += "<KEY_DIFFUSE_LIGHT_TABLE>\n{\n\tindex\t" + std::to_string(i) + "\n\tname\t" + l.name + "\n";
		const std::pair<const char*, const DskTrack*> fields[] = {
			{ "light_pos_x", &l.posX }, { "light_pos_y", &l.posY }, { "light_pos_z", &l.posZ }, { "light_r_param", &l.r },
			{ "light_g_param", &l.g },  { "light_b_param", &l.b },  { "light_visible", &l.visible },
		};
		for (const auto& f : fields) {
			if (!trackLine(f.first, *f.second, lightData, line, error)) return fail(error, "light " + l.name + ": " + error);
			lightTables += line;
		}
		lightTables += "}\n\n";
	}

	out += "<SCENE_KEY_ANM_INFO>\n{\n";
	out += "\tnumframes\t" + std::to_string(scene.numFrames) + "\n";
	out += "\tnumcameras\t" + std::to_string(scene.cameras.size()) + "\n";
	out += "\tnumDifLights\t" + std::to_string(scene.lights.size()) + "\n";
	out += "}\n\n";
	// A chunk of size 0 would make getData swallow the closing brace, so an
	// empty one is left out (every table field is then empty too).
	if (!camData.empty()) out += chunk("<KEY_CAMERA_ANM>", camData);
	out += camTables;
	if (!lightData.empty()) out += chunk("<KEY_DIFFUSE_LIGHT_ANM>", lightData);
	out += lightTables;
	return true;
}

} // namespace nectar
