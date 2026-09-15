#include "pc_tev_shader.h"

#include <cstdio>
#include <cstring>

namespace {

#if PIKI_USE_GLES
const char* kGlslVersion = "#version 300 es\n";
const char* kGlslPrecision = "precision highp float;\nprecision highp int;\n";
#else
const char* kGlslVersion = "#version 140\n";
const char* kGlslPrecision = "";
#endif

// Combiner operand ids, named to keep the generator readable.
enum : uint8_t {
	CC_CPREV = 0, CC_APREV, CC_C0, CC_A0, CC_C1, CC_A1, CC_C2, CC_A2,
	CC_TEXC, CC_TEXA, CC_RASC, CC_RASA, CC_ONE, CC_HALF, CC_KONST, CC_ZERO
};
enum : uint8_t {
	CA_APREV = 0, CA_A0, CA_A1, CA_A2, CA_TEXA, CA_RASA, CA_KONST, CA_ZERO
};
enum : uint8_t { CMP_NEVER = 0, CMP_ALWAYS = 7 };

const char* color_arg_expr(uint8_t id)
{
	switch (id) {
	case CC_CPREV: return "prev.rgb";
	case CC_APREV: return "vec3(prev.a)";
	case CC_C0:    return "c0.rgb";
	case CC_A0:    return "vec3(c0.a)";
	case CC_C1:    return "c1.rgb";
	case CC_A1:    return "vec3(c1.a)";
	case CC_C2:    return "c2.rgb";
	case CC_A2:    return "vec3(c2.a)";
	case CC_TEXC:  return "tex.rgb";
	case CC_TEXA:  return "vec3(tex.a)";
	case CC_RASC:  return "rast.rgb";
	case CC_RASA:  return "vec3(rast.a)";
	case CC_ONE:   return "vec3(1.0)";
	case CC_HALF:  return "vec3(0.5)";
	case CC_KONST: return "konst.rgb";
	default:       return "vec3(0.0)";
	}
}

const char* alpha_arg_expr(uint8_t id)
{
	switch (id) {
	case CA_APREV: return "prev.a";
	case CA_A0:    return "c0.a";
	case CA_A1:    return "c1.a";
	case CA_A2:    return "c2.a";
	case CA_TEXA:  return "tex.a";
	case CA_RASA:  return "rast.a";
	case CA_KONST: return "konst.a";
	default:       return "0.0";
	}
}

const char* bias_literal(uint8_t bias)
{
	return bias == 1 ? "0.5" : bias == 2 ? "-0.5" : "0.0";
}

const char* scale_literal(uint8_t scale)
{
	return scale == 1 ? "2.0" : scale == 2 ? "4.0" : scale == 3 ? "0.5" : "1.0";
}

const char* register_name(uint8_t reg)
{
	switch (reg) {
	case 0:  return "prev";
	case 1:  return "c0";
	case 2:  return "c1";
	default: return "c2";
	}
}

const char* swizzle_component(uint8_t component)
{
	switch (component) {
	case 0:  return "r";
	case 1:  return "g";
	case 2:  return "b";
	default: return "a";
	}
}

bool swap_table_is_identity(const uint8_t table[4])
{
	return table[0] == 0 && table[1] == 1 && table[2] == 2 && table[3] == 3;
}

// Applies a swap table by generating a swizzle, which the driver resolves for
// free. The ubershader had to index a uniform array and rebuild a vec4
// component by component.
void emit_swap(std::string& out, const char* target, const uint8_t table[4])
{
	if (swap_table_is_identity(table)) return;
	out += "\t";
	out += target;
	out += " = ";
	out += target;
	out += ".";
	for (int i = 0; i < 4; ++i) out += swizzle_component(table[i]);
	out += ";\n";
}

// Reproduces alphaCompare() from the ubershader for a known function. Returns
// an empty string when the result is a compile-time constant.
std::string compare_expr(uint8_t func, const char* value, const char* reference)
{
	char buffer[256];
	switch (func) {
	case 1: snprintf(buffer, sizeof(buffer), "(%s < %s)", value, reference); break;
	case 2: snprintf(buffer, sizeof(buffer), "(abs(%s - %s) < (0.5 / 255.0))", value, reference); break;
	case 3: snprintf(buffer, sizeof(buffer), "(%s <= %s)", value, reference); break;
	case 4: snprintf(buffer, sizeof(buffer), "(%s > %s)", value, reference); break;
	case 5: snprintf(buffer, sizeof(buffer), "(abs(%s - %s) >= (0.5 / 255.0))", value, reference); break;
	case 6: snprintf(buffer, sizeof(buffer), "(%s >= %s)", value, reference); break;
	default: return std::string();
	}
	return std::string(buffer);
}

bool compare_is_static(uint8_t func) { return func == CMP_NEVER || func == CMP_ALWAYS; }

bool combine_static(uint8_t op, bool test0, bool test1)
{
	switch (op) {
	case 0:  return test0 && test1;
	case 1:  return test0 || test1;
	case 2:  return test0 != test1;
	default: return test0 == test1;
	}
}

void emit_stage(std::string& out, const PcTevShaderKey& key, int index)
{
	const PcTevStageKey& stage = key.stages[index];
	char line[512];

	snprintf(line, sizeof(line), "\t// stage %d\n\t{\n", index);
	out += line;

	// Exactly one texture fetch, against the one sampler this stage names.
	if (stage.texMap >= 0) {
		snprintf(line, sizeof(line), "\t\tvec4 tex = texture(uTex%d, vTexCoord%d);\n",
		         int(stage.texMap), int(stage.texCoord));
	} else {
		snprintf(line, sizeof(line), "\t\tvec4 tex = vec4(1.0);\n");
	}
	out += line;
	std::string swap;
	emit_swap(swap, "tex", key.swapTable[stage.texSwapSel & 3]);
	if (!swap.empty()) out += "\t" + swap;

	if (stage.rasChannel < 0) {
		out += "\t\tvec4 rast = vec4(0.0);\n";
	} else if (stage.rasChannel == 1) {
		out += "\t\tvec4 rast = rast1;\n";
	} else {
		out += "\t\tvec4 rast = rast0;\n";
	}
	swap.clear();
	emit_swap(swap, "rast", key.swapTable[stage.rasSwapSel & 3]);
	if (!swap.empty()) out += "\t" + swap;

	// Indexed with a literal, so this stays a compile-time selection. The name
	// deliberately matches the ubershader's uniform so both programs are fed by
	// the same upload path.
	snprintf(line, sizeof(line), "\t\tvec4 konst = uTevKonst[%d];\n", index);
	out += line;

	// ── colour ──
	const char* ca = color_arg_expr(stage.colorIn[0]);
	const char* cb = color_arg_expr(stage.colorIn[1]);
	const char* cc = color_arg_expr(stage.colorIn[2]);
	const char* cd = color_arg_expr(stage.colorIn[3]);

	if (stage.colorOp >= 8) {
		// Comparison operations, ported from compareTevColor() with the op
		// resolved, so only the taken branch is generated.
		const bool isEqual = (stage.colorOp & 1) != 0;
		if (stage.colorOp >= 14) {
			snprintf(line, sizeof(line),
			         "\t\tvec3 qa = floor(clamp(%s, 0.0, 1.0) * 255.0 + 0.5);\n"
			         "\t\tvec3 qb = floor(clamp(%s, 0.0, 1.0) * 255.0 + 0.5);\n"
			         "\t\tbvec3 cpass = %s(qa, qb);\n"
			         "\t\tvec3 cResult = %s + vec3(cpass.x ? (%s).r : 0.0, cpass.y ? (%s).g : 0.0, cpass.z ? (%s).b : 0.0);\n",
			         ca, cb, isEqual ? "equal" : "greaterThan", cd, cc, cc, cc);
			out += line;
		} else {
			// packTevCompare(), with the component count resolved.
			const int components = (stage.colorOp < 10) ? 1 : (stage.colorOp < 12) ? 2 : 3;
			auto pack = [components](const char* name) {
				std::string expr(name);
				expr += ".r";
				if (components >= 2) { expr += " + "; expr += name; expr += ".g * 256.0"; }
				if (components >= 3) { expr += " + "; expr += name; expr += ".b * 65536.0"; }
				return expr;
			};
			snprintf(line, sizeof(line),
			         "\t\tvec3 qa = floor(clamp(%s, 0.0, 1.0) * 255.0 + 0.5);\n"
			         "\t\tvec3 qb = floor(clamp(%s, 0.0, 1.0) * 255.0 + 0.5);\n",
			         ca, cb);
			out += line;
			out += "\t\tfloat av = " + pack("qa") + ";\n";
			out += "\t\tfloat bv = " + pack("qb") + ";\n";
			snprintf(line, sizeof(line),
			         "\t\tbool cpass = %s;\n"
			         "\t\tvec3 cResult = %s + (cpass ? %s : vec3(0.0));\n",
			         isEqual ? "(av == bv)" : "(av > bv)", cd, cc);
			out += line;
		}
	} else {
		snprintf(line, sizeof(line), "\t\tvec3 cMix = %s * (vec3(1.0) - %s) + %s * %s;\n",
		         ca, cc, cb, cc);
		out += line;
		snprintf(line, sizeof(line), "\t\tvec3 cResult = %s %c cMix;\n",
		         cd, stage.colorOp == 0 ? '+' : '-');
		out += line;
		if (stage.colorBias != 0 || stage.colorScale != 0) {
			snprintf(line, sizeof(line), "\t\tcResult = (cResult + %s) * %s;\n",
			         bias_literal(stage.colorBias), scale_literal(stage.colorScale));
			out += line;
		}
	}
	snprintf(line, sizeof(line), "\t\tcResult = clamp(cResult, %s);\n",
	         stage.colorClamp ? "0.0, 1.0" : "-4.0, 4.0");
	out += line;

	// ── alpha ──
	const char* aa = alpha_arg_expr(stage.alphaIn[0]);
	const char* ab = alpha_arg_expr(stage.alphaIn[1]);
	const char* ac = alpha_arg_expr(stage.alphaIn[2]);
	const char* ad = alpha_arg_expr(stage.alphaIn[3]);

	if (stage.alphaOp >= 14) {
		const bool isEqual = (stage.alphaOp & 1) != 0;
		snprintf(line, sizeof(line),
		         "\t\tfloat aqa = floor(clamp(%s, 0.0, 1.0) * 255.0 + 0.5);\n"
		         "\t\tfloat aqb = floor(clamp(%s, 0.0, 1.0) * 255.0 + 0.5);\n"
		         "\t\tfloat aResult = %s + (%s ? %s : 0.0);\n",
		         aa, ab, ad, isEqual ? "(aqa == aqb)" : "(aqa > aqb)", ac);
		out += line;
	} else {
		snprintf(line, sizeof(line), "\t\tfloat aMix = %s * (1.0 - %s) + %s * %s;\n",
		         aa, ac, ab, ac);
		out += line;
		// The ubershader routes alpha ops 8..13 into its subtract branch. That
		// is reproduced exactly: this generator must not change pixels.
		snprintf(line, sizeof(line), "\t\tfloat aResult = %s %c aMix;\n",
		         ad, stage.alphaOp == 0 ? '+' : '-');
		out += line;
		if (stage.alphaOp < 8 && (stage.alphaBias != 0 || stage.alphaScale != 0)) {
			snprintf(line, sizeof(line), "\t\taResult = (aResult + %s) * %s;\n",
			         bias_literal(stage.alphaBias), scale_literal(stage.alphaScale));
			out += line;
		}
	}
	snprintf(line, sizeof(line), "\t\taResult = clamp(aResult, %s);\n",
	         stage.alphaClamp ? "0.0, 1.0" : "-4.0, 4.0");
	out += line;

	// GX routes the colour and alpha outputs independently, so each writes
	// only its own components of the destination register.
	snprintf(line, sizeof(line), "\t\t%s.rgb = cResult;\n\t\t%s.a = aResult;\n\t}\n",
	         register_name(stage.colorOutReg), register_name(stage.alphaOutReg));
	out += line;
}

} // namespace

