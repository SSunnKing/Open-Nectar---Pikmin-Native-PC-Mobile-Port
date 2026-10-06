#include <cmath>
/**
 * @file gx_stubs.cpp
 * @brief Stub implementations for all Dolphin GX (Graphics) functions.
 * All signatures match the exact declarations in the Dolphin/GX/ headers.
 *
 * Fase 4: reenvia el API GX a pc_gfx (OpenGL). Firmas de Pikmin 2.
 */
#include "Dolphin/gx.h"
#include "Dolphin/vi.h"
#include "gl/pc_gfx.h"

#include <cstdio>
#include <cstring>

/* ── Render mode objects ── */
GXRenderModeObj GXNtsc480IntDf    = {};
GXRenderModeObj GXNtsc480Int      = {};
GXRenderModeObj GXMpal480IntDf    = {};
GXRenderModeObj GXMpal480Int      = {};
GXRenderModeObj GXPal528IntDf     = {};
GXRenderModeObj GXEurgb60Hz480IntDf = {};
GXRenderModeObj* sRenderMode = nullptr;
#ifdef PIKI_PC_GXREGS
volatile PcWGPipe GXWGFifo;
#else
volatile PPCWGPipe GXWGFifo;
#endif

extern "C" {

/* ── GX Init ── */
GXFifoObj* GXInit(void* base, u32 size) {
    (void)base; (void)size;
    pc_gfx_init();
#if PIKI_USE_GLES
    printf("[PC Port] GXInit() - OpenGL ES 3.0 backend initialized\n");
#else
    printf("[PC Port] GXInit() - OpenGL 3.3 backend initialized\n");
#endif
    return nullptr;
}
void __GXInitGX()  { }
void __GXPEInit()  { }

/* ── Fifo (from GXFifo.h) ── */
void GXInitFifoBase(GXFifoObj* fifo, void* base, u32 size)   { (void)fifo; (void)base; (void)size; }
void GXInitFifoPtrs(GXFifoObj* fifo, void* readPtr, void* writePtr) { (void)fifo; (void)readPtr; (void)writePtr; }
void GXInitFifoLimits(GXFifoObj* fifo, u32 hiWatermark, u32 loWatermark) { (void)fifo; (void)hiWatermark; (void)loWatermark; }
GXFifoObj* GXGetCPUFifo()           { return nullptr; }
GXFifoObj* GXGetGPFifo()            { return nullptr; }
void GXSetCPUFifo(GXFifoObj* fifo)  { (void)fifo; }
void GXSetGPFifo(GXFifoObj* fifo)   { (void)fifo; }

/* ── Vertex Format (from GXGeometry.h) ── */
void GXClearVtxDesc()               { pc_gfx_clear_vtx_desc(); }
void GXSetVtxDesc(GXAttr attr, GXAttrType type)             { pc_gfx_set_vtx_desc(attr, type); }
void GXSetVtxAttrFmt(GXVtxFmt fmt, GXAttr attr, GXCompCnt cnt, GXCompType type, u8 frac) {
    pc_gfx_set_vtx_attr_fmt(fmt, attr, cnt, type, frac);
}
void GXSetVtxAttrFmtv(GXVtxFmt fmt, GXVtxAttrFmtList* list) {
	if (!list) return;
	for (; list->mAttr != GX_VA_NULL; ++list) {
		pc_gfx_set_vtx_attr_fmt(fmt, list->mAttr, list->mCount, list->mType, list->mFrac);
	}
}
void GXSetArray(GXAttr attr, void* basePtr, u8 stride) { pc_gfx_set_array(attr, basePtr, stride); }
void GXInvalidateVtxCache()         { pc_gfx_begin_frame(); }
void GXSetNumTexGens(u8 num)        { (void)num; }
void GXSetNumChans(u8 num)          { (void)num; }
void GXSetNumTevStages(u8 num)      { pc_gfx_set_num_tev_stages(num); }

/* ── Transform / Viewport (from GXTransform.h) ── */
void GXSetProjection(const Mtx44 mtx, GXProjectionType type) { pc_gfx_set_projection(mtx, type); }
void GXSetProjectionv(const f32* ptr) { (void)ptr; }
void GXLoadPosMtxImm(Mtx mtx, u32 id)  { pc_gfx_load_pos_mtx(mtx, id); }
void GXLoadNrmMtxImm(Mtx mtx, u32 id)  { pc_gfx_load_nrm_mtx(mtx, id); }
void GXSetCurrentMtx(u32 id)        { pc_gfx_set_current_mtx(id); }
void GXLoadTexMtxImm(const Mtx mtx, u32 id, GXTexMtxType type) { pc_gfx_load_tex_mtx(mtx, id); }
void __GXSetMatrixIndex(GXAttr index) { (void)index; }

void __GXSetViewport()              { }
void GXSetViewport(f32 xOrig, f32 yOrig, f32 wd, f32 ht, f32 nearZ, f32 farZ) {
    pc_gfx_set_viewport(xOrig, yOrig, wd, ht, nearZ, farZ);
}
void GXSetViewportJitter(f32 xOrig, f32 yOrig, f32 wd, f32 ht, f32 nearZ, f32 farZ, u32 field) {
    (void)xOrig; (void)yOrig; (void)wd; (void)ht; (void)nearZ; (void)farZ; (void)field;
}
void GXSetScissor(u32 xOrig, u32 yOrig, u32 wd, u32 ht) {
    pc_gfx_set_scissor(xOrig, yOrig, wd, ht);
}
void GXSetScissorBoxOffset(s32 xOrig, s32 yOrig) { (void)xOrig; (void)yOrig; }
extern "C" void pc_gfx_get_scissor(u32* l, u32* t, u32* w, u32* h);
void GXGetScissor(u32* l, u32* t, u32* w, u32* h) { pc_gfx_get_scissor(l, t, w, h); }
void GXSetClipMode(GXClipMode mode) { (void)mode; }
void GXLoadPosMtxIndx(u16 mtxIndx, u32 id) { (void)mtxIndx; (void)id; }
void GXLoadNrmMtxImm3x3(Mtx33 mtx, u32 id) { (void)mtx; (void)id; }
void GXLoadNrmMtxIndx3x3(u16 mtxIndx, u32 id) { (void)mtxIndx; (void)id; }
void GXLoadTexMtxIndx(u16 index, u32 id, GXTexMtxType type) { (void)index; (void)id; (void)type; }

/* ── TEV (from GXTev.h) ── */
void GXSetTevOrder(GXTevStageID stage, GXTexCoordID coord, GXTexMapID map, GXChannelID chan) {
	(void)chan;
	pc_gfx_set_tev_order(stage, coord, map, chan);
}

// Expands the high-level TEV mode convenience call into the exact low-level
// ColorIn/AlphaIn/Op configuration the real hardware SDK programs, matching
// libogc's reference implementation. Stage 0 cannot read CPREV (nothing has
// been written yet), so it uses zeroed/texture-based variants instead.
void GXSetTevOp(GXTevStageID stage, GXTevMode mode) {
	const bool first = (stage == GX_TEVSTAGE0);
	switch (mode) {
	case GX_MODULATE:
		pc_gfx_set_tev_color_in(stage,
		    first ? GX_CC_ZERO : GX_CC_CPREV,
		    GX_CC_TEXC, GX_CC_RASC, GX_CC_ZERO);
		pc_gfx_set_tev_alpha_in(stage,
		    first ? GX_CA_ZERO : GX_CA_APREV,
		    GX_CA_TEXA, GX_CA_RASA, GX_CA_ZERO);
		break;
	case GX_DECAL:
		pc_gfx_set_tev_color_in(stage,
		    GX_CC_TEXA, GX_CC_TEXC, GX_CC_RASC,
		    first ? GX_CC_TEXA : GX_CC_CPREV);
		pc_gfx_set_tev_alpha_in(stage,
		    GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO,
		    first ? GX_CA_TEXA : GX_CA_APREV);
		break;
	case GX_BLEND:
		pc_gfx_set_tev_color_in(stage,
		    first ? GX_CC_ZERO : GX_CC_CPREV,
		    GX_CC_TEXC, GX_CC_KONST, GX_CC_ZERO);
		pc_gfx_set_tev_alpha_in(stage,
		    first ? GX_CA_ZERO : GX_CA_APREV,
		    GX_CA_TEXA, GX_CA_KONST, GX_CA_ZERO);
		break;
	case GX_REPLACE:
		pc_gfx_set_tev_color_in(stage, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_TEXC);
		pc_gfx_set_tev_alpha_in(stage, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_TEXA);
		break;
	case GX_PASSCLR:
	default:
		// "Pass color": hand the rasterized vertex color through untouched.
		pc_gfx_set_tev_color_in(stage, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_RASC);
		pc_gfx_set_tev_alpha_in(stage, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_RASA);
		break;
	}
	pc_gfx_set_tev_color_op(stage, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
	pc_gfx_set_tev_alpha_op(stage, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
}
void GXSetTevColorIn(GXTevStageID stage, GXTevColorArg a, GXTevColorArg b, GXTevColorArg c, GXTevColorArg d) {
    pc_gfx_set_tev_color_in(stage, a, b, c, d);
}
void GXSetTevAlphaIn(GXTevStageID stage, GXTevAlphaArg a, GXTevAlphaArg b, GXTevAlphaArg c, GXTevAlphaArg d) {
    pc_gfx_set_tev_alpha_in(stage, a, b, c, d);
}
void GXSetTevColorOp(GXTevStageID stage, GXTevOp op, GXTevBias bias, GXTevScale scale, GXBool clamp, GXTevRegID outReg) {
    pc_gfx_set_tev_color_op(stage, op, bias, scale, clamp, outReg);
}
void GXSetTevAlphaOp(GXTevStageID stage, GXTevOp op, GXTevBias bias, GXTevScale scale, GXBool clamp, GXTevRegID outReg) {
    pc_gfx_set_tev_alpha_op(stage, op, bias, scale, clamp, outReg);
}
void GXSetTevColor(GXTevRegID reg, GXColor color) { pc_gfx_set_tev_color(reg, color); }
void GXSetTevColorS10(GXTevRegID reg, GXColorS10 color) { pc_gfx_set_tev_color_s10(reg, color); }
void GXSetTevKColor(GXTevKColorID id, GXColor color)    { pc_gfx_set_tev_kcolor(id, color); }
void GXSetTevKColorSel(GXTevStageID stage, GXTevKColorSel sel) { pc_gfx_set_tev_kcolor_sel(stage, sel); }
void GXSetTevKAlphaSel(GXTevStageID stage, GXTevKAlphaSel sel) { pc_gfx_set_tev_kalpha_sel(stage, sel); }
void GXSetTevSwapMode(GXTevStageID stage, GXTevSwapSel rasSel, GXTevSwapSel texSel) {
    pc_gfx_set_tev_swap_mode(stage, rasSel, texSel);
}
void GXSetTevSwapModeTable(GXTevSwapSel sel, GXTevColorChan r, GXTevColorChan g, GXTevColorChan b, GXTevColorChan a) {
    pc_gfx_set_tev_swap_mode_table(sel, r, g, b, a);
}
void GXSetTevDirect(GXTevStageID stage) { pc_gfx_set_tev_indirect(stage, 0, 0, 0, 0, 0, 0, 0); }
void GXSetAlphaCompare(GXCompare comp0, u8 ref0, GXAlphaOp op, GXCompare comp1, u8 ref1) {
    pc_gfx_set_alpha_compare(comp0, ref0, op, comp1, ref1);
}

/* ── Texture (from GXTexture.h) ── */
void GXInitTexObj(GXTexObj* obj, void* imagePtr, u16 width, u16 height,
                  GXTexFmt format, GXTexWrapMode wrapS, GXTexWrapMode wrapT, GXBool mipmap) {
    pc_gfx_init_tex_obj(obj, imagePtr, width, height, format, wrapS, wrapT, mipmap);
}
void GXInitTexObjCI(GXTexObj* obj, void* imagePtr, u16 width, u16 height,
                    GXCITexFmt format, GXTexWrapMode wrapS, GXTexWrapMode wrapT, GXBool mipmap, u32 tlutName) {
    pc_gfx_init_tex_obj_ci(obj, imagePtr, width, height, format, wrapS, wrapT, mipmap, tlutName);
}
void GXInitTexObjLOD(GXTexObj* obj, GXTexFilter minFilt, GXTexFilter magFilt, f32 minLOD, f32 maxLOD,
                     f32 lodBias, GXBool biasClampEnable, GXBool edgeLODEnable, GXAnisotropy maxAniso) {
    (void)minLOD; (void)maxLOD; (void)lodBias; (void)biasClampEnable; (void)edgeLODEnable; (void)maxAniso;
    pc_gfx_set_tex_obj_filter(obj, minFilt, magFilt);
}
void GXLoadTexObj(GXTexObj* obj, GXTexMapID id)     { pc_gfx_load_tex_obj(obj, id); }
void GXInvalidateTexAll()                           { }
void GXSetTexCoordGen2(GXTexCoordID dstCoord, GXTexGenType func, GXTexGenSrc srcParam,
                       u32 mtx, GXBool normalize, u32 postMtx) {
    (void)normalize; (void)postMtx;
    pc_gfx_set_tex_coord_gen(dstCoord, func, srcParam, mtx);
}
void GXInitTlutObj(GXTlutObj* obj, void* lut, GXTlutFmt fmt, u16 nEntries) {
    pc_gfx_init_tlut_obj(obj, lut, fmt, nEntries);
}
void GXLoadTlut(GXTlutObj* obj, u32 tlutName)      { pc_gfx_load_tlut(obj, tlutName); }
// Same computation as the SDK (GXTexture.c): tile size depends on the format
// and mipmap chains add each level. JUTTexture / ShadowCylinder size their
// capture buffers with this, so the exact value matters for heap use.
u32  GXGetTexBufferSize(u16 width, u16 height, u32 fmt, GXBool mipMap, u8 maxLOD) {
    u32 tileW, tileH, tileBytes;
    switch (fmt) {
    case GX_TF_I4: case GX_TF_C4: case GX_TF_CMPR: case GX_CTF_R4: case GX_CTF_Z4:
        tileW = 8; tileH = 8; tileBytes = 32; break;
    case GX_TF_I8: case GX_TF_IA4: case GX_TF_C8: case GX_TF_Z8:
    case GX_CTF_RA4: case GX_CTF_A8: case GX_CTF_R8: case GX_CTF_G8: case GX_CTF_B8:
    case GX_CTF_Z8M: case GX_CTF_Z8L:
        tileW = 8; tileH = 4; tileBytes = 32; break;
    case GX_TF_IA8: case GX_TF_RGB565: case GX_TF_RGB5A3: case GX_TF_C14X2: case GX_TF_Z16:
    case GX_CTF_RA8: case GX_CTF_RG8: case GX_CTF_GB8: case GX_CTF_Z16L:
        tileW = 4; tileH = 4; tileBytes = 32; break;
    case GX_TF_RGBA8: case GX_TF_Z24X8:
        tileW = 4; tileH = 4; tileBytes = 64; break;
    default:
        tileW = 4; tileH = 4; tileBytes = 64; break;
    }
    u32 size = 0;
    u32 w = width, h = height;
    if (mipMap) {
        for (u32 level = 0; level < maxLOD; ++level) {
            const u32 tw = (w + tileW - 1) / tileW;
            const u32 th = (h + tileH - 1) / tileH;
            size += tw * th * tileBytes;
            if (w == 1 && h == 1) break;
            w = w > 1 ? w >> 1 : 1;
            h = h > 1 ? h >> 1 : 1;
        }
    } else {
        const u32 tw = (w + tileW - 1) / tileW;
        const u32 th = (h + tileH - 1) / tileH;
        size = tw * th * tileBytes;
    }
    return size;
}

/* ── Lighting (from GXLight.h) ── */
void GXInitLightAttn(GXLightObj* ltObj, f32 a0, f32 a1, f32 a2, f32 k0, f32 k1, f32 k2) {
    pc_gfx_init_light_attn(ltObj, a0, a1, a2, k0, k1, k2);
}
// The a0..a2 / k0..k2 tables of the SDK's GXLight.c, which pikmin2_legacy
// does not build. The title and map lights are all spot lights with GENTLE
// distance attenuation; as no-ops these left a = k = 0 and the lit
// surfaces at ambient level.
void GXInitLightSpot(GXLightObj* ltObj, f32 cutoff, GXSpotFn spotFunc) {
    if (cutoff <= 0.0f || cutoff > 90.0f) spotFunc = GX_SP_OFF;
    const f32 cr = cosf(cutoff * 3.14159265f / 180.0f);
    f32 a0 = 1.0f, a1 = 0.0f, a2 = 0.0f, d;
    switch (spotFunc) {
    case GX_SP_FLAT:  a0 = -1000.0f * cr; a1 = 1000.0f; a2 = 0.0f; break;
    case GX_SP_COS:   a1 = 1.0f / (1.0f - cr); a0 = -cr * a1; a2 = 0.0f; break;
    case GX_SP_COS2:  a2 = 1.0f / (1.0f - cr); a0 = 0.0f; a1 = -cr * a2; break;
    case GX_SP_SHARP: d = 1.0f / ((1.0f - cr) * (1.0f - cr)); a0 = cr * (cr - 2.0f) * d; a1 = 2.0f * d; a2 = -d; break;
    case GX_SP_RING1: d = 1.0f / ((1.0f - cr) * (1.0f - cr)); a2 = -4.0f * d; a0 = a2 * cr; a1 = 4.0f * (1.0f + cr) * d; break;
    case GX_SP_RING2: d = 1.0f / ((1.0f - cr) * (1.0f - cr)); a0 = 1.0f - 2.0f * cr * cr * d; a1 = 4.0f * cr * d; a2 = -2.0f * d; break;
    default: break;
    }
    pc_gfx_init_light_attn_a(ltObj, a0, a1, a2);
}
void GXInitLightColor(GXLightObj* ltObj, GXColor color) { pc_gfx_init_light_color(ltObj, color); }
void GXInitLightPos(GXLightObj* ltObj, f32 x, f32 y, f32 z) { pc_gfx_init_light_pos(ltObj, x, y, z); }
void GXInitLightDir(GXLightObj* ltObj, f32 nx, f32 ny, f32 nz) { pc_gfx_init_light_dir(ltObj, nx, ny, nz); }
void GXInitSpecularDir(GXLightObj* ltObj, f32 nx, f32 ny, f32 nz) { pc_gfx_init_specular_dir(ltObj, nx, ny, nz); }
void GXInitLightAttnA(GXLightObj* ltObj, f32 a0, f32 a1, f32 a2) { pc_gfx_init_light_attn_a(ltObj, a0, a1, a2); }
void GXInitLightAttnK(GXLightObj* ltObj, f32 k0, f32 k1, f32 k2) { pc_gfx_init_light_attn_k(ltObj, k0, k1, k2); }
void GXLoadLightObjImm(GXLightObj* ltObj, GXLightID ltId) { pc_gfx_load_light(ltObj, static_cast<u32>(ltId)); }
void GXSetChanCtrl(GXChannelID chan, GXBool enable, GXColorSrc ambSrc, GXColorSrc matSrc,
                   u32 lightMask, GXDiffuseFn diffFn, GXAttnFn attnFn) {
	pc_gfx_set_chan_ctrl(chan, enable, ambSrc, matSrc, lightMask, diffFn, attnFn);
}
void GXSetChanAmbColor(GXChannelID chan, GXColor ambColor) { pc_gfx_set_chan_amb_color(chan, ambColor); }
void GXSetChanMatColor(GXChannelID chan, GXColor matColor) { pc_gfx_set_chan_mat_color(chan, matColor); }

/* ── Pixel / Blend / Z (from GXPixel.h) ── */
void GXSetBlendMode(GXBlendMode type, GXBlendFactor srcFactor, GXBlendFactor dstFactor, GXLogicOp op) {
    pc_gfx_set_blend_mode(type, srcFactor, dstFactor, op);
}
void GXSetColorUpdate(GXBool updateEnable)  { pc_gfx_set_color_update(updateEnable); }
void GXSetAlphaUpdate(GXBool updateEnable)  { pc_gfx_set_alpha_update(updateEnable); }
void GXSetZMode(GXBool compareEnable, GXCompare func, GXBool updateEnable) {
    pc_gfx_set_z_mode(compareEnable, func, updateEnable);
}
void GXSetZCompLoc(GXBool beforeTex)        { (void)beforeTex; }
void GXSetDither(GXBool dither)             { (void)dither; }
void GXSetDstAlpha(GXBool enable, u8 alpha) { (void)enable; (void)alpha; } // TODO(pc_gfx): constant destination alpha not modelled
void GXSetPixelFmt(GXPixelFmt pix_fmt, GXZFmt16 z_fmt) { (void)z_fmt; pc_gfx_set_pixel_fmt(pix_fmt); }
void GXSetFog(GXFogType type, f32 startz, f32 endz, f32 nearz, f32 farz, GXColor color) {
    // The game sets fog per stage, with its own colour and distances, and this
    // used to throw all of it away. GX_FOG_NONE is how fog is switched off.
    pc_gfx_set_fog(type != GX_FOG_NONE, startz, endz, nearz, farz,
                   color.r, color.g, color.b);
}
void GXSetFogRangeAdj(GXBool enable, u16 center, GXFogAdjTable* table) {
    (void)enable; (void)center; (void)table;
}

/* ── Geometry (from GXGeometry.h) ── */
void __GXSetDirtyState()                    { }
void GXBegin(GXPrimitive type, GXVtxFmt vtxfmt, u16 nverts) { pc_gfx_begin(type, vtxfmt, nverts); }
void __GXSendFlushPrim()                    { }
/* GXEnd() is typically an inline/macro in GXHardware.h - we don't redefine it */
void GXSetCullMode(GXCullMode mode)         { pc_gfx_set_cull_mode(mode); }
void GXSetCoPlanar(GXBool doEnable)         { (void)doEnable; }
void GXSetLineWidth(u8 width, GXTexOffset texOffset) { (void)width; (void)texOffset; }
void GXSetPointSize(u8 size, GXTexOffset texOffset)  { (void)size; (void)texOffset; }
void GXEnableTexOffsets(GXTexCoordID coord, GXBool lineOffset, GXBool pointOffset) {
    (void)coord; (void)lineOffset; (void)pointOffset;
}
void __GXSetGenMode()                       { }

/* ── Display / Copy (GXFrameBuf.h) ── */
void GXCopyDisp(void* dest, GXBool clear)   { pc_gfx_copy_disp(dest, clear); }
void GXCopyTex(void* dest, GXBool clear) { pc_gfx_copy_tex(dest, clear); }
void GXSetCopyClear(GXColor clearColor, u32 clearZ) { pc_gfx_set_copy_clear(clearColor, clearZ); }
void GXSetCopyFilter(GXBool useAA, u8 samplePattern[12][2], GXBool doVertFilt, u8 vFilt[7]) {
    (void)useAA; (void)samplePattern; (void)doVertFilt; (void)vFilt;
}
void GXSetDispCopySrc(u16 left, u16 top, u16 wd, u16 ht) { (void)left; (void)top; (void)wd; (void)ht; }
void GXSetTexCopySrc(u16 left, u16 top, u16 wd, u16 ht) { pc_gfx_set_tex_copy_src(left, top, wd, ht); }
void GXSetDispCopyDst(u16 wd, u16 ht) { (void)wd; (void)ht; }
void GXSetTexCopyDst(u16 wd, u16 ht, GXTexFmt fmt, GXBool mipmap) { (void)mipmap; pc_gfx_set_tex_copy_dst(wd, ht, (u32)fmt); }
u32  GXSetDispCopyYScale(f32 yscale) { (void)yscale; return 0; }
void GXSetDispCopyGamma(GXGamma gamma) { (void)gamma; }
void GXSetDispCopyFrame2Field(GXCopyMode mode) { (void)mode; }
void GXSetCopyClamp(GXFBClamp clamp) { (void)clamp; }
void GXSetFieldMode(GXBool fieldMode, GXBool halfAspectRatio) { (void)fieldMode; (void)halfAspectRatio; }
void GXSetFieldMask(GXBool oddMask, GXBool evenMask) { (void)oddMask; (void)evenMask; }
// JUTXfb sizes its buffers from these; a fixed 480 lines under-allocated the
// PAL 538-line XFB and whoever wrote the last lines (JUTDirectPrint) trashed
// the heap block after it.
u16  GXGetNumXfbLines(u16 efbHeight, f32 yScale) {
    u16 lines = (u16)ceilf(efbHeight * yScale);
    return lines > 1024 ? 1024 : lines;
}
f32  GXGetYScaleFactor(u16 efbHeight, u16 xfbHeight) { return efbHeight ? (f32)xfbHeight / (f32)efbHeight : 1.0f; }
void GXClearBoundingBox()                   { }

/* ── Misc (from GXMisc.h) ── */
void GXFlush()                              { }
void GXPixModeSync()                        { }
void GXTexModeSync()                        { }
void GXSetDrawSync(u16 token)               { (void)token; }
void GXSetDrawDone()                        { }
void GXWaitDrawDone()                       { }
void GXAbortFrame()                         { }
void GXDrawDone()                           { }
void __GXAbort()                            { }
void GXSetMisc(GXMiscToken token, u32 val)  { (void)token; (void)val; }
void GXResetWriteGatherPipe()               { }
void GXReadDrawSync()                       { }
GXDrawSyncCallback GXSetDrawSyncCallback(GXDrawSyncCallback cb) { (void)cb; return nullptr; }
GXDrawDoneCallback GXSetDrawDoneCallback(GXDrawDoneCallback cb) { (void)cb; return nullptr; }
struct OSThread* GXSetCurrentGXThread()     { return nullptr; }

/* ── Performance (from GXPerf.h) ── */
void GXSetGPMetric(GXPerf0 perf0, GXPerf1 perf1) { (void)perf0; (void)perf1; }
void GXReadGPMetric(u32* cnt0, u32* cnt1) { *cnt0 = 0; *cnt1 = 0; }
void GXClearGPMetric() { }

/* ── Indirect Textures / Bump (from GXBump.h) ── */
void GXSetTevIndirect(GXTevStageID stage, GXIndTexStageID indStage, GXIndTexFormat fmt,
                      GXIndTexBiasSel biasSel, GXIndTexMtxID mtxID, GXIndTexWrap wrapS,
                      GXIndTexWrap wrapT, GXBool addPrev, GXBool utcLod, GXIndTexAlphaSel alphaSel) {
    (void)utcLod; (void)alphaSel;
    pc_gfx_set_tev_indirect(stage, indStage, fmt, biasSel, mtxID, wrapS, wrapT, addPrev);
}
void GXSetIndTexOrder(GXIndTexStageID indStage, GXTexCoordID texCoord, GXTexMapID texMap) {
    pc_gfx_set_ind_tex_order(indStage, texCoord, texMap);
}
void GXSetNumIndStages(u8 num) { pc_gfx_set_num_ind_stages(num); }
void GXSetIndTexCoordScale(GXIndTexStageID indStage, GXIndTexScale scaleS, GXIndTexScale scaleT) {
    pc_gfx_set_ind_tex_coord_scale(indStage, scaleS, scaleT);
}
void GXSetIndTexMtx(GXIndTexMtxID id, const Mtx23 offset, s8 scaleExp) {
    pc_gfx_set_ind_tex_mtx(id, offset, scaleExp);
}
// Ayudas del SDK, con el mismo GXSetTevIndirect que generan en la consola.
void GXSetTevIndBumpST(GXTevStageID tevStage, GXIndTexStageID indStage, GXIndTexMtxID mtxID) {
    // Matriz dinámica S (5..7) y T (9..11) correspondiente a GX_ITM_0..2.
    const u32 base = (mtxID >= GX_ITM_0 && mtxID <= GX_ITM_2) ? u32(mtxID) : 0;
    const u32 sMtx = base ? base + 4 : 0, tMtx = base ? base + 8 : 0;
    pc_gfx_set_tev_indirect(tevStage, indStage, GX_ITF_8, GX_ITB_ST, sMtx, GX_ITW_0, GX_ITW_0, GX_FALSE);
    pc_gfx_set_tev_indirect(tevStage + 1, indStage, GX_ITF_8, GX_ITB_ST, tMtx, GX_ITW_0, GX_ITW_0, GX_TRUE);
    pc_gfx_set_tev_indirect(tevStage + 2, indStage, GX_ITF_8, GX_ITB_NONE, GX_ITM_OFF, GX_ITW_OFF, GX_ITW_OFF, GX_TRUE);
}
void GXSetTevIndBumpXYZ(GXTevStageID tevStage, GXIndTexStageID indStage, GXIndTexMtxID mtxID) {
    pc_gfx_set_tev_indirect(tevStage, indStage, GX_ITF_8, GX_ITB_STU, mtxID, GX_ITW_OFF, GX_ITW_OFF, GX_FALSE);
}
void GXSetTevIndRepeat(GXTevStageID tevStage) {
    pc_gfx_set_tev_indirect(tevStage, GX_IND_TEX_STAGE_0, GX_ITF_8, GX_ITB_NONE, GX_ITM_OFF, GX_ITW_0, GX_ITW_0, GX_TRUE);
}

/* ── Poke functions (from GXMisc.h) ── */
void GXPokeAlphaMode(GXCompare func, u8 threshold) { (void)func; (void)threshold; }
void GXPokeAlphaRead(GXAlphaReadMode mode) { (void)mode; }
void GXPokeAlphaUpdate(GXBool doUpdate) { (void)doUpdate; }
void GXPokeBlendMode(GXBlendMode mode, GXBlendFactor srcFactor, GXBlendFactor destFactor, GXLogicOp op) {
    (void)mode; (void)srcFactor; (void)destFactor; (void)op;
}
void GXPokeColorUpdate(GXBool doUpdate) { (void)doUpdate; }
void GXPokeDstAlpha(GXBool doEnable, u8 alpha) { (void)doEnable; (void)alpha; }
void GXPokeDither(GXBool doDither) { (void)doDither; }
void GXPokeZMode(GXBool doCompare, GXCompare func, GXBool doUpdate) {
    (void)doCompare; (void)func; (void)doUpdate;
}
void GXPeekARGB(u16 x, u16 y, u32* color) { (void)x; (void)y; *color = 0; }
void GXPokeARGB(u16 x, u16 y, u32 color) { (void)x; (void)y; (void)color; }
void GXPeekZ(u16 x, u16 y, u32* z) { (void)x; (void)y; *z = 0; }
void GXPokeZ(u16 x, u16 y, u32 z) { (void)x; (void)y; (void)z; }

void GXCallDisplayList(void* list, u32 nbytes) { pc_gfx_call_display_list(list, nbytes); }
void GXSetVtxDescv(GXVtxDescList* attrList) {
	if (!attrList) return;
	pc_gfx_clear_vtx_desc();
	for (; attrList->mAttr != GX_VA_NULL; ++attrList) {
		pc_gfx_set_vtx_desc(attrList->mAttr, attrList->mType);
	}
}
void GXBeginDisplayList(void* list, u32 size) { (void)list; (void)size; }
u32 GXEndDisplayList(void) { return 0; }


void GXGetGPStatus(GXBool* a, GXBool* b, GXBool* c, GXBool* d, GXBool* e) {
    if (a) *a = GX_FALSE; if (b) *b = GX_FALSE; if (c) *c = GX_TRUE; if (d) *d = GX_TRUE; if (e) *e = GX_FALSE;
}
void GXSaveCPUFifo(GXFifoObj* obj) { (void)obj; }
void GXInitFogAdjTable(GXFogAdjTable* table, u16 width, const Mtx44 projMtx) { (void)table; (void)width; (void)projMtx; }
void GXInitLightDistAttn(GXLightObj* obj, f32 refDist, f32 refBrightness, GXDistAttnFn distFunc) {
    if (refDist < 0.0f || refBrightness <= 0.0f || refBrightness >= 1.0f) distFunc = GX_DA_OFF;
    f32 k0 = 1.0f, k1 = 0.0f, k2 = 0.0f;
    switch (distFunc) {
    case GX_DA_GENTLE: k1 = (1.0f - refBrightness) / (refBrightness * refDist); break;
    case GX_DA_MEDIUM: k1 = 0.5f * (1.0f - refBrightness) / (refBrightness * refDist);
                       k2 = 0.5f * (1.0f - refBrightness) / (refBrightness * refDist * refDist); break;
    case GX_DA_STEEP:  k2 = (1.0f - refBrightness) / (refBrightness * refDist * refDist); break;
    default: break;
    }
    pc_gfx_init_light_attn_k(obj, k0, k1, k2);
}
void GXInitTexCacheRegion(GXTexRegion* region, GXBool is32bMIPmap, u32 memEven, GXTexCacheSize sizeEven, u32 memOdd, GXTexCacheSize sizeOdd) {
    (void)region; (void)is32bMIPmap; (void)memEven; (void)sizeEven; (void)memOdd; (void)sizeOdd;
}
void GXReadXfRasMetric(u32* a, u32* b, u32* c, u32* d) { if(a)*a=0; if(b)*b=0; if(c)*c=0; if(d)*d=0; }
void GXSetZTexture(GXZTexOp op, GXTexFmt format, u32 bias) { (void)op; (void)format; (void)bias; }
void __GXSetIndirectMask(u32 mask) { (void)mask; }
void GXSetTevIndWarp(GXTevStageID tevStage, GXIndTexStageID indStage, GXBool isSignedOffset, GXBool isReplaceMode, GXIndTexMtxID mtx) {
    const GXIndTexWrap wrap = isReplaceMode ? GX_ITW_0 : GX_ITW_OFF;
    pc_gfx_set_tev_indirect(tevStage, indStage, GX_ITF_8, isSignedOffset ? GX_ITB_STU : GX_ITB_NONE, mtx, wrap, wrap, GX_FALSE);
}
} // extern "C"
