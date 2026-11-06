// Write-then-read tests for nectar::writeCin, nectar::writeDsk and the stand-in
// cinemas (cinema.h).
//
// Oracle: the game's real CmdStream (cmdStream.cpp, linked with
// game_loader_stubs.cpp, as in archive_stages_writer_test.cpp). The loaders on
// top of it cannot link into a host test: CinematicPlayer::loadCin
// (cinePlayer.cpp) needs gsys, gameflow and the shape loader, and
// SceneData::parse / getAnimInfo sit in shapeBase.cpp with the whole renderer.
// Their token loops are transcribed below (same calls, same order, same
// sscanf formats), together with DataChunk::getData (Animator.h) and the
// constant-track read of CamDataInfo::update. DRIFT NOTE: if loadCin,
// SceneData::parse, getAnimInfo or DataChunk::getData change, update the copies.
// The scenario runs (scenario_*) load the stand-in demo60 through the real code.
//
// Optional argument: boot-manifest.txt. Every cinemas/ path it lists must be a
// stand-in file.
#include "nectar_assets/cinema.h"

#include "CmdStream.h"
#include "Stream.h"

#include <cstdio>
#include <cstring>
#include <fstream>
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

// ── CinematicPlayer::loadCin (cinePlayer.cpp:131-279) ───────────────────────
struct ReadKey {
	int type, cmd, value, frame;
};
struct ReadActor {
	std::string shape;
	int acflags = 2, anim = 1, colour = -1;
};
struct ReadCut {
	int scene = 0, start = 0, end = 0, flags = 3;
	std::vector<ReadActor> actors;
	std::vector<ReadKey> keys;
};
struct ReadCin {
	int type = 0, flags = 0;
	std::vector<std::string> scenes;
	std::vector<nectar::CinActorFiles> actors;
	std::vector<ReadCut> cuts;
	bool nullDeref = false; ///< an actor field with no actor, a key or flags with no cut
};

ReadCin loadCin(const std::string& text)
{
	ReadCin r;
	RamStream file(const_cast<char*>(text.data()), int(text.size()));
	CmdStream* cmd = new CmdStream(&file);
	while (!cmd->endOfCmds() && !cmd->endOfSection()) {
		cmd->getToken(true);
		if (cmd->isToken("type")) {
			sscanf(cmd->getToken(true), "%d", &r.type);
			continue;
		}
		if (cmd->isToken("flags")) {
			sscanf(cmd->getToken(true), "%d", &r.flags);
			continue;
		}
		if (cmd->isToken("addScene")) {
			cmd->getToken(true);
			while (!cmd->endOfCmds() && !cmd->endOfSection()) {
				cmd->getToken(true);
				if (cmd->isToken("scene")) r.scenes.push_back(cmd->getToken(true));
			}
			if (!cmd->endOfCmds()) cmd->getToken(true);
			continue;
		}
		if (cmd->isToken("addActor")) {
			cmd->getToken(true);
			nectar::CinActorFiles* actor = nullptr;
			while (!cmd->endOfCmds() && !cmd->endOfSection()) {
				cmd->getToken(true);
				if (cmd->isToken("shape")) {
					r.actors.emplace_back();
					actor        = &r.actors.back();
					actor->shape = cmd->getToken(true);
				} else if (cmd->isToken("bundle")) {
					if (!actor) r.nullDeref = true;
					else actor->bundle = cmd->getToken(true);
				} else if (cmd->isToken("anims")) {
					if (!actor) r.nullDeref = true;
					else actor->anims = cmd->getToken(true);
				}
			}
			if (!cmd->endOfCmds()) cmd->getToken(true);
			continue;
		}
		if (cmd->isToken("addCut")) {
			cmd->getToken(true);
			ReadCut* scene   = nullptr;
			ReadActor* actor = nullptr;
			while (!cmd->endOfCmds() && !cmd->endOfSection()) {
				cmd->getToken(true);
				if (cmd->isToken("cut")) {
					r.cuts.emplace_back();
					scene = &r.cuts.back();
					sscanf(cmd->getToken(true), "%d", &scene->scene);
					sscanf(cmd->getToken(true), "%d", &scene->start);
					sscanf(cmd->getToken(true), "%d", &scene->end);
					actor = nullptr;
					continue;
				}
				if (cmd->isToken("flags")) {
					if (!scene) { r.nullDeref = true; cmd->getToken(true); continue; }
					sscanf(cmd->getToken(true), "%d", &scene->flags);
					continue;
				}
				if (cmd->isToken("actor")) {
					if (!scene) { r.nullDeref = true; cmd->getToken(true); continue; }
					scene->actors.emplace_back();
					actor        = &scene->actors.back();
					actor->shape = cmd->getToken(true);
					continue;
				}
				if (cmd->isToken("acflags")) {
					if (!actor) { r.nullDeref = true; cmd->getToken(true); continue; }
					sscanf(cmd->getToken(true), "%d", &actor->acflags);
					continue;
				}
				if (cmd->isToken("anim")) {
					if (!actor) { r.nullDeref = true; cmd->getToken(true); cmd->getToken(true); continue; }
					sscanf(cmd->getToken(true), "%d", &actor->anim);
					sscanf(cmd->getToken(true), "%d", &actor->colour);
					continue;
				}
				if (cmd->isToken("keys")) {
					int max = 0;
					sscanf(cmd->getToken(true), "%d", &max);
					cmd->getToken(true);
					for (int i = 0; i < max; i++) {
						ReadKey k {};
						sscanf(cmd->getToken(true), "%d", &k.type);
						sscanf(cmd->getToken(true), "%d", &k.cmd);
						sscanf(cmd->getToken(true), "%d", &k.value);
						sscanf(cmd->getToken(true), "%d", &k.frame);
						if (!scene) r.nullDeref = true;
						else scene->keys.push_back(k);
					}
					cmd->getToken(true);
					continue;
				}
			}
			if (!cmd->endOfCmds()) cmd->getToken(true);
		}
	}
	delete cmd;
	return r;
}

