#!/usr/bin/env python3
"""Monta los archivos de una release de Open Nectar Fusion para un sistema.

Una release (una sola etiqueta en GitHub) lleva, por sistema:

  nectar-<os>.<ext>            paquete completo: launcher + Pikmin 1 + Pikmin 2.
                               Lo que se descarga a mano, y lo que buscan los
                               launchers 0.9.2 y anteriores para actualizarse.
  nectar-launcher-<os>.<ext>   solo el launcher
  nectar-pikmin1-<os>.<ext>    Pikmin 1 (USA y PAL)
  nectar-pikmin2-<os>.<ext>    Pikmin 2
  manifest.json                versión de cada parte y el archivo de cada una

El launcher lee manifest.json de la última release, compara cada parte con lo
instalado y descarga solo lo que ha cambiado. Las versiones salen de
versions.json, en la raíz del repo: es lo único que se edita al publicar.

Uso:
  make_release.py --os linux|windows --out DIR
                  --launcher F --p1-usa F --p1-pal F --p2 F [--extra F ...]
"""
import argparse
import json
import os
import shutil
import sys
import tarfile
import zipfile

PARTS = ("launcher", "pikmin1", "pikmin2")


def asset_name(part, os_name):
    ext = "zip" if os_name == "windows" else "tar.gz"
    return f"nectar-{part}-{os_name}.{ext}"


def bundle_name(os_name):
    return "nectar-windows.zip" if os_name == "windows" else "nectar-linux.tar.gz"


def manifest(versions):
    """Mismo contenido para los dos sistemas: los nombres son fijos."""
    return {
        "release": versions["release"],
        "parts": {
            part: {
                "version": versions[part],
                "assets": {os_name: asset_name(part, os_name) for os_name in ("linux", "windows")},
            }
            for part in PARTS
        },
    }


def archive(src_dir, dst, windows):
    """Comprime src_dir (la carpeta entera, con su nombre) en dst."""
    base = os.path.dirname(src_dir)
    if windows:
        with zipfile.ZipFile(dst, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
            for root, _, files in os.walk(src_dir):
                for name in sorted(files):
                    path = os.path.join(root, name)
                    z.write(path, os.path.relpath(path, base))
    else:
        with tarfile.open(dst, "w:gz") as t:
            t.add(src_dir, arcname=os.path.basename(src_dir))


def stage(folder, files):
    """files: lista de (origen, nombre en el paquete, ejecutable?)."""
    shutil.rmtree(folder, ignore_errors=True)
    os.makedirs(folder)
    for src, name, executable in files:
        if not os.path.isfile(src):
            sys.exit(f"Falta {src}")
        dst = os.path.join(folder, name)
        shutil.copy2(src, dst)
        if executable:
            os.chmod(dst, 0o755)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--os", required=True, choices=("linux", "windows"))
    ap.add_argument("--out", required=True)
    ap.add_argument("--versions", default=os.path.join(os.path.dirname(__file__), "..", "..", "versions.json"))
    ap.add_argument("--launcher", required=True)
    ap.add_argument("--p1-usa", required=True)
    ap.add_argument("--p1-pal", required=True)
    ap.add_argument("--p2", required=True)
    ap.add_argument("--extra", action="append", default=[], help="archivo que va junto al launcher (README, icono)")
    args = ap.parse_args()

    with open(args.versions) as f:
        versions = json.load(f)
    for key in ("release",) + PARTS:
        if not versions.get(key):
            sys.exit(f"versions.json no tiene '{key}'")

    windows = args.os == "windows"
    exe = ".exe" if windows else ""
    out = os.path.abspath(args.out)
    os.makedirs(out, exist_ok=True)
    work = os.path.join(out, "staging")

    launcher_files = [(args.launcher, "nectar-launcher" + exe, True)]
    launcher_files += [(f, os.path.basename(f), False) for f in args.extra]
    p1_files = [(args.p1_usa, "nectar" + exe, True), (args.p1_pal, "nectar-pal" + exe, True)]
    p2_files = [(args.p2, "pikmin2_pc" + exe, True)]

    if open(args.p1_usa, "rb").read() == open(args.p1_pal, "rb").read():
        sys.exit("nectar y nectar-pal son el mismo archivo: la build PAL no se hizo.")

    man = manifest(versions)
    man_path = os.path.join(out, "manifest.json")
    with open(man_path, "w") as f:
        json.dump(man, f, indent=2)
        f.write("\n")

    produced = [man_path]
    for part, files in (("launcher", launcher_files), ("pikmin1", p1_files), ("pikmin2", p2_files)):
        folder = os.path.join(work, f"nectar-{part}-{args.os}")
        stage(folder, files)
        dst = os.path.join(out, asset_name(part, args.os))
        archive(folder, dst, windows)
        produced.append(dst)

    # Paquete completo: todo junto en una carpeta, con el manifiesto dentro
    # (el launcher sabe así qué versión de cada parte trae).
    full = os.path.join(work, f"nectar-{args.os}")
    stage(full, launcher_files + p1_files + p2_files)
    shutil.copy2(man_path, os.path.join(full, "manifest.json"))
    dst = os.path.join(out, bundle_name(args.os))
    archive(full, dst, windows)
    produced.append(dst)

    print(f"\nRelease {versions['release']} ({args.os}): sube estos archivos a la release {versions['release']}:")
    for p in produced:
        print(f"  {p}")
    print(f"Carpeta del paquete completo (para el AppImage): {full}")


if __name__ == "__main__":
    main()
