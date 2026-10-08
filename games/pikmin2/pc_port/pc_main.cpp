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
#ifdef __ANDROID__
	// bionic no tiene RTLD_DL_LINKMAP: la base de la .so la da dladdr.
	if (dladdr((void*)&pc_module_base, &info)) {
		return (uintptr_t)info.dli_fbase;
	}
#else
	struct link_map* map = nullptr;
	if (dladdr1((void*)&pc_module_base, &info, (void**)&map, RTLD_DL_LINKMAP) && map) {
		return (uintptr_t)map->l_addr;
	}
#endif
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
#elif defined(_WIN32)
#include <io.h>
#include <windows.h>

// Equivalente al de Linux: excepción, dirección y pila (desenrollada con las
// tablas de x64, sin dbghelp) en stderr y en pikmin2_crash.log, junto a los
// datos del juego. rel = dirección - base del módulo, para addr2line/nm.
static void pc_crash_frame(FILE* out, int index, DWORD64 pc)
{
	HMODULE module = nullptr;
	wchar_t path[MAX_PATH] = L"?";
	GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
	                   (LPCWSTR)(uintptr_t)pc, &module);
	if (module) GetModuleFileNameW(module, path, MAX_PATH);
	const wchar_t* name = wcsrchr(path, L'\\');
	fprintf(out, "  #%-2d abs=0x%016llx rel=0x%010llx  %ls\n", index, (unsigned long long)pc,
	        (unsigned long long)(pc - (DWORD64)(uintptr_t)module), name ? name + 1 : path);
}

static LONG WINAPI pc_crash_filter(EXCEPTION_POINTERS* ep)
{
	static volatile LONG entered = 0;
	if (InterlockedExchange(&entered, 1)) return EXCEPTION_CONTINUE_SEARCH;

	FILE* outs[2] = { stderr, fopen("pikmin2_crash.log", "w") };
	for (FILE* out : outs) {
		if (!out) continue;
		const EXCEPTION_RECORD* rec = ep->ExceptionRecord;
		fprintf(out, "\n=== [PC Port] CRASH: exception 0x%08lx at %p ===\n", (unsigned long)rec->ExceptionCode,
		        rec->ExceptionAddress);
		if (rec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && rec->NumberParameters >= 2) {
			fprintf(out, "%s address = 0x%llx\n", rec->ExceptionInformation[0] ? "write" : "read",
			        (unsigned long long)rec->ExceptionInformation[1]);
		}
		fprintf(out, "module base = %p  (rel: nm -C / addr2line -Cfe pikmin2_pc.exe sin strip)\n",
		        (void*)GetModuleHandleW(nullptr));
		CONTEXT ctx = *ep->ContextRecord;
		for (int i = 0; i < 64 && ctx.Rip; ++i) {
			pc_crash_frame(out, i, ctx.Rip);
			DWORD64 imageBase = 0;
			PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(ctx.Rip, &imageBase, nullptr);
			if (!fn) {
				// Función hoja: la dirección de vuelta está en la cima de la pila.
				ctx.Rip = *(DWORD64*)(uintptr_t)ctx.Rsp;
				ctx.Rsp += 8;
				continue;
			}
			void* handlerData = nullptr;
			DWORD64 establisher = 0;
			RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, ctx.Rip, fn, &ctx, &handlerData, &establisher, nullptr);
		}
		fprintf(out, "=== end backtrace ===\n");
		fflush(out);
	}
	if (outs[1]) fclose(outs[1]);
	return EXCEPTION_CONTINUE_SEARCH;
}

static void pc_install_crash_handler()
{
	// Sin consola (abierto desde el launcher) la salida del juego se perdería:
	// va a pikmin2.log, junto a los datos.
	if (GetConsoleWindow() == nullptr || std::getenv("PIKMIN2_LOG_TO_FILE")) {
		if (freopen("pikmin2.log", "w", stdout)) {
			_dup2(_fileno(stdout), _fileno(stderr));
			setvbuf(stderr, NULL, _IONBF, 0);
		}
	}
	// El CRT de Windows limita a 512 FILE* abiertos (Linux deja ~1024 fd):
	// el juego mantiene abiertos archivos de audio y DVD a la vez.
	// 2048 es el máximo que acepta msvcrt (MinGW); la UCRT admite más.
	_setmaxstdio(2048);
	SetUnhandledExceptionFilter(pc_crash_filter);
}
#else
static void pc_install_crash_handler() { }
#endif

int main(int argc, char* argv[])
{
	pc_install_crash_handler();
	setvbuf(stdout, NULL, _IONBF, 0);
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
#if defined(VERSION_PAL)
	gameSys->setFlag(System::SF_TVModeSelected); // solo PAL: elección 50/60 Hz
	gameSys->setFlag(System::SF_RestoredRenderMode);
#endif

	printf("[PC Port] GameFlow::run() — section loop (boot → title → ...)\n");
	const int status = gameSys->run();

	printf("[PC Port] Game exited (status %d).\n", status);
	return status;
}
