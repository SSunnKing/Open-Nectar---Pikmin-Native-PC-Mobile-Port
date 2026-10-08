/**
 * @file pc_save_android.cpp
 * @brief Puente JNI de la copia de seguridad de partidas (issue #36).
 *
 * La tarjeta vive en <carpeta del juego>/save. El menú F1 sólo pide abrir los
 * selectores de exportar/importar; el .zip lo escribe y lee Java. Este fichero
 * es el cable: reutiliza el JavaVM y la actividad capturados por el puente de
 * los packs de texturas (un único JNI_OnLoad por biblioteca) y entrega el
 * resultado al menú con pc_save_transfer_finished().
 */

#ifdef __ANDROID__

#include "pc_save_android.h"
#include "android/pc_android.h"
#include "android/pc_texpack_android.h"
#include "settings/pc_settings.h"

#include <jni.h>
#include <cstring>
#include <string>

namespace {

JNIEnv* jni_env()
{
	JavaVM* vm = pc_android_jni_vm();
	if (!vm) return nullptr;
	JNIEnv* env = nullptr;
	const jint status = vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
	if (status == JNI_OK) return env;
	if (status == JNI_EDETACHED && vm->AttachCurrentThread(&env, nullptr) == JNI_OK) return env;
	return nullptr;
}

// Llama a un metodo sin argumentos de NectarActivity desde el hilo del juego.
void call_activity(const char* method)
{
	JNIEnv* env = jni_env();
	jobject activity = pc_android_jni_activity();
	if (!env || !activity) return;
	jclass cls = env->GetObjectClass(activity);
	if (!cls) return;
	jmethodID id = env->GetMethodID(cls, method, "()V");
	env->DeleteLocalRef(cls);
	if (id) env->CallVoidMethod(activity, id);
}

} // namespace

void pc_save_android_open_backup(void)
{
	call_activity("openSaveBackupPicker");
}

void pc_save_android_open_restore(void)
{
	call_activity("openSaveRestorePicker");
}

extern "C" JNIEXPORT jstring JNICALL
Java_org_opennectar_SaveTransfer_nativeSaveDir(JNIEnv* env, jclass /*clazz*/)
{
	const char* dir = pc_android_game_dir();
	const std::string save = dir ? std::string(dir) + "/save" : std::string();
	return env->NewStringUTF(save.c_str());
}

extern "C" JNIEXPORT void JNICALL
Java_org_opennectar_SaveTransfer_nativeSaveTransferFinished(JNIEnv* env, jclass /*clazz*/,
														  jboolean ok, jstring message)
{
	const bool success = ok != JNI_FALSE;
	const char* chars = message ? env->GetStringUTFChars(message, nullptr) : nullptr;
	const char* text = (chars && chars[0]) ? chars : (success ? "Done." : "Save transfer failed.");
	pc_save_transfer_finished(success, text);
	if (chars) env->ReleaseStringUTFChars(message, chars);
}

#endif // __ANDROID__
