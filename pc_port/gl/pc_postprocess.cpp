#include "pc_postprocess.h"

#include <cmath>
#include <cstdio>

namespace {
#if PIKI_USE_GLES
const char* kGlslVersion = "#version 300 es\n";
const char* kGlslPrecision = "precision highp float;\nprecision highp int;\n";
#else
const char* kGlslVersion = "#version 330 core\n";
const char* kGlslPrecision = "";
#endif
}

bool pc_post_bloom_active(const PcPostEffects& fx)
{
	// Zero intensity is the same picture, and bloom is the one effect here
	// that costs three extra passes to produce it.
	return fx.bloom && fx.bloomIntensity > 0.0f;
}

bool pc_post_ssao_active(const PcPostEffects& fx)
{
	return fx.ssao && fx.ssaoIntensity > 0.0f && fx.ssaoRadius > 0.0f;
}

bool pc_post_dof_active(const PcPostEffects& fx)
{
	// Zero strength composites the sharp image over itself, at the price of
	// three extra passes to produce something identical to the input.
	return fx.dof && fx.dofStrength > 0.0f && fx.dofIterations > 0;
}

bool pc_post_any_enabled(const PcPostEffects& fx)
{
	if (fx.fxaa) return true;
	if (pc_post_ssao_active(fx)) return true;
	if (pc_post_dof_active(fx)) return true;
	if (pc_post_bloom_active(fx)) return true;
	if (!fx.colourGrading) return false;
	// Switched on but set to neutral values is the same picture, so skip the
	// pass instead of spending a fullscreen draw reproducing the input.
	return fx.gamma != 1.0f || fx.brightness != 0.0f || fx.saturation != 1.0f;
}

bool pc_post_needs_depth(const PcPostEffects& fx)
{
	// Ambient occlusion and depth of field. Both reconstruct view depth with
	// the GameCube range, and both have to be dropped outright when the driver
	// gave us a renderbuffer instead of a depth texture.
	return pc_post_ssao_active(fx) || pc_post_dof_active(fx);
}

// The GameCube depth range, in one place.
//
// C_MTXPerspective puts the far plane at ndc 0 rather than +1, so only half the
// depth range is used and OpenGL's linearisation formula is not slightly wrong
// here, it is useless: over a 1..15000 view it reports about two units at the
// far plane, and every pixel in the world measures as touching the camera. That
// left the fog invisible once already.
//
// Three effects now depend on getting it right -- fog, ambient occlusion and
// depth of field -- so it is emitted from here rather than written out again.
// Expects uProjInfo.zw to hold the near and far planes, and a sampler uDepth.
static std::string pc_post_view_depth_glsl()
{
	std::string src;
	src += "float viewDepth(vec2 uv) {\n";
	src += "    float raw = texture(uDepth, uv).r;\n";
	// The other half of the same trap. Because the far plane lands at ndc 0,
	// the projection only ever writes raw depths up to 0.5, and anything above
	// that is the value the buffer was cleared to: nothing was drawn there.
	// Fed through the formula it does not produce a large distance, it produces
	// a negative one -- about -1 over a 1..15000 view -- which passes every
	// "is this the sky" test and is then treated as geometry a millimetre from
	// the lens. That is what turned the title screen black once the pass
	// started reading depth that had not been overwritten by the interface.
	src += "    if (raw > 0.5) return uProjInfo.w;\n";
	src += "    float ndc = raw * 2.0 - 1.0;\n";
	src += "    float n = uProjInfo.z;\n";
	src += "    float f = uProjInfo.w;\n";
	src += "    float denom = n - ndc * (f - n);\n";
	src += "    return (abs(denom) < 1e-6) ? f : (n * f) / denom;\n";
	src += "}\n";
	return src;
}

// The circle of confusion, from the focus the game pushed in.
//
// uDofFocus is (focus distance, sharp fraction, falloff fraction, strength).
// The band and the falloff are fractions of the focus distance so that the
// look survives the camera moving between its near and far positions.
static std::string pc_post_coc_glsl()
{
	std::string src;
	src += "float cocAt(vec2 uv) {\n";
	src += "    float d = viewDepth(uv);\n";
	src += "    float sharp = uDofFocus.x * uDofFocus.y;\n";
	src += "    float falloff = max(uDofFocus.x * uDofFocus.z, 1e-3);\n";
	src += "    return clamp((abs(d - uDofFocus.x) - sharp) / falloff, 0.0, 1.0);\n";
	src += "}\n";
	return src;
}

