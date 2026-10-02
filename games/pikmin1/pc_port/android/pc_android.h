/**
 * @file pc_android.h
 * @brief Capa de plataforma Android (fase 4 de docs/ANDROID_PLAN.md).
 *
 * Todo lo que en escritorio da el sistema por hecho y en Android no existe:
 * una consola donde leer los printf, un directorio de trabajo con los assets
 * y un sitio donde guardar la partida.
 */
#pragma once

#ifdef __ANDROID__

/**
 * Prepara el proceso antes de tocar SDL o el juego:
 *  - redirige stdout/stderr a logcat (etiqueta "OpenNectar"), porque en
 *    Android no hay terminal y todo el diagnóstico del port es printf;
 *  - elige la carpeta del juego (almacenamiento externo privado de la app,
 *    /storage/emulated/0/Android/data/<paquete>/files) y hace chdir a ella,
 *    de modo que las rutas relativas del juego ("assets/dataDir/...",
 *    "pikmin_settings.conf") funcionan sin cambios;
 *  - fija NECTAR_SAVE_DIR a <carpeta>/save.
 * Devuelve false si no hay carpeta utilizable.
 */
bool pc_android_init();

/** Carpeta del juego elegida por pc_android_init(), o "" si aún no se llamó. */
const char* pc_android_game_dir();

/**
 * Pide que la superficie EGL tenga w×h píxeles en vez de la pantalla entera
 * (ANativeWindow_setBuffersGeometry); el compositor la escala a la pantalla.
 * Tres buffers de 3216×1440×4 son 55 MB; a 2144×960, 25. Se aplica al
 * instante y de nuevo tras volver al primer plano (pc_android_reapply_surface).
 */
void pc_android_request_surface_size(int w, int h);
void pc_android_reapply_surface();
/** Tamaño pedido, si se pidió: es el tamaño real de la superficie que ve GL. */
bool pc_android_surface_size(int* w, int* h);

#endif
