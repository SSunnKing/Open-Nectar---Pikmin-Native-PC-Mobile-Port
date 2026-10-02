#ifdef __ANDROID__
#include "pc_android.h"

#include <SDL.h>
#include <SDL_syswm.h>
#include <android/log.h>
#include <android/native_window.h>
#include <pthread.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sched.h>
#include <vector>

namespace {

constexpr const char* kLogTag = "OpenNectar";
std::string sGameDir;

// Un hilo lee de una tubería a la que apuntan stdout y stderr y vuelca cada
// línea a logcat. Es el truco habitual: el port informa de todo por printf y
// sin esto en Android no se ve nada.
void* logcatPump(void* arg)
{
	const int fd = static_cast<int>(reinterpret_cast<intptr_t>(arg));
	std::string line;
	char buf[512];
	for (;;) {
		const ssize_t n = read(fd, buf, sizeof buf);
		if (n <= 0) {
			if (n < 0 && errno == EINTR) continue;
			break;
		}
		for (ssize_t i = 0; i < n; ++i) {
			if (buf[i] == '\n') {
				__android_log_write(ANDROID_LOG_INFO, kLogTag, line.c_str());
				line.clear();
			} else {
				line.push_back(buf[i]);
			}
		}
	}
	if (!line.empty()) __android_log_write(ANDROID_LOG_INFO, kLogTag, line.c_str());
	return nullptr;
}

bool redirectStdioToLogcat()
{
	int fds[2];
	if (pipe(fds) != 0) return false;
	// Sin búfer: el orden entre stdout y stderr se conserva y una caída deja
	// escrito hasta el último mensaje.
	setvbuf(stdout, nullptr, _IONBF, 0);
	setvbuf(stderr, nullptr, _IONBF, 0);
	dup2(fds[1], STDOUT_FILENO);
	dup2(fds[1], STDERR_FILENO);
	close(fds[1]);
	pthread_t thread;
	if (pthread_create(&thread, nullptr, logcatPump, reinterpret_cast<void*>(static_cast<intptr_t>(fds[0]))) != 0)
		return false;
	pthread_detach(thread);
	return true;
}

bool ensureDir(const std::string& path)
{
	struct stat st;
	if (stat(path.c_str(), &st) == 0) return S_ISDIR(st.st_mode);
	return mkdir(path.c_str(), 0700) == 0;
}

// El hilo que llama (SDL_main: lógica del juego y todo el GL) acaba, si se
// deja al planificador, en el cluster pequeño: medido en un SM8550 a
// 0,7–1,7 GHz en cpu0–2 mientras el prime estaba libre (PLAN_RENDIMIENTO.md,
// fase 0). Se fija a los núcleos que no son del cluster más lento —leyendo
// cpuinfo_max_freq, sin tabla de SoCs— y se sube su prioridad como hace
// Android con el hilo de dibujo. PIKMIN_BIG_CORES=0 lo desactiva para
// comparar.
void pinGameThreadToBigCores()
{
	const char* flag = getenv("PIKMIN_BIG_CORES");
	if (flag && flag[0] == '0') {
		printf("[Android] game thread: affinity left to the scheduler (PIKMIN_BIG_CORES=0)\n");
		return;
	}
	const long ncpu = sysconf(_SC_NPROCESSORS_CONF);
	if (ncpu <= 1 || ncpu > CPU_SETSIZE) return;

	std::vector<long> maxFreq(ncpu, 0);
	long slowest = 0;
	for (long i = 0; i < ncpu; i++) {
		char path[128];
		snprintf(path, sizeof path, "/sys/devices/system/cpu/cpu%ld/cpufreq/cpuinfo_max_freq", i);
		if (FILE* f = fopen(path, "r")) {
			if (fscanf(f, "%ld", &maxFreq[i]) != 1) maxFreq[i] = 0;
			fclose(f);
		}
		if (maxFreq[i] > 0 && (slowest == 0 || maxFreq[i] < slowest)) slowest = maxFreq[i];
	}
	cpu_set_t set;
	CPU_ZERO(&set);
	int big = 0;
	for (long i = 0; i < ncpu; i++) {
		if (maxFreq[i] > slowest) {
			CPU_SET(i, &set);
			big++;
		}
	}
	// Un SoC homogéneo (o sin cpufreq legible) no tiene cluster grande: nada
	// que fijar, que el planificador haga lo suyo.
	if (big == 0 || big == ncpu) {
		printf("[Android] game thread: %ld cpus, no big cluster detected; affinity untouched\n", ncpu);
	} else if (sched_setaffinity(0, sizeof set, &set) != 0) {
		printf("[Android] game thread: sched_setaffinity failed: %s\n", strerror(errno));
	} else {
		std::string list;
		for (long i = 0; i < ncpu; i++) {
			if (CPU_ISSET(i, &set)) list += (list.empty() ? "" : ",") + std::to_string(i);
		}
		printf("[Android] game thread: pinned to cpus %s (slowest cluster %ld kHz excluded)\n", list.c_str(), slowest);
	}
	// -8 es THREAD_PRIORITY_URGENT_DISPLAY: lo que Android da a su propio hilo
	// de UI/render. Una app puede bajar el nice de sus hilos sin permisos.
	const pid_t tid = static_cast<pid_t>(syscall(SYS_gettid));
	if (setpriority(PRIO_PROCESS, tid, -8) != 0) {
		printf("[Android] game thread: setpriority failed: %s\n", strerror(errno));
	} else {
		printf("[Android] game thread: nice %d\n", getpriority(PRIO_PROCESS, tid));
	}
}

} // namespace

