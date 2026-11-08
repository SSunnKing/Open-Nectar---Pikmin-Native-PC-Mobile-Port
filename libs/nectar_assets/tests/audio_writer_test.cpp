// Write-then-read test for the JAudio sequence writers (audio_seq.h) and the
// stand-in SndData (standin_audio.h). Oracles (#62: the game's own loaders
// where they link):
//  - HEAD_pikiseq, the BARC table compiled into the game (src/jaudio/pikiseq.c),
//    is the layout input, so the test checks the writer against the real table
//    for the configured PIKMIN_GAME_VERSION.
//  - PCSequenceArchive (pc_port/audio/pc_sequence_archive.cpp) loads the written
//    .arc with that table and reads every sequence back.
//  - PCJamPlayer (pc_port/audio/pc_jam.cpp), the port's JAM interpreter, runs the
//    stand-in sequences: they must stay alive (no Finished, no unsupported
//    opcode) and open the 16 children Jac_InitEventSystem reads.
// The native engine's own reader (virload.c, jammain_2.c) needs the audio
// thread and the DVD layer and cannot link here; the JAudio-ON scenario runs
// (standin-audio.md) are its check.
// A small decoder below walks the five opcodes the writer emits to check the
// connection names, which neither port class exposes.

#include "nectar_assets/audio_seq.h"
#include "nectar_assets/standin_audio.h"

#include "audio/pc_jam.h"
#include "audio/pc_sequence_archive.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

extern "C" u8 HEAD_pikiseq[];

