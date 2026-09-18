#ifndef PC_VR_H
#define PC_VR_H

/*
 * OpenXR VR mode (PIKMIN_VR).
 *
 * The game still runs exactly once per frame. VR changes three things around
 * it, and nothing else:
 *
 *  - The camera. Before the world is drawn, the camera the game picked (Pcam
 *    or a cutscene) is replaced by the headset's head pose, placed in the world
 *    by the active rig: third-person (a view floating behind the captain) or
 *    tabletop (the level as a miniature in front of the player). Culling, the
 *    stick's movement direction and billboards all read that camera, so they
 *    follow the headset without further changes.
 *
 *  - The world draw. Between pc_gfx_vr_world_begin/end every GL draw is issued
 *    once per eye into that eye's target, with the eye's offset from the head
 *    and its lens folded into the projection uniform. Game code is not run
 *    twice, which it cannot survive: the simulation lives inside the draw.
 *
 *  - The interface. Whatever is drawn outside that span (HUD, pause menu, and
 *    every screen that is not gameplay) goes to the ordinary render target,
 *    which is shown on a floating panel.
 *
 * Every function is safe to call when VR is unavailable or off: they report
 * "inactive" and the game renders flat as usual.
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef enum PcVrRigMode {
	PC_VR_RIG_THIRD_PERSON = 0,
	PC_VR_RIG_TABLETOP     = 1,
} PcVrRigMode;

/* ── Lifecycle (pc_main) ─────────────────────────────────────────────────── */

/// Starts OpenXR against the current GL context. Leaves VR off, with a log
/// line saying why, when there is no runtime or no headset.
void pc_vr_init(void);
void pc_vr_shutdown(void);

/// An OpenXR session is running. Pacing, internal resolution and
/// post-processing change while this holds, even on frames that do not render.
int pc_vr_session_running(void);

/// The frame being built will be shown in the headset.
int pc_vr_frame_active(void);

/* ── Per frame (GL layer) ────────────────────────────────────────────────── */

/// Waits for the headset's next frame and locates the eyes. Once per frame,
/// from pc_gfx_begin_frame; repeated calls before submit are ignored.
void pc_vr_frame_begin(void);

/// Where an eye's picture goes. Both eyes share one framebuffer, side by side,
/// so a frame changes viewport between them and never the render target:
/// swapping targets per draw makes a tile-based GPU resolve its tiles every
/// time, which is most of a standalone headset's frame budget.
typedef struct PcVrEyeTarget {
	unsigned framebuffer;
	int x;
	int y;
	int width;
	int height;
} PcVrEyeTarget;

int pc_vr_eye_target(int eye, PcVrEyeTarget* out);

/// Column-major GL matrix taking head view space to the eye's clip space, in
/// the GameCube depth convention (near at NDC -1, far at 0) that the port's
/// shaders and fog reconstruction assume. nearZ/farZ are the game's.
void pc_vr_eye_projection(int eye, float nearZ, float farZ, float outGl[16]);

/// Hands the frame to the compositor. `worldDrawn` says whether the eye
/// targets hold a world this frame; `interfaceTexture` is the ordinary render
/// target (the HUD over transparency when the world was drawn, the whole
/// screen otherwise). Must follow pc_vr_frame_begin.
void pc_vr_submit(int worldDrawn, unsigned interfaceTexture, int width, int height);

/* ── Game hooks ──────────────────────────────────────────────────────────── */

typedef struct PcVrSceneInput {
	float cameraPos[3];     ///< where the game's own camera is this frame
	float cameraForward[3]; ///< and where it looks
	float focusPos[3];      ///< the captain
	float focusForward[3];  ///< and the way he faces, for the snap-behind button
	int cutscene;           ///< the camera is a cutscene's, not Pcam's
} PcVrSceneInput;

typedef struct PcVrSceneView {
	float view[3][4]; ///< GX row-major world -> head, rigid, world units
	float position[3];
	float nearZ;
	float farZ;
	/// A frustum covering both eyes with room for the head to turn before the
	/// frame is shown. The game culls against this: not culling at all costs
	/// far more than it saves, on a standalone headset above all.
	float fovDegrees;
	float aspect;
} PcVrSceneView;

/// How far the game should cull in VR, in world units: the fixed 2600 of the
/// flat game is measured for a camera a few hundred units from the captain,
/// and a shrunken tabletop level needs more. 0 when VR is not rendering.
float pc_vr_cull_distance(void);

/// Places the head in the world for this frame. Returns 0 (and leaves `out`
/// alone) unless the frame renders to the headset.
int pc_vr_scene_view(const PcVrSceneInput* in, PcVrSceneView* out);

/// The pointing hand's ray in world units, valid after pc_vr_scene_view.
int pc_vr_aim_ray(float origin[3], float direction[3]);

/// Where the pointing ray met the ground, for drawing the laser.
void pc_vr_set_aim_hit(const float hit[3], int valid);
int pc_vr_laser(float from[3], float to[3]);

/// World distance to add to fog start/end: in tabletop the eye is far from
/// the miniature in world units, and unshifted fog would swallow it.
float pc_vr_fog_offset(void);

/// GameCube rumble motor on/off, routed to the controllers' haptics.
void pc_vr_set_rumble(int on);

/* ── Input (pc_window) ───────────────────────────────────────────────────── */

typedef struct PcVrPad {
	int active;
	/// The VR button was pressed together with the off-hand grip: open or close
	/// the port's own settings menu, which is not reachable with a headset on.
	int settingsToggle;
	int a, b, x, y, z, l, r, start;
	int dpadUp, dpadDown, dpadLeft, dpadRight;
	float stickX, stickY;       ///< -1..1, +y up
	float substickX, substickY; ///< -1..1, +y up
	float triggerL, triggerR;   ///< 0..1
} PcVrPad;

/// Controller state mapped onto the GameCube pad. Syncs OpenXR input, so call
/// it once per pad read.
void pc_vr_read_pad(PcVrPad* out);

#ifdef __cplusplus
}
#endif

#endif /* PC_VR_H */
