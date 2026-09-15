/**
 * @file pc_texpack_android.cpp
 * @brief Puente JNI de los packs de texturas para Android (PLAN_TEXTURAS_HD,
 * fase 2).
 *
 * El menú F1 corre en el hilo SDL del juego; el selector de archivos del
 * sistema y la instalación del pack corren en Java. Este fichero es el cable
 * entre ambos:
 *
 *  - JNI_OnLoad captura el JavaVM del proceso cuando libnectar.so (o
 *    libnectar-pal.so) se carga con System.loadLibrary en NectarActivity.
 *  - TexturePack.nativeRegisterActivity() guarda una referencia global a la
 *    actividad; desde ahí el menú F1 la llama para abrir el picker o
 *    reiniciar la app.
 *  - TexturePack.nativeInstallFinished() entrega el resultado de la
 *    instalación al menú (pc_texpack_install_finished) desde un hilo Java.
 *  - TexturePack.nativeGameDir() devuelve la carpeta del juego que eligió
 *    pc_android_init(), que es donde Java debe extraer Load/Textures/.
 */

#ifdef __ANDROID__

#include "pc_texpack_android.h"
#include "android/pc_android.h"
#include "settings/pc_settings.h"

#include <jni.h>
#include <android/log.h>
#include <cstring>

namespace {

JavaVM* gVm = nullptr;
jobject gActivity = nullptr;  // referencia global a NectarActivity

JNIEnv* jni_env()
{
	if (!gVm) return nullptr;
	JNIEnv* env = nullptr;
	const jint status = gVm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
	if (status == JNI_OK) return env;
	if (status == JNI_EDETACHED && gVm->AttachCurrentThread(&env, nullptr) == JNI_OK) return env;
	return nullptr;
}

jmethodID activity_method(JNIEnv* env, const char* name, const char* signature)
{
	if (!env || !gActivity) return nullptr;
	jclass cls = env->GetObjectClass(gActivity);
	if (!cls) return nullptr;
	jmethodID id = env->GetMethodID(cls, name, signature);
	env->DeleteLocalRef(cls);
	return id;
}

} // namespace

// El VM llama a JNI_OnLoad al cargar libnectar*.so (una de las dos versiones
// del disco). SDL2, en su propia biblioteca, tiene el suyo: no chocan.
extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* /*reserved*/)
{
	gVm = vm;
	return JNI_VERSION_1_6;
}

extern "C" JNIEXPORT void JNICALL
Java_org_opennectar_TexturePack_nativeRegisterActivity(JNIEnv* env, jclass /*clazz*/, jobject activity)
{
	if (gActivity) env->DeleteGlobalRef(gActivity);
	gActivity = activity ? env->NewGlobalRef(activity) : nullptr;
}

extern "C" JNIEXPORT void JNICALL
Java_org_opennectar_TexturePack_nativeUnregisterActivity(JNIEnv* env, jclass /*clazz*/)
{
	if (gActivity) {
		env->DeleteGlobalRef(gActivity);
		gActivity = nullptr;
	}
}

extern "C" JNIEXPORT jstring JNICALL
Java_org_opennectar_TexturePack_nativeGameDir(JNIEnv* env, jclass /*clazz*/)
{
	const char* dir = pc_android_game_dir();
	return env->NewStringUTF(dir ? dir : "");
}

extern "C" JNIEXPORT void JNICALL
Java_org_opennectar_TexturePack_nativeInstallFinished(JNIEnv* env, jclass /*clazz*/,
													  jboolean ok, jstring message)
{
	const bool success = ok != JNI_FALSE;
	const char* chars = message ? env->GetStringUTFChars(message, nullptr) : nullptr;
	const char* text = (chars && chars[0]) ? chars : (success ? "Pack instalado." : "No se pudo instalar el pack.");
	pc_texpack_install_finished(success, text);
	if (chars) env->ReleaseStringUTFChars(message, chars);
}

extern "C" JNIEXPORT void JNICALL
Java_org_opennectar_TexturePack_nativeInstallProgress(JNIEnv* /*env*/, jclass /*clazz*/, jint files)
{
	pc_texpack_install_progress(static_cast<int>(files));
}

void pc_texpack_android_open_picker(void)
{
	JNIEnv* env = jni_env();
	jmethodID open = activity_method(env, "openTexturePackPicker", "()V");
	if (env && open && gActivity) env->CallVoidMethod(gActivity, open);
}

void pc_texpack_android_restart(void)
{
	JNIEnv* env = jni_env();
	jmethodID restart = activity_method(env, "restartTexturePacks", "()V");
	if (env && restart && gActivity) env->CallVoidMethod(gActivity, restart);
	// No esperar a que Java muera el proceso: el juego se detiene en breve.
}

#endif // __ANDROID__