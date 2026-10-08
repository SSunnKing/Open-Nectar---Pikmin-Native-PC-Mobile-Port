/**
 * @file p2_host_jpa.h
 * @brief Host-endian promotion for JSystem JParticle .jpc (JPAC2-10) files.
 *
 * JPAResourceLoader casts the file straight to the *BlockData structs, so on
 * LE hosts every u16/u32/f32/s16 field is promoted in place, once, before the
 * loader runs. The "2-10" version tag at +4 doubles as the marker: once it
 * reads as the host multi-char constant '2-10' the file is already promoted.
 * Block magics are swapped too, so the loader's `switch (magic)` matches.
 *
 * ResTIMG headers inside TEX1 blocks go through pc_promote_restimg (marker at
 * +0x19), which JUTTexture::storeTIMG would apply anyway.
 */
#ifndef _P2_HOST_JPA_H
#define _P2_HOST_JPA_H

#include "types.h"
#include "JSystem/ResTIMG.h"
#include "p2_host_restimg.h"
#include <string.h>

static inline void pc_jpa_sw16(u8* p) { u16 v; memcpy(&v, p, 2); v = __builtin_bswap16(v); memcpy(p, &v, 2); }
static inline void pc_jpa_sw32(u8* p) { u32 v; memcpy(&v, p, 4); v = __builtin_bswap32(v); memcpy(p, &v, 4); }
static inline u16 pc_jpa_rd16(const u8* p) { u16 v; memcpy(&v, p, 2); return v; }
static inline u32 pc_jpa_rd32(const u8* p) { u32 v; memcpy(&v, p, 4); return v; }
static inline void pc_jpa_sw32n(u8* p, int n) { for (int i = 0; i < n; i++) pc_jpa_sw32(p + 4 * i); }
static inline void pc_jpa_sw16n(u8* p, int n) { for (int i = 0; i < n; i++) pc_jpa_sw16(p + 2 * i); }

/* Block header: magic (4) + u32 size. Returns the promoted size. */
static inline u32 pc_jpa_promote_block_header(u8* b)
{
	pc_jpa_sw32(b);     // magic → host multi-char constant
	pc_jpa_sw32(b + 4); // size
	return pc_jpa_rd32(b + 4);
}

static inline void pc_jpa_promote_bem1(u8* b)
{
	pc_jpa_sw32n(b + 0x08, 2);  // flags, resUserWork
	pc_jpa_sw32n(b + 0x10, 22); // 3 vec3f + 13 f32 (0x10..0x64)
	pc_jpa_sw16n(b + 0x68, 8);  // rot xyz, maxFrame, startFrame, lifeTime, volumeSize, divNumber
}

static inline void pc_jpa_promote_bsp1(u8* b)
{
	pc_jpa_sw32(b + 0x08);      // flags
	pc_jpa_sw16n(b + 0x0C, 2);  // clrPrmAnmOffset, clrEnvAnmOffset
	pc_jpa_sw32n(b + 0x10, 2);  // baseSizeX/Y
	pc_jpa_sw16(b + 0x18);      // blendModeCfg
	pc_jpa_sw16(b + 0x24);      // clrAnmFrmMax
	u32 flags   = pc_jpa_rd32(b + 0x08);
	u8 clrFlg   = b[0x21];
	if (flags & 0x01000000)     // isTexCrdAnm: 10 f32 after the 0x34-byte header
		pc_jpa_sw32n(b + 0x34, 10);
	// tex idx anim table is u8: nothing to do.
	if (clrFlg & 0x02) { // prm color keys: { s16 index; GXColor }
		s16 off = (s16)pc_jpa_rd16(b + 0x0C);
		for (int i = 0; i < b[0x22]; i++)
			pc_jpa_sw16(b + off + i * 6);
	}
	if (clrFlg & 0x08) { // env color keys
		s16 off = (s16)pc_jpa_rd16(b + 0x0E);
		for (int i = 0; i < b[0x23]; i++)
			pc_jpa_sw16(b + off + i * 6);
	}
}