// ── SceneData::parse, getAnimInfo (shapeBase.cpp:779-983), DataChunk::getData
// (Animator.h:110-135) ───────────────────────────────────────────────────────
struct Param {
	int entries = 0, offset = 0, flags = 0;
};
struct ReadDsk {
	int numFrames = 0, numCameras = 0, numLights = 0;
	std::vector<float> camData, lightData;
	int camDeclared = -1, lightDeclared = -1;
	std::vector<std::vector<Param>> cams;   ///< 10 per camera, cam_pos_x .. cam_far
	std::vector<std::vector<Param>> lights; ///< 7 per light, light_pos_x .. light_visible
	bool badIndex = false;
};

void getData(CmdStream* stream, std::vector<float>& out, int& declared)
{
	stream->getToken(true);
	int tokenCount = 0;
	while (!stream->endOfCmds() && !stream->endOfSection()) {
		if ((tokenCount & 7) == 0) {
			stream->getToken(true);
			if (stream->isToken("size")) {
				sscanf(stream->getToken(true), "%d", &declared);
				stream->getToken(true);
			}
		}
		float data;
		sscanf(stream->getToken(true), "%f", &data);
		out.push_back(data);
		++tokenCount;
	}
	if (!stream->endOfCmds()) stream->getToken(true);
}

void readTable(CmdStream* stream, std::vector<std::vector<Param>>& slots, const char* const* names, int count, bool& badIndex)
{
	stream->getToken(true);
	int index = -1;
	while (!stream->endOfCmds() && !stream->endOfSection()) {
		stream->getToken(true);
		if (stream->isToken("index")) {
			sscanf(stream->getToken(true), "%d", &index);
		} else if (stream->isToken("name")) {
			stream->getToken(true);
		} else {
			for (int f = 0; f < count; f++) {
				if (!stream->isToken(names[f])) continue;
				if (index < 0 || index >= int(slots.size())) {
					badIndex = true;
					for (int i = 0; i < 3; i++) stream->getToken(true);
					break;
				}
				Param& p = slots[index][f];
				sscanf(stream->getToken(true), "%d", &p.entries);
				sscanf(stream->getToken(true), "%d", &p.offset);
				sscanf(stream->getToken(true), "%d", &p.flags);
				break;
			}
		}
	}
	if (!stream->endOfCmds()) stream->getToken(true);
}

