#ifndef PC_GFX_H
#include <cstddef>
#define PC_GFX_H

#include "Dolphin/gx.h"
#include "pc_opengl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initialization
void pc_gfx_init(void);
void pc_gfx_begin_frame(void);
void pc_gfx_present(void);
void pc_gfx_perf_scope_begin(const char* name);
void pc_gfx_perf_scope_end(void);

// Internal 3D render resolution scale (multiplier on the native 640x480).
// begin_frame picks the change up and resizes the internal framebuffer.
void pc_gfx_set_render_scale(float scale);
// Resolución base del render interno (0,0 = la del área de salida). Se encaja
// en la relación de aspecto de salida y se multiplica por el render scale.
void pc_gfx_set_render_resolution(int width, int height);
float pc_gfx_get_render_scale(void);

// Aspect ratio support.
// Mode: 0=auto (detect from window), 1=4:3, 2=16:10, 3=16:9, 4=21:9
void pc_gfx_set_aspect_ratio_mode(int mode);
int pc_gfx_get_aspect_ratio_mode(void);
float pc_gfx_get_current_aspect_ratio(void);

// Menu 2D: uniform 640x480 inside the RT (pillarbox). World keeps the
// stretched map. Viewport and scissor share map_gx_rect, so this flag
// applies to both. Reset at begin_frame.
void pc_gfx_set_ui_43(int enabled);
void pc_gfx_set_ui_43_no_bars(int enabled);
int pc_gfx_get_ui_43(void);

// Translucent quad over the whole render target, ignoring GX 640/hud mapping.
// Used by the F1 overlay so the dim covers 16:9 even when leftover menu
// scissors still describe a left-aligned 4:3 rect.
void pc_gfx_dim_full_target(unsigned char alpha);

// Field HUD: GX space is V=480*aspect by 480, mapped uniformly onto the RT.
// Panes are translated in that space (left / centre / right). Not a stretch.
void pc_gfx_set_hud_wide(int enabled);
int pc_gfx_get_hud_wide(void);
int pc_gfx_get_hud_virtual_width(void);

// Menu layout. Default is widescreen (same as field HUD B).
// Revert to 4:3 pillarbox (A) without a rebuild: PIKMIN_MENU_PILLARBOX=1
// or compile with -DNECTAR_MENU_PILLARBOX.
int pc_gfx_menu_wide(void);
void pc_gfx_begin_menu_2d(void);
int pc_gfx_menu_virt_width(void);
int pc_gfx_menu_shift_center(void);
int pc_gfx_menu_shift_right(void);
int pc_gfx_menu_shift_slot(int slotIndex);

// Title 2D only: keep MenuPanel p00N (the water-drop pictures) inside the
// centred 640×480. P2DPerspGraph disables pane scissor, so they otherwise
// fly across the 16:9 bars. Reset at begin_frame.
void pc_gfx_set_menu_clip_43(int enabled);
void pc_gfx_apply_menu_clip_43(void);

// Viewport / Scissor / Matrices
void pc_gfx_set_projection(const Mtx44 mtx, GXProjectionType type);
void pc_gfx_set_viewport(f32 xOrig, f32 yOrig, f32 wd, f32 ht, f32 nearZ, f32 farZ);
void pc_gfx_set_scissor(u32 xOrig, u32 yOrig, u32 wd, u32 ht);
void pc_gfx_load_pos_mtx(const Mtx mtx, u32 id);
void pc_gfx_set_current_mtx(u32 id);
void pc_gfx_load_nrm_mtx(const Mtx mtx, u32 id);
void pc_gfx_load_tex_mtx(const Mtx mtx, u32 id);
void pc_gfx_set_tex_coord_gen(GXTexCoordID coord, GXTexGenType type, GXTexGenSrc src, u32 matrixIdx);

// State Settings
void pc_gfx_set_z_mode(GXBool compareEnable, GXCompare func, GXBool updateEnable);
void pc_gfx_set_blend_mode(GXBlendMode type, GXBlendFactor srcFactor, GXBlendFactor dstFactor, GXLogicOp op);
void pc_gfx_set_cull_mode(GXCullMode mode);

