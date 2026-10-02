#!/usr/bin/env python3
"""Genera pc_port/settings/pc_ui_font.h: atlas I8 (ASCII 32..126) para el menu
F1 de Pikmin 2. Fuente: DejaVu Sans Mono Bold (licencia Bitstream Vera/DejaVu,
permite incrustarla)."""
import sys
from PIL import Image, ImageDraw, ImageFont

TTF = sys.argv[1] if len(sys.argv) > 1 else "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf"
OUT = sys.argv[2] if len(sys.argv) > 2 else "pc_port/settings/pc_ui_font.h"
CW, CH, COLS = 12, 16, 16
FIRST, LAST = 32, 126
rows = (LAST - FIRST + COLS) // COLS
W, H = CW * COLS, CH * rows
font = ImageFont.truetype(TTF, 14)
img = Image.new("L", (W, H), 0)
d = ImageDraw.Draw(img)
asc, desc = font.getmetrics()
for c in range(FIRST, LAST + 1):
    i = c - FIRST
    x, y = (i % COLS) * CW, (i // COLS) * CH
    bw = d.textlength(chr(c), font=font)
    d.text((x + (CW - bw) / 2, y + (CH - (asc + desc)) / 2), chr(c), font=font, fill=255)
data = img.tobytes()
with open(OUT, "w") as f:
    f.write("// Generado por tools/gen_ui_font.py - no editar.\n")
    f.write("// DejaVu Sans Mono Bold 14px, ASCII %d..%d en celdas %dx%d.\n" % (FIRST, LAST, CW, CH))
    f.write("#pragma once\n")
    f.write("enum { kPcUiFontW = %d, kPcUiFontH = %d, kPcUiFontCellW = %d, kPcUiFontCellH = %d,\n" % (W, H, CW, CH))
    f.write("       kPcUiFontCols = %d, kPcUiFontFirst = %d, kPcUiFontLast = %d };\n" % (COLS, FIRST, LAST))
    f.write("static const unsigned char kPcUiFont[%d] = {\n" % len(data))
    for i in range(0, len(data), 24):
        f.write("  " + ",".join(str(b) for b in data[i:i + 24]) + ",\n")
    f.write("};\n")
print("ok", W, H)