bool pc_android_init()
{
	redirectStdioToLogcat();

	// Dos carpetas candidatas. La externa privada de la app
	// (/storage/emulated/0/Android/data/<paquete>/files) es la que un gestor
	// de archivos puede llenar; pero lo que se sube con `adb push` queda con
	// dueño `shell` y la app no puede leerlo (Android 11+). La interna
	// (/data/data/<paquete>/files) siempre es legible y se llena con
	// `adb shell run-as <paquete> cp`. Se elige la primera que tenga los
	// assets; si ninguna los tiene, la externa si está montada.
	const char* external = SDL_AndroidGetExternalStoragePath();
	const char* internal = SDL_AndroidGetInternalStoragePath();
	if (external && !(SDL_AndroidGetExternalStorageState() & SDL_ANDROID_EXTERNAL_STORAGE_WRITE))
		external = nullptr;
	auto hasAssets = [](const char* dir) {
		if (!dir) return false;
		struct stat st;
		const std::string path = std::string(dir) + "/assets/dataDir";
		return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
	};
	const char* chosen = hasAssets(external) ? external : hasAssets(internal) ? internal : external ? external : internal;
	if (!chosen) {
		printf("[Android] no storage path available: %s\n", SDL_GetError());
		return false;
	}
	sGameDir = chosen;
	if (!ensureDir(sGameDir) || chdir(sGameDir.c_str()) != 0) {
		printf("[Android] cannot use game dir %s: %s\n", sGameDir.c_str(), strerror(errno));
		return false;
	}
	const std::string save = sGameDir + "/save";
	ensureDir(save);
	setenv("NECTAR_SAVE_DIR", save.c_str(), 1);

	printf("[Android] game dir: %s\n", sGameDir.c_str());

	// <carpeta>/env.txt: una variable de entorno por línea (CLAVE=valor).
	// Todos los interruptores de diagnóstico y rendimiento del port son
	// getenv (PIKMIN_TICK_STATS, PIKMIN_RENDER_SCALE, ...), y en Android no
	// hay shell desde la que fijarlos: este fichero es esa shell.
	if (FILE* env = fopen("env.txt", "r")) {
		char line[512];
		while (fgets(line, sizeof line, env)) {
			char* eq = strchr(line, '=');
			if (!eq || line[0] == '#') continue;
			*eq = '\0';
			char* value = eq + 1;
			value[strcspn(value, "\r\n")] = '\0';
			setenv(line, value, 1);
			printf("[Android] env.txt: %s=%s\n", line, value);
		}
		fclose(env);
	}
	pinGameThreadToBigCores();
	if (!hasAssets(chosen)) {
		printf("[Android] assets/dataDir not found (%s). Copy the extracted disc with:\n"
		       "  adb push assets /data/local/tmp/nectar-assets\n"
		       "  adb shell run-as org.opennectar cp -r /data/local/tmp/nectar-assets files/assets\n",
		       strerror(errno));
	}
	return true;
}

const char* pc_android_game_dir() { return sGameDir.c_str(); }

static int sSurfaceW = 0, sSurfaceH = 0;

static void applySurfaceGeometry()
{
	if (sSurfaceW <= 0 || sSurfaceH <= 0) return;
	SDL_Window* sdlWindow = SDL_GL_GetCurrentWindow();
	if (!sdlWindow) return;
	SDL_SysWMinfo info;
	SDL_VERSION(&info.version);
	if (!SDL_GetWindowWMInfo(sdlWindow, &info) || !info.info.android.window) return;
	const int rc = ANativeWindow_setBuffersGeometry(info.info.android.window, sSurfaceW, sSurfaceH, 0);
	printf("[Android] surface buffers set to %dx%d (%s)\n", sSurfaceW, sSurfaceH, rc == 0 ? "ok" : "failed");
	if (rc != 0) sSurfaceW = sSurfaceH = 0;
}

void pc_android_request_surface_size(int w, int h)
{
	if (const char* v = getenv("PIKMIN_NATIVE_SURFACE")) {
		if (v[0] == '0') { sSurfaceW = sSurfaceH = 0; return; }
	}
	sSurfaceW = w;
	sSurfaceH = h;
	applySurfaceGeometry();
}

void pc_android_reapply_surface() { applySurfaceGeometry(); }

bool pc_android_surface_size(int* w, int* h)
{
	if (sSurfaceW <= 0 || sSurfaceH <= 0) return false;
	if (w) *w = sSurfaceW;
	if (h) *h = sSurfaceH;
	return true;
}

#endif // __ANDROID__