const char* const kCamFields[]   = { "cam_pos_x", "cam_pos_y", "cam_pos_z", "cam_lat_x", "cam_lat_y", "cam_lat_z", "cam_twist", "cam_fovy", "cam_near", "cam_far" };
const char* const kLightFields[] = { "light_pos_x", "light_pos_y", "light_pos_z", "light_r_param", "light_g_param", "light_b_param", "light_visible" };

ReadDsk parseDsk(const std::string& text)
{
	ReadDsk r;
	RamStream file(const_cast<char*>(text.data()), int(text.size()));
	CmdStream* stream = new CmdStream(&file);
	while (!stream->endOfCmds() && !stream->endOfSection()) {
		stream->getToken(true);
		if (stream->isToken("<SCENE_KEY_ANM_INFO>")) {
			stream->getToken(true);
			while (!stream->endOfCmds() && !stream->endOfSection()) {
				stream->getToken(true);
				if (stream->isToken("numcameras")) {
					sscanf(stream->getToken(true), "%d", &r.numCameras);
					r.cams.assign(r.numCameras, std::vector<Param>(10));
				} else if (stream->isToken("numDifLights")) {
					sscanf(stream->getToken(true), "%d", &r.numLights);
					r.lights.assign(r.numLights, std::vector<Param>(7));
				} else if (stream->isToken("numframes")) {
					sscanf(stream->getToken(true), "%d", &r.numFrames);
				} else {
					stream->skipLine();
				}
			}
			if (!stream->endOfCmds()) stream->getToken(true);
		} else if (stream->isToken("<KEY_CAMERA_ANM>")) {
			getData(stream, r.camData, r.camDeclared);
		} else if (stream->isToken("<KEY_CAMERA_TABLE>")) {
			readTable(stream, r.cams, kCamFields, 10, r.badIndex);
		} else if (stream->isToken("<KEY_DIFFUSE_LIGHT_ANM>")) {
			getData(stream, r.lightData, r.lightDeclared);
		} else if (stream->isToken("<KEY_DIFFUSE_LIGHT_TABLE>")) {
			readTable(stream, r.lights, kLightFields, 7, r.badIndex);
		} else {
			stream->getToken(true);
			while (!stream->endOfCmds() && !stream->endOfSection()) stream->getToken(true);
			if (!stream->endOfCmds()) stream->getToken(true);
		}
	}
	delete stream;
	return r;
}

/// Compares a written track with what the reader stored for it.
void checkTrack(const nectar::DskTrack& t, const Param& p, const std::vector<float>& data, const std::string& what)
{
	check(p.entries == int(t.size()), what + ": entry count");
	if (t.empty()) return;
	check(p.flags == 0, what + ": flags 0 (three floats per key)");
	if (t.size() == 1) {
		check(p.offset >= 0 && p.offset < int(data.size()) && data[p.offset] == t[0].value, what + ": constant value");
		return;
	}
	bool ok = p.offset >= 0 && p.offset + int(t.size()) * 3 <= int(data.size());
	for (std::size_t i = 0; ok && i < t.size(); i++) {
		ok = data[p.offset + 3 * i] == t[i].frame && data[p.offset + 3 * i + 1] == t[i].value && data[p.offset + 3 * i + 2] == t[i].tangent;
	}
	check(ok, what + ": keys");
}

