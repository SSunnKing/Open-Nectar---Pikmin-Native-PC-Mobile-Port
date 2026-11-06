// Write-then-read tests for nectar::writeArchivePair (.dir/.arc) and
// nectar::writeStagesIni (stages/stages.ini).
//
// CmdStream link experiment (issue #62 "Check first"): src/sysCommon/cmdStream.cpp
// links into a host test with only System::halt and sysCon stubbed
// (tests/game_loader_stubs.cpp), alongside stream.cpp and string.cpp. system.h
// compiles standalone. So the tokenizer oracle is the REAL CmdStream.
//
// What is not the real code: FlowController::readMapList and
// System::parseArchiveDirectory need gsys, StageInfo and the DVD layer, so the
// token loop of readMapList and the .dir loop of parseArchiveDirectory are
// transcribed below (same calls, same order) over the real CmdStream and the
// real Stream::readInt/readString. If those functions change, update the copies.
#include "nectar_assets/archive.h"
#include "nectar_assets/stages_ini.h"

#include "CmdStream.h"
#include "Common/String.h"
#include "Stream.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
int failures = 0;

void check(bool ok, const char* what)
{
	if (!ok) {
		std::printf("FAIL: %s\n", what);
		failures++;
	}
}

// --- parseArchiveDirectory's .dir loop (system.cpp:675-688) ---------------------
struct DirRead {
	std::vector<std::string> names;
	std::vector<std::uint32_t> offsets, sizes;
	int pendingAfter = -1;
};

DirRead readDir(const std::vector<std::uint8_t>& dir)
{
	DirRead r;
	RamStream file(const_cast<std::uint8_t*>(dir.data()), int(dir.size()));
	file.readInt(); // header, discarded
	u32 num = file.readInt();
	for (u32 i = 0; i < num; i++) {
		u32 b = file.readInt();
		u32 c = file.readInt();
		String str(0);
		file.readString(str);
		r.offsets.push_back(b);
		r.sizes.push_back(c);
		r.names.push_back(str.mString);
	}
	r.pendingAfter = file.getPending();
	return r;
}

// --- FlowController::readMapList's token loop (game.cpp:129-200) -----------------
struct MapGen {
	std::string name;
	u32 start, end, duration;
};
struct MapRead {
	bool visible = false;
	std::string name, file;
	u32 id = 5, chid = 99; // StageInfo defaults are STAGE_TESTMAP / CHALSTAGE_NOT; any sentinel works here
	std::vector<MapGen> gens;
};

std::vector<MapRead> readMapList(const std::string& text)
{
	std::vector<MapRead> maps;
	RamStream file(const_cast<char*>(text.data()), int(text.size()));
	CmdStream* commands = new CmdStream(&file);
	while (!commands->endOfCmds() && !commands->endOfSection()) {
		commands->getToken(true);
		if (!commands->isToken("new_map")) {
			continue;
		}
		maps.emplace_back();
		MapRead& m = maps.back();
		commands->getToken(true);
		m.visible = std::strcmp(commands->mCurrentToken, "visible") == 0;
		commands->getToken(true); // {
		while (!commands->endOfCmds() && !commands->endOfSection()) {
			commands->getToken(true);
			if (commands->isToken("name")) {
				m.name = commands->getToken(true);
				continue;
			}
			if (commands->isToken("id")) {
				sscanf(commands->getToken(true), "%d", &m.id);
				continue;
			}
			if (commands->isToken("chid")) {
				sscanf(commands->getToken(true), "%d", &m.chid);
				continue;
			}
			if (commands->isToken("file")) {
				m.file = commands->getToken(true);
				continue;
			}
			if (commands->isToken("generator")) {
				commands->getToken(true); // {
				// StageInfo::parseGenerators
				while (!commands->endOfCmds() && !commands->endOfSection()) {
					commands->getToken(true);
					if (!commands->isToken("genfile")) {
						continue;
					}
					MapGen g;
					g.name = commands->getToken(true);
					sscanf(commands->getToken(true), "%d", &g.start);
					sscanf(commands->getToken(true), "%d", &g.end);
					sscanf(commands->getToken(true), "%d", &g.duration);
					m.gens.push_back(g);
				}
				if (!commands->endOfCmds()) {
					commands->getToken(true);
				}
			}
		}
		if (!commands->endOfCmds()) {
			commands->getToken(true);
		}
	}
	delete commands;
	return maps;
}
}

