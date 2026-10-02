#ifndef PC_GPU_PREFERENCE_H
#define PC_GPU_PREFERENCE_H

/*
 * Asking a switchable-graphics laptop for the dedicated GPU.
 *
 * On such a machine every process runs on the integrated GPU unless it asks
 * otherwise, and the port never asked: a GTX 1050 sat idle while the game
 * rendered on an HD 630. The two platforms want the request in different
 * forms and at different moments.
 *
 * Windows is handled in pc_main.cpp, not here. Both vendors read an exported
 * symbol out of the process image as it is loaded, before any code runs, so
 * there is nothing to decide at runtime.
 *
 * Linux is a runtime decision, and a dangerous one to get wrong.
 * __GLX_VENDOR_LIBRARY_NAME=nvidia on a machine with no NVIDIA GLX vendor
 * leaves libglvnd with no provider and OpenGL fails outright -- the fix would
 * break the game for everyone who does not have this hardware. So the choice
 * depends on what is actually installed, and the deciding is kept here, as a
 * pure function over that evidence, where it can be tested without a laptop.
 *
 * The three variables are NVIDIA's PRIME render offload protocol:
 *   __NV_PRIME_RENDER_OFFLOAD   ask for the discrete GPU at all
 *   __GLX_VENDOR_LIBRARY_NAME   route GLX to the NVIDIA vendor (X11, Xwayland)
 *   __EGL_VENDOR_LIBRARY_FILENAMES  the same for EGL (native Wayland)
 * GLX is requested automatically: X11 and Xwayland use it. Exclusive NVIDIA
 * EGL is not — pointing __EGL_VENDOR_LIBRARY_FILENAMES at 10_nvidia.json
 * alone makes SDL's Wayland path call eglGetDisplay on a vendor that cannot
 * talk to an Intel/AMD compositor, and the window never opens
 * ("Could not get EGL display"). Opt in with NECTAR_PRIME_EGL=1.
 *
 * AMD switchable graphics use DRI_PRIME instead and are NOT handled: that path
 * has no hardware here to verify it on, and an unverified guess is how the
 * integrated GPU was chosen in the first place.
 */

#ifdef __cplusplus
extern "C" {
#endif

/// The evidence the decision is made from. Gathered by the caller so that the
/// decision itself touches neither the filesystem nor the environment.
typedef struct PcGpuEvidence {
    /// The proprietary NVIDIA kernel module is loaded (/proc/driver/nvidia).
    /// Nouveau does not create it, and it exists only where the vendor GLX
    /// and EGL libraries can be expected alongside it.
    int nvidiaKernelModuleLoaded;
    /// The glvnd EGL vendor description for NVIDIA exists on disk. Its path is
    /// what __EGL_VENDOR_LIBRARY_FILENAMES has to be set to, so a missing file
    /// means the EGL half cannot be requested at all.
    int eglVendorFilePresent;
    /// Non-zero if the user already set any of the three variables by hand. A
    /// deliberate choice outranks this one, including a deliberate choice of
    /// the integrated GPU.
    int userAlreadyChose;
    /// Non-zero if NECTAR_NO_PRIME is set: the opt-out, for when the discrete
    /// GPU is the broken one.
    int optedOut;
    /// Non-zero if NECTAR_PRIME_EGL=1: also pin EGL to the NVIDIA vendor file.
    /// Off by default; see the EGL note above.
    int forceEglRoute;
} PcGpuEvidence;

/// What to do about it. Each field means "set this variable"; the caller sets
/// none of them when the evidence does not support it.
typedef struct PcGpuDecision {
    int requestOffload;   ///< __NV_PRIME_RENDER_OFFLOAD=1
    int routeGlx;         ///< __GLX_VENDOR_LIBRARY_NAME=nvidia
    int routeEgl;         ///< __EGL_VENDOR_LIBRARY_FILENAMES=<vendor file>
    /// Why, in one line, for the log. Never null: a decision to do nothing
    /// still says what stopped it, because a silent no-op is indistinguishable
    /// from a fix that did not work.
    const char* reason;
} PcGpuDecision;

/// Decide from the evidence alone. Pure: no I/O, no globals, no environment.
PcGpuDecision pc_gpu_preference_decide(PcGpuEvidence evidence);

/// The glvnd path the EGL variable is set to when routeEgl is chosen.
extern const char* const kPcGpuEglVendorPath;

/// Gather the evidence, decide, and apply it by setting the variables. Must be
/// called before SDL_Init: libglvnd reads them when it first selects a vendor,
/// and by the time a context exists the choice is already made. No-op off
/// Linux. Reports what it did on stdout either way.
void pc_gpu_preference_apply(void);

/// Drop the three PRIME variables this module may have set. Used when window
/// creation fails so a second SDL_Init can talk to the compositor again.
void pc_gpu_preference_clear(void);

/// Non-zero if the NVIDIA kernel module is loaded.
int pc_gpu_preference_nvidia_present(void);

/// Non-zero if this process is on a Wayland session (Xwayland included).
int pc_gpu_preference_session_is_wayland(void);

/// Put GLX offload and SDL on X11. Used when the first context landed on the
/// iGPU: Wayland EGL ignores the GLX vendor, so the window has to be remade.
void pc_gpu_preference_force_x11_glx(void);

#ifdef __cplusplus
}
#endif

#endif // PC_GPU_PREFERENCE_H