namespace {

int failures = 0;

void check(bool ok, const std::string& what)
{
	if (!ok) {
		std::fprintf(stderr, "FAIL: %s\n", what.c_str());
		failures++;
	}
}

std::size_t headSize()
{
	const u8* h = HEAD_pikiseq;
	const std::uint32_t n = std::uint32_t(h[12]) << 24 | std::uint32_t(h[13]) << 16 | std::uint32_t(h[14]) << 8 | h[15];
	return 0x20 + std::size_t(n) * 0x20;
}

std::uint32_t u24(const std::vector<std::uint8_t>& s, std::size_t at) { return std::uint32_t(s[at]) << 16 | std::uint32_t(s[at + 1]) << 8 | s[at + 2]; }

/// Walks the writer's opcodes from `pc` (one track) and collects the connection
/// names it registers, recursing into OpenTrack targets. Returns the number of
/// children the track opened, or -1 on an unexpected byte.
int walk(const std::vector<std::uint8_t>& s, std::size_t pc, std::set<std::uint32_t>& names, int depth = 0)
{
	if (depth > 4) return -1;
	int children = 0;
	std::uint32_t name = 0;
	bool named = false;
	while (pc < s.size()) {
		const std::uint8_t op = s[pc];
		if (op == 0xD0 && pc + 5 <= s.size()) {
			// ConnectName high, low: connectionId = high << 16 | low.
			name  = std::uint32_t(s[pc + 1]) << 24 | std::uint32_t(s[pc + 2]) << 16 | std::uint32_t(s[pc + 3]) << 8 | s[pc + 4];
			named = true;
			pc += 5;
		} else if (op == 0xE5) {
			if (!named) return -1;
			names.insert(name);
			pc += 1;
		} else if (op == 0xC1 && pc + 5 <= s.size()) {
			if (s[pc + 1] != children) return -1;
			if (walk(s, u24(s, pc + 2), names, depth + 1) < 0) return -1;
			children++;
			pc += 5;
		} else if (op == 0x88 && pc + 7 <= s.size() && s[pc + 3] == 0xC7) {
			// wait, then Jmp back to the wait: the track idles from here on.
			return u24(s, pc + 4) == pc ? children : -1;
		} else {
			return -1;
		}
	}
	return -1;
}

std::string tempArc()
{
	return (std::filesystem::temp_directory_path() / ("nectar_audio_writer_test_" + std::to_string(std::rand()) + ".arc")).string();
}

void testHeaderMatchesPortReader()
{
	std::vector<nectar::SeqArcEntry> layout;
	std::string error;
	check(nectar::readSeqArcHeader(HEAD_pikiseq, headSize(), layout, error), "readSeqArcHeader(HEAD_pikiseq): " + error);
	check(layout.size() == 22, "HEAD_pikiseq has 22 entries");
	check(layout.size() > 13 && layout[0].name == "pikise.jam" && layout[1].name == "sysevent.jam" && layout[13].name == "demobgm.jam",
	      "entries 0, 1 and 13 are pikise, sysevent and demobgm (Jac_InitBgm's started sequences)");

	std::string bad;
	check(!nectar::readSeqArcHeader(HEAD_pikiseq, 0x20, layout, bad), "a truncated table is rejected");
	const std::uint8_t junk[0x40] = { 'X' };
	check(!nectar::readSeqArcHeader(junk, sizeof junk, layout, bad), "a table without BARC is rejected");
}

void testStandInArchive()
{
	std::vector<nectar::StandInFile> files;
	std::string error;
	check(nectar::standInSndDataFiles(HEAD_pikiseq, headSize(), files, error), "standInSndDataFiles: " + error);
	check(files.size() == 1 && files[0].path == "SndData/Seqs/pikiseq.arc", "one file, SndData/Seqs/pikiseq.arc");
	if (files.size() != 1) return;

	std::vector<nectar::StandInFile> again;
	nectar::standInSndDataFiles(HEAD_pikiseq, headSize(), again, error);
	check(again.size() == 1 && again[0].bytes == files[0].bytes, "two runs give identical bytes");

	const std::string path = tempArc();
	{
		std::ofstream f(path, std::ios::binary);
		f.write(reinterpret_cast<const char*>(files[0].bytes.data()), std::streamsize(files[0].bytes.size()));
	}
	PCSequenceArchive arc;
	check(arc.load(path.c_str(), HEAD_pikiseq, headSize()), "PCSequenceArchive loads the stand-in .arc: " + arc.error());

	std::vector<nectar::SeqArcEntry> layout;
	nectar::readSeqArcHeader(HEAD_pikiseq, headSize(), layout, error);
	for (std::uint32_t i = 0; i < layout.size(); i++) {
		if (!layout[i].used || layout[i].size == 0) continue;
		std::vector<u8> seq;
		check(arc.read(i, seq), "PCSequenceArchive reads sequence " + std::to_string(i));
		std::vector<std::uint8_t> expected;
		nectar::writeJamIdleSequence(nectar::standInSeqTracks(i), expected, error);
		check(seq.size() == layout[i].size && std::equal(expected.begin(), expected.end(), seq.begin()),
		      "sequence " + std::to_string(i) + " (" + layout[i].name + ") holds the writer's bytes at its offset");

		PCJamPlayer player;
		std::vector<PCJamEvent> events;
		check(player.start(seq, 0), "PCJamPlayer starts sequence " + std::to_string(i));
		std::size_t notes = 0;
		for (int t = 0; t < 300; t++) {
			const PCJamResult r = player.tick(events);
			if (r != PCJamResult::Ok) {
				check(false, "PCJamPlayer ran sequence " + std::to_string(i) + " to result " + std::to_string(int(r)) + " at tick " + std::to_string(t));
				break;
			}
			notes += events.size();
		}
		check(player.active(), "sequence " + std::to_string(i) + " is still running after 300 ticks");
		check(notes == 0, "sequence " + std::to_string(i) + " plays no notes");
		if (i == 1) {
			bool all = true;
			for (u8 c = 0; c < 16; c++) all = all && player.writeChildPort(c, 0, 1);
			check(all, "sysevent.jam opens the 16 children Jac_InitEventSystem reads");
		}

		std::set<std::uint32_t> names;
		const int kids = walk(seq, 0, names);
		if (i == 0) {
			std::set<std::uint32_t> want;
			for (std::uint32_t n = 0; n < 16; n++) want.insert(0x10000 | n);
			check(kids == 16 && names == want, "pikise.jam registers 0x10000 to 0x1000F");
		} else if (i == 1) {
			check(kids == 16 && names == std::set<std::uint32_t> { 0x20000 }, "sysevent.jam registers 0x20000 with 16 children");
		} else if (i == 13) {
			check(kids == 0 && names == std::set<std::uint32_t> { 0x30000 }, "demobgm.jam registers 0x30000");
		} else {
			check(kids == 0 && names.empty(), "sequence " + std::to_string(i) + " is a bare idle track");
		}
	}
	std::filesystem::remove(path);
}

void testWriterErrors()
{
	std::string error;
	std::vector<std::uint8_t> out;
	nectar::JamIdleTrack wide;
	wide.children.resize(17);
	check(!nectar::writeJamIdleSequence(wide, out, error), "17 children are rejected");

	nectar::SeqArcInput in;
	in.layout = { { "a", true, 0, 16 }, { "b", true, 8, 16 } };
	check(!nectar::writeSeqArc(in, out, error), "overlapping entries are rejected");

	in.layout = { { "a", true, 0, 8 }, { "dummy", false, 0, 0 } };
	in.sequences[0] = std::vector<std::uint8_t>(9, 0x88);
	check(!nectar::writeSeqArc(in, out, error), "a sequence larger than its entry is rejected");
	in.sequences.clear();
	in.sequences[1] = { 0x88, 0, 1 };
	check(!nectar::writeSeqArc(in, out, error), "a sequence for a dummy entry is rejected");

	in.sequences.clear();
	in.layout = { { "a", true, 0x10, 0x20 }, { "dummy", false, 0, 0 } };
	check(nectar::writeSeqArc(in, out, error) && out.size() == 0x30, "the .arc ends at the last entry: " + error);
}

} // namespace

int main()
{
	testHeaderMatchesPortReader();
	testStandInArchive();
	testWriterErrors();
	if (failures) {
		std::fprintf(stderr, "%d failure(s)\n", failures);
		return 1;
	}
	std::puts("audio_writer_test: OK");
	return 0;
}
