#ifndef PC_SETTINGS_P2D_H
#define PC_SETTINGS_P2D_H

#include "Colour.h"

// Initialise after System::Initialise, while the permanent system heap exists.
void pc_settings_p2d_init();
bool pc_settings_p2d_active();
int pc_settings_p2d_text_width(const char* text, int fontWidth = 12);
void pc_settings_p2d_text(int x, int y, const char* text, Colour color, int fontWidth = 12, int fontHeight = 18);
// 0: blue pause-menu plate; 1: glass options plate; 2: yellow selection.
void pc_settings_p2d_plate(int x, int y, int w, int h, int style);
// Imagen arbitraria (Texture GX) escalada al rectángulo; u1/v1 recortan la
// textura (0..1) cuando está rellenada a múltiplo de 4. Va en la misma capa
// que las placas, así que se dibuja encima de las encoladas antes.
class Texture;
void pc_settings_p2d_image(int x, int y, int w, int h, Texture* texture, float u1, float v1, Colour tint);
void pc_settings_p2d_clear();

// Flush even when a submenu or confirmation dialog returns early.
struct PcSettingsP2DFrame {
    PcSettingsP2DFrame(int width, int height);
    ~PcSettingsP2DFrame();
    PcSettingsP2DFrame(const PcSettingsP2DFrame&) = delete;
    PcSettingsP2DFrame& operator=(const PcSettingsP2DFrame&) = delete;
};
#endif