std::string pc_post_build_ssao_shader()
{
	// There is no G-buffer here, so position and normal are both rebuilt from
	// the depth buffer. The normal comes from the screen-space derivatives of
	// the reconstructed position, which is faceted per 2x2 quad and wrong
	// across a depth discontinuity -- the range check below is what keeps those
	// edges from producing a black halo.
	const int kSamples = 12;

	std::string src;
	src += kGlslVersion; src += kGlslPrecision;
	src += "in vec2 vUV;\n";
	src += "out vec4 oColour;\n";
	src += "uniform sampler2D uDepth;\n";
	// x,y are the inverse projection scales, z,w the near and far planes.
	src += "uniform vec4 uProjInfo;\n";
	// x radius in world units, y intensity, z bias.
	src += "uniform vec4 uAOParams;\n";
	src += "uniform vec2 uTexel;\n";

	src += pc_post_view_depth_glsl();

	src += "vec3 viewPos(vec2 uv) {\n";
	src += "    float z = viewDepth(uv);\n";
	src += "    vec2 ndc = uv * 2.0 - 1.0;\n";
	src += "    return vec3(ndc * uProjInfo.xy * z, -z);\n";
	src += "}\n";

	// A spiral built at generation time. Baking the offsets keeps the loop free
	// of trigonometry and makes the pattern identical on every machine.
	src += "const vec2 kKernel[";
	src += std::to_string(kSamples);
	src += "] = vec2[](\n";
	for (int i = 0; i < kSamples; ++i) {
		const double golden = 2.399963229728653;
		const double angle  = golden * i;
		const double radius = std::sqrt((i + 0.5) / kSamples);
		char buf[128];
		std::snprintf(buf, sizeof(buf), "    vec2(%.6f, %.6f)%s\n",
		              std::cos(angle) * radius, std::sin(angle) * radius,
		              i + 1 == kSamples ? "" : ",");
		src += buf;
	}
	src += ");\n";

	src += "void main() {\n";
	src += "    vec3 P = viewPos(vUV);\n";
	// The sky sits on the far plane and has nothing in front of it to occlude.
	src += "    if (-P.z >= uProjInfo.w * 0.999) { oColour = vec4(1.0); return; }\n";
	// Derivatives were the first attempt and they are faceted per 2x2 quad, so
	// grass -- where every quad straddles a silhouette -- came out as noise.
	// Taking both neighbours on each axis and keeping whichever is nearer in
	// depth means the difference never crosses a discontinuity unless both
	// sides do.
	src += "    vec3 Pr = viewPos(vUV + vec2(uTexel.x, 0.0));\n";
	src += "    vec3 Pl = viewPos(vUV - vec2(uTexel.x, 0.0));\n";
	src += "    vec3 Pu = viewPos(vUV + vec2(0.0, uTexel.y));\n";
	src += "    vec3 Pd = viewPos(vUV - vec2(0.0, uTexel.y));\n";
	src += "    vec3 dx = (abs(Pr.z - P.z) < abs(P.z - Pl.z)) ? (Pr - P) : (P - Pl);\n";
	src += "    vec3 dy = (abs(Pu.z - P.z) < abs(P.z - Pd.z)) ? (Pu - P) : (P - Pd);\n";
	// A region of constant depth -- a cleared buffer, a flat wall exactly
	// facing the camera -- gives two parallel differences whose cross product
	// is zero, and normalize(vec3(0)) is a NaN. Every comparison against a NaN
	// is false and clamp() of one is whatever the driver feels like, which on
	// this hardware is zero: the occlusion buffer came out black and the
	// composite multiplied the screen by it.
	src += "    vec3 cross_dxdy = cross(dx, dy);\n";
	src += "    float crossLen = length(cross_dxdy);\n";
	src += "    if (!(crossLen > 1e-12)) { oColour = vec4(1.0); return; }\n";
	src += "    vec3 N = cross_dxdy / crossLen;\n";
	// The sign follows the winding of the difference, so force it rather than
	// trusting it.
	src += "    if (N.z < 0.0) N = -N;\n";

	// Interleaved gradient noise rather than a sine hash. The sine version
	// clusters -- neighbouring pixels often draw similar angles -- which the
	// blur cannot average away, and that is what showed up as lines crawling
	// across the ground as the camera moved. This distributes evenly over every
	// small neighbourhood, so the blur has something it can actually cancel.
	src += "    float a = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715)))) * 6.2831853;\n";
	src += "    float sa = sin(a), ca = cos(a);\n";
	src += "    mat2 rot = mat2(ca, -sa, sa, ca);\n";

	// A fixed world radius has to shrink on screen as it recedes, or distant
	// geometry gets sampled across half the screen.
	src += "    float z = -P.z;\n";
	src += "    vec2 uvScale = 0.5 * uAOParams.x / (uProjInfo.xy * z);\n";

	src += "    float occlusion = 0.0;\n";
	src += "    for (int i = 0; i < " + std::to_string(kSamples) + "; ++i) {\n";
	src += "        vec2 uv = vUV + (rot * kKernel[i]) * uvScale;\n";
	src += "        vec3 S = viewPos(clamp(uv, vec2(0.0), vec2(1.0)));\n";
	src += "        vec3 diff = S - P;\n";
	src += "        float dist = length(diff);\n";
	src += "        if (dist > 1e-4) {\n";
	// Anything far outside the radius is a different surface, not an occluder.
	src += "            float range = smoothstep(0.0, 1.0, uAOParams.x / dist);\n";
	src += "            occlusion += max(dot(N, diff / dist) - uAOParams.z, 0.0) * range;\n";
	src += "        }\n";
	src += "    }\n";
	src += "    occlusion = occlusion / float(" + std::to_string(kSamples) + ") * uAOParams.y;\n";
	src += "    oColour = vec4(vec3(clamp(1.0 - occlusion, 0.0, 1.0)), 1.0);\n";
	src += "}\n";
	return src;
}

