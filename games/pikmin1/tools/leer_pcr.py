#!/usr/bin/env python3
"""Lee un .pcr de Pikmin sin ejecutar el juego.

Replica `zen::particleGenerator::pmSetDDF` y `zen::bBoardColourAnimData::set`
para imprimir lo que un efecto pide de verdad: factores de mezcla, modo Z, modo
TEV y los colores prim/env de cada fotograma.

    tools/leer_pcr.py assets/dataDir/effects/pcr/sd_rakk1.pcr

Sirvió para cerrar FX-001 (cuadrados blancos): los colores autorizados en el
archivo dijeron qué registro TEV se pretendía leer, sin depender de una
partida. Si se toca el orden de lectura en particleGenerator.cpp, hay que
tocarlo aquí también; una desalineación de cuatro bytes ya dio una vez
factores de mezcla imposibles, que es la señal de que el lector está desfasado.
"""
import struct, sys

F = {
    'EmissionRateManual': 1 << 6, 'EmissionRateLinear': 1 << 7,
    'EmissionRadiusManual': 1 << 8, 'EmissionRadiusLinear': 1 << 9,
    'InitVelocityManual': 1 << 10, 'InitVelocityLinear': 1 << 11,
    'EnableChildParticles': 1 << 13,
    'UseGravityField': 1 << 16, 'UseAirField': 1 << 17,
    'UseVortexField': 1 << 18, 'UseDampedNewtonField': 1 << 19,
    'UseNewtonField': 1 << 20, 'UseSolidTexField': 1 << 21,
    'UseJitterField': 1 << 22, 'UseLineField': 1 << 23,
}

BL = ['ZERO', 'ONE', 'SRCCOL/DSTCOL', 'INVSRCCOL/INVDSTCOL',
      'SRCALPHA', 'INVSRCALPHA', 'DSTALPHA', 'INVDSTALPHA']
CMP = ['NEVER', 'LESS', 'EQUAL', 'LEQUAL', 'GREATER', 'NEQUAL', 'GEQUAL', 'ALWAYS']


class R:
    def __init__(self, buf, pos):
        self.b, self.p = buf, pos

    def u32(self, size=4):
        v = struct.unpack_from('>I', self.b, self.p)[0]; self.p += size; return v

    def f32(self, size=4):
        v = struct.unpack_from('>f', self.b, self.p)[0]; self.p += size; return v

    def s16(self, size=2):
        v = struct.unpack_from('>h', self.b, self.p)[0]; self.p += size; return v

    def u8(self, size=1):
        v = self.b[self.p]; self.p += size; return v

    def vec(self):
        v = struct.unpack_from('>3f', self.b, self.p); self.p += 12; return v

    def farr(self, n):
        v = struct.unpack_from('>%df' % n, self.b, self.p); self.p += n * 4; return v

    def carr(self, n):
        v = [tuple(self.b[self.p + i * 4: self.p + i * 4 + 4]) for i in range(n)]
        self.p += n * 4; return v


def anim(buf, off):
    r = R(buf, off)
    blend, dur, flags, maxf = r.u8(), r.u8(), r.u8(), r.u8()
    return {'blendMode': blend, 'duration': dur, 'flags': flags, 'maxFrame': maxf,
            'thresholds': r.farr(maxf), 'prim': r.carr(maxf), 'env': r.carr(maxf)}


def ddf(buf, off):
    r = R(buf, off)
    fl = r.u32()
    r.vec(); r.vec(); r.vec(); r.f32()                       # offset, dir, box, spread
    if fl & (F['EmissionRateManual'] | F['EmissionRateLinear']):
        n = r.u8(4); r.farr(n); r.farr(n)
    else:
        r.f32()
    r.f32(); r.f32()                                          # rate jitter, radius scale
    if fl & (F['EmissionRadiusManual'] | F['EmissionRadiusLinear']):
        n = r.u8(4); r.farr(n); r.farr(n)
    else:
        r.f32()
    if fl & (F['InitVelocityManual'] | F['InitVelocityLinear']):
        n = r.u8(4); r.farr(n); r.farr(n)
    else:
        r.f32()
    for _ in range(14):                                       # initVelJitter..lifetimeJitter
        r.f32()
    for _ in range(5):                                        # rot/life/frame shorts
        r.s16()
    r.u8(1); r.u8(1); r.u8(1)                                 # motionTime, unused, maxPasses
    blendFactor = r.u8(1)
    zmode = r.u8(2)
    return {'flags': fl, 'blendFactor': blendFactor, 'zMode': zmode}


for path in sys.argv[1:]:
    buf = open(path, 'rb').read()
    ver = buf[0:4].decode('ascii', 'replace')
    ddfOff = struct.unpack_from('>H', buf, 4)[0]
    billboard = buf[6]
    a = anim(buf, 8 if billboard else 16)
    d = ddf(buf, ddfOff)

    bf, zm = d['blendFactor'], d['zMode']
    src, dst = bf & 0xF, bf >> 4
    print('=' * 66)
    print('%s   version=%s  billboard=%d  ddf@0x%X' % (path.split('/')[-1], ver, billboard, ddfOff))
    print('  blendFactor = 0x%02X -> src=%d (%s)  dst=%d (%s)' % (
        bf, src, BL[src] if src < 8 else '¡FUERA DE RANGO!',
        dst, BL[dst] if dst < 8 else '¡FUERA DE RANGO!'))
    print('  zMode       = 0x%02X -> test=%d  func=%s  write=%d' % (
        zm, (zm & 8) >> 3, CMP[zm & 7], zm >> 4))
    print('  TEV blendMode = %d   duration=%d flags=0x%02X maxFrame=%d' % (
        a['blendMode'], a['duration'], a['flags'], a['maxFrame']))
    print('  umbrales   :', ['%.3f' % t for t in a['thresholds']])
    print('  primColors :', a['prim'])
    print('  envColors  :', a['env'])
