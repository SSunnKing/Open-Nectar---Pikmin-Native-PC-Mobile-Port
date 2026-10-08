#ifndef PC_ART_H
#define PC_ART_H

/* Arte del port (pc_port/touch/assets/art/<nombre>.png, empaquetado en el
   APK como art/<nombre>.png) cargado como Texture GX (RGBA8 en tiles) para
   dibujarlo con Graphics::drawRectangle o meterlo en un pane del HUD. Se
   cachea por nombre; nullptr si no existe. */
class Texture;

Texture* pc_art_texture(const char* name);
/// Tamaño real de la imagen (la textura se rellena a múltiplo de 4).
bool pc_art_size(const char* name, int* width, int* height);

#endif // PC_ART_H
