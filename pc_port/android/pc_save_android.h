#ifndef PC_SAVE_ANDROID_H
#define PC_SAVE_ANDROID_H

/**
 * @file pc_save_android.h
 * @brief Puente JNI de la copia de seguridad de partidas (issue #36).
 *
 * El menú F1 pide exportar o importar la tarjeta de memoria; el trabajo real
 * (escribir/leer un .zip en el documento que elija el jugador) vive en Java
 * (SaveTransfer.java) sobre el Storage Access Framework, sin permisos de
 * almacenamiento. El resultado llega por pc_save_transfer_finished().
 */

#ifdef __ANDROID__

/// Abre el selector del sistema para exportar la partida a un .zip.
void pc_save_android_open_backup(void);

/// Abre el selector del sistema para importar una partida desde un .zip.
void pc_save_android_open_restore(void);

#endif // __ANDROID__

#endif // PC_SAVE_ANDROID_H