void checkDsk(const nectar::DskScene& in, const std::string& name)
{
	std::string text, err;
	check(nectar::writeDsk(in, text, err), name + ": write (" + err + ")");
	const ReadDsk r = parseDsk(text);
	check(r.numFrames == in.numFrames, name + ": numframes");
	check(r.numCameras == int(in.cameras.size()) && r.numLights == int(in.lights.size()), name + ": counts");
	check(!r.badIndex, name + ": every table index is in range");
	check(r.camDeclared < 0 || r.camDeclared == int(r.camData.size()), name + ": camera chunk size matches its values");
	check(r.lightDeclared < 0 || r.lightDeclared == int(r.lightData.size()), name + ": light chunk size matches its values");
	for (std::size_t i = 0; i < in.cameras.size() && i < r.cams.size(); i++) {
		const nectar::DskCamera& c = in.cameras[i];
		const nectar::DskTrack* t[] = { &c.posX, &c.posY, &c.posZ, &c.latX, &c.latY, &c.latZ, &c.twist, &c.fovy, &c.nearZ, &c.farZ };
		for (int f = 0; f < 10; f++) checkTrack(*t[f], r.cams[i][f], r.camData, name + " camera " + std::to_string(i) + " " + kCamFields[f]);
	}
	for (std::size_t i = 0; i < in.lights.size() && i < r.lights.size(); i++) {
		const nectar::DskLight& l = in.lights[i];
		const nectar::DskTrack* t[] = { &l.posX, &l.posY, &l.posZ, &l.r, &l.g, &l.b, &l.visible };
		for (int f = 0; f < 7; f++) checkTrack(*t[f], r.lights[i][f], r.lightData, name + " light " + std::to_string(i) + " " + kLightFields[f]);
	}
}

void checkCin(const nectar::Cinema& in, const std::string& name)
{
	std::string text, err;
	check(nectar::writeCin(in, text, err), name + ": write (" + err + ")");
	const ReadCin r = loadCin(text);
	check(!r.nullDeref, name + ": no field the loader would apply to a null actor or cut");
	check(r.type == int(in.type) && std::uint32_t(r.flags) == in.flags, name + ": type and flags");
	check(r.scenes == in.scenes, name + ": scenes");
	check(r.actors.size() == in.actors.size(), name + ": actor count");
	for (std::size_t i = 0; i < in.actors.size() && i < r.actors.size(); i++) {
		check(r.actors[i].shape == in.actors[i].shape && r.actors[i].bundle == in.actors[i].bundle && r.actors[i].anims == in.actors[i].anims,
		      name + ": actor files");
	}
	check(r.cuts.size() == in.cuts.size(), name + ": cut count");
	for (std::size_t i = 0; i < in.cuts.size() && i < r.cuts.size(); i++) {
		const nectar::CinCut& c = in.cuts[i];
		const ReadCut& rc       = r.cuts[i];
		const std::string at    = name + " cut " + std::to_string(i);
		check(rc.scene == c.scene && rc.start == c.startFrame && rc.end == c.endFrame && rc.flags == c.flags, at + ": header");
		check(rc.actors.size() == c.actors.size(), at + ": actor count");
		for (std::size_t a = 0; a < c.actors.size() && a < rc.actors.size(); a++) {
			check(rc.actors[a].shape == c.actors[a].shape && std::uint32_t(rc.actors[a].acflags) == c.actors[a].acflags
			          && rc.actors[a].anim == c.actors[a].animState && rc.actors[a].colour == c.actors[a].colourAnim,
			      at + ": actor");
		}
		check(rc.keys.size() == c.keys.size(), at + ": key count");
		for (std::size_t k = 0; k < c.keys.size() && k < rc.keys.size(); k++) {
			check(rc.keys[k].type == c.keys[k].eventType && rc.keys[k].cmd == c.keys[k].cmd && rc.keys[k].value == c.keys[k].value
			          && rc.keys[k].frame == c.keys[k].frame,
			      at + ": key " + std::to_string(k));
		}
		// Every cut's scene index must name a scene (findScene returns null otherwise).
		check(rc.scene >= 0 && rc.scene < int(r.scenes.size()), at + ": scene index names a scene");
	}
}

} // namespace

