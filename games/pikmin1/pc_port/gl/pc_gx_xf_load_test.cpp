/**
 * @file pc_gx_xf_load_test.cpp
 * @brief Display-list XF loads read count + 1 big-endian words.
 */

#include "pc_gx_xf_load.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace {

int sFailures = 0;

void expect(bool ok, const char* what)
{
	if (!ok) {
		std::printf("FAIL: %s\n", what);
		++sFailures;
	}
}

void pushBe32(std::vector<uint8_t>& out, uint32_t v)
{
	out.push_back(uint8_t(v >> 24));
	out.push_back(uint8_t(v >> 16));
	out.push_back(uint8_t(v >> 8));
	out.push_back(uint8_t(v));
}

uint32_t floatBits(float f)
{
	uint32_t bits;
	std::memcpy(&bits, &f, sizeof(bits));
	return bits;
}

} // namespace

int main()
{
	{
		// GXLoadPosMtxImm(mtx, GX_PNMTX1) writes header 0x000B0000 | 3*4, then
		// 12 big-endian floats. All 12 must come out, in host order, and the
		// cursor must land on the next command.
		std::vector<uint8_t> list;
		pushBe32(list, 0x000B0000u | 12u);
		for (int i = 0; i < 12; ++i) pushBe32(list, floatBits(float(i) + 0.5f));
		list.push_back(0x61); // next command (BP load) must stay aligned
		const uint8_t* cursor = list.data();
		PcGxXfLoad xf;
		expect(pc_gx_decode_xf_load(cursor, list.data() + list.size(), xf), "position matrix decodes");
		expect(xf.addrBase == 12, "position matrix address");
		expect(xf.numWords == 12, "count field is length minus one");
		bool wordsOk = true;
		for (uint32_t i = 0; i < 12; ++i) wordsOk &= xf.words[i] == floatBits(float(i) + 0.5f);
		expect(wordsOk, "words are byte-swapped to host order");
		expect(cursor == list.data() + list.size() - 1 && *cursor == 0x61, "cursor lands on the next command");
	}
	{
		// A single-register write (count field 0) carries one word, e.g. a
		// material color: RGBA bytes in that order.
		std::vector<uint8_t> list;
		pushBe32(list, 0x0000100Cu);
		pushBe32(list, 0x11223344u);
		const uint8_t* cursor = list.data();
		PcGxXfLoad xf;
		expect(pc_gx_decode_xf_load(cursor, list.data() + list.size(), xf), "single word decodes");
		expect(xf.numWords == 1 && xf.addrBase == 0x100C, "single word header");
		expect(xf.words[0] == 0x11223344u, "single word value");
		expect(cursor == list.data() + list.size(), "single word consumed");
	}
	{
		// Largest load: count field 0xF is 16 words.
		std::vector<uint8_t> list;
		pushBe32(list, 0x000F0000u);
		for (uint32_t i = 0; i < 16; ++i) pushBe32(list, i);
		const uint8_t* cursor = list.data();
		PcGxXfLoad xf;
		expect(pc_gx_decode_xf_load(cursor, list.data() + list.size(), xf) && xf.numWords == 16
		           && xf.words[15] == 15,
		       "16-word load");
	}
	{
		// A list that ends mid-command is rejected without moving the cursor.
		std::vector<uint8_t> list;
		pushBe32(list, 0x00020000u); // claims 3 words
		pushBe32(list, 1);
		pushBe32(list, 2);
		const uint8_t* cursor = list.data();
		PcGxXfLoad xf;
		expect(!pc_gx_decode_xf_load(cursor, list.data() + list.size(), xf), "truncated payload rejected");
		expect(cursor == list.data(), "truncated payload leaves cursor");
		const uint8_t* header = list.data();
		expect(!pc_gx_decode_xf_load(header, list.data() + 3, xf), "truncated header rejected");
	}
	if (sFailures == 0) std::printf("pc_gx_xf_load_test: all passed\n");
	return sFailures == 0 ? 0 : 1;
}
