#pragma once

#include <string>

// Screen-space post-processing.
//
// The port already renders the scene to its own framebuffer -- that is what
// the render scale setting drives -- and blits it to the window afterwards.
// This module owns what happens in between.
//
// Like pc_tev_shader, it only builds GLSL text and answers questions about the
// effect set. Every GL call lives in pc_gfx.cpp, where the entry points are.
// That keeps the interesting decisions -- which effects are on, what the shader
// should therefore contain, whether the pass is worth running at all -- testable
// without a window or a driver.

/// One post-process configuration. Effects are additive: the generated shader
/// contains exactly the ones switched on and nothing else, so a pass with two
/// effects costs two effects rather than the worst case.
struct PcPostEffects {
	// Antialiasing, as a post-process rather than MSAA on the framebuffer.
	// That is a deliberate choice for this game: Pikmin's undergrowth is drawn
	// with alpha-tested quads, and MSAA does nothing for those edges because
	// they are not geometry. FXAA works on the finished image, so it smooths
	// the leaves and the grass along with everything else.
	bool fxaa = false;

	// Ambient occlusion. The first effect here that reads depth, and the only
	// one that cannot run at all when the driver denied us a depth texture.
	bool ssao          = false;
	// Shows the occlusion buffer on its own instead of applying it, so an
	// artefact can be traced to the pass that produced it rather than guessed
	// at from the composited picture. Set with PIKMIN_AO_DEBUG=1.
	bool ssaoDebug     = false;
	float ssaoRadius    = 40.0f;  // world units searched around each pixel
	float ssaoIntensity = 0.0f;   // 0 darkens nothing, so the pass is skipped

	// Bloom. Unlike the others this is not one pass: bright areas are pulled
	// out into a half-resolution target, blurred separably, and added back.
	// Half resolution is not a compromise here -- the result is a wide blur, so
	// the detail thrown away was never going to survive it, and it makes the
	// blur four times cheaper.
	bool bloom          = false;
	float bloomThreshold = 0.75f;  // luma above which a pixel contributes
	float bloomIntensity = 0.0f;   // 0 adds nothing, so the pass is skipped

	// Depth of field, focused on the captain.
	//
	// The focus distance is NOT here. It changes every frame as the camera
	// moves, and this struct is compared field by field to decide whether the
	// shader has to be rebuilt -- a per-frame float in it would recompile the
	// post-process program on every frame of the game. It is pushed separately
	// through pc_gfx_set_dof_focus().
	//
	// Everything here is a setting, constant until the player changes it.
	bool dof = false;
	// How much of the blurred image is allowed through at maximum circle of
	// confusion. 0 blurs nothing, so the passes are skipped.
	float dofStrength = 0.0f;
	// The sharp band and the falloff, as fractions of the focus distance
	// rather than absolute world units. Pikmin's camera sits at very different
	// distances between the close follow view and the far one, and a fixed
	// band in world units would be most of the screen in one and a sliver in
	// the other. Proportional keeps the look the same at every zoom.
	float dofSharpFraction   = 0.25f;
	float dofFalloffFraction = 1.0f;
	// How many times the separable blur runs. Widening the five-tap kernel
	// instead would undersample it and band; running it again is what actually
	// produces a wider blur.
	int dofIterations = 1;

	// Colour grading. Cheap, needs only the scene colour, and it is the effect
	// that proves the whole path works end to end.
	// Sombras proyectadas (shadow map del sol, aplicadas en pantalla): la
	// máscara multiplica el color, como la oclusión. strength 0 = sin sombra.
	bool shadows        = false;
	float shadowStrength = 0.0f;  // 0 .. 1, cuánto oscurece la sombra
	int shadowMapSize   = 2048;
	float shadowRange   = 1400.0f; // unidades de vista cubiertas por el mapa

	bool colourGrading = false;
	float gamma        = 1.0f;   // 0.5 .. 2.0, 1.0 is untouched
	float brightness   = 0.0f;   // -0.5 .. 0.5, 0.0 is untouched
	float saturation   = 1.0f;   // 0.0 .. 2.0, 1.0 is untouched