int main()
{
	std::string err;

	// ---- archives ----
	{
		nectar::ArchiveInput in;
		nectar::ArchivePair out;
		check(nectar::writeArchivePair(in, out, err), "minimal archive");
		const std::uint8_t want[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
		check(out.dir.size() == 8 && std::memcmp(out.dir.data(), want, 8) == 0, "zero-entry .dir is 8 zero bytes");
		check(out.arc.empty(), ".arc is 0 bytes");
		DirRead r = readDir(out.dir);
		check(r.names.empty() && r.pendingAfter == 0, "zero-entry .dir parses as count 0 with nothing left over");
	}
	{
		// The issue's well-formed case: one entry, 3-byte name, 64-byte .arc.
		nectar::ArchiveInput in;
		in.blob.assign(64, 0xAB);
		in.entries.push_back({ "abc", 16, 48 });
		nectar::ArchivePair out;
		check(nectar::writeArchivePair(in, out, err), "one-entry archive");
		check(out.arc.size() == 64, ".arc blob carried through");
		DirRead r = readDir(out.dir);
		check(r.names.size() == 1 && r.names[0] == "abc" && r.offsets[0] == 16 && r.sizes[0] == 48, "entry parses back");
		check(r.pendingAfter == 0, ".dir has no trailing bytes");
		in.entries.push_back({ "defg", 60, 8 });
		check(!nectar::writeArchivePair(in, out, err) && !err.empty(), "entry past the blob rejected");
	}

	// ---- stages.ini ----
	{
		std::vector<nectar::StagesEntry> entries;
		for (int i = 0; i < 5; i++) {
			nectar::StagesEntry e;
			e.name = "stage" + std::to_string(i);
			e.id   = i;
			e.chid = i;
			e.file = "stages/s" + std::to_string(i) + ".ini";
			entries.push_back(e);
		}
		entries[1].generators = { { "[1-1].gen", 0, 30, 7 }, { "[1-2].gen", 5, 6, 1 } };
		nectar::StagesEntry sandbox; // the sixth entry #68 appends: not visible, no name, no chid
		sandbox.visible = false;
		sandbox.id      = 3;
		sandbox.file    = "stages/sandbox.ini";
		entries.push_back(sandbox);
		nectar::StagesEntry spaced;
		spaced.name = "Impact Site";
		spaced.file = "stages/x y.ini";
		entries.push_back(spaced);

		std::string text;
		check(nectar::writeStagesIni(entries, text, err), "write stages.ini");
		std::vector<MapRead> maps = readMapList(text);
		check(maps.size() == entries.size(), "every new_map block is found");
		if (maps.size() == entries.size()) {
			for (size_t i = 0; i < entries.size(); i++) {
				const nectar::StagesEntry& e = entries[i];
				const MapRead& m = maps[i];
				check(m.visible == e.visible, "visible flag");
				check(m.name == e.name, "name");
				check(m.file == e.file, "file");
				check(e.id < 0 || m.id == u32(e.id), "id");
				check(e.chid < 0 || m.chid == u32(e.chid), "chid");
				check(m.gens.size() == e.generators.size(), "generator count");
				for (size_t g = 0; g < m.gens.size() && g < e.generators.size(); g++) {
					const nectar::StagesGenFile& w = e.generators[g];
					check(m.gens[g].name == w.name && m.gens[g].start == u32(w.startDay) && m.gens[g].end == u32(w.endDay)
					          && m.gens[g].duration == u32(w.duration),
					      "genfile fields");
				}
			}
			check(maps[5].id == 3 && maps[5].chid == 99, "omitted chid is left to the game default");
		}
		nectar::StagesEntry bad;
		check(!nectar::writeStagesIni({ bad }, text, err), "entry without file rejected");
		bad.file = "it's.ini";
		check(!nectar::writeStagesIni({ bad }, text, err), "quote in a value rejected");
	}

	if (failures) {
		std::printf("%d failure(s)\n", failures);
		return 1;
	}
	std::printf("archive_stages_writer_test: ok\n");
	return 0;
}