int main(int argc, char** argv)
{
	std::string err, text;

	// ---- .dsk ----
	{
		nectar::DskScene s;
		s.numFrames = 45;
		nectar::DskCamera c;
		c.posX  = { { 0, 1.5f, 0 }, { 20, -7.25f, 0.5f }, { 45, 3, -1 } };
		c.posY  = { { 0, 100, 0 } };
		c.posZ  = { { 0, 200, 0 }, { 45, 150, 0 } };
		c.latY  = { { 0, 10, 0 } };
		c.fovy  = { { 0, 35, 0 } };
		c.farZ  = { { 0, 9000, 0 } };
		nectar::DskCamera c2 = c;
		c2.name = "second";
		c2.posX = { { 0, -1, 0 } };
		s.cameras = { c, c2 };
		nectar::DskLight l;
		l.posY    = { { 0, 500, 0 } };
		l.r       = { { 0, 200, 0 }, { 10, 100, 0 } };
		l.g       = { { 0, 255, 0 } };
		l.b       = { { 0, 0, 0 } };
		l.visible = { { 0, 1, 0 } };
		s.lights  = { l, l };
		checkDsk(s, "dsk two cameras two lights");

		// More than 8 floats crosses DataChunk::getData's "float" lines.
		nectar::DskScene big = s;
		for (int i = 0; i < 9; i++) big.cameras[0].posZ.push_back({ float(50 + i), float(i), 0 });
		big.numFrames = 60;
		checkDsk(big, "dsk long curve");

		nectar::DskScene noLights = s;
		noLights.lights.clear();
		checkDsk(noLights, "dsk no lights");

		nectar::DskScene bad = s;
		bad.cameras.clear();
		check(!nectar::writeDsk(bad, text, err) && !err.empty(), "dsk with no camera rejected");
		bad = s;
		bad.numFrames = 0;
		check(!nectar::writeDsk(bad, text, err), "dsk with no frames rejected");
		bad = s;
		bad.cameras[0].fovy.clear();
		check(!nectar::writeDsk(bad, text, err), "camera with no fovy rejected");
		bad = s;
		bad.cameras[0].posZ = { { 10, 0, 0 }, { 10, 1, 0 } };
		check(!nectar::writeDsk(bad, text, err), "non-increasing key frames rejected");
		bad = s;
		bad.cameras[0].name = "two words";
		check(!nectar::writeDsk(bad, text, err), "camera name with a space rejected");
	}

	// ---- .cin ----
	{
		nectar::Cinema cin;
		cin.type   = 2;
		cin.flags  = nectar::kCinLocalized | nectar::kCinHideNavi;
		cin.scenes = { "cinemas/x/a.dsk", "cinemas/x/b.dsk" };
		cin.actors = { { "pikis/a.mod", "cinemas/x/a.anm", "cinemas/x/a.bin" }, { "objects/b.mod", "", "" } };
		nectar::CinCut c0;
		c0.endFrame = 100;
		c0.actors   = { { "pikis/a.mod", 3, 2, 0 }, { "objects/b.mod", 0x200, 1, -1 } };
		c0.keys     = { { nectar::kCinEventAction, 3, 5, 0 }, { nectar::kCinEventNotify, 0, 1, 99 } };
		nectar::CinCut c1;
		c1.scene      = 1;
		c1.startFrame = 30;
		c1.endFrame   = 10; // reversed cuts are legal: the player uses abs()
		c1.flags      = nectar::kCutLoop;
		cin.cuts      = { c0, c1 };
		checkCin(cin, "cin full");

		nectar::Cinema empty;
		checkCin(empty, "cin empty");

		nectar::Cinema bad = cin;
		bad.scenes.clear();
		check(!nectar::writeCin(bad, text, err) && !err.empty(), "cuts with no scene rejected");
		bad = cin;
		bad.cuts[0].scene = 2;
		check(!nectar::writeCin(bad, text, err), "cut scene index out of range rejected");
		bad = cin;
		bad.cuts[0].actors[0].shape = "pikis/missing.mod";
		check(!nectar::writeCin(bad, text, err), "cut actor missing from the actor list rejected");
		bad = cin;
		bad.scenes[0] = "cinemas/x y.dsk";
		check(!nectar::writeCin(bad, text, err), "path with a space rejected");
		bad = cin;
		bad.scenes[0] = "//comment.dsk";
		check(!nectar::writeCin(bad, text, err), "path that reads as a comment rejected");
		bad = cin;
		bad.cuts[0].keys[0].eventType = 7;
		check(!nectar::writeCin(bad, text, err), "unknown event type rejected");
	}

	// ---- stand-in cinemas ----
	std::vector<nectar::StandInCinemaFile> files;
	check(nectar::writeStandInCinemas(files, err), "stand-in cinemas (" + err + ")");
	std::set<std::string> paths;
	for (const auto& f : files) {
		check(paths.insert(f.path).second, f.path + ": written once");
		check(f.text.size() < 0x4000, f.path + ": fits CmdStream's first buffer fill");
	}
	for (const auto& f : files) {
		if (f.path.size() < 4 || f.path.compare(f.path.size() - 4, 4, ".cin") != 0) continue;
		const ReadCin r = loadCin(f.text);
		check(!r.nullDeref && !r.cuts.empty(), f.path + ": parses with cuts");
		check(r.actors.empty(), f.path + ": no actors (no model or animation is opened)");
		for (const std::string& s : r.scenes) {
			check(paths.count("dataDir/" + s) == 1, f.path + ": scene " + s + " is written");
			for (const auto& g : files) {
				if (g.path != "dataDir/" + s) continue;
				const ReadDsk d = parseDsk(g.text);
				check(d.numCameras >= 1 && !d.badIndex && d.numFrames > 0, g.path + ": a camera and frames");
				check(d.cams.empty() || d.cams[0][7].entries >= 1, g.path + ": fovy set");
			}
		}
		for (const ReadCut& c : r.cuts) {
			check(c.scene >= 0 && c.scene < int(r.scenes.size()), f.path + ": cut scene in range");
			// demo36 (the results backdrop) ends with a looping cut on purpose: it keeps the results screen up
			// until exitDayEnd drops it, as the disc's does. Every other stand-in ends on its own.
			// demo73 (the bad ending's escape) and demo77 (the neutral and happy endings' space flight)
			// do the same for the final results window, and demo20 (the Main Engine reaching the ship)
			// until its own TextDemo key skips the scene, as the disc's does; demo78 (the Whimsical Radar) likewise.
			if ((f.path == "dataDir/cinemas/demo36.cin" || f.path == "dataDir/cinemas/demo73.cin" || f.path == "dataDir/cinemas/demo77.cin"
			     || f.path == "dataDir/cinemas/demo20.cin" || f.path == "dataDir/cinemas/demo78.cin")
			    && &c == &r.cuts.back())
				check(c.flags == nectar::kCutLoop, f.path + ": the last cut loops until the results window closes it");
			else
				check(c.flags == nectar::kCutPlayOnce, f.path + ": every cut plays once, so the movie ends without a skip");
			for (const ReadKey& k : c.keys) {
				const int lo = c.start < c.end ? c.start : c.end, hi = c.start < c.end ? c.end : c.start;
				check(k.frame >= lo && k.frame < hi, f.path + ": key inside its cut, so playback fires it");
			}
		}
	}
	// What the scenario code relies on (standin-cinema.md).
	auto keysOf = [&](const std::string& path) {
		std::vector<ReadKey> keys;
		for (const auto& f : files) {
			if (f.path == path) {
				for (const ReadCut& c : loadCin(f.text).cuts) keys.insert(keys.end(), c.keys.begin(), c.keys.end());
			}
		}
		return keys;
	};
	auto has = [](const std::vector<ReadKey>& keys, int type, int cmd, int value) {
		for (const ReadKey& k : keys) {
			if (k.type == type && k.cmd == cmd && k.value == value) return true;
		}
		return false;
	};
	{
		const auto k60 = keysOf("dataDir/cinemas/demo60.cin");
		check(has(k60, nectar::kCinEventNotify, nectar::kMovieCmdTextDemo, 13), "demo60 opens the TUT_Zenmetu text window");
		check(has(k60, nectar::kCinEventAction, nectar::kDemoSenderRedOnion, nectar::kDemoOnionBonusPiki), "demo60 sends BonusPiki");
		const auto k04 = keysOf("dataDir/cinemas/demo04.cin");
		check(has(k04, nectar::kCinEventAction, nectar::kDemoSenderRedOnion, nectar::kDemoOnionBoot), "demo04 boots the red Onion");
		check(has(k04, nectar::kCinEventAction, nectar::kDemoSenderRedOnion, nectar::kDemoOnionEmitCone), "demo04 emits the cone");
		int boot = -1, cone = -1;
		for (const ReadKey& k : k04) {
			if (k.type == nectar::kCinEventAction && k.value == nectar::kDemoOnionBoot) boot = k.frame;
			if (k.type == nectar::kCinEventAction && k.value == nectar::kDemoOnionEmitCone) cone = k.frame;
		}
		check(boot >= 0 && cone > boot, "demo04: Boot comes before EmitCone, leaving a skip window between them");
		// demo06 (blue, the object stage's Onion, scenario onion_skip): Boot, then the
		// seed from the boot motion's KEY_Action0 at motion frame 60 (the stand-in
		// blue Onion table), then EmitCone, each with room for a skip between.
		const auto k06 = keysOf("dataDir/cinemas/demo06.cin");
		int boot6 = -1, cone6 = -1;
		for (const ReadKey& k : k06) {
			check(k.type != nectar::kCinEventAction || k.cmd == nectar::kDemoSenderBluOnion, "demo06 acts on the blue Onion only");
			if (k.type == nectar::kCinEventAction && k.value == nectar::kDemoOnionBoot) boot6 = k.frame;
			if (k.type == nectar::kCinEventAction && k.value == nectar::kDemoOnionEmitCone) cone6 = k.frame;
		}
		check(boot6 >= 5 && boot6 + 60 + 10 <= cone6, "demo06: windows before Boot, between Boot and the seed, and between the seed and EmitCone");
		// demo05 (yellow, scenario onion_day3): the same order with the disc's yellow boot
		// motion (KEY_Action0 at motion frame 240).
		const auto k05 = keysOf("dataDir/cinemas/demo05.cin");
		int boot5 = -1, cone5 = -1;
		for (const ReadKey& k : k05) {
			check(k.type != nectar::kCinEventAction || k.cmd == nectar::kDemoSenderYelOnion, "demo05 acts on the yellow Onion only");
			if (k.type == nectar::kCinEventAction && k.value == nectar::kDemoOnionBoot) boot5 = k.frame;
			if (k.type == nectar::kCinEventAction && k.value == nectar::kDemoOnionEmitCone) cone5 = k.frame;
		}
		check(boot5 >= 5 && boot5 + 240 + 10 <= cone5, "demo05: windows before Boot, between Boot and the seed, and between the seed and EmitCone");
		// demo73 (scenario extinction_day30): StartTotalResults in the cut played once, before the loop.
		int starts = 0;
		for (const auto& f : files) {
			if (f.path != "dataDir/cinemas/demo73.cin") continue;
			const ReadCin r = loadCin(f.text);
			check(r.cuts.size() == 2 && r.cuts[1].flags == nectar::kCutLoop, "demo73: a cut played once, then a loop");
			for (std::size_t i = 0; i < r.cuts.size(); i++)
				for (const ReadKey& k : r.cuts[i].keys)
					if (k.type == nectar::kCinEventNotify && k.cmd == nectar::kMovieCmdStartTotalResults) starts += i == 0 ? 1 : 100;
		}
		check(starts == 1, "demo73 starts the final results window once, from its first cut");
		// demo77 (scenarios ending_neutral_day30, ending_happy): the same shape as demo73.
		int starts77 = 0;
		for (const auto& f : files) {
			if (f.path != "dataDir/cinemas/demo77.cin") continue;
			const ReadCin r = loadCin(f.text);
			check(r.cuts.size() == 2 && r.cuts[1].flags == nectar::kCutLoop, "demo77: a cut played once, then a loop");
			for (std::size_t i = 0; i < r.cuts.size(); i++)
				for (const ReadKey& k : r.cuts[i].keys)
					if (k.type == nectar::kCinEventNotify && k.cmd == nectar::kMovieCmdStartTotalResults) starts77 += i == 0 ? 1 : 100;
		}
		check(starts77 == 1, "demo77 starts the final results window once, from its first cut");
		// demo26 (scenarios ending_happy, ending_happy_skip): SpecialDayEnd once, listed before
		// FadeOut and two frames after it, as on the disc, so a skip's frame-order replay
		// (pc_cine_sort_skip_keys) reorders them.
		const auto k26 = keysOf("dataDir/cinemas/demo26.cin");
		int special = -1, fadeOut = -1, specialAt = -1, fadeOutAt = -1;
		for (std::size_t i = 0; i < k26.size(); i++) {
			if (k26[i].type != nectar::kCinEventNotify) continue;
			if (k26[i].cmd == nectar::kMovieCmdSpecialDayEnd) special = int(i), specialAt = k26[i].frame;
			if (k26[i].cmd == nectar::kMovieCmdFadeOut) fadeOut = int(i), fadeOutAt = k26[i].frame;
		}
		check(special >= 0 && fadeOut >= 0 && special < fadeOut && specialAt == fadeOutAt + 2, "demo26: SpecialDayEnd listed before FadeOut, two frames after it");
		check(specialAt >= 30, "demo26: SpecialDayEnd late enough to leave a skip window before it");
		// demo17 (scenario tutorial_day1): the ship's Henka, then ForceDayEnd, then the TUT_GetEngine text, in the disc's order.
		int henka = -1, dayEnd = -1, text = -1;
		for (const auto& f : files) {
			if (f.path != "dataDir/cinemas/demo17.cin") continue;
			const ReadCin r = loadCin(f.text);
			for (const auto& c : r.cuts)
				for (const ReadKey& k : c.keys) {
					if (k.type == nectar::kCinEventAction && k.cmd == nectar::kDemoSenderUfo && k.value == nectar::kDemoUfoHenka) henka = k.frame;
					if (k.type == nectar::kCinEventNotify && k.cmd == nectar::kMovieCmdForceDayEnd) dayEnd = k.frame;
					if (k.type == nectar::kCinEventNotify && k.cmd == nectar::kMovieCmdTextDemo && k.value == 9) text = k.frame;
				}
		}
		check(henka >= 0 && henka < dayEnd && dayEnd < text, "demo17: the ship's Henka, ForceDayEnd, then TUT_GetEngine (9)");
		check(has(keysOf("dataDir/cinemas/demo78.cin"), nectar::kCinEventNotify, nectar::kMovieCmdTextDemo, 17), "demo78 (the Whimsical Radar) opens TUT_GetParts (17)");
	}
	std::vector<nectar::StandInCinemaFile> again;
	check(nectar::writeStandInCinemas(again, err) && again.size() == files.size(), "second build");
	for (std::size_t i = 0; i < files.size() && i < again.size(); i++) check(files[i].path == again[i].path && files[i].text == again[i].text, "deterministic: " + files[i].path);

	if (argc > 1) {
		std::ifstream in(argv[1]);
		check(bool(in), std::string("read ") + argv[1]);
		std::string line;
		int listed = 0;
		while (std::getline(in, line)) {
			if (line.empty() || line[0] == '#') continue;
			const std::string path = line.substr(0, line.find('\t'));
			if (path.rfind("dataDir/cinemas/", 0) != 0) continue;
			listed++;
			check(paths.count(path) == 1, "boot manifest cinema path is a stand-in: " + path);
		}
		check(listed > 0, "the boot manifest lists cinema paths");
	}

	if (failures) {
		std::printf("%d failure(s)\n", failures);
		return 1;
	}
	std::printf("cinema writer test: all checks passed (%zu stand-in files)\n", files.size());
	return 0;
}