static inline void pc_jpa_promote_esp1(u8* b)
{
	pc_jpa_sw32(b + 0x08);
	pc_jpa_sw32n(b + 0x0C, 7);  // 0x0C..0x24
	pc_jpa_sw16n(b + 0x28, 2);  // scaleAnmCycleX/Y
	pc_jpa_sw32n(b + 0x2C, 13); // 0x2C..0x5C
}

static inline void pc_jpa_promote_etx1(u8* b)
{
	pc_jpa_sw32(b + 0x08);
	pc_jpa_sw32n(b + 0x0C, 6); // indTexMtx[2][3]
}

static inline void pc_jpa_promote_ssp1(u8* b)
{
	pc_jpa_sw32(b + 0x08);
	pc_jpa_sw32n(b + 0x0C, 10); // 0x0C..0x30 f32
	// 0x34 prmClr, 0x38 envClr: GXColor bytes
	pc_jpa_sw32(b + 0x3C);      // timing
	pc_jpa_sw16n(b + 0x40, 2);  // life, rate
	pc_jpa_sw16(b + 0x46);      // rotSpeed
}

static inline void pc_jpa_promote_fld1(u8* b)
{
	pc_jpa_sw32(b + 0x08);
	pc_jpa_sw32n(b + 0x0C, 13); // offset, velocity, 7 f32 (0x0C..0x3C)
}

static inline void pc_jpa_promote_kfa1(u8* b)
{
	u8 keyNum = b[0x09];
	pc_jpa_sw32n(b + 0x0C, keyNum * 4); // {frame, value, tanIn, tanOut}
}

static inline void pc_jpa_promote_tdb1(u8* b, u32 size)
{
	pc_jpa_sw16n(b + 8, (int)((size - 8) / 2));
}

/** Promote a JPAC2-10 file in place. Safe to call more than once. */
static inline void pc_promote_jpc(u8* p)
{
	if (!p || memcmp(p, "JPAC", 4) != 0)
		return;
	if (pc_jpa_rd32(p + 4) == (u32)'2-10')
		return; // already host-endian
	pc_jpa_sw32(p + 4);
	pc_jpa_sw16(p + 0x08); // resource count
	pc_jpa_sw16(p + 0x0A); // texture count
	pc_jpa_sw32(p + 0x0C); // texture offset
	u16 resCount = pc_jpa_rd16(p + 0x08);
	u16 texCount = pc_jpa_rd16(p + 0x0A);
	u32 texOff   = pc_jpa_rd32(p + 0x0C);

	u32 off = 0x10;
	for (int i = 0; i < resCount; i++) {
		u8* hdr = p + off;
		pc_jpa_sw16(hdr + 0); // usrIdx
		pc_jpa_sw16(hdr + 2); // blockNum
		u16 blockNum = pc_jpa_rd16(hdr + 2);
		off += 8;
		for (int j = 0; j < blockNum; j++) {
			u8* b    = p + off;
			u32 size = pc_jpa_promote_block_header(b);
			switch (pc_jpa_rd32(b)) {
			case 'BEM1': pc_jpa_promote_bem1(b); break;
			case 'BSP1': pc_jpa_promote_bsp1(b); break;
			case 'ESP1': pc_jpa_promote_esp1(b); break;
			case 'ETX1': pc_jpa_promote_etx1(b); break;
			case 'SSP1': pc_jpa_promote_ssp1(b); break;
			case 'FLD1': pc_jpa_promote_fld1(b); break;
			case 'KFA1': pc_jpa_promote_kfa1(b); break;
			case 'TDB1': pc_jpa_promote_tdb1(b, size); break;
			default: break;
			}
			off += size;
		}
	}

	off = texOff;
	for (int i = 0; i < texCount; i++) {
		u8* b    = p + off;
		u32 size = pc_jpa_promote_block_header(b);
		pc_promote_restimg((ResTIMG*)(b + 0x20));
		off += size;
	}
}

#endif /* _P2_HOST_JPA_H */
