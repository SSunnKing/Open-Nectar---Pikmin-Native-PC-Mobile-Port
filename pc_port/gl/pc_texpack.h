#ifndef PC_TEXPACK_H
#define PC_TEXPACK_H

#include <string>
#include <vector>

#include "pc_opengl.h"

// PLAN_TEXTURAS_HD fase 1 y 2: carga de packs de texturas en formato Dolphin.
//
// pc_gfx le pasa a pc_texpack_try_upload el nombre tex1_* que acababa de
// calcular; aquí se busca en Load/Textures/<GameID>, se parsea el fichero
// (DDS BC7/BC1/BC2/BC3 via DX10 o fourCC, DDS RGBA8 descomprimido, o PNG
// decodificado a RGBA8 con stb_image) y se sube al texId ya vinculado con
// glCompressedTexImage2D cuando el driver lo permite, o con glTexImage2D para
// lo descomprimido. Todo lo que falle por cualquier razón se anota una vez y
// devuelve false: el llamador sigue con la textura original del juego. El
// juego nunca se queda sin textura.
//
// La activación se decide al arrancar (pc_texpack_init), por eso el menú F1
// pide reiniciar al cambiar de pack: el índice se construye una sola vez.

void pc_texpack_request_enable(void);
void pc_texpack_request_disable(void);
bool pc_texpack_enabled(void);
// Restringe el índice al pack `folder` (nombre de carpeta bajo
// Load/Textures/, p.ej. "GPI"); vacío = indexar todas las carpetas de
// GameID. Debe llamarse antes de pc_texpack_init().
void pc_texpack_select_pack(const char* folder);
const std::string& pc_texpack_selected_pack(void);
// Carpetas de GameID instaladas bajo Load/Textures/. Lee del disco en cada
// llamada, así que el menú refleja una instalación recién hecha.
std::vector<std::string> pc_texpack_list_packs(void);
// Escanea Load/Textures/ bajo el directorio de trabajo y comprueba el driver.
// Llamar tras crear el contexto GL y antes del primer dibujo.
void pc_texpack_init(void);
// Resumen de lo que hizo pc_texpack_init, para el menú F1: cuántas texturas
// se indexaron y si los DDS comprimidos van a la GPU tal cual o se
// descodifican en CPU (móviles sin BPTC/S3TC). El menú marca "activo" con
// solo mirar el .conf; esta línea dice si el pack de verdad está en uso.
const char* pc_texpack_status(void);
// Contadores en vivo de pc_texpack_try_upload: texturas del juego que el
// pack sustituyó, que no estaban en el índice y que fallaron al cargar.
void pc_texpack_stats(size_t* replaced, size_t* missing, size_t* failed);

// Sube la textura `name` al texId que el llamador ya tiene vinculado a
// GL_TEXTURE_2D. Devuelve true si la subió o false si no hay pack, el driver
// no lo soporta o el fichero no vale: entonces el llamador usa la original.
// glLevels/gpuBytes devuelven los niveles GL y bytes en VRAM (0 si false).
bool pc_texpack_try_upload(const char* name, GLuint texId, int* glLevels, size_t* gpuBytes);

#endif // PC_TEXPACK_H