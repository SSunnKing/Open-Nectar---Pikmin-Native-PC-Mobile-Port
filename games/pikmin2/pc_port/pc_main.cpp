/**
 * @file pc_main.cpp
 * @brief Native entry point for the Pikmin 2 PC port (Phase 2).
 *
 * Replaces pikmin2-decomp/src/sysBootupU/sysBootup.cpp::main(), which is
 * excluded from pikmin2_legacy so this file owns the process entry.
 *
 * GameCube boot:
 *   System::initialize();
 *   return (new System())->run();  // GameFlow::run() loops sections
 *
 * Pikmin 1 used gsys->run(new PlugPikiApp()); that path does not exist here.
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <SDL.h>
#include "pc_discord.h"

#ifdef _WIN32
extern "C" {
__declspec(dllexport) unsigned long NvOptimusEnablement                  = 1;
__declspec(dllexport) int           AmdPowerXpressRequestHighPerformance = 1;
}
#endif

#ifdef __ANDROID__
#include "android/pc_android.h"
#endif
#include "pc_gpu_preference.h"
#include "pc_window.h"
#include "settings/pc_settings.h"

#include "System.h"

#if defined(__linux__) || defined(__ANDROID__)
#include <csignal>
#include <unistd.h>
#include <execinfo.h>
#include <dlfcn.h>
#include <link.h>

static uintptr_t pc_module_base()
{
	Dl_info info;
	struct link_map* map = nullptr;
	if (dladdr1((void*)&pc_module_base, &info, (void**)&map, RTLD_DL_LINKMAP) && map) {
		return (uintptr_t)map->l_addr;
	}
	return 0;
}

extern "C" void pc_crash_handler(int sig, siginfo_t* info, void* /*ucontext*/)
{
	static volatile sig_atomic_t entered = 0;
	if (entered) {
		_exit(128 + sig);
	}
	entered = 1;

	const char* name = "signal";
	switch (sig) {
	case SIGSEGV: name = "SIGSEGV (bad memory access)"; break;
	case SIGBUS:  name = "SIGBUS (misaligned/bad address)"; break;
	case SIGABRT: name = "SIGABRT (abort/assert)"; break;
	case SIGFPE:  name = "SIGFPE (arithmetic)"; break;
	case SIGILL:  name = "SIGILL (illegal instruction)"; break;
	}

	void* frames[64];
	int   count = backtrace(frames, 64);

	uintptr_t base = pc_module_base();
	fprintf(stderr, "\n=== [PC Port] CRASH: %s ===\n", name);
	if (info) {
		fprintf(stderr, "faulting address = %p\n", info->si_addr);
	}
	fprintf(stderr, "module base = 0x%lx  (addr2line -Cfe pikmin2_pc <rel>)\n", (unsigned long)base);
	for (int i = 0; i < count; i++) {
		uintptr_t abs = (uintptr_t)frames[i];
		Dl_info   info;
		const char* sym = (dladdr(frames[i], &info) && info.dli_sname) ? info.dli_sname : "?";
		fprintf(stderr, "  #%-2d abs=0x%016lx rel=0x%010lx  %s\n", i, (unsigned long)abs,
		        (unsigned long)(abs - base), sym);
	}
	fprintf(stderr, "=== end backtrace ===\n");
	fflush(stderr);

	signal(sig, SIG_DFL);
	raise(sig);
}

static void pc_install_crash_handler()
{
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = pc_crash_handler;
	sa.sa_flags     = SA_SIGINFO;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGSEGV, &sa, nullptr);
	sigaction(SIGBUS, &sa, nullptr);
	sigaction(SIGABRT, &sa, nullptr);
	sigaction(SIGFPE, &sa, nullptr);
	sigaction(SIGILL, &sa, nullptr);
}
#else
static void pc_install_crash_handler() { }
#endif

int main(int argc, char* argv[])
{
	setvbuf(stdout, NULL, _IONBF, 0);
	pc_install_crash_handler();
	SDL_SetMainReady();

	// El launcher Fusion consulta y modifica los ajustes sin crear la ventana
	// del juego. El backend ya implementaba este protocolo, pero el entry point
	// de Pikmin 2 todavía no lo despachaba.
	{
		const int settingsResult = pc_settings_cli(argc, argv);
		if (settingsResult >= 0) return settingsResult;
	}

	if (argc == 2 && std::strcmp(argv[1], "--audio-self-test") == 0) {
		// jaudio_integration_test.cpp drives Pikmin 1's src/jaudio engine
		// (PIKMIN_NATIVE_JAUDIO). Pikmin 2 already runs its own JSystem JAS/JAI
		// engine through the pc_dsp_host software DSP, so that backend has
		// nothing to plug into here: an explicit skip, not a missing-asset one.
		std::puts("[jaudio-test] SKIP: this suite targets Pikmin 1's src/jaudio engine; pikmin2_pc plays audio "
		          "through JSystem JAS/JAI + pc_dsp_host (covered by p2_aram_test / p2_envelope_test)");
		return 77;
	}

#ifdef __ANDROID__
	if (!pc_android_init()) {
		printf("[PC Port Fatal Error] Android storage unavailable\n");
		return 1;
	}
#endif

	pc_gpu_preference_apply();

	printf("╔══════════════════════════════════════════╗\n");
	printf("║   Pikmin 2 - Native PC Port              ║\n");
	printf("╚══════════════════════════════════════════╝\n\n");

	printf("[PC Port] Initializing SDL2 window and GL context...\n");
	// Ventana, contexto GL y entrada compartidos con el port de Pikmin 1.
	if (!pc_window_init("Open Nectar 2", 1280, 720)) {
		printf("[PC Port Fatal Error] Could not initialize window/OpenGL!\n");
		return 1;
	}
	// Menu F1: carga y aplica la configuracion guardada (video, controles, graficos).
	pc_settings_init();
	pc_discord_init("Pikmin 2");

	printf("[PC Port] System::initialize() (JFW heaps, render mode)...\n");
	System::initialize();

	printf("[PC Port] Constructing System / GameFlow...\n");
	System* gameSys = new System();
	gameSys->setFlag(System::SF_TVModeSelected);
	gameSys->setFlag(System::SF_RestoredRenderMode);

	printf("[PC Port] GameFlow::run() — section loop (boot → title → ...)\n");
	const int status = gameSys->run();

	printf("[PC Port] Game exited (status %d).\n", status);
	return status;
}