std::string pc_post_build_ao_blur_shader()
{
	// A plain Gaussian is wrong for occlusion. It averages across silhouettes,
	// which both softens edges that should stay sharp and drags a blade of
	// grass's occlusion onto the ground behind it -- the shimmering the plain
	// blur left in foliage. Weighting each tap by how close it is in depth
	// keeps the average inside one surface.
	std::string src;
	src += kGlslVersion; src += kGlslPrecision;
	src += "in vec2 vUV;\n";
	src += "out vec4 oColour;\n";
	src += "uniform sampler2D uSource;\n";
	src += "uniform sampler2D uDepth;\n";
	src += "uniform vec2 uBlurStep;\n";
	src += "uniform vec4 uProjInfo;\n";

	// The shared helper, not a fourth hand-written copy. This one had its own,
	// which is how it kept the bug after the others were fixed.
	src += pc_post_view_depth_glsl();

	src += "void main() {\n";
	src += "    float centre = viewDepth(vUV);\n";
	// Scaled by distance: a centimetre of depth difference means something very
	// different a metre away than it does across the whole stage.
	src += "    float tolerance = max(centre * 0.02, 1.0);\n";
	src += "    float total = 0.0;\n";
	src += "    float weightSum = 0.0;\n";
	// Seven taps rather than five. The kernel has to be at least as wide as the
	// noise pattern it is cancelling, or some of that noise survives.
	src += "    for (int i = -3; i <= 3; ++i) {\n";
	src += "        vec2 uv = vUV + uBlurStep * float(i);\n";
	src += "        float d = viewDepth(uv);\n";
	src += "        float spatial = exp(-float(i * i) * 0.25);\n";
	src += "        float depthWeight = max(1.0 - abs(d - centre) / tolerance, 0.0);\n";
	src += "        float w = spatial * depthWeight;\n";
	src += "        total += texture(uSource, uv).r * w;\n";
	src += "        weightSum += w;\n";
	src += "    }\n";
	// The centre tap always has weight, so this cannot divide by zero, but a
	// surface isolated from every neighbour should keep its own value.
	src += "    float ao = (weightSum > 1e-5) ? (total / weightSum) : texture(uSource, vUV).r;\n";
	src += "    oColour = vec4(vec3(ao), 1.0);\n";
	src += "}\n";
	return src;
}

std::string pc_post_build_brightpass_shader()
{
	// Runs at half resolution, so each fetch already averages four scene
	// pixels through bilinear filtering -- a free first blur step.
	std::string src;
	src += kGlslVersion; src += kGlslPrecision;
	src += "in vec2 vUV;\n";
	src += "out vec4 oColour;\n";
	src += "uniform sampler2D uScene;\n";
	src += "uniform float uThreshold;\n";
	src += "void main() {\n";
	src += "    vec3 c = texture(uScene, vUV).rgb;\n";
	src += "    float luma = dot(c, vec3(0.2126, 0.7152, 0.0722));\n";
	// Scaling by how far past the threshold it went, rather than passing the
	// colour through unchanged, keeps the transition soft. A hard cut makes
	// bloom pop in and out as something drifts across the threshold.
	src += "    float excess = max(luma - uThreshold, 0.0);\n";
	src += "    float weight = (luma > 1e-5) ? (excess / luma) : 0.0;\n";
	src += "    oColour = vec4(c * weight, 1.0);\n";
	src += "}\n";
	return src;
}