bool operator==(const PcTevShaderKey& a, const PcTevShaderKey& b)
{
	if (a.numStages != b.numStages) return false;
	if (a.alphaComp0 != b.alphaComp0 || a.alphaComp1 != b.alphaComp1) return false;
	if (a.alphaTestOp != b.alphaTestOp) return false;
	if (a.useMaterialRgb != b.useMaterialRgb) return false;
	if (a.useMaterialAlpha != b.useMaterialAlpha) return false;
	if (a.useMaterialRgb1 != b.useMaterialRgb1) return false;
	if (a.fog != b.fog) return false;
	if (std::memcmp(a.swapTable, b.swapTable, sizeof(a.swapTable)) != 0) return false;
	// Only the stages in use take part: whatever sits in the unused tail must
	// never split one configuration across two cache entries.
	const size_t used = size_t(a.numStages) * sizeof(PcTevStageKey);
	return std::memcmp(a.stages, b.stages, used) == 0;
}

uint64_t pc_tev_hash_key(const PcTevShaderKey& key)
{
	uint64_t hash = 1469598103934665603ull;
	auto mix = [&hash](const void* data, size_t size) {
		const uint8_t* bytes = static_cast<const uint8_t*>(data);
		for (size_t i = 0; i < size; ++i) {
			hash ^= bytes[i];
			hash *= 1099511628211ull;
		}
	};
	mix(&key.numStages, sizeof(key.numStages));
	mix(&key.alphaComp0, sizeof(key.alphaComp0));
	mix(&key.alphaComp1, sizeof(key.alphaComp1));
	mix(&key.alphaTestOp, sizeof(key.alphaTestOp));
	mix(&key.useMaterialRgb, sizeof(key.useMaterialRgb));
	mix(&key.useMaterialAlpha, sizeof(key.useMaterialAlpha));
	mix(&key.useMaterialRgb1, sizeof(key.useMaterialRgb1));
	mix(&key.fog, sizeof(key.fog));
	mix(key.swapTable, sizeof(key.swapTable));
	mix(key.stages, size_t(key.numStages) * sizeof(PcTevStageKey));
	return hash;
}

