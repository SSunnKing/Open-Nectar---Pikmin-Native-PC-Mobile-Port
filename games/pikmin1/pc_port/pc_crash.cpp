#include "pc_crash.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cwchar>

#ifndef NECTAR_RELEASE_VERSION
#define NECTAR_RELEASE_VERSION "unknown"
#endif
#ifndef NECTAR_GIT_COMMIT
#define NECTAR_GIT_COMMIT "unknown"
#endif
#if defined(_WIN32)
#define PC_CRASH_PLATFORM "Windows x86-64"
#elif defined(__ANDROID__)
#define PC_CRASH_PLATFORM "Android arm64"
#else
#define PC_CRASH_PLATFORM "Linux x86-64"
#endif

static const char kPcCrashFile[] = "pikmin_crash.log";
static const char kPcLogFile[]   = "pikmin.log";

// Launcher que abrió el juego (NECTAR_LAUNCHER) y ruta absoluta del informe.
// Se copian al arrancar: dentro del gestor de crash no se reserva memoria.
#ifdef _WIN32
static wchar_t sPcCrashLauncher[1024];
static wchar_t sPcCrashPath[1024];
#else
static char sPcCrashLauncher[1024];
static char sPcCrashPath[1024];
#endif

// Definido en os_stubs.cpp (OSPanic lo rellena antes de abortar).
extern "C" char pc_crash_note[512];

[[maybe_unused]] static void pc_crash_header(FILE* out)
{
	if (pc_crash_note[0]) fprintf(out, "reason: %.511s\n", pc_crash_note);
	char stamp[32] = "?";
	const time_t now = time(nullptr);
	struct tm utc;
#ifdef _WIN32
	if (gmtime_s(&utc, &now) == 0)
#else
	if (gmtime_r(&now, &utc))
#endif
		strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &utc);
	fprintf(out, "game: Pikmin\nversion: %s (commit %s)\nplatform: %s\ntime: %s UTC\n", NECTAR_RELEASE_VERSION,
	        NECTAR_GIT_COMMIT, PC_CRASH_PLATFORM, stamp);
}

#if defined(__linux__) || defined(__ANDROID__)
#include <csignal>
#include <dlfcn.h>
#include <link.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __ANDROID__
// bionic solo trae backtrace() desde Android 13 (API 33): se desenrolla con libunwind.
#include <unwind.h>
struct PcUnwindState {
	void** frames;
	int count, max;
};
static _Unwind_Reason_Code pc_unwind_frame(struct _Unwind_Context* ctx, void* arg)
{
	PcUnwindState* state = (PcUnwindState*)arg;
	const uintptr_t pc = _Unwind_GetIP(ctx);
	if (pc && state->count < state->max) state->frames[state->count++] = (void*)pc;
	return state->count < state->max ? _URC_NO_REASON : _URC_END_OF_STACK;
}
static int backtrace(void** frames, int max)
{
	PcUnwindState state = { frames, 0, max };
	_Unwind_Backtrace(pc_unwind_frame, &state);
	return state.count;
}
#else
#include <execinfo.h>
#endif

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

#if defined(__x86_64__) && !defined(__ANDROID__)
#include <sys/ucontext.h>
#include "pc_intdiv_recover.h"

// Una línea por dirección (solo write: estamos dentro de una señal).
static void pc_intdiv_log(uint64_t at)
{
	static uint64_t seen[32];
	static int      seenCount = 0;
	for (int i = 0; i < seenCount; i++) {
		if (seen[i] == at) return;
	}
	if (seenCount < 32) seen[seenCount++] = at;
	char line[96] = "[PC Port] integer division by zero recovered at rel 0x";
	size_t len    = strlen(line);
	const uint64_t rel = at - pc_module_base();
	for (int shift = 36; shift >= 0; shift -= 4) line[len++] = "0123456789abcdef"[(rel >> shift) & 0xF];
	line[len++] = '\n';
	ssize_t ignored = write(2, line, len);
	(void)ignored;
}
#endif

