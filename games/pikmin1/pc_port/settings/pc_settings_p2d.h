#ifndef PC_SETTINGS_P2D_H
#define PC_SETTINGS_P2D_H

#include "Colour.h"

// Initialise after System::Initialise, while the permanent system heap exists.
void pc_settings_p2d_init();
bool pc_settings_p2d_active();
int pc_settings_p2d_text_width(const char* text, int fontWidth = 12);
void pc_settings_p2d_text(int x, int y, const char* text, Colour color, int fontWidth = 12, int fontHeight = 18);
// Texto con degradado vertical (top→bottom) y, si shadow, la sombra tenue
// desplazada hacia abajo de los menús del juego.
void pc_settings_p2d_text_styled(int x, int y, const char* text, Colour top, Colour bottom, int fontWidth = 12,
                                 int fontHeight = 18, bool shadow = true);
// 0: blue pause-menu plate; 1: glass options plate; 2: yellow selection;
// 3: glass plate with large bubble corners (game-mode style windows).
void pc_settings_p2d_plate(int x, int y, int w, int h, int style);
// Cursor de los menús del juego (dot_32.bti) girando sobre su eje vertical,
// con estela; angle en radianes (el juego avanza 10 rad/s).
void pc_settings_p2d_cursor(int cx, int cy, int size, float angle);
// Icono del botón del mando del juego (a/b/x/y/z_btn.bti). false si no hay.
bool pc_settings_p2d_button(int x, int y, int size, char button);
// Imagen arbitraria (Texture GX) escalada al rectángulo; u1/v1 recortan la
// textura (0..1) cuando está rellenada a múltiplo de 4. Va en la misma capa
// que las placas, así que se dibuja encima de las encoladas antes.
class Texture;
/// u0/u1: columnas de textura (0..1) que se dibujan; v1: altura.
void pc_settings_p2d_image(int x, int y, int w, int h, Texture* texture, float u1, float v1, Colour tint, float u0 = 0.0f);
void pc_settings_p2d_clear();
// Zona horizontal en la que el texto se encoge para caber (como hace la placa 0).
void pc_settings_p2d_set_content(int left, int right);

// Flush even when a submenu or confirmation dialog returns early.
struct PcSettingsP2DFrame {
    PcSettingsP2DFrame(int width, int height);
    ~PcSettingsP2DFrame();
    PcSettingsP2DFrame(const PcSettingsP2DFrame&) = delete;
    PcSettingsP2DFrame& operator=(const PcSettingsP2DFrame&) = delete;
};
#endif
