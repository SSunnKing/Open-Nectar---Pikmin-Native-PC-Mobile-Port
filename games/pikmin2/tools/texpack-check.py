#!/usr/bin/env python3
"""Coteja un volcado --dump-texture-names con un pack de texturas Dolphin.

Uso:
    texpack-check.py <texture_names.log> <carpeta del pack>

El log lo escribe Nectar al ejecutarse con --dump-texture-names (una línea por
textura: nombre tab ancho tab alto tab formato tab bytes). La carpeta del pack
es la raíz que contiene los tex1_*.dds / tex1_*.png (se recorre con subcarpetas,
como hace Dolphin con Load/Textures/<GameID>/).

Salida: cuántos nombres casan, y de los que faltan, cuántos casarían ignorando
el hash de TLUT (apunta al rango min/max de la paleta) o cambiando el flag _m
(apunta al tamaño hasheado con mipmaps), agrupados por formato GX. Código de
salida 0 si casa al menos el 90 %, que es el criterio de la fase 0.
"""

import re
import sys
from collections import defaultdict
from pathlib import Path

RX_MIP = re.compile(r"_mip\d+$")
RX_HASH = re.compile(r"[0-9a-f]{16}")


def nombres_del_log(log_path):
    nombres = []
    for linea in Path(log_path).read_text(encoding="utf-8", errors="replace").splitlines():
        linea = linea.strip()
        if linea:
            nombres.append(linea.split("\t")[0])
    return nombres


def nombres_del_pack(pack_dir):
    """Nombres base que Dolphin indexaría: sin extensión, sin _mipN, sin _arb."""
    exactos = set()
    comodines = []  # nombres con '$': Dolphin los admite como wildcard de hash
    for ruta in Path(pack_dir).rglob("*"):
        if ruta.suffix.lower() not in (".dds", ".png"):
            continue
        stem = ruta.stem
        if not stem.startswith("tex1_"):
            continue
        stem = RX_MIP.sub("", stem)
        idx = stem.rfind("_arb")
        if idx != -1:
            stem = stem[:idx] + stem[idx + 4:]
        if "$" in stem:
            patron = "^" + re.escape(stem).replace(r"\$", "[0-9a-f]{16}") + "$"
            comodines.append(re.compile(patron))
        else:
            exactos.add(stem)
    return exactos, comodines


def ignorando_tlut(nombre):
    """tex1_WxH[_m]_hashTex_hashTlut_fmt -> tex1_WxH[_m]_hashTex_fmt."""
    partes = nombre.split("_")
    # tex1, WxH, [m,] hashTex, [hashTlut,] fmt
    if len(partes) >= 5 and RX_HASH.fullmatch(partes[-2]):
        return "_".join(partes[:-2] + partes[-1:])
    return None


def casa(nombre, exactos, comodines):
    if nombre in exactos:
        return True
    return any(p.match(nombre) for p in comodines)


def main():
    if len(sys.argv) != 3:
        print(__doc__.strip())
        return 2

    log, pack_dir = sys.argv[1], sys.argv[2]
    unicos = sorted(set(nombres_del_log(log)))
    exactos, comodines = nombres_del_pack(pack_dir)

    casan = [n for n in unicos if casa(n, exactos, comodines)]
    faltan = [n for n in unicos if not casa(n, exactos, comodines)]

    # Índices alternativos para el diagnóstico de causas.
    pack_sin_tlut = {}
    for n in exactos:
        clave = ignorando_tlut(n)
        if clave:
            pack_sin_tlut.setdefault(clave, n)
    pack_por_toggle_m = {}
    for n in exactos:
        alterno = n.replace("x_m_", "x_", 1) if "x_m_" in n else None
        if alterno is None:
            # añade _m tras las dimensiones: tex1_WxH_ -> tex1_WxHm_
            m = re.match(r"^(tex1_\d+x\d+)_", n)
            if m:
                alterno = m.group(1) + "m_" + n[m.end(1) + 1:]
        if alterno:
            pack_por_toggle_m.setdefault(alterno, n)

    salvo_tlut = 0
    salvo_m = 0
    por_formato = defaultdict(list)
    for n in faltan:
        clave = ignorando_tlut(n)
        if clave and clave in pack_sin_tlut:
            salvo_tlut += 1
        alterno = n.replace("x_m_", "x_", 1)
        if alterno == n:
            m = re.match(r"^(tex1_\d+x\d+)_", n)
            if m:
                alterno = m.group(1) + "m_" + n[m.end(1) + 1:]
        if alterno in exactos or alterno in pack_por_toggle_m:
            salvo_m += 1
        por_formato[n.rsplit("_", 1)[-1]].append(n)

    total = len(unicos)
    pct = 100.0 * len(casan) / max(1, total)
    print(f"Nombres únicos en el log: {total}")
    print(f"Ficheros tex1_* en el pack: {len(exactos)} (+{len(comodines)} comodines)")
    print(f"Casan: {len(casan)} ({pct:.1f} %)")
    print(f"Faltan: {len(faltan)}")
    if faltan:
        print(f"  de los que faltan, casarían ignorando el hash TLUT: {salvo_tlut}")
        print(f"  de los que faltan, casarían cambiando el flag _m:     {salvo_m}")
        print("Faltan por formato GX:")
        for fmt, lista in sorted(por_formato.items(), key=lambda kv: -len(kv[1])):
            print(f"  formato {fmt}: {len(lista)}")
            for n in lista[:10]:
                print(f"    {n}")
            if len(lista) > 10:
                print(f"    ... y {len(lista) - 10} más")

    return 0 if pct >= 90.0 else 1


if __name__ == "__main__":
    sys.exit(main())
