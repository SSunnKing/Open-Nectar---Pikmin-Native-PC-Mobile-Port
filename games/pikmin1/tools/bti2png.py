#!/usr/bin/env python3
"""Convierte texturas BTI de GameCube a PNG (I4, I8, IA4, IA8, RGB565, RGB5A3,
RGBA8, CMPR). Uso: tools/bti2png.py fichero.bti [salida.png]
Sirve para ver los assets 2D del disco al diseñar la interfaz del port."""
import struct, sys
from PIL import Image

def decode(data):
    fmt, _, w, h = struct.unpack('>BBHH', data[:6])
    off = struct.unpack('>I', data[0x1c:0x20])[0]
    px = data[off:]
    img = Image.new('RGBA', (w, h))
    put = img.putpixel
    def blocks(bw, bh, fn):
        i = 0
        for by in range(0, h, bh):
            for bx in range(0, w, bw):
                i = fn(bx, by, i)
    def rgb5a3(v):
        if v & 0x8000:
            r = (v >> 10) & 31; g = (v >> 5) & 31; b = v & 31
            return (r * 255 // 31, g * 255 // 31, b * 255 // 31, 255)
        a = (v >> 12) & 7; r = (v >> 8) & 15; g = (v >> 4) & 15; b = v & 15
        return (r * 17, g * 17, b * 17, a * 255 // 7)
    def rgb565(v):
        return ((v >> 11) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31, 255)
    if fmt == 0:  # I4
        def fn(bx, by, i):
            for y in range(8):
                for x in range(0, 8, 2):
                    b = px[i]; i += 1
                    for k, v in ((0, b >> 4), (1, b & 15)):
                        if bx + x + k < w and by + y < h: put((bx + x + k, by + y), (v * 17,) * 3 + (255,))
            return i
        blocks(8, 8, fn)
    elif fmt == 1:  # I8
        def fn(bx, by, i):
            for y in range(4):
                for x in range(8):
                    v = px[i]; i += 1
                    if bx + x < w and by + y < h: put((bx + x, by + y), (v, v, v, 255))
            return i
        blocks(8, 4, fn)
    elif fmt == 2:  # IA4
        def fn(bx, by, i):
            for y in range(4):
                for x in range(8):
                    b = px[i]; i += 1
                    a = (b >> 4) * 17; v = (b & 15) * 17
                    if bx + x < w and by + y < h: put((bx + x, by + y), (v, v, v, a))
            return i
        blocks(8, 4, fn)
    elif fmt == 3:  # IA8
        def fn(bx, by, i):
            for y in range(4):
                for x in range(4):
                    a, v = px[i], px[i + 1]; i += 2
                    if bx + x < w and by + y < h: put((bx + x, by + y), (v, v, v, a))
            return i
        blocks(4, 4, fn)
    elif fmt in (4, 5):  # RGB565 / RGB5A3
        conv = rgb565 if fmt == 4 else rgb5a3
        def fn(bx, by, i):
            for y in range(4):
                for x in range(4):
                    v = struct.unpack('>H', px[i:i + 2])[0]; i += 2
                    if bx + x < w and by + y < h: put((bx + x, by + y), conv(v))
            return i
        blocks(4, 4, fn)
    elif fmt == 6:  # RGBA8
        def fn(bx, by, i):
            ar = px[i:i + 32]; gb = px[i + 32:i + 64]; i += 64
            for y in range(4):
                for x in range(4):
                    k = (y * 4 + x) * 2
                    if bx + x < w and by + y < h:
                        put((bx + x, by + y), (ar[k + 1], gb[k], gb[k + 1], ar[k]))
            return i
        blocks(4, 4, fn)
    elif fmt == 14:  # CMPR
        def dxt(bx, by, i):
            c0, c1 = struct.unpack('>HH', px[i:i + 4]); bits = px[i + 4:i + 8]
            p0, p1 = rgb565(c0), rgb565(c1)
            if c0 > c1:
                p2 = tuple((2 * p0[k] + p1[k]) // 3 for k in range(3)) + (255,)
                p3 = tuple((p0[k] + 2 * p1[k]) // 3 for k in range(3)) + (255,)
            else:
                p2 = tuple((p0[k] + p1[k]) // 2 for k in range(3)) + (255,)
                p3 = (0, 0, 0, 0)
            pal = (p0, p1, p2, p3)
            for y in range(4):
                row = bits[y]
                for x in range(4):
                    if bx + x < w and by + y < h: put((bx + x, by + y), pal[(row >> (6 - 2 * x)) & 3])
            return i + 8
        def fn(bx, by, i):
            for sy in (0, 4):
                for sx in (0, 4):
                    i = dxt(bx + sx, by + sy, i)
            return i
        blocks(8, 8, fn)
    else:
        raise SystemExit(f'formato {fmt} no soportado')
    return img

if __name__ == '__main__':
    src = sys.argv[1]; dst = sys.argv[2] if len(sys.argv) > 2 else src.rsplit('.', 1)[0] + '.png'
    decode(open(src, 'rb').read()).save(dst); print(dst)