// Snapshot of the fixed-function state a one-off draw (the HD model mod)
// changes, so it can be put back exactly: the engine's material display
// lists do not restate everything, so a leaked cull/blend/z mode showed up
// on the next mesh drawn (a transparent Onion, a vanished helmet).
struct PcGfxPipelineState {
    GXBool zCompare; GXCompare zFunc; GXBool zUpdate;
    GXBlendMode blendType; GXBlendFactor blendSrc, blendDst; GXLogicOp blendOp;
    GXCullMode cull;
};
PcGfxPipelineState pc_gfx_get_pipeline_state(void);
void pc_gfx_set_pipeline_state(const PcGfxPipelineState& state);
void pc_gfx_set_color_update(GXBool updateEnable);
void pc_gfx_set_alpha_update(GXBool updateEnable);
void pc_gfx_set_alpha_compare(GXCompare comp0, u8 ref0, GXAlphaOp op, GXCompare comp1, u8 ref1);
void pc_gfx_set_chan_ctrl(GXChannelID chan, GXBool enable, GXColorSrc ambSrc, GXColorSrc matSrc, u32 lightMask, GXDiffuseFn diffFn, GXAttnFn attnFn);
void pc_gfx_set_chan_mat_color(GXChannelID chan, GXColor color);
void pc_gfx_set_chan_amb_color(GXChannelID chan, GXColor color);
void pc_gfx_init_light_pos(void* ltObj, f32 x, f32 y, f32 z);
void pc_gfx_init_light_dir(void* ltObj, f32 x, f32 y, f32 z);
void pc_gfx_init_light_color(void* ltObj, GXColor color);
void pc_gfx_init_light_attn(void* ltObj, f32 a0, f32 a1, f32 a2, f32 k0, f32 k1, f32 k2);
void pc_gfx_init_light_attn_a(void* ltObj, f32 a0, f32 a1, f32 a2);
void pc_gfx_init_light_attn_k(void* ltObj, f32 k0, f32 k1, f32 k2);
void pc_gfx_init_specular_dir(void* ltObj, f32 x, f32 y, f32 z);
void pc_gfx_load_light(void* ltObj, u32 lightMask);
void pc_gfx_set_tev_order(GXTevStageID stage, GXTexCoordID coord, GXTexMapID map, GXChannelID chan);
void pc_gfx_set_tev_op(GXTevStageID stage, GXTevMode mode);
void pc_gfx_set_num_tev_stages(u8 num);
void pc_gfx_set_tev_color_in(GXTevStageID stage, GXTevColorArg a, GXTevColorArg b, GXTevColorArg c, GXTevColorArg d);
void pc_gfx_set_tev_alpha_in(GXTevStageID stage, GXTevAlphaArg a, GXTevAlphaArg b, GXTevAlphaArg c, GXTevAlphaArg d);
void pc_gfx_set_tev_color_op(GXTevStageID stage, GXTevOp op, GXTevBias bias, GXTevScale scale, GXBool clamp, GXTevRegID outReg);
void pc_gfx_set_tev_alpha_op(GXTevStageID stage, GXTevOp op, GXTevBias bias, GXTevScale scale, GXBool clamp, GXTevRegID outReg);
void pc_gfx_set_tev_color(GXTevRegID reg, GXColor color);
void pc_gfx_set_tev_color_s10(GXTevRegID reg, GXColorS10 color);
void pc_gfx_set_tev_kcolor(GXTevKColorID id, GXColor color);
void pc_gfx_set_tev_kcolor_sel(GXTevStageID stage, GXTevKColorSel sel);
void pc_gfx_set_tev_kalpha_sel(GXTevStageID stage, GXTevKAlphaSel sel);
void pc_gfx_set_tev_swap_mode(GXTevStageID stage, GXTevSwapSel rasSel, GXTevSwapSel texSel);
void pc_gfx_set_tev_swap_mode_table(GXTevSwapSel table, GXTevColorChan red, GXTevColorChan green, GXTevColorChan blue, GXTevColorChan alpha);

// Texture Management
void pc_gfx_init_tex_obj(GXTexObj* obj, void* imagePtr, u16 width, u16 height, GXTexFmt format, GXTexWrapMode wrapS, GXTexWrapMode wrapT, GXBool mipmap);

/// Vuelca en texture_names.log el nombre tex1_* que Dolphin daría a cada
/// textura subida (PLAN_TEXTURAS_HD fase 0). Lo activa --dump-texture-names.
void pc_gfx_set_dump_texture_names(int enabled);

/// A texture whose pixels are already RGBA, row by row, with no GameCube
/// tiling. Nothing on the console could do this; it exists so the H4M player
/// can hand over a finished picture instead of encoding one into a hardware
/// format and unpicking it again with four TEV stages.
void pc_gfx_init_tex_obj_rgba(GXTexObj* obj, void* rgba, u16 width, u16 height,
                              GXTexWrapMode wrapS = GX_CLAMP, GXTexWrapMode wrapT = GX_CLAMP);
void pc_gfx_init_tex_obj_ci(GXTexObj* obj, void* imagePtr, u16 width, u16 height, GXCITexFmt format,
                            GXTexWrapMode wrapS, GXTexWrapMode wrapT, GXBool mipmap, u32 tlutName);
