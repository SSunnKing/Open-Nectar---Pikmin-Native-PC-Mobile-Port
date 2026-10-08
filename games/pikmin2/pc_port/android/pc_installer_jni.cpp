/**
 * @file pc_installer_jni.cpp
 * @brief Instalador Android (fase 7 de docs/ANDROID_PLAN.md): puente JNI
 * sobre la biblioteca de extracción del launcher de escritorio.
 *
 * La interfaz vive en Java (InstallerActivity): pide la imagen con el
 * selector de archivos del sistema y recibe un descriptor. Aquí ese
 * descriptor se envuelve en un std::istream (FdStream) del que
 * gamecube_image.cpp lee como de cualquier fichero, y se extrae a
 * <files>/assets. Ni se copia la imagen ni se necesita permiso de
 * almacenamiento.
 */
#include <jni.h>
#include <android/log.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>
#include <sys/stat.h>
#include <cerrno>
#include <cstring>
#include <algorithm>
#include <streambuf>

#include "gamecube_image.h"
#include "asset_finalize.h"

namespace fs = std::filesystem;
using namespace pikmin::launcher;

namespace {

constexpr const char* kTag = "OpenNectar";

// std::istream sobre un descriptor de fichero. El selector de archivos
// (Storage Access Framework) entrega un descriptor que no se puede reabrir
// por ruta (/proc/self/fd/N falla con "Could not open the disc image"), así
// que el extractor lee de él a través de este búfer: pread() posicionado,
// sin estado compartido con nadie.
class FdStreamBuf : public std::streambuf {
public:
	explicit FdStreamBuf(int fd) : mFd(fd) { setg(mBuffer, mBuffer, mBuffer); }

	std::uint64_t size() const
	{
		struct stat st;
		return fstat(mFd, &st) == 0 ? (std::uint64_t)st.st_size : 0;
	}

protected:
	pos_type seekoff(off_type off, std::ios_base::seekdir dir, std::ios_base::openmode) override
	{
		std::int64_t base = 0;
		if (dir == std::ios_base::cur) base = (std::int64_t)mPos - (egptr() - gptr());
		else if (dir == std::ios_base::end) base = (std::int64_t)size();
		return seekpos(pos_type(base + off), std::ios_base::in);
	}
	pos_type seekpos(pos_type pos, std::ios_base::openmode) override
	{
		if ((std::int64_t)pos < 0) return pos_type(off_type(-1));
		mPos = (std::uint64_t)pos;
		setg(mBuffer, mBuffer, mBuffer); // descartar lo leído por adelantado
		return pos;
	}
	int_type underflow() override
	{
		if (gptr() < egptr()) return traits_type::to_int_type(*gptr());
		ssize_t n;
		do n = pread(mFd, mBuffer, sizeof mBuffer, (off_t)mPos);
		while (n < 0 && errno == EINTR);
		if (n <= 0) return traits_type::eof();
		mPos += (std::uint64_t)n;
		setg(mBuffer, mBuffer, mBuffer + n);
		return traits_type::to_int_type(*gptr());
	}
	std::streamsize xsgetn(char* dst, std::streamsize count) override
	{
		std::streamsize done = 0;
		// Lo que quede en el búfer, y el resto directo del fichero.
		const std::streamsize buffered = egptr() - gptr();
		if (buffered > 0) {
			const std::streamsize take = std::min(buffered, count);
			memcpy(dst, gptr(), (size_t)take);
			gbump((int)take);
			done += take;
		}
		while (done < count) {
			ssize_t n;
			do n = pread(mFd, dst + done, (size_t)(count - done), (off_t)mPos);
			while (n < 0 && errno == EINTR);
			if (n <= 0) break;
			mPos += (std::uint64_t)n;
			done += n;
		}
		return done;
	}

private:
	int mFd;
	std::uint64_t mPos = 0; // posición del fichero tras el búfer
	char mBuffer[64 * 1024];
};

class FdStream : public std::istream {
public:
	explicit FdStream(int fd) : std::istream(&mBuf), mBuf(fd) {}
	std::uint64_t size() const { return mBuf.size(); }
private:
	FdStreamBuf mBuf;
};

struct Progress {
	JNIEnv* env;
	jobject listener;
	jmethodID onProgress;
	std::uint32_t lastPercent = 101;
	void report(std::uint32_t percent, const std::string& text)
	{
		if (percent == lastPercent) return;
		lastPercent = percent;
		// Solo ASCII: el disco trae nombres con bytes fuera de UTF-8 (Shift-JIS
		// en los archivos japoneses) y NewStringUTF aborta el proceso con ellos.
		std::string ascii = text;
		for (char& c : ascii)
			if ((unsigned char)c < 0x20 || (unsigned char)c > 0x7e) c = '?';
		jstring jtext = env->NewStringUTF(ascii.c_str());
		env->CallVoidMethod(listener, onProgress, (jint)percent, jtext);
		env->DeleteLocalRef(jtext);
	}
};

jstring fail(JNIEnv* env, const std::string& message)
{
	__android_log_print(ANDROID_LOG_ERROR, kTag, "[installer] %s", message.c_str());
	std::string ascii = message;
	for (char& c : ascii)
		if (((unsigned char)c < 0x20 && c != '\n') || (unsigned char)c > 0x7e) c = '?';
	return env->NewStringUTF(ascii.c_str());
}

} // namespace