	bool operator==(const PcPostEffects& o) const
	{
		return fxaa == o.fxaa && ssao == o.ssao && ssaoDebug == o.ssaoDebug
		    && ssaoRadius == o.ssaoRadius && ssaoIntensity == o.ssaoIntensity
		    && bloom == o.bloom
		    && bloomThreshold == o.bloomThreshold && bloomIntensity == o.bloomIntensity
		    && dof == o.dof && dofStrength == o.dofStrength
		    && dofSharpFraction == o.dofSharpFraction
		    && dofFalloffFraction == o.dofFalloffFraction
		    && dofIterations == o.dofIterations
		    && shadows == o.shadows && shadowStrength == o.shadowStrength
		    && shadowMapSize == o.shadowMapSize && shadowRange == o.shadowRange
		    && colourGrading == o.colourGrading && gamma == o.gamma
		    && brightness == o.brightness && saturation == o.saturation;
	}
	bool operator!=(const PcPostEffects& o) const { return !(*this == o); }
};

/**
 * @brief Whether the pass is worth running at all.
 *
 * With nothing switched on, the scene blits straight to the window exactly as
 * it did before this module existed. A pass that copies the frame to say
 * nothing about it is pure cost, and on the port's reference GPU that cost is
 * not free.
 */
bool pc_post_any_enabled(const PcPostEffects& fx);

/**
 * @brief True when the shader will sample the depth buffer.
 *
 * Depth is not always available: the port falls back to a renderbuffer where a
 * driver will not give it a depth texture, and a renderbuffer cannot be read.
 * Effects that need depth must be dropped in that case rather than assumed.
 */
bool pc_post_needs_depth(const PcPostEffects& fx);

/// Vertex shader for the fullscreen pass. Generates its own geometry from
/// gl_VertexID, so the pass needs no vertex buffer of its own.
const char* pc_post_vertex_shader();

/// Fragment shader containing only the effects that are switched on.
std::string pc_post_build_fragment_shader(const PcPostEffects& fx);

/// True when bloom will actually contribute. Bloom at zero intensity is the
/// same picture for the price of three extra passes.
bool pc_post_bloom_active(const PcPostEffects& fx);

/// Extracts the parts of the scene bright enough to bloom, into a smaller
/// target. Shares the fullscreen vertex shader.
std::string pc_post_build_brightpass_shader();

/// True when ambient occlusion will actually darken something.
bool pc_post_ssao_active(const PcPostEffects& fx);

/// Computes occlusion into a single-channel-ish target. Reconstructs view
/// position and normal from depth alone: the port has no G-buffer.
std::string pc_post_build_ssao_shader();

/// One half of a depth-aware blur, for denoising occlusion. Unlike the plain
/// Gaussian this will not average across a silhouette, which is what kept
/// foliage shimmering.
std::string pc_post_build_ao_blur_shader();

/// Sombras: true cuando hay que renderizar el mapa y aplicar la máscara.
bool pc_post_shadows_active(const PcPostEffects& fx);
/// Pasada de profundidad desde la luz (misma disposición de vértices que el
/// juego: posición, uv0 y slot de paleta; recorte por alpha de textura).
std::string pc_post_build_shadow_depth_vertex();
std::string pc_post_build_shadow_depth_fragment();
/// Máscara en pantalla: reconstruye la posición de vista desde la profundidad
/// y compara con el mapa (PCF 3x3). Escribe 1 = iluminado, 1-strength = sombra.
std::string pc_post_build_shadow_mask_shader();

/// True when depth of field will actually blur something.
bool pc_post_dof_active(const PcPostEffects& fx);

/// Downsamples the scene and computes the circle of confusion in one pass,
/// writing colour premultiplied by it, with the coverage in alpha.
///
/// The premultiplication is the whole point. A plain blur of the scene mixes
/// the sharp captain into the pixels behind him, and those pixels then show
/// that colour as a halo around his silhouette. Weighting each texel by how
/// blurred it is meant to be keeps a sharp subject from contributing to the
/// blur that surrounds it.
std::string pc_post_build_dof_coc_shader();

/// One half of the separable blur for depth of field. Unlike the Gaussian used
/// by bloom this carries alpha through, because that is where the coverage
/// from the circle-of-confusion pass lives.
std::string pc_post_build_dof_blur_shader();

/// One half of a separable Gaussian blur. uBlurStep carries the direction and
/// the texel size together, so the same program serves both axes.
std::string pc_post_build_blur_shader();