std::string pc_post_build_dof_coc_shader()
{
	// Downsample and circle of confusion in one pass. Two jobs in one draw
	// because they read the same texel: splitting them would double the
	// bandwidth to produce the same answer.
	std::string src;
	src += kGlslVersion; src += kGlslPrecision;
	src += "in vec2 vUV;\n";
	src += "out vec4 oColour;\n";
	src += "uniform sampler2D uScene;\n";
	src += "uniform sampler2D uDepth;\n";
	src += "uniform vec4 uProjInfo;\n";
	src += "uniform vec4 uDofFocus;\n";
	src += pc_post_view_depth_glsl();
	src += pc_post_coc_glsl();
	src += "void main() {\n";
	src += "    float coc = cocAt(vUV);\n";
	// Premultiplied by coverage, with the coverage kept in alpha. A sharp
	// pixel contributes nothing to the blur around it, which is what stops the
	// captain from bleeding a halo of himself into the background behind him.
	// The composite divides by the alpha to get back a colour.
	src += "    oColour = vec4(texture(uScene, vUV).rgb * coc, coc);\n";
	src += "}\n";
	return src;
}

std::string pc_post_build_dof_blur_shader()
{
	// The same separable Gaussian bloom uses, but four channels wide. Bloom's
	// version ends with vec4(c, 1.0) and would throw the coverage away on the
	// first axis, leaving the second to divide by a constant 1.0 -- which is
	// exactly the halo the premultiplication exists to prevent.
	std::string src;
	src += kGlslVersion; src += kGlslPrecision;
	src += "in vec2 vUV;\n";
	src += "out vec4 oColour;\n";
	src += "uniform sampler2D uSource;\n";
	src += "uniform vec2 uBlurStep;\n";
	src += "void main() {\n";
	src += "    const float w0 = 0.227027;\n";
	src += "    const float w1 = 0.316216;\n";
	src += "    const float w2 = 0.070270;\n";
	src += "    const float o1 = 1.384615;\n";
	src += "    const float o2 = 3.230769;\n";
	src += "    vec4 c = texture(uSource, vUV) * w0;\n";
	src += "    c += texture(uSource, vUV + uBlurStep * o1) * w1;\n";
	src += "    c += texture(uSource, vUV - uBlurStep * o1) * w1;\n";
	src += "    c += texture(uSource, vUV + uBlurStep * o2) * w2;\n";
	src += "    c += texture(uSource, vUV - uBlurStep * o2) * w2;\n";
	src += "    oColour = c;\n";
	src += "}\n";
	return src;
}

std::string pc_post_build_blur_shader()
{
	// Separable Gaussian: the same program is run once across and once down,
	// which is nine taps instead of the eighty-one a single two-dimensional
	// kernel of the same width would need.
	std::string src;
	src += kGlslVersion; src += kGlslPrecision;
	src += "in vec2 vUV;\n";
	src += "out vec4 oColour;\n";
	src += "uniform sampler2D uSource;\n";
	src += "uniform vec2 uBlurStep;\n";
	src += "void main() {\n";
	src += "    const float w0 = 0.227027;\n";
	src += "    const float w1 = 0.316216;\n";
	src += "    const float w2 = 0.070270;\n";
	// Sampling between texels lets one bilinear fetch stand for two, so a
	// five-tap loop covers the reach of nine.
	src += "    const float o1 = 1.384615;\n";
	src += "    const float o2 = 3.230769;\n";
	src += "    vec3 c = texture(uSource, vUV).rgb * w0;\n";
	src += "    c += texture(uSource, vUV + uBlurStep * o1).rgb * w1;\n";
	src += "    c += texture(uSource, vUV - uBlurStep * o1).rgb * w1;\n";
	src += "    c += texture(uSource, vUV + uBlurStep * o2).rgb * w2;\n";
	src += "    c += texture(uSource, vUV - uBlurStep * o2).rgb * w2;\n";
	src += "    oColour = vec4(c, 1.0);\n";
	src += "}\n";
	return src;
}

