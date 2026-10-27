#pragma once

// Two-pass frame for netplay: an authoritative sim pass and a local
// presentation pass in one logical tick.
//
// Inert unless deterministic mode is on (pc_netplay_deterministic()); the
// game then runs one pass exactly as before.
//
// PlugPikiApp::idle in deterministic mode:
//   - authoritative pass: SimCamera installed, null GX, sim blocks run;
//   - presentation pass: real camera, sim blocks skipped; GL submission is
//     suppressed during deterministic catch-up, otherwise normal.
// pc_netplay_present_set_skip_presentation(1) runs the authoritative pass
// only (for resimulation).

#if defined(PIKI_PC_PORT) && !defined(PC_NETPLAY_PRESENT_HOST)
class Camera;
class Graphics;
class Matrix4f;
#endif

#include <cstddef>

#ifdef __cplusplus
extern "C" {
#endif

void pc_netplay_present_set_skip_presentation(int skip);
int pc_netplay_present_skip_presentation(void);

// True when the two-pass frame is active (deterministic mode, engine build).
int pc_netplay_present_two_pass_active(void);

// True when this pass may write sim state: single-pass mode, or the
// authoritative pass of a two-pass tick. The presentation pass only reads
// sim state and writes view state.
int pc_netplay_present_sim_side(void);

// True only inside the authoritative pass of a two-pass tick.
int pc_netplay_present_sim_pass(void);

// Null-GX flag for the authoritative pass and catch-up presentation:
// GL-issuing submission is skipped.
// "attempted" counts skipped submissions; "gl calls" counts real GL issued
// while the flag was on, measured at the counting choke points in
// pc_gfx.cpp and pc_texpack.cpp, and must stay 0. Uploads happen in the
// presentation pass outside catch-up only.
void pc_netplay_present_set_null_gx(int on);
int pc_netplay_present_null_active(void);
unsigned long long pc_netplay_present_null_gl_calls(void);
unsigned long long pc_netplay_present_null_attempted(void);
void pc_netplay_present_note_attempt(void);
void pc_netplay_present_note_real(void);
void pc_netplay_present_reset_counters(void);

// Local player's view: PIKMIN_NETPLAY_LOCAL_PLAYER=0|1, else the default
// (0 unless a session sets it: host 0, joiner 1). Cached.
int pc_netplay_present_local_player(void);
void pc_netplay_present_reset_local_player(void);
void pc_netplay_present_set_local_player_default(int player);

// Shapes whose animation matrices the presentation pass redirected.
unsigned long long pc_netplay_present_saved_shapes(void);

#ifdef __cplusplus
}
#endif

#if defined(PIKI_PC_PORT) && !defined(PC_NETPLAY_PRESENT_HOST) && defined(__cplusplus)
// SimCamera: identity lookAt (world-space pose) with a fixed 16:9 gameplay
// projection. It never reads the live camera, whose FOV and near clip follow
// zoom and first person and so differ between peers.
Camera* pc_netplay_present_sim_camera(void);
void pc_netplay_present_begin_authoritative(Graphics& gfx);
void pc_netplay_present_end_authoritative(Graphics& gfx);
void pc_netplay_present_begin_presentation(Graphics& gfx);
void pc_netplay_present_end_presentation(Graphics& gfx);
// BaseShape::updateAnim in the presentation pass records each shape's sim
// matrix pointer once. Returns true if the shape was newly saved.
bool pc_netplay_present_save_shape_ptr(void* shape, void* savedPtr);
size_t pc_netplay_present_saved_count(void);
void* pc_netplay_present_saved_shape_at(size_t i, void** outSaved);
void pc_netplay_present_clear_saved(void);
// Restores BaseShape::mAnimMatrices for every saved shape (shapeBase.cpp).
void pc_netplay_present_restore_all_shapes(void);
#endif
