/**
 * @file p2_host_jstudio.h
 * @brief Host-endian access to JStudio .stb cutscene data.
 *
 * JStudio reads the big-endian .stb straight from memory. Two strategies:
 *
 *  - STB stream (blocks, sequences, paragraphs, operands): the payload of a
 *    paragraph depends on the object type and operation code (strings, u32,
 *    f32, u16 lists...), so it is *not* promoted in place. Every scalar read
 *    in the decomp goes through pc_jst_rd16/rd32/rdf32 under PIKI_PC_PORT.
 *
 *  - FVB block ('JFVB', function-value curves): the key arrays are consumed
 *    in place by the interpolators (TFunctionValue_hermite/list), so the
 *    whole embedded FVB file is promoted once by pc_promote_fvb(). Its header
 *    byte-order mark (0xFEFF) doubles as the "already promoted" marker.
 */
#ifndef _P2_HOST_JSTUDIO_H
#define _P2_HOST_JSTUDIO_H

#include "types.h"
#include <string.h>

static inline u16 pc_jst_rd16(const void* p)
{
	u16 v;
	memcpy(&v, p, 2);
	return __builtin_bswap16(v);
}
static inline u32 pc_jst_rd32(const void* p)
{
	u32 v;
	memcpy(&v, p, 4);
	return __builtin_bswap32(v);
}
static inline s32 pc_jst_rds32(const void* p) { return (s32)pc_jst_rd32(p); }
static inline f32 pc_jst_rdf32(const void* p)
{
	u32 v = pc_jst_rd32(p);
	f32 f;
	memcpy(&f, &v, 4);
	return f;
}

static inline void pc_fvb_sw16(u8* p) { u16 v; memcpy(&v, p, 2); v = __builtin_bswap16(v); memcpy(p, &v, 2); }
static inline void pc_fvb_sw32(u8* p) { u32 v; memcpy(&v, p, 4); v = __builtin_bswap32(v); memcpy(p, &v, 4); }
static inline u16 pc_fvb_ld16(const u8* p) { u16 v; memcpy(&v, p, 2); return v; }
static inline u32 pc_fvb_ld32(const u8* p) { u32 v; memcpy(&v, p, 4); return v; }
static inline u32 pc_fvb_align4(u32 v) { return (v + 3) & ~3u; }

/**
 * Promotes an FVB file (JStudio::fvb::data::THeader + TBlocks) in place.
 * Layout, see fvb-data.h / fvb.cpp TObject::prepare:
 *   header: "FVB", u16 byteOrder(0xFEFF), u16 version, u32, u32 blockNum
 *   block : u32 size, u16 type, u16 idSize, id[align4(idSize)], paragraphs...
 *   paragraph: variable u16/u32 size + type (JGadget parseVariableUInt_16_32),
 *              content[align4(size)]; type 0 ends the block.
 * Content is 32-bit words except paragraph 0x10 (u32 count, then
 * {u32 len, id[align4(len)]}) and 0x15 (two u16).
 */
static inline void pc_promote_fvb(void* file, u32 fileSize)
{
	u8* p = (u8*)file;
	if (!p || fileSize < 0x10 || memcmp(p, "FVB", 3) != 0)
		return;
	if (p[4] == 0xFF && p[5] == 0xFE)
		return; // already host order
	if (p[4] != 0xFE || p[5] != 0xFF)
		return; // not a byte-order mark we understand

	pc_fvb_sw16(p + 4);
	pc_fvb_sw16(p + 6);
	pc_fvb_sw32(p + 8);
	pc_fvb_sw32(p + 12);
	const u32 blockNum = pc_fvb_ld32(p + 12);
	u8* const fileEnd  = p + fileSize;

	u8* block = p + 0x10;
	for (u32 b = 0; b < blockNum && block + 8 <= fileEnd; b++) {
		pc_fvb_sw32(block);
		pc_fvb_sw16(block + 4);
		pc_fvb_sw16(block + 6);
		const u32 size   = pc_fvb_ld32(block);
		const u16 idSize = pc_fvb_ld16(block + 6);
		if (size < 8 || block + size > fileEnd)
			break;
		u8* const blockEnd = block + size;
		u8* q              = block + 8 + pc_fvb_align4(idSize);

		while (q + 4 <= blockEnd) {
			u32 psize, ptype;
			u8* content;
			u16 first = (u16)((q[0] << 8) | q[1]);
			if (first & 0x8000) {
				if (q + 8 > blockEnd)
					break;
				pc_fvb_sw16(q);
				pc_fvb_sw16(q + 2);
				pc_fvb_sw32(q + 4);
				psize   = ((u32)first << 16 & 0x7FFF0000u) | pc_fvb_ld16(q + 2);
				ptype   = pc_fvb_ld32(q + 4);
				content = q + 8;
			} else {
				pc_fvb_sw16(q);
				pc_fvb_sw16(q + 2);
				psize   = first;
				ptype   = pc_fvb_ld16(q + 2);
				content = q + 4;
			}
			if (ptype == 0)
				break;
			u8* const contentEnd = content + psize;
			if (contentEnd > blockEnd)
				break;

			switch (ptype) {
			case 0x10: { // referenced function values by name
				if (psize >= 4) {
					pc_fvb_sw32(content);
					u32 cnt = pc_fvb_ld32(content);
					u8* r   = content + 4;
					for (; cnt != 0 && r + 4 <= contentEnd; cnt--) {
						pc_fvb_sw32(r);
						u32 len = pc_fvb_ld32(r);
						r += 4 + pc_fvb_align4(len);
					}
				}
			} break;
			case 0x15: // outside mode pair
				if (psize >= 4) {
					pc_fvb_sw16(content);
					pc_fvb_sw16(content + 2);
				}
				break;
			default: // data / ranges / indices: 32-bit words
				for (u8* r = content; r + 4 <= contentEnd; r += 4)
					pc_fvb_sw32(r);
				break;
			}
			q = content + pc_fvb_align4(psize);
		}
		block = blockEnd;
	}
}

#endif /* _P2_HOST_JSTUDIO_H */
