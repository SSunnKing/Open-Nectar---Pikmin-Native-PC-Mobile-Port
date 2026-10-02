/*
 * The decision has to be right on machines this project cannot run on: a
 * desktop with one GPU, a laptop with Nouveau, an X11 session with no glvnd
 * EGL vendor file. Getting it wrong there does not pick the slower GPU, it
 * stops OpenGL from starting at all.
 */
#include "pc_gpu_preference.h"

#include <cstdio>
#include <cstring>

static int sFailures = 0;

static void check(bool condition, const char* what)
{
    if (!condition) {
        printf("FAIL: %s\n", what);
        sFailures++;
    }
}

static PcGpuEvidence evidence(int nvidia, int eglFile, int chose, int optOut, int forceEgl = 0)
{
    PcGpuEvidence e;
    e.nvidiaKernelModuleLoaded = nvidia;
    e.eglVendorFilePresent     = eglFile;
    e.userAlreadyChose         = chose;
    e.optedOut                 = optOut;
    e.forceEglRoute            = forceEgl;
    return e;
}

int main()
{
    // Default: offload + GLX. Exclusive NVIDIA EGL is opt-in — it is what
    // yields "Could not get EGL display" on a Wayland compositor owned by the
    // integrated GPU.
    PcGpuDecision d = pc_gpu_preference_decide(evidence(1, 1, 0, 0));
    check(d.requestOffload && d.routeGlx && !d.routeEgl, "NVIDIA laptop: offload and GLX, not EGL");

    d = pc_gpu_preference_decide(evidence(1, 1, 0, 0, 1));
    check(d.requestOffload && d.routeGlx && d.routeEgl, "NECTAR_PRIME_EGL pins NVIDIA EGL");

    // No NVIDIA module: an Intel-only or AMD machine. Setting
    // __GLX_VENDOR_LIBRARY_NAME=nvidia here leaves libglvnd with no vendor and
    // OpenGL fails, so the whole request must be withheld.
    d = pc_gpu_preference_decide(evidence(0, 0, 0, 0));
    check(!d.requestOffload && !d.routeGlx && !d.routeEgl, "no NVIDIA: nothing is set");

    // The EGL vendor file decides only the EGL half. Without it there is no
    // valid path to name, but GLX is unaffected and is what an X11 session uses.
    d = pc_gpu_preference_decide(evidence(1, 0, 0, 0));
    check(d.requestOffload && d.routeGlx && !d.routeEgl, "NVIDIA without EGL vendor file: GLX only");

    // A choice already in the environment wins, including a deliberate choice
    // of the integrated GPU -- which is exactly how someone works around a
    // broken discrete driver.
    d = pc_gpu_preference_decide(evidence(1, 1, 1, 0));
    check(!d.requestOffload && !d.routeGlx && !d.routeEgl, "user choice is not overridden");

    // The opt-out outranks everything, including a present NVIDIA.
    d = pc_gpu_preference_decide(evidence(1, 1, 0, 1));
    check(!d.requestOffload && !d.routeGlx && !d.routeEgl, "NECTAR_NO_PRIME wins");

    // Every outcome explains itself. A no-op that says nothing cannot be told
    // apart from a fix that ran and did not work.
    const PcGpuEvidence cases[] = {
        evidence(1, 1, 0, 0), evidence(0, 0, 0, 0), evidence(1, 0, 0, 0),
        evidence(1, 1, 1, 0), evidence(1, 1, 0, 1),
    };
    for (const PcGpuEvidence& e : cases) {
        PcGpuDecision r = pc_gpu_preference_decide(e);
        check(r.reason != nullptr && std::strlen(r.reason) > 0, "every decision gives a reason");
    }

    // EGL is never requested without the offload that gives it meaning.
    for (const PcGpuEvidence& e : cases) {
        PcGpuDecision r = pc_gpu_preference_decide(e);
        check(!r.routeEgl || r.requestOffload, "EGL routing implies offload");
        check(!r.routeGlx || r.requestOffload, "GLX routing implies offload");
    }

    if (sFailures == 0) printf("pc_gpu_preference_test: all checks passed\n");
    return sFailures == 0 ? 0 : 1;
}
