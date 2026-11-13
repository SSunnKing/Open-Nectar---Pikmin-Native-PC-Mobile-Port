#pragma once

/**
 * @file pc_gx_xf_load.h
 * @brief Decodes the payload of a GX_CMD_LOAD_XF_REG (0x10) display-list command.
 *
 * After the 0x10 opcode comes a 32-bit header: the XF address in the low 16
 * bits and the word count minus one above it. GXLoadPosMtxImm, for example,
 * writes 0x000B0000 | addr for its 12 words. Dolphin reads the same field as
 * ((header >> 16) & 0xF) + 1 (VideoCommon/OpcodeDecoding.h), so one load
 * carries 1 to 16 words.
 *
 * The header and the words come from the display list, which Pikmin 1 reads
 * straight out of its .mod files: big-endian bytes. Each word is assembled
 * from bytes so the result is the same on any host.
 */

#include <cstddef>
#include <cstdint>

struct PcGxXfLoad {
	uint32_t addrBase;
	uint32_t numWords;     // 1..16
	uint32_t words[16];    // host-order register values
};

// Reads the header and words at `cursor` and advances past them. Returns false,
// leaving `cursor` untouched, when the list ends before the command does.
inline bool pc_gx_decode_xf_load(const uint8_t*& cursor, const uint8_t* end, PcGxXfLoad& out)
{
	const auto be32 = [](const uint8_t* b) {
		return (uint32_t(b[0]) << 24) | (uint32_t(b[1]) << 16) | (uint32_t(b[2]) << 8) | uint32_t(b[3]);
	};
	if (end - cursor < 4) return false;
	const uint32_t header = be32(cursor);
	const uint32_t numWords = ((header >> 16) & 0xF) + 1;
	if (end - cursor < std::ptrdiff_t(4 + numWords * 4)) return false;
	out.addrBase = header & 0xFFFF;
	out.numWords = numWords;
	for (uint32_t w = 0; w < numWords; ++w) out.words[w] = be32(cursor + 4 + w * 4);
	cursor += 4 + numWords * 4;
	return true;
}
