// The post-process pass costs a fullscreen draw every frame, so the decision
// of whether to run it at all matters as much as what it does. Both that
// decision and the generated shader are checked here, without a GL context.
#include "pc_postprocess.h"

#include <cstdio>
#include <string>

namespace {
int failures = 0;

void check(bool ok, const char* what)
{
	if (!ok) {
		std::printf("FAIL: %s\n", what);
		failures++;
	}
}

bool contains(const std::string& hay, const char* needle)
{
	return hay.find(needle) != std::string::npos;
}
}

int main()
{
	// Nothing on: the scene must blit straight through, untouched.
	{
		PcPostEffects fx;
		check(!pc_post_any_enabled(fx), "a default effect set runs no pass");
	}

	// On but neutral is the same picture. Spending a fullscreen draw to
	// reproduce the input exactly is the one case that is pure waste.
	{
		PcPostEffects fx;
		fx.colourGrading = true;
		check(!pc_post_any_enabled(fx), "neutral grading runs no pass");
		fx.gamma = 1.2f;
		check(pc_post_any_enabled(fx), "a non-neutral gamma runs the pass");
		fx.gamma = 1.0f;
		fx.brightness = 0.1f;
		check(pc_post_any_enabled(fx), "a non-neutral brightness runs the pass");
		fx.brightness = 0.0f;
		fx.saturation = 0.0f;
		check(pc_post_any_enabled(fx), "a non-neutral saturation runs the pass");
	}

	// Off overrides its own values: turning grading off must not leave the
	// pass running because a slider is still parked somewhere non-neutral.
	{
		PcPostEffects fx;
		fx.colourGrading = false;
		fx.gamma = 1.8f;
		fx.saturation = 0.0f;
		check(!pc_post_any_enabled(fx), "switching grading off stops the pass");
	}

	// The shader carries only what is switched on. An effect nobody asked for
	// must not cost instructions, which is the whole reason this is generated
	// rather than written once with uniforms deciding everything.
	{
		PcPostEffects off;
		const std::string bare = pc_post_build_fragment_shader(off);
		check(contains(bare, "uniform sampler2D uScene"), "the scene is always sampled");
		check(!contains(bare, "uGamma"), "grading uniforms are absent when it is off");
		check(!contains(bare, "uDepth"), "depth is not sampled when nothing needs it");

		PcPostEffects grade;
		grade.colourGrading = true;
		const std::string graded = pc_post_build_fragment_shader(grade);
		check(contains(graded, "uGamma"), "grading declares its gamma uniform");
		check(contains(graded, "uBrightness"), "grading declares its brightness uniform");
		check(contains(graded, "uSaturation"), "grading declares its saturation uniform");
		check(graded.size() > bare.size(), "enabling an effect adds to the shader");
	}

	// Both stages must be complete GLSL, since a failure to compile shows up as
	// a black screen rather than as an error the player can read.
	{
		PcPostEffects fx;
		fx.colourGrading = true;
		const std::string frag = pc_post_build_fragment_shader(fx);
		const std::string vert = pc_post_vertex_shader();
		for (const std::string* src : { &vert, &frag }) {
			check(src->rfind("#version", 0) == 0, "a stage begins with its version");
			check(contains(*src, "void main"), "a stage has an entry point");
			check(contains(*src, "}"), "a stage is closed");
		}
		check(contains(vert, "gl_VertexID"), "the pass builds its own geometry");
		check(contains(frag, "oColour"), "the fragment stage writes its output");
#if PIKI_USE_GLES
		check(vert.rfind("#version 300 es\n", 0) == 0, "GLES vertex stage uses GLSL ES 3.00");
		check(frag.rfind("#version 300 es\n", 0) == 0, "GLES fragment stage uses GLSL ES 3.00");
		check(contains(vert, "precision highp float;"), "GLES vertex stage declares precision");
		check(contains(frag, "precision highp int;"), "GLES fragment stage declares integer precision");
#else
		check(vert.rfind("#version 330 core\n", 0) == 0, "desktop vertex stage uses GLSL 3.30");
		check(frag.rfind("#version 330 core\n", 0) == 0, "desktop fragment stage uses GLSL 3.30");
		check(!contains(frag, "precision highp"), "desktop stage emits no ES precision qualifiers");
#endif
	}

	// Depth is not always available -- the port falls back to a renderbuffer
	// where a driver will not give it a depth texture -- so anything claiming
	// to need depth has to be answerable before the pass is set up.
	{
		PcPostEffects fx;
		fx.colourGrading = true;
		check(!pc_post_needs_depth(fx), "colour grading does not need depth");
	}

	// Equality drives shader recompilation. If it missed a field the pass would
	// keep running a shader built for different settings, and the menu would
	// look broken rather than the code.
	{
		PcPostEffects a, b;
		check(a == b, "identical sets compare equal");
		b.gamma = 1.5f;
		check(a != b, "gamma is part of the comparison");
		b = a; b.brightness = 0.2f;
		check(a != b, "brightness is part of the comparison");
		b = a; b.saturation = 0.5f;
		check(a != b, "saturation is part of the comparison");
		b = a; b.colourGrading = true;
		check(a != b, "the on/off switch is part of the comparison");
	}

	// Antialiasing stands on its own: it has no neutral setting, so switching
	// it on must run the pass even with grading off and everything neutral.
	{
		PcPostEffects fx;
		fx.fxaa = true;
		check(pc_post_any_enabled(fx), "FXAA alone runs the pass");
		check(!pc_post_needs_depth(fx), "FXAA does not need depth");

		const std::string src = pc_post_build_fragment_shader(fx);
		check(contains(src, "uniform vec4 uTexelSize"), "FXAA declares its texel size");
		check(contains(src, "fxaaFilter"), "FXAA emits its filter");
		// The early return is what keeps the cost down on the flat majority of
		// a frame. Losing it would be invisible in a screenshot and expensive.
		check(contains(src, "return rgbM;"), "FXAA keeps its flat-area early out");
		check(!contains(src, "uGamma"), "FXAA alone brings no grading uniforms");
	}

	// Off means the filter is not in the shader at all, not that it is compiled
	// in and skipped by a branch.
	{
		PcPostEffects fx;
		fx.colourGrading = true;
		fx.gamma = 1.2f;
		const std::string src = pc_post_build_fragment_shader(fx);
		check(!contains(src, "fxaaFilter"), "grading alone brings no FXAA code");
		check(!contains(src, "uTexelSize"), "grading alone brings no texel size");
	}

	// Both together: the scene is filtered first and the tone curve applied to
	// the result, so grading must not read the raw texture when FXAA is on.
	{
		PcPostEffects fx;
		fx.fxaa = true;
		fx.colourGrading = true;
		const std::string src = pc_post_build_fragment_shader(fx);
		check(contains(src, "vec3 c = fxaaFilter(vUV, uTexelSize.xy);"),
		      "with FXAA on, grading works from the filtered colour");
		check(!contains(src, "vec3 c = texture(uScene, vUV).rgb;"),
		      "the unfiltered fetch is gone when FXAA is on");
	}

	// Recompilation is driven by equality, so the new field has to be in it.
	{
		PcPostEffects a, b;
		b.fxaa = true;
		check(a != b, "antialiasing is part of the comparison");
	}

	// Bloom is the one effect that costs three extra passes, so "on" alone is
	// not enough to run it -- it has to actually contribute something.
	{
		PcPostEffects fx;
		fx.bloom = true;
		fx.bloomIntensity = 0.0f;
		check(!pc_post_bloom_active(fx), "bloom at zero intensity is not active");
		check(!pc_post_any_enabled(fx), "bloom at zero intensity runs no pass");
		fx.bloomIntensity = 0.6f;
		check(pc_post_bloom_active(fx), "bloom with intensity is active");
		check(pc_post_any_enabled(fx), "active bloom runs the pass");
		fx.bloom = false;
		check(!pc_post_bloom_active(fx), "switching bloom off overrides its intensity");
	}

	// The composite must be added and not mixed: bloom is light that scattered
	// on the way to the lens, so it arrives on top of the image rather than
	// replacing part of it.
	{
		PcPostEffects fx;
		fx.bloom = true;
		fx.bloomIntensity = 0.6f;
		const std::string src = pc_post_build_fragment_shader(fx);
		check(contains(src, "uniform sampler2D uBloom"), "bloom declares its sampler");
		check(contains(src, "c += texture(uBloom, vUV).rgb * uBloomIntensity;"),
		      "bloom is added, not mixed");

		PcPostEffects off;
		const std::string bare = pc_post_build_fragment_shader(off);
		check(!contains(bare, "uBloom"), "no bloom means no bloom sampler");
	}

	// Order matters: grading is the tone curve applied to the light reaching
	// the sensor, and bloom is part of that light, so it must come first.
	{
		PcPostEffects fx;
		fx.bloom = true;
		fx.bloomIntensity = 0.6f;
		fx.colourGrading = true;
		fx.gamma = 1.2f;
		const std::string src = pc_post_build_fragment_shader(fx);
		const size_t bloomAt = src.find("uBloomIntensity");
		const size_t gradeAt = src.find("1.0 / uGamma");
		check(bloomAt != std::string::npos && gradeAt != std::string::npos,
		      "both effects are present");
		check(bloomAt < gradeAt, "bloom is composited before grading");
	}

	// The two helper stages are separate programs and must stand alone.
	{
		const std::string bright = pc_post_build_brightpass_shader();
		const std::string blur   = pc_post_build_blur_shader();
		for (const std::string* src : { &bright, &blur }) {
			check(src->rfind("#version", 0) == 0, "a helper stage begins with its version");
			check(contains(*src, "void main"), "a helper stage has an entry point");
			check(contains(*src, "oColour"), "a helper stage writes its output");
		}
		check(contains(bright, "uThreshold"), "the bright pass takes a threshold");
		// A hard cut makes bloom pop in and out as something drifts across the
		// threshold; weighting by how far past it went keeps that smooth.
		check(contains(bright, "excess / luma"), "the bright pass fades in rather than cutting");
		check(contains(blur, "uBlurStep"), "the blur takes a direction");
		check(contains(blur, "uSource"), "the blur reads its own source, not the scene");
	}

	// Recompilation is driven by equality, so every bloom field has to be in it.
	{
		PcPostEffects a, b;
		b.bloom = true;
		check(a != b, "the bloom switch is part of the comparison");
		b = a; b.bloomIntensity = 0.5f;
		check(a != b, "bloom intensity is part of the comparison");
		b = a; b.bloomThreshold = 0.5f;
		check(a != b, "bloom threshold is part of the comparison");
	}

	// Occlusion is the first effect that cannot run without a depth texture,
	// and the port falls back to a renderbuffer on drivers that refuse one.
	// Saying so has to be answerable before the pass is set up.
	{
		PcPostEffects fx;
		check(!pc_post_needs_depth(fx), "an empty effect set needs no depth");
		fx.ssao = true;
		fx.ssaoIntensity = 0.8f;
		check(pc_post_ssao_active(fx), "occlusion with intensity is active");
		check(pc_post_needs_depth(fx), "occlusion needs depth");
		check(pc_post_any_enabled(fx), "active occlusion runs the pass");

		fx.ssaoIntensity = 0.0f;
		check(!pc_post_ssao_active(fx), "occlusion at zero intensity is not active");
		check(!pc_post_needs_depth(fx), "inactive occlusion does not demand depth");
		fx.ssaoIntensity = 0.8f;
		fx.ssaoRadius = 0.0f;
		check(!pc_post_ssao_active(fx), "occlusion with no radius is not active");
	}

	// Multiplied, and before bloom: occlusion is ambient light that never
	// arrived, so it scales what the surface received; bloom is light that did
	// arrive and then scattered, so it lands on top of the result.
	{
		PcPostEffects fx;
		fx.ssao = true;
		fx.ssaoIntensity = 0.8f;
		fx.bloom = true;
		fx.bloomIntensity = 0.6f;
		const std::string src = pc_post_build_fragment_shader(fx);
		check(contains(src, "uniform sampler2D uAO"), "occlusion declares its sampler");
		check(contains(src, "c *= texture(uAO, vUV).r;"), "occlusion multiplies");
		const size_t aoAt = src.find("uAO, vUV");
		const size_t bloomAt = src.find("uBloom, vUV");
		check(aoAt != std::string::npos && bloomAt != std::string::npos, "both are present");
		check(aoAt < bloomAt, "occlusion is applied before bloom");

		PcPostEffects off;
		check(!contains(pc_post_build_fragment_shader(off), "uAO"),
		      "no occlusion means no occlusion sampler");
	}

	// The occlusion stage rebuilds position and normal from depth alone, and
	// the depth range is the GameCube's. The same mistake made the fog
	// invisible once; here it would make occlusion cover everything or nothing.
	{
		const std::string src = pc_post_build_ssao_shader();
		check(src.rfind("#version", 0) == 0, "the occlusion stage begins with its version");
		check(contains(src, "void main"), "the occlusion stage has an entry point");
		check(contains(src, "n - ndc * (f - n)"),
		      "occlusion linearises with the GameCube depth range");
		check(!contains(src, "f + n - ndc"),
		      "occlusion does not use the OpenGL depth range");
		check(contains(src, "uProjInfo"), "occlusion takes the projection terms");
		// Derivatives were the first attempt: faceted per quad, and in grass
		// every quad straddles a silhouette, which came out as noise. Taking
		// the nearer neighbour on each axis keeps the difference inside one
		// surface.
		check(!contains(src, "dFdx(P)"), "the normal does not come from raw derivatives");
		check(contains(src, "abs(Pr.z - P.z) < abs(P.z - Pl.z)"),
		      "the normal picks the nearer horizontal neighbour");
		check(contains(src, "abs(Pu.z - P.z) < abs(P.z - Pd.z)"),
		      "the normal picks the nearer vertical neighbour");
		check(contains(src, "uTexel"), "the normal taps need a texel size");
		// A sine hash clusters, and neighbouring pixels drawing similar angles
		// is what the blur cannot cancel -- it showed up as lines crawling
		// across the ground.
		check(!contains(src, "sin(dot(gl_FragCoord.xy"),
		      "the kernel rotation does not use a clustering sine hash");
		check(contains(src, "52.9829189"), "the kernel rotation uses interleaved gradient noise");
		// Without the flip the normal's sign follows the quad winding and half
		// the screen occludes backwards.
		check(contains(src, "if (N.z < 0.0) N = -N;"), "the normal is forced to face the camera");
		// Without the range check a depth discontinuity reads as an occluder
		// and every silhouette gets a black halo.
		check(contains(src, "smoothstep"), "occlusion range-checks its samples");
		check(contains(src, "kKernel"), "occlusion uses a baked sample kernel");
	}

	// Recompilation is driven by equality.
	{
		PcPostEffects a, b;
		b.ssao = true;
		check(a != b, "the occlusion switch is part of the comparison");
		b = a; b.ssaoIntensity = 0.5f;
		check(a != b, "occlusion intensity is part of the comparison");
		b = a; b.ssaoRadius = 10.0f;
		check(a != b, "occlusion radius is part of the comparison");
	}

	// Occlusion is denoised with a depth-aware blur, not the Gaussian bloom
	// uses. A plain average crosses silhouettes, which drags a blade of grass's
	// occlusion onto the ground behind it and shimmers as either one moves.
	{
		const std::string ao = pc_post_build_ao_blur_shader();
		const std::string plain = pc_post_build_blur_shader();
		check(ao.rfind("#version", 0) == 0, "the occlusion blur begins with its version");
		check(contains(ao, "uniform sampler2D uDepth"), "the occlusion blur reads depth");
		check(contains(ao, "depthWeight"), "the occlusion blur weights by depth");
		check(contains(ao, "n - ndc * (f - n)"),
		      "the occlusion blur linearises with the GameCube depth range");
		check(!contains(plain, "uDepth"), "the plain blur stays depth-unaware");
		// The kernel has to be at least as wide as the noise it cancels.
		check(contains(ao, "i = -3; i <= 3"), "the occlusion blur is wide enough for the noise");
	}

	// Depth of field.
	{
		PcPostEffects off;
		check(!pc_post_dof_active(off), "depth of field is off by default");

		PcPostEffects fx;
		fx.dof = true;
		check(!pc_post_dof_active(fx),
		      "switched on at zero strength is not active: it would cost three passes to reproduce the input");
		fx.dofStrength = 0.7f;
		check(pc_post_dof_active(fx), "strength above zero makes it active");
		fx.dofIterations = 0;
		check(!pc_post_dof_active(fx), "no blur passes means nothing to composite");
		fx.dofIterations = 2;

		check(pc_post_any_enabled(fx), "depth of field alone is worth a pass");
		// It samples depth, so it must be dropped when the driver refused a
		// depth texture -- the same rule that governs occlusion.
		check(pc_post_needs_depth(fx), "depth of field needs the depth buffer");

		const std::string src = pc_post_build_fragment_shader(fx);
		check(contains(src, "uniform sampler2D uDof"), "the composite declares the blurred sampler");
		check(contains(src, "uniform vec4 uDofFocus"), "the composite takes the focus");
		check(contains(src, "n - ndc * (f - n)"),
		      "depth of field linearises with the GameCube depth range");
		// The premultiplication is undone with a floor on the divisor. Without
		// it a fully sharp neighbourhood divides by zero, and mix() with a NaN
		// is a NaN however small the weight.
		check(contains(src, "max(blurred.a, 1e-4)"), "the composite guards the divide");

		const std::string bare = pc_post_build_fragment_shader(PcPostEffects());
		check(!contains(bare, "uDof"), "no depth of field means no sampler");
		check(!contains(bare, "cocAt"), "no depth of field means no coverage function");
	}

	// Order in the composite: the blurred texture holds the scene colour and
	// nothing else, so it has to be mixed in before the effects that add to
	// that colour. Mixing it after bloom would replace a pixel that had just
	// received its bloom with a version that never got any.
	{
		PcPostEffects fx;
		fx.dof = true; fx.dofStrength = 0.7f;
		fx.bloom = true; fx.bloomIntensity = 0.5f;
		fx.ssao = true; fx.ssaoIntensity = 0.8f;
		const std::string src = pc_post_build_fragment_shader(fx);
		const size_t dofAt   = src.find("farColour, coc");
		const size_t aoAt    = src.find("texture(uAO, vUV).r;");
		const size_t bloomAt = src.find("uBloom, vUV");
		check(dofAt != std::string::npos && aoAt != std::string::npos
		          && bloomAt != std::string::npos,
		      "all three effects reached the composite");
		check(dofAt < aoAt && dofAt < bloomAt, "depth of field composites before occlusion and bloom");
	}

	// The coverage pass premultiplies. This is the whole reason it exists
	// rather than blurring the scene directly: an in-focus captain must not
	// contribute his colour to the blur of the background behind him, or he
	// wears a halo of himself.
	{
		const std::string coc = pc_post_build_dof_coc_shader();
		check(coc.rfind("#version", 0) == 0, "the coverage pass begins with its version");
		check(contains(coc, "texture(uScene, vUV).rgb * coc"), "colour is weighted by coverage");
		check(contains(coc, "vec4(texture(uScene, vUV).rgb * coc, coc)"),
		      "coverage travels in alpha");

		// And the blur that follows has to carry that alpha. Bloom's Gaussian
		// ends with vec4(c, 1.0), which would throw the coverage away on the
		// first axis and leave the composite dividing by a constant.
		const std::string blur = pc_post_build_dof_blur_shader();
		check(contains(blur, "vec4 c = texture(uSource, vUV) * w0"),
		      "the depth-of-field blur filters four channels");
		check(contains(blur, "oColour = c;"), "the depth-of-field blur keeps alpha");
		check(contains(pc_post_build_blur_shader(), "vec4(c, 1.0)"),
		      "the bloom blur still discards alpha, which is why this one exists");
	}

	// Recompilation is driven by equality here too: every one of these changes
	// the generated shader or the passes around it.
	{
		PcPostEffects a, b;
		b = a; b.dof = true;
		check(a != b, "the depth-of-field switch is part of the comparison");
		b = a; b.dofStrength = 0.5f;
		check(a != b, "strength is part of the comparison");
		b = a; b.dofSharpFraction = 0.1f;
		check(a != b, "the sharp band is part of the comparison");
		b = a; b.dofFalloffFraction = 0.1f;
		check(a != b, "the falloff is part of the comparison");
		b = a; b.dofIterations = 3;
		check(a != b, "the blur pass count is part of the comparison");
	}

	// A depth the projection never wrote is not geometry.
	//
	// The GameCube projection puts the far plane at ndc 0, so it only ever
	// writes raw depths up to 0.5. Above that is the value the buffer was
	// cleared to. Run through the linearisation it yields a NEGATIVE distance,
	// not a large one, so every "is this the sky" test passes it through and it
	// gets treated as geometry against the lens. That turned the title screen
	// black the moment the pass started reading depth the interface had not
	// already overwritten.
	{
		const std::string ssao = pc_post_build_ssao_shader();
		check(contains(ssao, "if (raw > 0.5) return uProjInfo.w;"),
		      "an untouched depth reads as the far plane, not as a negative distance");

		PcPostEffects fx;
		fx.dof = true; fx.dofStrength = 0.7f;
		check(contains(pc_post_build_fragment_shader(fx), "if (raw > 0.5) return uProjInfo.w;"),
		      "depth of field gets the same guard, from the same shared helper");
	}

	// The reconstructed normal must not be able to become a NaN.
	//
	// Constant depth across a neighbourhood -- a cleared buffer, or a flat
	// surface square to the camera -- gives parallel differences, a zero cross
	// product, and normalize(vec3(0)) is a NaN. Comparisons against a NaN are
	// all false and clamp() of one is up to the driver; on the reference GPU it
	// came out zero, and the composite multiplies the scene by that.
	{
		const std::string ssao = pc_post_build_ssao_shader();
		check(!contains(ssao, "normalize(cross(dx, dy))"),
		      "the normal is not normalised without checking its length");
		check(contains(ssao, "float crossLen = length(cross_dxdy);"),
		      "the cross product's length is measured");
		// Written as !(x > eps) rather than (x <= eps) on purpose: a NaN fails
		// every comparison, so only the negated form catches one.
		check(contains(ssao, "if (!(crossLen > 1e-12)) { oColour = vec4(1.0); return; }"),
		      "a degenerate normal yields no occlusion instead of total occlusion");
	}

	if (failures == 0) std::printf("pc_postprocess_test: all checks passed\n");
	return failures == 0 ? 0 : 1;
}