bool pc_tev_alpha_test_always_passes(const PcTevShaderKey& key)
{
	if (!compare_is_static(key.alphaComp0) || !compare_is_static(key.alphaComp1)) return false;
	return combine_static(key.alphaTestOp,
	                      key.alphaComp0 == CMP_ALWAYS,
	                      key.alphaComp1 == CMP_ALWAYS);
}

std::string pc_tev_build_fragment_source(const PcTevShaderKey& key)
{
	const int stageCount = key.numStages < 1 ? 1 : (key.numStages > 16 ? 16 : key.numStages);

	bool usesSampler[8] = {};
	bool usesChannel1 = false;
	for (int i = 0; i < stageCount; ++i) {
		const PcTevStageKey& stage = key.stages[i];
		if (stage.texMap >= 0 && stage.texMap < 8) usesSampler[stage.texMap] = true;
		if (stage.rasChannel == 1) usesChannel1 = true;
	}

	std::string out;
	out.reserve(4096);
	out += kGlslVersion;
	out += kGlslPrecision;
	out += "in vec3 vLit0;\n";
	if (usesChannel1) out += "in vec3 vLit1;\n";
	out += "in vec4 vColor;\n";
	out += "in vec2 vTexCoord0;\n";
	out += "in vec2 vTexCoord1;\n";
	out += "in vec2 vTexCoord2;\n";
	out += "in vec2 vTexCoord3;\n";
	out += "out vec4 fragColor;\n";

	char line[256];
	// Only the samplers a stage actually reads are declared. The ubershader
	// declared all eight and branched between them for every stage.
	for (int i = 0; i < 8; ++i) {
		if (!usesSampler[i]) continue;
		snprintf(line, sizeof(line), "uniform sampler2D uTex%d;\n", i);
		out += line;
	}
	out += "uniform vec4 uMaterialColor;\n";
	if (usesChannel1) out += "uniform vec4 uMaterialColor1;\n";
	if (key.fog) {
		out += "uniform vec4 uFogParams;\n";   // start, end, near, far
		out += "uniform vec4 uFogColour;\n";
	}
	out += "uniform vec4 uTevPrev;\n";
	out += "uniform vec4 uTevReg0;\n";
	out += "uniform vec4 uTevReg1;\n";
	out += "uniform vec4 uTevReg2;\n";
	snprintf(line, sizeof(line), "uniform vec4 uTevKonst[%d];\n", stageCount);
	out += line;
	const bool needsRef0 = !compare_is_static(key.alphaComp0);
	const bool needsRef1 = !compare_is_static(key.alphaComp1);
	if (needsRef0) out += "uniform float uAlphaRef0;\n";
	if (needsRef1) out += "uniform float uAlphaRef1;\n";

	out += "void main() {\n";

	// Raster channels. The material-source selection is a compile-time choice
	// here; it was a uniform branch per pixel in the ubershader.
	snprintf(line, sizeof(line), "\tvec4 base0 = vec4(%s, %s);\n",
	         key.useMaterialRgb ? "uMaterialColor.rgb" : "vColor.rgb",
	         key.useMaterialAlpha ? "uMaterialColor.a" : "vColor.a");
	out += line;
	out += "\tvec4 rast0 = (vLit0.x < -0.5) ? base0 : vec4(clamp(base0.rgb * vLit0, 0.0, 1.0), base0.a);\n";
	if (usesChannel1) {
		snprintf(line, sizeof(line), "\tvec4 base1 = vec4(%s, base0.a);\n",
		         key.useMaterialRgb1 ? "uMaterialColor1.rgb" : "vColor.rgb");
		out += line;
		out += "\tvec4 rast1 = (vLit1.x < -0.5) ? base1 : vec4(clamp(base1.rgb * vLit1, 0.0, 1.0), base1.a);\n";
	}

	out += "\tvec4 prev = uTevPrev;\n";
	out += "\tvec4 c0 = uTevReg0;\n";
	out += "\tvec4 c1 = uTevReg1;\n";
	out += "\tvec4 c2 = uTevReg2;\n";

	for (int i = 0; i < stageCount; ++i) emit_stage(out, key, i);

	// Alpha test. When both functions are constant the outcome is decided
	// here, and the common always-pass case leaves no discard in the shader
	// at all, which lets the driver keep early depth rejection.
	if (compare_is_static(key.alphaComp0) && compare_is_static(key.alphaComp1)) {
		const bool passes = combine_static(key.alphaTestOp,
		                                   key.alphaComp0 == CMP_ALWAYS,
		                                   key.alphaComp1 == CMP_ALWAYS);
		if (!passes) out += "\tdiscard;\n";
	} else {
		std::string test0 = compare_expr(key.alphaComp0, "prev.a", "uAlphaRef0");
		std::string test1 = compare_expr(key.alphaComp1, "prev.a", "uAlphaRef1");
		if (test0.empty()) test0 = (key.alphaComp0 == CMP_ALWAYS) ? "true" : "false";
		if (test1.empty()) test1 = (key.alphaComp1 == CMP_ALWAYS) ? "true" : "false";
		const char* combiner = key.alphaTestOp == 0 ? "&&"
		                     : key.alphaTestOp == 1 ? "||"
		                     : key.alphaTestOp == 2 ? "!=" : "==";
		out += "\tif (!(" + test0 + " " + combiner + " " + test1 + ")) discard;\n";
	}

	if (key.fog) {
		// The GameCube's fog unit ran after the TEV stages and before the
		// blend, on colour only -- alpha is left alone so a fogged transparent
		// surface stays as transparent as it was.
		//
		// uFogParams is (start, end, near, far). gl_FragCoord.z is the window
		// depth, which is not linear, so it is turned back into a distance
		// along the view axis before being measured against start and end.
		//
		// The reconstruction is NOT the usual OpenGL one, because the
		// projection is not an OpenGL projection. C_MTXPerspective builds the
		// GameCube form,
		//     m[2][2] = -n/(f-n)   m[2][3] = -fn/(f-n)   m[3][2] = -1
		// which puts the near plane at ndc -1 and the far plane at ndc 0,
		// where OpenGL would put it at +1. Only half the depth range is used.
		// Inverting that matrix gives
		//     distance = n*f / (n - ndc*(f - n))
		// which checks out at both ends: ndc -1 yields n, ndc 0 yields f.
		//
		// Using the OpenGL formula here is not subtly wrong, it is useless:
		// with a 1..15000 view it reports about two units at the far plane, so
		// the whole world measures as touching the camera and no fragment ever
		// reaches the fog. That is what this looked like before -- fog that was
		// switched on, fed correct values, and invisible.
		out += "\tfloat fogNdc = gl_FragCoord.z * 2.0 - 1.0;\n";
		out += "\tfloat fogNear = uFogParams.z;\n";
		out += "\tfloat fogFar = uFogParams.w;\n";
		out += "\tfloat fogDenom = fogNear - fogNdc * (fogFar - fogNear);\n";
		out += "\tfloat fogZ = (abs(fogDenom) < 1e-6) ? fogFar : (fogNear * fogFar) / fogDenom;\n";
		out += "\tfloat fogSpan = uFogParams.y - uFogParams.x;\n";
		out += "\tfloat fogAmount = (abs(fogSpan) < 1e-6) ? 0.0 : clamp((fogZ - uFogParams.x) / fogSpan, 0.0, 1.0);\n";
		out += "\tprev.rgb = mix(prev.rgb, uFogColour.rgb, fogAmount);\n";
	}

	out += "\tfragColor = prev;\n";
	out += "}\n";
	return out;
}