void pc_gfx_init_tlut_obj(GXTlutObj* obj, void* lut, GXTlutFmt format, u16 numEntries);
void pc_gfx_load_tlut(GXTlutObj* obj, u32 tlutName);
void pc_gfx_load_tex_obj(GXTexObj* obj, GXTexMapID id);

// Vertex Descriptor & Stream Setup
void pc_gfx_clear_vtx_desc(void);
void pc_gfx_set_vtx_desc(GXAttr attr, GXAttrType type);
void pc_gfx_set_vtx_attr_fmt(GXVtxFmt fmt, GXAttr attr, GXCompCnt cnt, GXCompType type, u8 frac);
void pc_gfx_set_array(GXAttr attr, void* basePtr, u8 stride);

// Drawing & FIFO Stream Parser
void pc_gfx_begin(GXPrimitive type, GXVtxFmt vtxfmt, u16 nverts);
void pc_gfx_push_u8(u8 val);
void pc_gfx_push_u16(u16 val);
void pc_gfx_push_u32(u32 val);
void pc_gfx_push_s8(s8 val);
void pc_gfx_push_s16(s16 val);
void pc_gfx_push_s32(s32 val);
void pc_gfx_push_f32(f32 val);
void pc_gfx_position(f32 x, f32 y, f32 z);
void pc_gfx_color(u8 r, u8 g, u8 b, u8 a);
void pc_gfx_texcoord(f32 u, f32 v);
void pc_gfx_normal(f32 x, f32 y, f32 z);
void pc_gfx_end(void);
void pc_gfx_call_display_list(const void* list, u32 nbytes);

// Frame presentation
void pc_gfx_copy_disp(void* dest, GXBool clear);
void pc_gfx_set_copy_clear(GXColor color, u32 clearZ);

// Render packet capture (for immutable replay without re-entering game code)
void pc_gfx_begin_capture(uint64_t serial);
void pc_gfx_end_capture();
void pc_gfx_enable_capture(bool enabled);
bool pc_gfx_is_capture_enabled();
void pc_gfx_replay_display_list(const void* list, u32 nbytes);
bool pc_gfx_replay_captured_frame(void);

#ifdef __cplusplus
// TEV shader specialisation: one generated program per material configuration
// instead of a single interpreting ubershader. Enabled by default; the
// ubershader remains available for A/B comparison and as an automatic fallback.
void pc_gfx_set_shader_specialisation(bool enabled);
bool pc_gfx_get_shader_specialisation(void);
size_t pc_gfx_get_specialised_program_count(void);

/**
 * @brief The scene's depth buffer, as a texture a shader can sample.
 *
 * Zero when the driver would not give us one and the port fell back to a
 * renderbuffer. Every screen-space effect has to check this and switch itself
 * off rather than assume: the fallback is a real path, not a theoretical one.
 */
unsigned int pc_gfx_get_depth_texture(void);

/// Linear fog, as GXSetFog describes it. Distances are in view space; the
/// colour components are 0-255.
void pc_gfx_set_fog(int enabled, float startZ, float endZ, float nearZ, float farZ,
                    unsigned char r, unsigned char g, unsigned char b);

/// Player override. The game keeps asking for fog either way; this decides
/// whether the request is honoured, so a toggle lands on the next frame.
void pc_gfx_set_fog_allowed(int allowed);

/// Anisotropic filtering sample count: 0 or 1 off, otherwise 2, 4, 8, 16.
/// Clamped to what the driver reports, and only applied to textures that carry
/// mip levels -- without a chain to choose among there is nothing for it to do.
void pc_gfx_set_anisotropy(int samples);

/**
 * @brief Frees the GL texture belonging to one GX texture object.
 *
 * Nothing ever did. The cache is keyed by the GXTexObj address, and because
 * the port's heap resets do not actually free memory, a reloaded stage builds
 * its texture objects at fresh addresses -- so every reload added a full set of
 * GL textures and kept the previous one. Called when a heap is reset, which is
 * where the game already says which of its objects are dying.
 */
void pc_gfx_release_texture(void* gxTexObj);

/// Resident-mesh cache (PLAN_RENDIMIENTO fase 1). The game flushes the CPU
/// cache over any vertex data it rewrites, which is exactly when a mesh built
/// from that data is stale; a heap reset means every mesh may be.
void pc_gfx_invalidate_cpu_range(const void* addr, size_t bytes);
void pc_gfx_invalidate_resident_meshes(void);