const char* pc_post_vertex_shader()
{
	// One triangle covering the screen, built from the vertex index. A quad
	// would need a buffer and would rasterise its diagonal twice; this needs
	// neither a vertex buffer nor an attribute set, so the pass cannot be
	// disturbed by whatever vertex state the scene left behind.
#if PIKI_USE_GLES
	return "#version 300 es\n"
	       "precision highp float;\n"
	       "precision highp int;\n"
#else
	return "#version 330 core\n"
#endif
	       "out vec2 vUV;\n"
	       "void main() {\n"
	       "    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);\n"
	       "    vUV = p;\n"
	       "    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
	       "}\n";
}

std::string pc_post_build_fragment_shader(const PcPostEffects& fx)
{
	std::string src;
	src += kGlslVersion; src += kGlslPrecision;
	src += "in vec2 vUV;\n";
	src += "out vec4 oColour;\n";
	src += "uniform sampler2D uScene;\n";
	if (fx.fxaa) {
		// xy is one texel, zw is the render size. Passed as a vec4 because the
		// port already loads glUniform4f and nothing else needed a vec2.
		src += "uniform vec4 uTexelSize;\n";
	}

	if (pc_post_needs_depth(fx)) {
		src += "uniform sampler2D uDepth;\n";
	}
	if (pc_post_ssao_active(fx)) {
		src += "uniform sampler2D uAO;\n";
	}
	if (pc_post_dof_active(fx)) {
		src += "uniform sampler2D uDof;\n";
		src += "uniform vec4 uProjInfo;\n";
		src += "uniform vec4 uDofFocus;\n";
	}
	if (pc_post_bloom_active(fx)) {
		src += "uniform sampler2D uBloom;\n";
		src += "uniform float uBloomIntensity;\n";
	}
	if (fx.colourGrading) {
		src += "uniform float uGamma;\n";
		src += "uniform float uBrightness;\n";
		src += "uniform float uSaturation;\n";
	}

	if (fx.fxaa) {
		// FXAA, Lottes' compact variant. Five taps decide whether the pixel is
		// on an edge at all and which way it runs; four more blend along it.
		//
		// The early return matters as much as the filter: most of a frame is
		// flat, and a pixel with no local contrast is left exactly as it was
		// rather than paying for four more samples to reproduce itself.
		src += "const vec3 kLuma = vec3(0.299, 0.587, 0.114);\n";
		src += "vec3 fxaaFilter(vec2 uv, vec2 rcp) {\n";
		src += "    vec3 rgbNW = texture(uScene, uv + vec2(-1.0, -1.0) * rcp).rgb;\n";
		src += "    vec3 rgbNE = texture(uScene, uv + vec2( 1.0, -1.0) * rcp).rgb;\n";
		src += "    vec3 rgbSW = texture(uScene, uv + vec2(-1.0,  1.0) * rcp).rgb;\n";
		src += "    vec3 rgbSE = texture(uScene, uv + vec2( 1.0,  1.0) * rcp).rgb;\n";
		src += "    vec3 rgbM  = texture(uScene, uv).rgb;\n";
		src += "    float lNW = dot(rgbNW, kLuma);\n";
		src += "    float lNE = dot(rgbNE, kLuma);\n";
		src += "    float lSW = dot(rgbSW, kLuma);\n";
		src += "    float lSE = dot(rgbSE, kLuma);\n";
		src += "    float lM  = dot(rgbM,  kLuma);\n";
		src += "    float lMin = min(lM, min(min(lNW, lNE), min(lSW, lSE)));\n";
		src += "    float lMax = max(lM, max(max(lNW, lNE), max(lSW, lSE)));\n";
		src += "    if (lMax - lMin < max(0.0312, lMax * 0.125)) return rgbM;\n";
		src += "    vec2 dir = vec2(-((lNW + lNE) - (lSW + lSE)),\n";
		src += "                     ((lNW + lSW) - (lNE + lSE)));\n";
		// Without the reduction term a near-flat edge produces an enormous
		// direction and the filter smears across half the screen.
		src += "    float reduce = max((lNW + lNE + lSW + lSE) * 0.03125, 0.0078125);\n";
		src += "    float rcpMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + reduce);\n";
		src += "    dir = clamp(dir * rcpMin, vec2(-8.0), vec2(8.0)) * rcp;\n";
		src += "    vec3 rgbA = 0.5 * (texture(uScene, uv + dir * (1.0 / 3.0 - 0.5)).rgb\n";
		src += "                     + texture(uScene, uv + dir * (2.0 / 3.0 - 0.5)).rgb);\n";
		src += "    vec3 rgbB = rgbA * 0.5 + 0.25 * (texture(uScene, uv + dir * -0.5).rgb\n";
		src += "                                   + texture(uScene, uv + dir *  0.5).rgb);\n";
		// The wider blend is better when it stays within the local range; when
		// it does not, it has reached past the edge and the narrow one is right.
		src += "    float lB = dot(rgbB, kLuma);\n";
		src += "    return (lB < lMin || lB > lMax) ? rgbA : rgbB;\n";
		src += "}\n";
	}

	if (pc_post_dof_active(fx)) {
		// The coverage is recomputed here at full resolution rather than read
		// back from the half-resolution buffer. That buffer's alpha has been
		// blurred, so at a silhouette it says how much of the neighbourhood is
		// out of focus, not whether this pixel is -- and using it would soften
		// the edge of every sharp object by half a low-resolution texel.
		src += pc_post_view_depth_glsl();
		src += pc_post_coc_glsl();
	}

	src += "void main() {\n";
	if (fx.fxaa) {
		// Antialiasing first, grading afterwards: grading is a per-pixel tone
		// curve, so applying it to the resolved colour is the same as applying
		// it to each sample, and this way it costs one evaluation instead of
		// nine.
		src += "    vec3 c = fxaaFilter(vUV, uTexelSize.xy);\n";
	} else {
		src += "    vec3 c = texture(uScene, vUV).rgb;\n";
	}

	if (pc_post_dof_active(fx)) {
		// Before occlusion and bloom, because the blurred texture was built
		// from the scene colour and nothing else. Mixing it in further down
		// would replace the pixels that had just received their occlusion and
		// their bloom with a version that never got either.
		//
		// The consequence is that occlusion is applied to an already blurred
		// pixel, so its detail stays sharp inside a blurred region. It is a
		// low-frequency darkening and it does not read as an edge; the
		// alternative is a G-buffer, which the port does not have.
		src += "    float coc = cocAt(vUV);\n";
		src += "    vec4 blurred = texture(uDof, vUV);\n";
		// Undo the premultiplication. The guard matters: where everything in
		// the neighbourhood was in focus the coverage is zero, and the mix
		// below discards this value anyway -- but a division by zero is a NaN,
		// and mix() with a NaN is a NaN however small its weight.
		src += "    vec3 farColour = blurred.rgb / max(blurred.a, 1e-4);\n";
		src += "    c = mix(c, farColour, coc * uDofFocus.w);\n";
	}

	if (pc_post_ssao_active(fx) && fx.ssaoDebug) {
		// Straight out, nothing else applied. What reaches the screen is what
		// the occlusion pass produced.
		src += "    oColour = vec4(vec3(texture(uAO, vUV).r), 1.0);\n";
		src += "    return;\n";
	}

	if (pc_post_ssao_active(fx)) {
		// Multiplied, and before bloom: occlusion is ambient light that never
		// arrived, so it scales what the surface received. Bloom is light that
		// did arrive and then scattered, which lands on top of the result.
		src += "    c *= texture(uAO, vUV).r;\n";
	}

	if (pc_post_bloom_active(fx)) {
		// Added, not mixed: bloom is light that scattered on its way to the
		// lens, so it arrives on top of what is already there rather than
		// replacing part of it.
		//
		// Before grading, because grading is the tone curve applied to the
		// light reaching the sensor, and this is part of that light.
		src += "    c += texture(uBloom, vUV).rgb * uBloomIntensity;\n";
	}

	if (fx.colourGrading) {
		// Gamma first, on positive values only: pow() of a negative is
		// undefined, and a scene colour should never be negative but a driver
		// is not obliged to agree.
		src += "    c = pow(max(c, vec3(0.0)), vec3(1.0 / uGamma));\n";
		src += "    c += uBrightness;\n";
		// Rec. 709 luma, so desaturating keeps the apparent brightness rather
		// than dragging reds and blues down at different rates.
		src += "    float luma = dot(c, vec3(0.2126, 0.7152, 0.0722));\n";
		src += "    c = mix(vec3(luma), c, uSaturation);\n";
		src += "    c = clamp(c, 0.0, 1.0);\n";
	}

	src += "    oColour = vec4(c, 1.0);\n";
	src += "}\n";
	return src;
}
