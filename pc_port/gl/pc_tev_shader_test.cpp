#include "pc_tev_shader.h"

#include <cstdio>
#include <cctype>
#include <cstring>
#include <string>

namespace {

int gFailures = 0;

void check(bool condition, const char* what)
{
	if (!condition) {
		printf("FAIL: %s\n", what);
		++gFailures;
	}
}

bool contains(const std::string& haystack, const char* needle)
{
	return haystack.find(needle) != std::string::npos;
}

int count_of(const std::string& haystack, const char* needle)
{
	int total = 0;
	size_t at = 0;
	const size_t length = strlen(needle);
	while ((at = haystack.find(needle, at)) != std::string::npos) {
		++total;
		at += length;
	}
	return total;
}

// A single modulate stage: texture 0 * raster, the most common material.
PcTevShaderKey modulate_key()
{
	PcTevShaderKey key;
	key.numStages = 1;
	PcTevStageKey& stage = key.stages[0];
	stage.colorIn[0] = 15; // ZERO
	stage.colorIn[1] = 8;  // TEXC
	stage.colorIn[2] = 10; // RASC
	stage.colorIn[3] = 15; // ZERO
	stage.alphaIn[0] = 7;  // ZERO
	stage.alphaIn[1] = 4;  // TEXA
	stage.alphaIn[2] = 5;  // RASA
	stage.alphaIn[3] = 7;  // ZERO
	stage.texMap = 0;
	stage.texCoord = 0;
	stage.rasChannel = 0;
	return key;
}

void test_determinism()
{
	const PcTevShaderKey a = modulate_key();
	const PcTevShaderKey b = modulate_key();
	check(a == b, "identical keys compare equal");
	check(pc_tev_hash_key(a) == pc_tev_hash_key(b), "identical keys hash equally");
	check(pc_tev_build_fragment_source(a) == pc_tev_build_fragment_source(b),
	      "generation is deterministic");
}

// The cache is keyed on the configuration, so leftover data in stages beyond
// numStages must never split one material into two programs.
void test_unused_stage_tail_is_ignored()
{
	PcTevShaderKey a = modulate_key();
	PcTevShaderKey b = modulate_key();
	b.stages[1].colorIn[0] = 3;
	b.stages[1].texMap = 5;
	b.stages[7].alphaOp = 9;
	check(a == b, "unused stage tail does not affect equality");
	check(pc_tev_hash_key(a) == pc_tev_hash_key(b), "unused stage tail does not affect the hash");
	check(pc_tev_build_fragment_source(a) == pc_tev_build_fragment_source(b),
	      "unused stage tail does not affect generated source");
}

// The whole point of specialising: nothing is left for the GPU to interpret.
void test_no_runtime_interpretation()
{
	const std::string source = pc_tev_build_fragment_source(modulate_key());
	// El único bucle permitido es el de luces de la iluminación GX compartida.
	std::string body = source;
	const size_t lightsAt = body.find("vec3 gxDoLights(");
	if (lightsAt != std::string::npos) {
		const size_t lightsEnd = body.find("void main()", lightsAt);
		body.erase(lightsAt, lightsEnd == std::string::npos ? std::string::npos : lightsEnd - lightsAt);
	}
	check(!contains(body, "for ("), "no loop over stages");
	check(!contains(source, "uNumStages"), "stage count is not a uniform");
	check(!contains(source, "uTevCSel"), "operand selectors are not uniforms");
	check(!contains(source, "uTevCOps"), "combiner ops are not uniforms");
	check(!contains(source, "uFastPath"), "no fast-path branch remains");
	check(!contains(source, "resolveTex"), "no sampler dispatch helper");
	check(!contains(source, "resolveC"), "no operand dispatch helper");
}

void test_only_referenced_samplers_are_declared()
{
	PcTevShaderKey key = modulate_key();
	key.stages[0].texMap = 2;
	key.stages[0].texCoord = 1;
	const std::string source = pc_tev_build_fragment_source(key);
	check(contains(source, "uniform sampler2D uTex2;"), "the sampler in use is declared");
	check(!contains(source, "uniform sampler2D uTex0;"), "unused sampler 0 is absent");
	check(!contains(source, "uniform sampler2D uTex7;"), "unused sampler 7 is absent");
	check(contains(source, "texture(uTex2, vTexCoord1)"), "the stage samples its own map and coord");
	check(count_of(source, "texture(") == 1, "exactly one fetch for a one-stage material");
}

void test_stage_without_texture_has_no_fetch()
{
	PcTevShaderKey key = modulate_key();
	key.stages[0].texMap = -1;
	const std::string source = pc_tev_build_fragment_source(key);
	check(count_of(source, "texture(") == 0, "an untextured stage samples nothing");
	check(!contains(source, "uniform sampler2D"), "an untextured stage declares no sampler");
}

void test_channel1_is_omitted_when_unused()
{
	const std::string source = pc_tev_build_fragment_source(modulate_key());
	check(!contains(source, "vLit1"), "channel 1 varying is absent when unused");
	check(!contains(source, "uMaterialColor1"), "channel 1 material colour is absent when unused");

	PcTevShaderKey key = modulate_key();
	key.stages[0].rasChannel = 1;
	const std::string withChannel1 = pc_tev_build_fragment_source(key);
	check(contains(withChannel1, "vLit1"), "channel 1 varying appears when a stage selects it");
	check(contains(withChannel1, "rast1"), "channel 1 raster is computed when selected");
}

// An always-passing alpha test must leave no discard, so early depth rejection
// stays available to the driver.
void test_alpha_test_elimination()
{
	PcTevShaderKey key = modulate_key();
	key.alphaComp0 = 7; // ALWAYS
	key.alphaComp1 = 7; // ALWAYS
	key.alphaTestOp = 0; // AND
	check(pc_tev_alpha_test_always_passes(key), "always/always/AND always passes");
	const std::string source = pc_tev_build_fragment_source(key);
	check(!contains(source, "discard"), "no discard when the test cannot reject");
	check(!contains(source, "uAlphaRef"), "no alpha reference uniform when unused");

	// ALWAYS xor ALWAYS is statically false: every fragment is rejected.
	key.alphaTestOp = 2;
	check(!pc_tev_alpha_test_always_passes(key), "always/always/XOR never passes");
	check(contains(pc_tev_build_fragment_source(key), "discard"),
	      "a statically failing test discards");

	key.alphaComp0 = 4; // GREATER
	key.alphaTestOp = 0;
	const std::string tested = pc_tev_build_fragment_source(key);
	check(!pc_tev_alpha_test_always_passes(key), "a real comparison may reject");
	check(contains(tested, "discard"), "a real comparison emits a discard");
	check(contains(tested, "uAlphaRef0"), "a real comparison declares its reference");
	check(!contains(tested, "uAlphaRef1"), "the always-passing half needs no reference");
}

void test_swap_tables()
{
	PcTevShaderKey key = modulate_key();
	const std::string identity = pc_tev_build_fragment_source(key);
	check(!contains(identity, "tex = tex."), "an identity swap emits nothing");

	// Table 1 rewritten to broadcast red, as GX_TEV_SWAP1 style tables do.
	key.swapTable[1][0] = 0;
	key.swapTable[1][1] = 0;
	key.swapTable[1][2] = 0;
	key.swapTable[1][3] = 3;
	key.stages[0].texSwapSel = 1;
	const std::string swapped = pc_tev_build_fragment_source(key);
	check(contains(swapped, "tex = tex.rrra;"), "a real swap becomes a swizzle");
	check(identity != swapped, "changing a swap table changes the program");
}

void test_stage_count_is_honoured()
{
	PcTevShaderKey key = modulate_key();
	key.numStages = 3;
	key.stages[1] = key.stages[0];
	key.stages[2] = key.stages[0];
	key.stages[1].texMap = 1;
	key.stages[2].texMap = -1;
	const std::string source = pc_tev_build_fragment_source(key);
	check(count_of(source, "// stage ") == 3, "every stage is unrolled");
	check(count_of(source, "texture(") == 2, "only textured stages fetch");
	check(pc_tev_hash_key(key) != pc_tev_hash_key(modulate_key()),
	      "a different stage count hashes differently");
}

void test_distinct_configurations_differ()
{
	const PcTevShaderKey base = modulate_key();
	PcTevShaderKey other = base;
	other.stages[0].colorOp = 1; // subtract
	check(!(base == other), "a different combiner op is a different key");
	check(pc_tev_build_fragment_source(base) != pc_tev_build_fragment_source(other),
	      "a different combiner op is a different program");
	check(contains(pc_tev_build_fragment_source(other), "- cMix"), "subtract is generated");
	check(contains(pc_tev_build_fragment_source(base), "+ cMix"), "add is generated");
}

void test_output_registers_are_independent()
{
	PcTevShaderKey key = modulate_key();
	key.stages[0].colorOutReg = 1; // REG0
	key.stages[0].alphaOutReg = 0; // PREV
	const std::string source = pc_tev_build_fragment_source(key);
	check(contains(source, "c0.rgb = cResult;"), "colour writes its own register");
	check(contains(source, "prev.a = aResult;"), "alpha writes its own register");
}

// The wiring feeds specialised programs through the same uniform upload path
// as the ubershader, relying on absent uniforms resolving to location -1 and
// their writes being ignored. That only holds while every generated uniform
// name also exists in the ubershader, so guard it here.
void test_uniform_names_are_a_subset_of_the_ubershader()
{
	static const char* kUbershaderUniforms[] = {
		"uTex0", "uTex1", "uTex2", "uTex3", "uTex4", "uTex5", "uTex6", "uTex7",
		"uMaterialColor", "uMaterialColor1",
		"uTevPrev", "uTevReg0", "uTevReg1", "uTevReg2",
		"uTevKonst", "uAlphaRef0", "uAlphaRef1",
		// Iluminación GX compartida (pc_gx_lighting_glsl.h) y su selector.
		"uNumLights", "uLightPos", "uLightColor", "uLightK", "uAmbColor",
		"uChan0En", "uChan1En", "uChan0AttnFn", "uChan1AttnFn",
		"uNumLights1", "uLightPos1", "uLightColor1", "uLightK1", "uAmbColor1",
		"uSpecHalf1", "uSpecAttn1", "uPerPixel", "uOutTint",
	};

	PcTevShaderKey key = modulate_key();
	key.numStages = 2;
	key.stages[1] = key.stages[0];
	key.stages[1].texMap = 3;
	key.stages[1].rasChannel = 1;
	key.alphaComp0 = 4;
	key.alphaComp1 = 1;
	key.useMaterialRgb = 1;
	key.useMaterialRgb1 = 1;
	const std::string source = pc_tev_build_fragment_source(key);

	size_t at = 0;
	int checked = 0;
	while ((at = source.find("uniform ", at)) != std::string::npos) {
		at += strlen("uniform ");
		const size_t typeEnd = source.find(' ', at);
		if (typeEnd == std::string::npos) break;
		const size_t nameStart = typeEnd + 1;
		size_t nameEnd = nameStart;
		while (nameEnd < source.size()
		       && (isalnum(static_cast<unsigned char>(source[nameEnd])) || source[nameEnd] == '_')) {
			++nameEnd;
		}
		const std::string name = source.substr(nameStart, nameEnd - nameStart);

		bool known = false;
		for (const char* candidate : kUbershaderUniforms) {
			if (name == candidate) { known = true; break; }
		}
		if (!known) printf("FAIL: generated uniform '%s' is unknown to the ubershader\n", name.c_str());
		check(known, "every generated uniform exists in the ubershader");
		++checked;
	}
	check(checked >= 6, "the sample configuration declares several uniforms");
}


// The projection is the GameCube one, not OpenGL's: C_MTXPerspective puts the
// far plane at ndc 0 rather than +1, so only half the depth range is used.
// Reconstructing view distance with the usual OpenGL formula is not slightly
// wrong here, it is useless -- across a 1..15000 view it reports about two
// units at the far plane, so nothing ever reaches the fog. That bug shipped
// once already; this pins the correct form down.
void test_fog()
{
	PcTevShaderKey key = modulate_key();

	key.fog = 0;
	const std::string plain = pc_tev_build_fragment_source(key);
	check(!contains(plain, "uFogParams"), "no fog means no fog uniform");
	check(!contains(plain, "fogAmount"), "no fog means no fog maths");

	key.fog = 1;
	const std::string fogged = pc_tev_build_fragment_source(key);
	check(contains(fogged, "uniform vec4 uFogParams;"), "fog declares its parameters");
	check(contains(fogged, "uniform vec4 uFogColour;"), "fog declares its colour");

	// The denominator is the whole difference between the two conventions.
	check(contains(fogged, "fogNear - fogNdc * (fogFar - fogNear)"),
	      "fog linearises with the GameCube depth range");
	check(!contains(fogged, "fogFar + fogNear - fogNdc"),
	      "fog does not use the OpenGL depth range");

	// Colour only. Fogging alpha would make a transparent surface solid as it
	// receded, which is not what the hardware did.
	check(contains(fogged, "prev.rgb = mix(prev.rgb, uFogColour.rgb, fogAmount);"),
	      "fog affects colour and leaves alpha alone");

	// Fog is a property of the draw, so two draws that differ only in it must
	// not share a program.
	PcTevShaderKey a = modulate_key();
	PcTevShaderKey b = modulate_key();
	b.fog = 1;
	check(a != b, "fog is part of the shader key");
	check(pc_tev_hash_key(a) != pc_tev_hash_key(b), "fog changes the key hash");
}

} // namespace