extern "C" {

/**
 * chdir a la carpeta del juego ANTES de cargar libnectar.so: el juego lee el
 * idioma (pikmin_settings.conf) desde un inicializador estático, es decir, al
 * cargar la biblioteca y antes de que pc_android_init() pueda cambiar de
 * directorio. Sin esto el idioma elegido en F1 nunca se aplicaba al
 * reiniciar. Devuelve false si el directorio no existe.
 */
JNIEXPORT jboolean JNICALL
Java_org_opennectar_Installer_nativeChdir(JNIEnv* env, jclass, jstring jDir)
{
	const char* dir = env->GetStringUTFChars(jDir, nullptr);
	const bool ok = chdir(dir) == 0;
	if (!ok) __android_log_print(ANDROID_LOG_ERROR, kTag, "[installer] chdir(%s): %s", dir, strerror(errno));
	else __android_log_print(ANDROID_LOG_INFO, kTag, "[installer] chdir(%s) ok; settings file %s", dir,
	                         access("pikmin_settings.conf", R_OK) == 0 ? "present" : "absent");
	env->ReleaseStringUTFChars(jDir, dir);
	return ok ? JNI_TRUE : JNI_FALSE;
}

/**
 * Inspecciona la imagen y devuelve su descripción ("Pikmin USA Rev. 1"), o
 * null si no es un disco de Pikmin admitido; en ese caso `error[0]` recibe el
 * motivo.
 */
JNIEXPORT jstring JNICALL
Java_org_opennectar_Installer_nativeInspect(JNIEnv* env, jclass, jint fd, jobjectArray errorOut)
{
	FdStream input(fd);
	DiscIdentity identity;
	std::string error;
	if (!inspectGameCubeImage(input, identity, error) || !isSupportedPikminDisc(identity, error)) {
		env->SetObjectArrayElement(errorOut, 0, env->NewStringUTF(error.c_str()));
		return nullptr;
	}
	const KnownDisc* disc = findKnownDisc(identity);
	const std::string description = disc ? disc->description : identity.gameId;
	__android_log_print(ANDROID_LOG_INFO, kTag, "[installer] disc: %s (%s rev %u)",
	                    description.c_str(), identity.gameId.c_str(), unsigned(identity.revision));
	return env->NewStringUTF(description.c_str());
}

/** Nombre base de la biblioteca del juego que necesita el disco ("nectar" o
 *  "nectar-pal"), o null si no se reconoce. */
JNIEXPORT jstring JNICALL
Java_org_opennectar_Installer_nativeRequiredBuild(JNIEnv* env, jclass, jint fd)
{
	FdStream input(fd);
	DiscIdentity identity;
	std::string error;
	if (!inspectGameCubeImage(input, identity, error)) return nullptr;
	const KnownDisc* disc = findKnownDisc(identity);
	return (disc && disc->executable) ? env->NewStringUTF(disc->executable) : nullptr;
}

/**
 * Extrae la imagen a <gameDir>/assets. Devuelve null si todo fue bien o un
 * mensaje de error. `listener.onProgress(int percent, String text)` recibe el
 * avance. Se ejecuta en el hilo que la llama (Java la lanza en segundo plano).
 */
JNIEXPORT jstring JNICALL
Java_org_opennectar_Installer_nativeInstall(JNIEnv* env, jclass, jint fd, jstring jGameDir, jobject listener)
{
	const char* gameDirChars = env->GetStringUTFChars(jGameDir, nullptr);
	const fs::path gameDir = gameDirChars;
	env->ReleaseStringUTFChars(jGameDir, gameDirChars);
	FdStream image(fd);

	jclass listenerClass = env->GetObjectClass(listener);
	Progress progress { env, listener, env->GetMethodID(listenerClass, "onProgress", "(ILjava/lang/String;)V") };

	DiscIdentity identity;
	std::string error;
	if (!inspectGameCubeImage(image, identity, error) || !isSupportedPikminDisc(identity, error))
		return fail(env, error);
	const KnownDisc* disc = findKnownDisc(identity);
	const std::string requiredBuild = (disc && disc->executable) ? disc->executable : "nectar";

	const fs::path finalAssets = gameDir / "assets";
	const fs::path partialAssets = gameDir / ("assets.partial." + std::to_string(getpid()));
	std::error_code ec;
	fs::create_directories(gameDir, ec);
	fs::remove_all(partialAssets, ec); // restos de un intento anterior interrumpido
	if (fs::exists(finalAssets)) {
		// Una instalación previa incompleta (sin marcador) se descarta; una
		// completa no se toca: la actividad no debería haber llegado aquí.
		if (fs::is_regular_file(finalAssets / ".pikmin-assets")) return fail(env, "The game data is already installed.");
		fs::remove_all(finalAssets, ec);
	}

	const bool extracted = extractGameCubeImage(
	    image, image.size(), partialAssets, error,
	    [&](std::uint32_t current, std::uint32_t total, const std::string& path) {
		    progress.report(total ? current * 100 / total : 100, path);
	    });
	if (!extracted) {
		fs::remove_all(partialAssets, ec);
		return fail(env, "Extraction failed: " + error);
	}
	if (!fs::is_regular_file(partialAssets / "dataDir/parms/gamePrms.bin")) {
		fs::remove_all(partialAssets, ec);
		return fail(env, "The image does not contain the expected dataDir tree.");
	}
	{
		std::ofstream marker(partialAssets / ".pikmin-assets", std::ios::trunc);
		marker << identity.gameId << " revision=" << unsigned(identity.revision) << '\n';
		std::ofstream buildMarker(partialAssets / ".pikmin-build", std::ios::trunc);
		buildMarker << requiredBuild << '\n';
	}
	progress.report(100, "Finishing installation...");
	ec = finalizeAssets(partialAssets, finalAssets);
	if (ec) return fail(env, "Could not finish the installation: " + ec.message());
	__android_log_print(ANDROID_LOG_INFO, kTag, "[installer] game data installed in %s", finalAssets.c_str());
	return nullptr;
}

} // extern "C"