static void pc_crash_handler(int sig, siginfo_t* info, void* ucontext)
{
#if defined(__x86_64__) && !defined(__ANDROID__)
	if (sig == SIGFPE && info && ucontext && (info->si_code == FPE_INTDIV || info->si_code == FPE_INTOVF)) {
		greg_t* regs      = static_cast<ucontext_t*>(ucontext)->uc_mcontext.gregs;
		const uint64_t at = (uint64_t)regs[REG_RIP];
		if (pc_intdiv_recover((uint64_t*)&regs[REG_RIP], (uint64_t*)&regs[REG_RAX], (uint64_t*)&regs[REG_RDX],
		                      info->si_code == FPE_INTOVF)) {
			pc_intdiv_log(at);
			return;
		}
	}
#else
	(void)ucontext;
#endif
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
	const int count = backtrace(frames, 64);
	const uintptr_t base = pc_module_base();
	FILE* outs[2] = { stderr, fopen(kPcCrashFile, "w") };
	for (FILE* out : outs) {
		if (!out) continue;
		fprintf(out, "\n=== [PC Port] CRASH: %s ===\n", name);
		pc_crash_header(out);
		if (info) {
			fprintf(out, "faulting address = %p\n", info->si_addr);
		}
		fprintf(out, "module base = 0x%lx  (addr2line -Cfe nectar <rel>)\n", (unsigned long)base);
		for (int i = 0; i < count; i++) {
			const uintptr_t abs = (uintptr_t)frames[i];
			Dl_info dl;
			const char* sym = (dladdr(frames[i], &dl) && dl.dli_sname) ? dl.dli_sname : "?";
			fprintf(out, "  #%-2d abs=0x%016lx rel=0x%010lx  %s\n", i, (unsigned long)abs,
			        (unsigned long)(abs - base), sym);
		}
		fprintf(out, "=== end backtrace ===\n");
		fflush(out);
	}
	if (outs[1]) fclose(outs[1]);

#ifndef __ANDROID__
	// Ventana del informe al momento, en un proceso nuevo (el launcher). En
	// Android la muestra el launcher al volver a abrirse.
	if (sPcCrashLauncher[0] && fork() == 0) {
		setsid();
		execl(sPcCrashLauncher, sPcCrashLauncher, "--crash-report", sPcCrashPath, (char*)nullptr);
		_exit(127);
	}
#endif

	signal(sig, SIG_DFL);
	raise(sig);
}

