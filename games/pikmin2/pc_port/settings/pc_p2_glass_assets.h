#ifndef PC_P2_GLASS_ASSETS_H
#define PC_P2_GLASS_ASSETS_H

// Assets de Pikmin 2 para el menú F1 "de cristal": texturas de sus pantallas
// (placa de cristal, degradado, burbujas, iconos de botón) y los glifos de su
// fuente, decodificados a un atlas RGBA para el lote de UI del port
// (pc_gfx_ui_draw con tex=2). Nada de esto viene de Pikmin 1.

enum PcGlassTex {
    PCG_Glass,    // w08_160.bti: placa de cristal del menú de pausa
    PCG_Gradient, // w08_160_gra.bti: degradado para placas de color teñidas
    PCG_Dot24,    // dot_24.bti: burbuja pequeña
    PCG_Dot32,    // dot_32.bti: burbuja del cursor
    PCG_BtnA,     // a_48.bti
    PCG_BtnB,     // b_48.bti
    PCG_BtnX,     // x_btn.bti
    PCG_BtnY,     // y_btn.bti
    PCG_BtnZ,     // z_btn.bti
    PCG_COUNT
};

struct PcGlassRect {
    int x, y, w, h; // en píxeles del atlas; w == 0 si no se cargó
};

struct PcGlassGlyph {
    short x, y;          // celda en el atlas
    short offset, width; // kerning de entrada y ancho de avance (píxeles de celda)
    bool valid;
};

struct PcGlassAtlas {
    const unsigned char* rgba;
    int w, h;
    unsigned generation;
    PcGlassRect tex[PCG_COUNT];
    PcGlassGlyph glyphs[128]; // ASCII
    int cellW, cellH;         // celda de la fuente
    int ascent, descent;
    bool hasFont;
};

// Carga perezosa (la primera vez que se pide). nullptr si los archivos del
// juego todavía no están disponibles; se reintenta en la siguiente llamada.
const PcGlassAtlas* pc_p2_glass_atlas();

#endif