/// Toques sobre menús 2D: la pantalla anota su espacio de dibujo (ancho y
/// alto de su P2DGrafContext) justo después de setPort(); un toque
/// normalizado sobre la ventana se convierte a ese espacio.
void pc_gfx_note_menu_tap_space(int graphWidth, int graphHeight);
bool pc_gfx_menu_tap_to_graph(float nx, float ny, float* x, float* y);

/// Live texture count, bytes held, peak bytes, and lifetime created/released.
void pc_gfx_get_texture_stats(size_t* live, size_t* liveBytes, size_t* peakBytes,
                              size_t* created, size_t* released);

/// Sets the post-process effect set. With nothing enabled the pass is skipped
/// entirely and the scene blits straight to the window.
struct PcPostEffects;
void pc_gfx_set_post_effects(const PcPostEffects& effects);

/// Where the depth-of-field pass should focus, in view units along the camera's
/// forward axis. The game pushes it once a frame from the captain's position;
/// zero means there is nothing to focus on and the effect stands down.
///
/// Deliberately not part of PcPostEffects: that struct decides when the
/// post-process shader is rebuilt, and this value changes every frame.
void pc_gfx_set_dof_focus(float viewDistance);

/**
 * @brief The scene colour target, at internal render resolution.
 *
 * This is what the render scale changes, and it is not the window size.
 */
unsigned int pc_gfx_get_colour_texture(void);
void pc_gfx_get_render_size(int* width, int* height);

/**
 * @brief Peak use of the two fixed draw pools since the process started.
 *
 * Both are fatal to overflow and both grow with the number of Pikmin drawn,
 * so a log line carrying the headroom beats working it out from where a
 * crash landed. Defined in sysCommon/graphics.cpp.
 */
void pc_gfx_get_pool_peaks(int* matrixPeak, int* matrixMax, int* shapePeak, int* shapeMax);

// Hand this frame's submission cost to the tick profiler and reset it. Called
// once per frame, right after renderall. No-op unless PIKMIN_TICK_STATS is set.
void pc_gfx_flush_batch(void);
void pc_gfx_flush_submit_stats(void);

// File-select stain probe. Off unless PIKMIN_FILESEL_DEBUG=1. Reads the same
// left/right pixels before particles, after particles, after the rest of the
// UI, and on both sides of the late post pass, and dumps blend/TEV on the
// particle draws in between. One report every 60 file-select frames.
void pc_gfx_filesel_debug_probe(const char* tag);
void pc_gfx_filesel_debug_set_fx(int active);
void pc_gfx_filesel_debug_note_aspect(float aspect, int screenW, int screenH);
void pc_gfx_filesel_debug_note_ptcl(unsigned blendFactor, unsigned zMode, unsigned tevMode, float scaleSize);

// Title Start/Options crop probe. Off unless PIKMIN_TITLE_DEBUG=1.
// Prints viewport/scissor/mapping plus a 7-point scan. Heartbeat once a
// second; also prints the rest of that frame if the 5%/95% samples are
// black while the centre is not — the failure, not the first N events.
void pc_gfx_title_debug_probe(const char* tag);

}
// Fuera del bloque extern "C": devuelve una referencia a un tipo C++, y Clang
// lo rechaza con enlace C (GCC sólo avisaba).
class PcRenderPacketStore;
PcRenderPacketStore& pc_gfx_get_packet_store();

// ── Capa de sprites en coordenadas de ventana ────────────────────────────────
// Para la interfaz táctil (pc_port/touch): cuadrados con textura dibujados
// directamente sobre el framebuffer de la ventana, después del blit del juego,
// en píxeles de ventana con el origen arriba a la izquierda. No usa VBO ni
// atributos: el vértice se construye en el shader.
unsigned pc_gfx_overlay_texture_create(int width, int height, const unsigned char* rgba);
void pc_gfx_overlay_texture_destroy(unsigned texture);
// Prepara el estado GL (framebuffer 0, viewport de la ventana, blending).
void pc_gfx_overlay_begin(void);
// Dibuja `texture` en el rectángulo dado (píxeles de ventana, y hacia abajo),
// multiplicada por el color y girada `angleRadians` sobre su centro.
void pc_gfx_overlay_sprite(unsigned texture, float x, float y, float w, float h,
                           float r, float g, float b, float a, float angleRadians);
// Devuelve el estado GL al framebuffer nativo del juego.
void pc_gfx_overlay_end(void);
void pc_gfx_get_drawable_size(int* width, int* height);
// Proyecta un punto en el espacio de dibujo actual del juego (matriz de
// posición y proyección GX vigentes) a píxeles de ventana. Sirve para
// colocar sprites de la capa sobre texto o paneles P2D. false si no se puede.
bool pc_gfx_project_current(float x, float y, float z, float* winX, float* winY);
#endif

#endif // PC_GFX_H
