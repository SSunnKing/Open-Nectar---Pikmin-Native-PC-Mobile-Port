// Carga de los assets de Pikmin 2 para el menú F1 de cristal (ver
// pc_p2_glass_assets.h). Lee los .szs del juego con su lector de disco a un
// búfer propio (descomprimido) y los interpreta aquí, en big-endian: no se
// montan, porque si el juego ya tiene el archivo montado JKRMountArchive
// devuelve su copia, que el juego ya ha convertido al orden del host.
// Texturas de pantallas y la fuente pikmin2main.bfn.
#include "settings/pc_p2_glass_assets.h"

#include "JSystem/JKernel/JKRDvdRipper.h"
#include "JSystem/JKernel/JKRHeap.h"
#include "Dolphin/gx.h"

#include <cstdio>
#include <cstring>

namespace {

constexpr int kAtlasW = 512;
constexpr int kAtlasH = 512;
unsigned char sAtlas[kAtlasW * kAtlasH * 4];
PcGlassAtlas sInfo;
bool sTexturesDone = false;
bool sFontDone     = false;
int sCursorX = 0, sCursorY = 0, sRowH = 0; // empaquetado por filas

void put(int x, int y, unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
    if (x < 0 || y < 0 || x >= kAtlasW || y >= kAtlasH)
        return;
    unsigned char* p = sAtlas + (y * kAtlasW + x) * 4;
    p[0] = r; p[1] = g; p[2] = b; p[3] = a;
}

bool reserve(int w, int h, int& outX, int& outY)
{
    // 1 píxel de margen para que el filtrado lineal no mezcle vecinos.
    if (sCursorX + w + 1 > kAtlasW) {
        sCursorX = 0;
        sCursorY += sRowH + 1;
        sRowH = 0;
    }
    if (sCursorY + h + 1 > kAtlasH)
        return false;
    outX = sCursorX;
    outY = sCursorY;
    sCursorX += w + 1;
    if (h > sRowH)
        sRowH = h;
    return true;
}

// Decodifica una imagen GX (formatos de intensidad y color directos) en el
// atlas, en (ox, oy). Formatos con paleta no hacen falta aquí.
bool decodeGX(const unsigned char* src, int fmt, int w, int h, int ox, int oy)
{
    int bw, bh, bpp; // tamaño de bloque y bytes por bloque
    switch (fmt) {
    case GX_TF_I4:     bw = 8; bh = 8; bpp = 32; break;
    case GX_TF_I8:     bw = 8; bh = 4; bpp = 32; break;
    case GX_TF_IA4:    bw = 8; bh = 4; bpp = 32; break;
    case GX_TF_IA8:    bw = 4; bh = 4; bpp = 32; break;
    case GX_TF_RGB5A3: bw = 4; bh = 4; bpp = 32; break;
    case GX_TF_RGBA8:  bw = 4; bh = 4; bpp = 64; break;
    default: return false;
    }
    const unsigned char* p = src;
    for (int by = 0; by < h; by += bh) {
        for (int bx = 0; bx < w; bx += bw) {
            for (int i = 0; i < bw * bh; i++) {
                const int x = bx + i % bw, y = by + i / bw;
                unsigned char r = 0, g = 0, b = 0, a = 0;
                switch (fmt) {
                case GX_TF_I4: {
                    const unsigned char v = (i & 1) ? (p[i / 2] & 0xF) : (p[i / 2] >> 4);
                    r = g = b = a = (unsigned char)(v * 17);
                    break;
                }
                case GX_TF_I8: r = g = b = a = p[i]; break;
                case GX_TF_IA4:
                    a = (unsigned char)((p[i] >> 4) * 17);
                    r = g = b = (unsigned char)((p[i] & 0xF) * 17);
                    break;
                case GX_TF_IA8:
                    a = p[i * 2];
                    r = g = b = p[i * 2 + 1];
                    break;
                case GX_TF_RGB5A3: {
                    const unsigned v = (p[i * 2] << 8) | p[i * 2 + 1];
                    if (v & 0x8000) {
                        r = (unsigned char)(((v >> 10) & 31) * 255 / 31);
                        g = (unsigned char)(((v >> 5) & 31) * 255 / 31);
                        b = (unsigned char)((v & 31) * 255 / 31);
                        a = 255;
                    } else {
                        a = (unsigned char)(((v >> 12) & 7) * 255 / 7);
                        r = (unsigned char)(((v >> 8) & 15) * 17);
                        g = (unsigned char)(((v >> 4) & 15) * 17);
                        b = (unsigned char)((v & 15) * 17);
                    }
                    break;
                }
                case GX_TF_RGBA8:
                    a = p[i * 2];
                    r = p[i * 2 + 1];
                    g = p[32 + i * 2];
                    b = p[32 + i * 2 + 1];
                    break;
                }
                if (x < w && y < h)
                    put(ox + x, oy + y, r, g, b, a);
            }
            p += bpp;
        }
    }
    return true;
}

unsigned be16(const unsigned char* p) { return (p[0] << 8) | p[1]; }
unsigned be32(const unsigned char* p) { return ((unsigned)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }

// Archivo RARC en memoria (big-endian): busca un fichero por nombre.
const unsigned char* rarcFind(const unsigned char* d, const char* name, unsigned* sizeOut)
{
    if (!d || std::memcmp(d, "RARC", 4) != 0)
        return nullptr;
    const unsigned dataOff = be32(d + 0xC) + 0x20;
    const unsigned char* info = d + 0x20;
    const unsigned numEnt = be32(info + 8), entOff = be32(info + 12) + 0x20, strOff = be32(info + 20) + 0x20;
    for (unsigned i = 0; i < numEnt; i++) {
        const unsigned char* e = d + entOff + i * 20;
        const unsigned type = be16(e + 4);
        if ((type >> 8) & 0x2)
            continue; // carpeta
        const char* n = reinterpret_cast<const char*>(d + strOff + be16(e + 6));
        if (std::strcmp(n, name) == 0) {
            if (sizeOut)
                *sizeOut = be32(e + 12);
            return d + dataOff + be32(e + 8);
        }
    }
    return nullptr;
}

void* loadArchive(const char* path)
{
    return JKRDvdRipper::loadToMainRAM(path, nullptr, Switch_1, 0, JKRHeap::sSystemHeap, JKRDvdRipper::ALLOC_DIR_BOTTOM, 0,
                                       nullptr, nullptr);
}

void freeArchive(void* data)
{
    if (data)
        JKRHeap::free(data, JKRHeap::sSystemHeap);
}

bool addTexture(const unsigned char* arc, const char* name, PcGlassTex slot)
{
    const unsigned char* bti = rarcFind(arc, name, nullptr);
    if (!bti)
        return false;
    const int fmt = bti[0], w = be16(bti + 2), h = be16(bti + 4);
    unsigned off = be32(bti + 0x1C);
    if (off == 0)
        off = 0x20;
    int x, y;
    if (!reserve(w, h, x, y))
        return false;
    if (!decodeGX(bti + off, fmt, w, h, x, y))
        return false;
    sInfo.tex[slot] = { x, y, w, h };
    if (slot == PCG_Gradient) {
        // Del degradado solo interesa su forma redondeada (alfa): el color lo
        // pone el tinte de cada placa. Su blanco→negro oscurecía la mitad baja.
        for (int yy = 0; yy < h; yy++) {
            for (int xx = 0; xx < w; xx++) {
                unsigned char* px = sAtlas + ((y + yy) * kAtlasW + (x + xx)) * 4;
                px[0] = px[1] = px[2] = 255;
            }
        }
    }
    return true;
}

struct ArcTextures {
    const char* archive;
    const char* files[4];
    PcGlassTex slots[4];
};

void loadTextures()
{
    // Cada textura sale del primer archivo de pantallas que la trae.
    static const ArcTextures kList[] = {
        { "/new_screen/eng/res_s_menu_pause.szs", { "w08_160.bti", "w08_160_gra.bti", "dot_24.bti", nullptr },
          { PCG_Glass, PCG_Gradient, PCG_Dot24, PCG_COUNT } },
        { "/new_screen/eng/res_ufo.szs", { "dot_32.bti", nullptr, nullptr, nullptr }, { PCG_Dot32, PCG_COUNT, PCG_COUNT, PCG_COUNT } },
        { "/new_screen/eng/hensai_demo_kanryo.szs", { "a_48.bti", "b_48.bti", nullptr, nullptr },
          { PCG_BtnA, PCG_BtnB, PCG_COUNT, PCG_COUNT } },
        { "/new_screen/eng/res_itemZukan.szs", { "x_btn.bti", "y_btn.bti", nullptr, nullptr },
          { PCG_BtnX, PCG_BtnY, PCG_COUNT, PCG_COUNT } },
        { "/new_screen/eng/res_challengeSelect.szs", { "z_btn.bti", nullptr, nullptr, nullptr },
          { PCG_BtnZ, PCG_COUNT, PCG_COUNT, PCG_COUNT } },
    };
    for (const ArcTextures& entry : kList) {
        void* arc = loadArchive(entry.archive);
        if (!arc) {
            std::printf("[F1 glass] no se pudo leer %s\n", entry.archive);
            continue;
        }
        for (int i = 0; i < 4 && entry.files[i]; i++) {
            if (!addTexture(static_cast<const unsigned char*>(arc), entry.files[i], entry.slots[i]))
                std::printf("[F1 glass] falta %s en %s\n", entry.files[i], entry.archive);
        }
        freeArchive(arc);
    }
    sTexturesDone = true;
}

// Glifos ASCII de pikmin2main.bfn, cada uno en su celda del atlas con su
// intensidad (relleno blanco, contorno negro) y su alfa, como los pinta el
// juego; el color lo pone el vértice.
void loadFont()
{
    void* arcData = loadArchive("/message/font_foreign.szs");
    const unsigned char* arc = static_cast<const unsigned char*>(arcData);
    const unsigned char* bfn = arc ? rarcFind(arc, "pikmin2main.bfn", nullptr) : nullptr;
    if (!bfn || std::memcmp(bfn, "FONTbfn1", 8) != 0) {
        std::printf("[F1 glass] no se pudo leer la fuente pikmin2main.bfn\n");
        freeArchive(arcData);
        sFontDone = true; // sin fuente, el menú sigue con la del port
        return;
    }
    const unsigned fileSize = be32(bfn + 8);
    const unsigned char* inf = nullptr;
    const unsigned char* wid = nullptr;
    const unsigned char* gly = nullptr;
    const unsigned char* map = nullptr;
    for (unsigned o = 0x20; o + 8 <= fileSize;) {
        const unsigned char* blk = bfn + o;
        const unsigned size      = be32(blk + 4);
        if (!std::memcmp(blk, "INF1", 4)) inf = blk;
        else if (!std::memcmp(blk, "WID1", 4) && !wid) wid = blk;
        else if (!std::memcmp(blk, "GLY1", 4) && !gly) gly = blk;
        else if (!std::memcmp(blk, "MAP1", 4) && !map) map = blk;
        if (size == 0)
            break;
        o += size;
    }
    if (!inf || !gly || !map) {
        freeArchive(arcData);
        sFontDone = true;
        return;
    }
    sInfo.ascent  = be16(inf + 0x0A);
    sInfo.descent = be16(inf + 0x0C);
    const unsigned defaultW = be16(inf + 0x0E);
    const unsigned gStart = be16(gly + 0x08), gEnd = be16(gly + 0x0A);
    const unsigned cellW = be16(gly + 0x0C), cellH = be16(gly + 0x0E);
    const unsigned texSize = be32(gly + 0x10), texFmt = be16(gly + 0x14);
    const unsigned rows = be16(gly + 0x16), cols = be16(gly + 0x18);
    const unsigned texW = be16(gly + 0x1A), texH = be16(gly + 0x1C);
    const unsigned char* glyData = gly + 0x20;
    const unsigned mapMethod = be16(map + 0x08), mapStart = be16(map + 0x0A), mapEnd = be16(map + 0x0C);
    sInfo.cellW = (int)cellW;
    sInfo.cellH = (int)cellH;
    const unsigned perPage = rows * cols;
    if (perPage == 0 || texW > 512 || texH > 512 || mapMethod != 0) {
        std::printf("[F1 glass] fuente con un formato no previsto\n");
        freeArchive(arcData);
        sFontDone = true;
        return;
    }

    static unsigned char sheetA[512 * 512];
    static unsigned char sheetI[512 * 512];
    int sheetPage = -1;
    for (int c = 32; c < 127; c++) {
        if ((unsigned)c < mapStart || (unsigned)c > mapEnd)
            continue;
        const unsigned code = c - mapStart; // mapeo directo
        if (code < gStart || code > gEnd)
            continue;
        const unsigned idx = code - gStart, page = idx / perPage, cell = idx % perPage;
        // Mismo reparto que JUTResFont::loadImage.
        const unsigned col = cell % rows, row = cell / rows;
        if ((int)page != sheetPage) {
            std::memset(sheetA, 0, sizeof(sheetA));
            std::memset(sheetI, 0, sizeof(sheetI));
            const unsigned char* p = glyData + page * texSize;
            const int bw = 8, bh = texFmt == GX_TF_I4 ? 8 : 4;
            for (unsigned by = 0; by < texH; by += bh) {
                for (unsigned bx = 0; bx < texW; bx += bw) {
                    for (int i = 0; i < bw * bh; i++) {
                        const unsigned x = bx + i % bw, y = by + i / bw;
                        unsigned char a, l;
                        if (texFmt == GX_TF_I4) {
                            a = l = (unsigned char)(((i & 1) ? (p[i / 2] & 0xF) : (p[i / 2] >> 4)) * 17);
                        } else if (texFmt == GX_TF_IA4) {
                            a = (unsigned char)((p[i] >> 4) * 17);
                            l = (unsigned char)((p[i] & 0xF) * 17);
                        } else {
                            a = l = p[i];
                        }
                        if (x < texW && y < texH) {
                            sheetA[y * 512 + x] = a;
                            sheetI[y * 512 + x] = l;
                        }
                    }
                    p += 32;
                }
            }
            sheetPage = (int)page;
        }
        int ax, ay;
        if (!reserve((int)cellW, (int)cellH, ax, ay))
            break;
        for (unsigned y = 0; y < cellH; y++) {
            for (unsigned x = 0; x < cellW; x++) {
                const unsigned sx = col * cellW + x, sy = row * cellH + y;
                const bool in = sx < 512 && sy < 512;
                put(ax + (int)x, ay + (int)y, in ? sheetI[sy * 512 + sx] : 0, in ? sheetI[sy * 512 + sx] : 0,
                    in ? sheetI[sy * 512 + sx] : 0, in ? sheetA[sy * 512 + sx] : 0);
            }
        }
        // Ancho: tabla WID1 (2 bytes por código: entrada y ancho).
        short offset = 0, width = (short)defaultW;
        if (wid) {
            const unsigned wStart = be16(wid + 0x08), wEnd = be16(wid + 0x0A);
            if (code >= wStart && code <= wEnd) {
                offset = wid[0x0C + (code - wStart) * 2];
                width  = wid[0x0C + (code - wStart) * 2 + 1];
            }
        }
        sInfo.glyphs[c] = { (short)ax, (short)ay, offset, width, true };
    }
    freeArchive(arcData);
    sInfo.hasFont = true;
    sFontDone     = true;
}

} // namespace

const PcGlassAtlas* pc_p2_glass_atlas()
{
    if (!JKRHeap::sSystemHeap)
        return nullptr;
    if (!sTexturesDone)
        loadTextures();
    if (!sFontDone)
        loadFont();
    if (!sTexturesDone || sInfo.tex[PCG_Glass].w == 0)
        return nullptr;
    sInfo.rgba = sAtlas;
    sInfo.w    = kAtlasW;
    sInfo.h    = kAtlasH;
    // El atlas cambia al añadir la fuente; la generación avisa de subirlo.
    sInfo.generation = (sTexturesDone ? 1u : 0u) + (sFontDone ? 2u : 0u);
    return &sInfo;
}
