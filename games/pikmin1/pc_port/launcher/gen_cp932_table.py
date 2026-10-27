#!/usr/bin/env python3
"""Genera cp932_table.h: CP932 (Shift-JIS de Windows) a Unicode.

Lo usa gamecube_image.cpp en Android, cuyo iconv no trae Shift-JIS, para
convertir los nombres japoneses del disco igual que Windows (código 932) y
macOS (iconv CP932). Uso: python3 gen_cp932_table.py > cp932_table.h
"""

LEADS = list(range(0x81, 0xA0)) + list(range(0xE0, 0xFD))
TRAILS = [t for t in range(0x40, 0xFD) if t != 0x7F]

values = []
for lead in LEADS:
    for trail in TRAILS:
        try:
            ch = bytes([lead, trail]).decode("cp932")
            values.append(ord(ch) if len(ch) == 1 else 0)
        except UnicodeDecodeError:
            values.append(0)

print("// Generado por gen_cp932_table.py: no editar a mano.")
print("// CP932 de dos bytes a Unicode (0 = sin carácter). Índice:")
print("// fila del byte alto (0x81-0x9F, 0xE0-0xFC) * 188 + byte bajo")
print("// (0x40-0xFC sin 0x7F).")
print("#pragma once")
print("#include <cstdint>")
print("static const std::uint16_t kCp932Table[%d] = {" % len(values))
for i in range(0, len(values), 16):
    print("    " + ", ".join("0x%04X" % v for v in values[i:i + 16]) + ",")
print("};")
