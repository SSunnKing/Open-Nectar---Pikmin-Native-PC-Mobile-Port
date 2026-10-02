// The permadeath / Hard flags ride in a block inside the save file. Getting
// that wrong corrupts a save, and the only way to find out in-game is to lose
// one, so the round-trip is checked here instead.
#include "pc_permadeath.h"
#include "Stream.h"

#include <cstdio>
#include <cstring>
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

// One save file's block, as the game lays it out: 0x8000 bytes, the checksum
// trailer at 0x7FF8, everything the game writes well below our offset.
std::vector<unsigned char> makeFile() { return std::vector<unsigned char>(0x8000, 0); }
}

int main()
{
	// A file the original game wrote has zeros where our block goes, and must
	// read back as an ordinary run rather than as an uninitialised one.
	{
		std::vector<unsigned char> file = makeFile();
		RamStream in(file.data(), (int)file.size());
		pc_permadeath_set_pending(true);
		pc_hardmode_set_pending(true);
		pc_permadeath_begin_new_run();
		pc_hardmode_begin_new_run();
		check(pc_permadeath_active(), "pending permadeath starts the run");
		check(pc_hardmode_active(), "pending Hard starts the run");
		pc_permadeath_read_block(in);
		check(!pc_permadeath_active(), "a save with no block is a normal run");
		check(!pc_hardmode_active(), "a save with no block is not Hard");
	}

	// Round-trip both flags in every combination.
	for (int p = 0; p < 2; p++) {
		for (int h = 0; h < 2; h++) {
			std::vector<unsigned char> file = makeFile();
			RamStream out(file.data(), (int)file.size());
			out.setPosition(0x7FF8);
			pc_permadeath_write_block(out, p != 0, h != 0);
			check(out.getPosition() == 0x7FF8, "writing the block restores the position");

			RamStream in(file.data(), (int)file.size());
			in.setPosition(0x7FF8);
			pc_permadeath_read_block(in);
			check(pc_permadeath_active() == (p != 0), "permadeath survives a round trip");
			check(pc_hardmode_active() == (h != 0), "Hard survives a round trip");
			check(pc_permadeath_peek_block(in) == (p != 0), "peek matches permadeath");
			check(pc_hardmode_peek_block(in) == (h != 0), "peek matches Hard");
			check(in.getPosition() == 0x7FF8, "reading the block restores the position");
		}
	}

	// The block must not touch what the game writes, nor the checksum trailer.
	{
		std::vector<unsigned char> file = makeFile();
		for (int i = 0; i < 0x7000; i++) file[i] = (unsigned char)(i & 0xFF);
		std::vector<unsigned char> before = file;
		RamStream out(file.data(), (int)file.size());
		out.setPosition(0x7FF8);
		pc_permadeath_write_block(out, true, true);
		check(std::memcmp(file.data(), before.data(), 0x7000) == 0,
		      "the game's own data is untouched");
		check(std::memcmp(file.data() + 0x7FF8, before.data() + 0x7FF8, 8) == 0,
		      "the checksum trailer is untouched");
		check(PC_SAVE_BLOCK_OFFSET + 16 <= 0x7FF8, "the block is covered by the checksum");
	}

	// A version-1 block (permadeath only) must keep that flag and leave Hard off.
	{
		std::vector<unsigned char> file = makeFile();
		RamStream out(file.data(), (int)file.size());
		out.setPosition(PC_SAVE_BLOCK_OFFSET);
		out.writeInt(0x4E435431);
		out.writeInt(1);
		out.writeInt(1);
		RamStream in(file.data(), (int)file.size());
		pc_permadeath_read_block(in);
		check(pc_permadeath_active(), "a v1 permadeath file stays permadeath");
		check(!pc_hardmode_active(), "a v1 file is not Hard");
	}

	// A newer port's block must not be guessed at.
	{
		std::vector<unsigned char> file = makeFile();
		RamStream out(file.data(), (int)file.size());
		out.setPosition(0x7FF8);
		pc_permadeath_write_block(out, true, true);
		file[PC_SAVE_BLOCK_OFFSET + 7] = 99; // bump the version
		RamStream in(file.data(), (int)file.size());
		pc_permadeath_read_block(in);
		check(!pc_permadeath_active(), "an unknown block version reads as normal");
		check(!pc_hardmode_active(), "an unknown block version is not Hard");
	}

	// Combat scales are identity off Hard, and the v1 numbers on Hard.
	{
		pc_hardmode_set_pending(false);
		pc_hardmode_begin_new_run();
		check(pc_hardmode_teki_life(100.0f) == 100.0f, "teki life is unchanged off Hard");
		check(pc_hardmode_navi_damage(10.0f) == 10.0f, "Olimar damage is unchanged off Hard");
		pc_hardmode_set_pending(true);
		pc_hardmode_begin_new_run();
		check(pc_hardmode_teki_life(100.0f) == 133.0f, "teki life is 1.33x on Hard");
		check(pc_hardmode_navi_damage(10.0f) == 15.0f, "Olimar damage is 1.5x on Hard");
	}

	if (failures == 0) std::printf("pc_permadeath_test: all checks passed\n");
	return failures == 0 ? 0 : 1;
}
