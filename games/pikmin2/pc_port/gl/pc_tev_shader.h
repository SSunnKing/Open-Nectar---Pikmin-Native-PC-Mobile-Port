#pragma once

#include <cstdint>
#include <string>

// Specialised TEV fragment shader generation.
//
// The port originally ran a single "ubershader" that interpreted the GX TEV
// configuration per pixel: a dynamic loop over up to 16 stages, an eight-way
// branch to pick a sampler and 16-way selector chains for every combiner
// operand. Nothing in it could be folded by the driver because every decision
// arrived as a uniform, so the program carried the register pressure of the
// worst case for every draw and occupancy collapsed.
//
// The configuration is constant for the whole draw, so it belongs in the
// program, not in uniforms. This module turns a configuration into GLSL with
// every selector resolved at generation time: no loops, no operand branches,
// one texture fetch per stage and only the samplers a stage actually reads.
// Values that legitimately change per draw (colours, matrices, konst values,
// alpha reference levels) stay as uniforms, so two draws sharing a material
// still share one program.
//
// The encodings below mirror the GX enums deliberately rather than including
// the GX headers, which keeps the generator free of graphics dependencies and
// testable on its own.

// Colour combiner operand, matching GXTevColorArg.
//   0 CPREV   1 APREV   2 C0     3 A0     4 C1    5 A1
//   6 C2      7 A2      8 TEXC   9 TEXA  10 RASC 11 RASA
//  12 ONE    13 HALF   14 KONST  15 ZERO
//
// Alpha combiner operand, matching GXTevAlphaArg.
//   0 APREV   1 A0      2 A1     3 A2     4 TEXA  5 RASA  6 KONST  7 ZERO
//
// Combiner op: 0 add, 1 subtract, 8..15 the comparison operations.
// Bias: 0 zero, 1 +0.5, 2 -0.5.   Scale: 0 x1, 1 x2, 2 x4, 3 /2.
// Output register: 0 PREV, 1 REG0, 2 REG1, 3 REG2.
// Compare function, matching GXCompare:
//   0 never 1 less 2 equal 3 lequal 4 greater 5 nequal 6 gequal 7 always
// Alpha test operator: 0 AND, 1 OR, 2 XOR, 3 XNOR.

struct PcTevStageKey {
	uint8_t colorIn[4] = { 15, 15, 15, 10 };
	uint8_t alphaIn[4] = { 7, 7, 7, 5 };
	uint8_t colorOp    = 0;
	uint8_t colorBias  = 0;
	uint8_t colorScale = 0;
	uint8_t colorClamp = 1;
	uint8_t colorOutReg = 0;
	uint8_t alphaOp    = 0;
	uint8_t alphaBias  = 0;
	uint8_t alphaScale = 0;
	uint8_t alphaClamp = 1;
	uint8_t alphaOutReg = 0;
	// Texture map sampled by this stage, or -1 when the stage reads no texture.
	int8_t texMap   = -1;
	uint8_t texCoord = 0;
	// 0 or 1 select a lit colour channel; -1 is GX_COLOR_NULL.
	int8_t rasChannel = 0;
	uint8_t rasSwapSel = 0;
	uint8_t texSwapSel = 0;
	// Texturas indirectas (GXSetTevIndirect), con los valores de los enums GX.
	// Todo a cero cuando la etapa no usa nada indirecto, para no partir la caché.
	//   indMtx: 0 off, 1..3 matriz 0..2, 5..7 dinámica S, 9..11 dinámica T.
	//   indWrap: 0 off, 1..5 = 256,128,64,32,16 texels, 6 = 0 (sustituye).
	//   indFmt: 0 8 bits, 1 5, 2 4, 3 3.   indBias: bits S=1 T=2 U=4.
	uint8_t indStage   = 0;
	uint8_t indFmt     = 0;
	uint8_t indBias    = 0;
	uint8_t indMtx     = 0;
	uint8_t indWrapS   = 0;
	uint8_t indWrapT   = 0;
	uint8_t indAddPrev = 0;
};

struct PcTevShaderKey {
	uint8_t numStages = 1;
	PcTevStageKey stages[16];
	// Four swap tables, each selecting a source component (0=R 1=G 2=B 3=A)
	// for red, green, blue and alpha.
	uint8_t swapTable[4][4] = {
		{ 0, 1, 2, 3 }, { 0, 1, 2, 3 }, { 0, 1, 2, 3 }, { 0, 1, 2, 3 }
	};
	uint8_t alphaComp0 = 7;
	uint8_t alphaComp1 = 7;
	uint8_t alphaTestOp = 0;
	uint8_t useMaterialRgb = 0;
	uint8_t useMaterialAlpha = 0;
	uint8_t useMaterialRgb1 = 0;
	// Linear fog, as the GameCube's fog unit applied it: after the TEV stages
	// and before the blend, on colour only. Part of the key rather than a
	// uniform so a draw with fog off carries none of its cost -- the scene and
	// the HUD are drawn with different settings, not different materials.
	uint8_t fog = 0;
	// Etapas indirectas (GXSetNumIndStages / GXSetIndTexOrder /
	// GXSetIndTexCoordScale). Cero si ninguna etapa TEV las usa.
	uint8_t numIndStages = 0;
	uint8_t indTexCoord[4] = { 0, 0, 0, 0 };
	uint8_t indTexMap[4]   = { 0, 0, 0, 0 };
	uint8_t indScaleS[4]   = { 0, 0, 0, 0 }; // GXIndTexScale: divide entre 2^n
	uint8_t indScaleT[4]   = { 0, 0, 0, 0 };
};

// True cuando la etapa usa algo del bloque indirecto (desplazamiento, wrap o
// acumulación), y su coordenada hay que calcularla en el shader.
inline bool pc_tev_stage_indirect(const PcTevStageKey& stage)
{
	return stage.indMtx != 0 || stage.indWrapS != 0 || stage.indWrapT != 0 || stage.indAddPrev != 0;
}

bool operator==(const PcTevShaderKey& a, const PcTevShaderKey& b);
inline bool operator!=(const PcTevShaderKey& a, const PcTevShaderKey& b) { return !(a == b); }

// Stable hash over the significant bytes of the key. Equal keys always hash
// equally; the cache still compares keys on a hit, so collisions cost a miss
// and never a wrong shader.
uint64_t pc_tev_hash_key(const PcTevShaderKey& key);

// Generates the complete fragment shader for a configuration.
std::string pc_tev_build_fragment_source(const PcTevShaderKey& key);

// True when the alpha test can never reject a fragment, so the generated
// shader contains no discard and the driver may keep early-Z enabled.
bool pc_tev_alpha_test_always_passes(const PcTevShaderKey& key);
