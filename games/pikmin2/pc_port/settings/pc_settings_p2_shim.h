// Capa de compatibilidad para compilar el menu F1 (pc_settings.cpp) en el port
// de Pikmin 2. El menu se escribio contra las clases graficas de Pikmin 1
// (Colour, RectArea, Matrix4f, Font, DGXGraphics, gsys). Aqui se definen
// versiones minimas con la misma forma que dibujan con GX, igual que en
// Pikmin 1, sobre el mismo backend pc_gfx. La fuente va incrustada
// (pc_ui_font.h) porque la de Pikmin 1 salia de su disco.
#ifndef PC_SETTINGS_P2_SHIM_H
#define PC_SETTINGS_P2_SHIM_H

#include <cstddef>
#include <cstdio>

#include "types.h"
#include "Dolphin/mtx.h"

struct Colour {
    Colour()
        : r(255), g(255), b(255), a(255)
    {
    }
    Colour(int red, int green, int blue, int alpha)
        : r((u8)red), g((u8)green), b((u8)blue), a((u8)alpha)
    {
    }
    u8 r, g, b, a;
};

struct RectArea {
    RectArea()
        : mMinX(0), mMinY(0), mMaxX(0), mMaxY(0)
    {
    }
    RectArea(int x0, int y0, int x1, int y1)
        : mMinX(x0), mMinY(y0), mMaxX(x1), mMaxY(y1)
    {
    }
    int width() const { return mMaxX - mMinX; }
    int height() const { return mMaxY - mMinY; }
    int mMinX, mMinY, mMaxX, mMaxY;
};

struct Matrix4f {
    Mtx44 mMtx;
};

struct Texture {
    int mDummy;
    int mWidth  = 0; // P2 no carga texturas de P1: 0 = "sin icono"
    int mHeight = 0;
};

// Fuente monoespaciada de ancho fijo (celdas del atlas incrustado).
struct Font {
    void setTexture(Texture*, int, int) { }
    int stringWidth(const char* text) const;
};

class DGXGraphics {
public:
    DGXGraphics();
    void setColour(const Colour& colour, bool setAux);
    void setAuxColour(const Colour& colour);
    void setOrthogonal(Mtx44 mtx, const RectArea& bounds);
    void fillRectangle(const RectArea& rect);
    void texturePrintf(Font* font, int x, int y, const char* format, ...);
    // Iconos de logros (texturas de P1): en P2 no se dibujan.
    void useTexture(Texture*, int) { }
    void drawRectangle(const RectArea&, const RectArea&, void*) { }
    // El F1 de Android fija viewport y recorte; aquí el menú va en un lote
    // propio (pc_settings_p2_flush) que ya usa toda la pantalla.
    void setViewport(const RectArea&) { }
    void setScissor(const RectArea&) { }

    int mScreenWidth;
    int mScreenHeight;

private:
    Colour mPrimaryColour;
    Colour mAuxiliaryColour;
};

enum { SYSHEAP_Sys = 0 };

struct PcP2SettingsSystem {
    DGXGraphics* mDGXGfx;
    int getHeapNum() const { return 0; }
    void setHeap(int) { }
    Texture* loadTexture(const char*, bool);
};

extern PcP2SettingsSystem* gsys;

// Texto escalado de la pagina F1 (fw = ancho nominal de letra).
int pc_p2_text_width(const char* text, int fw);
void pc_p2_text(int x, int y, const char* text, Colour main, Colour shadow, int fw);

// Envia al renderer lo acumulado por el menu en este frame (llamar tras pc_settings_draw).
void pc_settings_p2_flush(void);
// Al empezar los dibujos de encima del frame (ver pc_settings.cpp).
void pc_settings_p2_begin_frame(void);

// pc_permadeath.h es de Pikmin 1 (formato de guardado propio). En Pikmin 2
// Hard y Permadeath van en la cabecera de la partida (pc_p2_rules.h).
#include "pc_p2_rules.h"
#define PC_HARDMODE_PIKI_LIMIT  (80)
#define PC_HARDMODE_DAY_MINUTES (8)
inline bool pc_hardmode_active(void) { return pc_hardmode_active_c() != 0; }

// Contadores de Pikmin 1 que lee el menu (contador de ociosos). En P2 aun no
// hay enganche: 0 hace que no se dibuje nada.
struct GameStat {
    static int freePikis;
};

// VS "Parts Race" de Pikmin 1: en P2 no existe.
inline bool pc_vs_active(void) { return false; }

// Menu de cristal del titulo (P1): en P2 no existe, F1 es la unica interfaz.
inline bool pc_glass_menu_active(void) { return false; }
inline void pc_glass_menu_input(void) { }

// Modelos HD de Pikmin 3 (P1): no aplican a P2. Las filas estan ocultas; esto
// solo deja compilar la logica compartida.
enum PcHdModelId {
    PC_HD_MODEL_OLIMAR = 0,
    PC_HD_MODEL_LOUIE,
    PC_HD_MODEL_LOUIE_HD,
    PC_HD_MODEL_PIKI_BLUE,
    PC_HD_MODEL_PIKI_RED,
    PC_HD_MODEL_PIKI_YELLOW,
    PC_HD_MODEL_HAPPA_LEAF,
    PC_HD_MODEL_HAPPA_BUD,
    PC_HD_MODEL_HAPPA_FLOWER,
    PC_HD_MODEL_BULBORB_DWARF,
    PC_HD_MODEL_BULBORB,
    PC_HD_MODEL_COUNT,
};
// Una ruta vacia representa una funcion no disponible. No devolver nullptr:
// std::filesystem::path(const char*) lo interpretaria como una cadena C y
// fallaria al generar el inventario headless que consume el launcher Fusion.
inline const char* pc_hd_model_path(PcHdModelId) { return ""; }
#include "mods/pc_hd_model_convert.h"
#include "pc_file_dialog.h"

#endif