int main()
{
	{
		const std::string src = pc_tev_build_fragment_source(modulate_key());
#if PIKI_USE_GLES
		check(src.rfind("#version 300 es\n", 0) == 0, "GLES emits GLSL ES 3.00");
		check(contains(src, "precision highp float;"), "GLES declares float precision");
		check(contains(src, "precision highp int;"), "GLES declares integer precision");
#else
		check(src.rfind("#version 140\n", 0) == 0, "desktop emits GLSL 1.40");
		check(!contains(src, "precision highp"), "desktop emits no ES precision qualifiers");
#endif
	}
	test_uniform_names_are_a_subset_of_the_ubershader();
	test_determinism();
	test_unused_stage_tail_is_ignored();
	test_no_runtime_interpretation();
	test_only_referenced_samplers_are_declared();
	test_stage_without_texture_has_no_fetch();
	test_channel1_is_omitted_when_unused();
	test_alpha_test_elimination();
	test_swap_tables();
	test_stage_count_is_honoured();
	test_distinct_configurations_differ();
	test_output_registers_are_independent();
	test_fog();

	if (gFailures != 0) {
		printf("pc_tev_shader_test: %d failure(s)\n", gFailures);
		return 1;
	}
	printf("pc_tev_shader_test: all checks passed\n");
	return 0;
}
