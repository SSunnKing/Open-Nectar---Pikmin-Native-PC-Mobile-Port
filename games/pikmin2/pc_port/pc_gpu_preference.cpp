#include "pc_gpu_preference.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef __linux__
#include <sys/stat.h>
#endif

const char* const kPcGpuEglVendorPath = "/usr/share/glvnd/egl_vendor.d/10_nvidia.json";

PcGpuDecision pc_gpu_preference_decide(PcGpuEvidence evidence)
{
    PcGpuDecision decision = { 0, 0, 0, "" };

    if (evidence.optedOut) {
        decision.reason = "NECTAR_NO_PRIME is set; leaving the GPU choice alone";
        return decision;
    }
    if (evidence.userAlreadyChose) {
        decision.reason = "the environment already selects a GPU; not overriding it";
        return decision;
    }
    if (!evidence.nvidiaKernelModuleLoaded) {
        decision.reason = "no NVIDIA kernel module; nothing to offload to";
        return decision;
    }

    // The module is loaded, so the GLX vendor it ships with can be asked for.
    // Requesting offload without routing GLX would be half a request: the
    // offload variable alone leaves the Mesa vendor answering, on the Intel GPU.
    decision.requestOffload = 1;
    decision.routeGlx       = 1;

    if (evidence.forceEglRoute && evidence.eglVendorFilePresent) {
        decision.routeEgl = 1;
        decision.reason   = "NVIDIA present; requesting PRIME offload for GLX and EGL "
                            "(NECTAR_PRIME_EGL)";
    } else {
        // GLX is enough for X11/Xwayland. Pinning EGL to NVIDIA exclusively
        // is what produces "Could not get EGL display" on Wayland when the
        // compositor is on the integrated GPU.
        decision.reason = "NVIDIA present; requesting PRIME offload for GLX "
                          "(EGL left to the compositor)";
    }
    return decision;
}

void pc_gpu_preference_apply(void)
{
#ifdef __linux__
    PcGpuEvidence evidence;

    struct stat st;
    evidence.nvidiaKernelModuleLoaded = (stat("/proc/driver/nvidia", &st) == 0);
    evidence.eglVendorFilePresent     = (stat(kPcGpuEglVendorPath, &st) == 0);
    evidence.userAlreadyChose         = (getenv("__NV_PRIME_RENDER_OFFLOAD") != nullptr
                                         || getenv("__GLX_VENDOR_LIBRARY_NAME") != nullptr
                                         || getenv("__EGL_VENDOR_LIBRARY_FILENAMES") != nullptr
                                         || getenv("DRI_PRIME") != nullptr);
    evidence.optedOut                 = (getenv("NECTAR_NO_PRIME") != nullptr);
    evidence.forceEglRoute            = (getenv("NECTAR_PRIME_EGL") != nullptr);

    PcGpuDecision decision = pc_gpu_preference_decide(evidence);

    if (decision.requestOffload) setenv("__NV_PRIME_RENDER_OFFLOAD", "1", 1);

    // Xwayland speaks Mesa GLX. Pinning the NVIDIA GLX vendor there makes
    // X_GLXCreateContext BadValue and abort the process. Native X11 can use
    // that vendor; Wayland has to stay on EGL (optionally NVIDIA via
    // NECTAR_PRIME_EGL). NECTAR_PRIME_X11=1 opts back into the X11/GLX path.
    const bool wayland = pc_gpu_preference_session_is_wayland();
    const bool forceX11 = getenv("NECTAR_PRIME_X11") != nullptr;
    if (decision.routeGlx && (!wayland || forceX11))
        setenv("__GLX_VENDOR_LIBRARY_NAME", "nvidia", 1);
    if (decision.routeEgl)
        setenv("__EGL_VENDOR_LIBRARY_FILENAMES", kPcGpuEglVendorPath, 1);
    else if (wayland && decision.requestOffload && evidence.eglVendorFilePresent) {
        // First context on Wayland has to ask EGL now. Re-init after an Intel
        // window already exists cannot change glvnd, and SDL_Quit+Init trips
        // a udev symbol error on this SDL2.
        setenv("__EGL_VENDOR_LIBRARY_FILENAMES", kPcGpuEglVendorPath, 1);
        printf("[PC Port] GPU preference: Wayland; requesting NVIDIA EGL for the first context\n");
    }

    if (forceX11 && !evidence.optedOut)
        pc_gpu_preference_force_x11_glx();
    else if (wayland && decision.requestOffload && !evidence.eglVendorFilePresent)
        printf("[PC Port] GPU preference: Wayland session; not pinning NVIDIA GLX (Xwayland rejects it)\n");

    printf("[PC Port] GPU preference: %s\n", decision.reason);
#endif
}

void pc_gpu_preference_clear(void)
{
#ifdef __linux__
    unsetenv("__NV_PRIME_RENDER_OFFLOAD");
    unsetenv("__GLX_VENDOR_LIBRARY_NAME");
    unsetenv("__EGL_VENDOR_LIBRARY_FILENAMES");
#endif
}

int pc_gpu_preference_nvidia_present(void)
{
#ifdef __linux__
    struct stat st;
    return stat("/proc/driver/nvidia", &st) == 0 ? 1 : 0;
#else
    return 0;
#endif
}

int pc_gpu_preference_session_is_wayland(void)
{
#ifdef __linux__
    if (getenv("WAYLAND_DISPLAY") != nullptr)
        return 1;
    const char* session = getenv("XDG_SESSION_TYPE");
    return (session && std::strcmp(session, "wayland") == 0) ? 1 : 0;
#else
    return 0;
#endif
}

void pc_gpu_preference_force_x11_glx(void)
{
#ifdef __linux__
    setenv("__NV_PRIME_RENDER_OFFLOAD", "1", 1);
    setenv("__GLX_VENDOR_LIBRARY_NAME", "nvidia", 1);
    if (getenv("SDL_VIDEODRIVER") == nullptr
        || std::strcmp(getenv("SDL_VIDEODRIVER"), "wayland") == 0) {
        setenv("SDL_VIDEODRIVER", "x11", 1);
        printf("[PC Port] GPU preference: using X11 so NVIDIA GLX can own the context\n");
    }
#endif
}