void pc_install_crash_handler()
{
	const char* launcher = getenv("NECTAR_LAUNCHER");
	if (launcher && strlen(launcher) < sizeof(sPcCrashLauncher)) strcpy(sPcCrashLauncher, launcher);
	char cwd[900];
	if (getcwd(cwd, sizeof(cwd))) snprintf(sPcCrashPath, sizeof(sPcCrashPath), "%s/%s", cwd, kPcCrashFile);

#ifndef __ANDROID__
	// Sin terminal (abierto desde el launcher o el escritorio) la salida se
	// perdía: va a pikmin.log y entra en el informe de crash. Una tubería o un
	// archivo se respetan (el launcher lee --settings-dump así).
	struct stat outStat;
	const bool outCaptured
	    = fstat(STDOUT_FILENO, &outStat) == 0 && (S_ISFIFO(outStat.st_mode) || S_ISREG(outStat.st_mode));
	if ((!isatty(STDOUT_FILENO) && !outCaptured) || getenv("PIKMIN_LOG_TO_FILE")) {
		if (freopen(kPcLogFile, "w", stdout)) {
			dup2(fileno(stdout), STDERR_FILENO);
			setvbuf(stdout, NULL, _IONBF, 0);
			setvbuf(stderr, NULL, _IONBF, 0);
		}
	}
#endif

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
#include <csignal>

// Pila desenrollada con las tablas de x64 (sin dbghelp). rel = dirección -
// base del módulo, para addr2line/nm sobre el ejecutable sin strip.
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

#include "pc_intdiv_recover.h"

// División entera entre cero: se recupera como en ARM64 (pc_intdiv_recover.h)
// antes de que llegue al filtro de crash.
static LONG WINAPI pc_intdiv_handler(EXCEPTION_POINTERS* ep)
{
	const DWORD code = ep->ExceptionRecord->ExceptionCode;
	if (code != EXCEPTION_INT_DIVIDE_BY_ZERO && code != EXCEPTION_INT_OVERFLOW) return EXCEPTION_CONTINUE_SEARCH;
	CONTEXT* ctx     = ep->ContextRecord;
	const DWORD64 at = ctx->Rip;
	if (!pc_intdiv_recover(&ctx->Rip, &ctx->Rax, &ctx->Rdx, code == EXCEPTION_INT_OVERFLOW)) {
		return EXCEPTION_CONTINUE_SEARCH;
	}
	static DWORD64 seen[32];
	static LONG    seenCount = 0;
	bool known = false;
	for (LONG i = 0; i < seenCount; i++) known = known || seen[i] == at;
	if (!known) {
		if (seenCount < 32) seen[seenCount++] = at;
		fprintf(stderr, "[PC Port] integer division by zero recovered at rel 0x%llx\n",
		        (unsigned long long)(at - (DWORD64)(uintptr_t)GetModuleHandleW(nullptr)));
	}
	return EXCEPTION_CONTINUE_EXECUTION;
}

// abort() (assert, std::terminate) levanta SIGABRT, que el filtro de
// excepciones no ve: el juego se cerraba sin informe. El manejador lo
// convierte en una excepción para que el filtro saque la pila de siempre.
static const DWORD kPcAbortException = 0xE0000ABD;
static void pc_abort_signal(int)
{
	RaiseException(kPcAbortException, EXCEPTION_NONCONTINUABLE, 0, nullptr);
}

static LONG WINAPI pc_crash_filter(EXCEPTION_POINTERS* ep)
{
	static volatile LONG entered = 0;
	if (InterlockedExchange(&entered, 1)) return EXCEPTION_CONTINUE_SEARCH;

	FILE* outs[2] = { stderr, fopen(kPcCrashFile, "w") };
	for (FILE* out : outs) {
		if (!out) continue;
		const EXCEPTION_RECORD* rec = ep->ExceptionRecord;
		fprintf(out, "\n=== [PC Port] CRASH: exception 0x%08lx%s at %p ===\n", (unsigned long)rec->ExceptionCode,
		        rec->ExceptionCode == kPcAbortException ? " (abort/assert)" : "",
		        rec->ExceptionAddress);
		pc_crash_header(out);
		if (rec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && rec->NumberParameters >= 2) {
			fprintf(out, "%s address = 0x%llx\n", rec->ExceptionInformation[0] ? "write" : "read",
			        (unsigned long long)rec->ExceptionInformation[1]);
		}
		fprintf(out, "module base = %p  (rel: nm -C / addr2line -Cfe nectar.exe sin strip)\n",
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

	// Ventana del informe al momento, en un proceso nuevo (el launcher).
	if (sPcCrashLauncher[0]) {
		wchar_t command[2200];
		_snwprintf(command, 2200, L"\"%ls\" --crash-report \"%ls\"", sPcCrashLauncher, sPcCrashPath);
		command[2199] = 0;
		STARTUPINFOW startup = {};
		startup.cb = sizeof(startup);
		PROCESS_INFORMATION process = {};
		if (CreateProcessW(nullptr, command, nullptr, nullptr, FALSE, DETACHED_PROCESS, nullptr, nullptr, &startup,
		                   &process)) {
			CloseHandle(process.hThread);
			CloseHandle(process.hProcess);
		}
	}
	return EXCEPTION_CONTINUE_SEARCH;
}

void pc_install_crash_handler()
{
	const wchar_t* launcher = _wgetenv(L"NECTAR_LAUNCHER");
	if (launcher && wcslen(launcher) < 1024) wcscpy(sPcCrashLauncher, launcher);
	if (!_wfullpath(sPcCrashPath, L"pikmin_crash.log", 1024)) sPcCrashPath[0] = 0;

	// Sin consola (abierto desde el launcher) la salida se perdería: va a
	// pikmin.log. Si stdout ya es una tubería o un archivo (el launcher lee así
	// --settings-dump), se respeta.
	const DWORD outType    = GetFileType(GetStdHandle(STD_OUTPUT_HANDLE));
	const bool outCaptured = outType == FILE_TYPE_PIPE || outType == FILE_TYPE_DISK;
	if ((GetConsoleWindow() == nullptr && !outCaptured) || std::getenv("PIKMIN_LOG_TO_FILE")) {
		if (freopen(kPcLogFile, "w", stdout)) {
			_dup2(_fileno(stdout), _fileno(stderr));
			setvbuf(stdout, NULL, _IONBF, 0);
			setvbuf(stderr, NULL, _IONBF, 0);
		}
	}
	SetUnhandledExceptionFilter(pc_crash_filter);
	AddVectoredExceptionHandler(1, pc_intdiv_handler);
	signal(SIGABRT, pc_abort_signal);
}

#else
void pc_install_crash_handler() { }
#endif
