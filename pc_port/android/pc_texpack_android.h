#ifndef PC_TEXPACK_ANDROID_H
#define PC_TEXPACK_ANDROID_H

/**
 * @file pc_texpack_android.h
 * @brief Puente JNI de los packs de texturas (PLAN_TEXTURAS_HD, fase 2).
 *
 * El selector de archivos y la instalación viven en Java (TexturePack.java);
 * el menú F1 nativo solo pide abrirlos. Ambas funciones no bloquean: el
 * installador informa por pc_texpack_install_finished() cuando termina.
 */

#ifdef __ANDROID__

/// Abre el selector de archivos del sistema (Storage Access Framework) para
/// elegir un .zip o .rar de pack de texturas, con el juego en marcha.
void pc_texpack_android_open_picker(void);

/// Relanza la aplicación para que el pack activado se indexe al arrancar.
void pc_texpack_android_restart(void);

#endif // __ANDROID__

#endif // PC_TEXPACK_ANDROID_H