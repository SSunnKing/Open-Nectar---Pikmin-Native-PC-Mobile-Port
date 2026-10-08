#!/usr/bin/env python3
"""Capture the real GX/P2D port menus without opening a game or writing settings.

Requires a completed native Unix Makefiles build, SDL's offscreen GL driver,
and the extracted game assets. Optional Pillow converts PPM captures to PNG.
"""
import argparse
import os
from pathlib import Path
import shlex
import subprocess

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build-dir', type=Path, default=root / 'build')
parser.add_argument('--output-dir', type=Path, default=Path('/tmp/nectar-menu-preview'))
parser.add_argument('--width', type=int, default=640)
parser.add_argument('--height', type=int, default=480)
args = parser.parse_args()
if not (320 <= args.width <= 3840 and 240 <= args.height <= 2160):
    parser.error('Use dimensions between 320x240 and 3840x2160.')
build = args.build_dir.resolve()
output = args.output_dir.resolve()
output.mkdir(parents=True, exist_ok=True)
flags = (build / 'CMakeFiles/pikmin_pc.dir/flags.make').read_text()
compile_args = []
for key in ('CXX_DEFINES', 'CXX_INCLUDES', 'CXX_FLAGS'):
    value = next(line.split(' = ', 1)[1] for line in flags.splitlines()
                 if line.startswith(key + ' = '))
    compile_args.extend(shlex.split(value))
link = shlex.split((build / 'CMakeFiles/pikmin_pc.dir/link.txt').read_text())
obj = output / 'preview.o'
exe = output / 'preview'
subprocess.run([link[0], *compile_args, '-O0', '-c',
                str(root / 'tools/preview_port_menus.cpp'), '-o', str(obj)], check=True)
main_obj = 'CMakeFiles/pikmin_pc.dir/pc_port/pc_main.cpp.o'
settings_obj = 'CMakeFiles/pikmin_pc.dir/pc_port/settings/pc_settings.cpp.o'
if main_obj not in link or settings_obj not in link:
    raise RuntimeError('Expected a native Unix Makefiles pikmin_pc link command.')
link = [str(obj) if arg == main_obj else arg for arg in link
        if arg != settings_obj and not arg.startswith('-Wl,--dependency-file')]
link[link.index('-o') + 1] = str(exe)
# Fast fixture link; production binaries keep their normal optimisation.
subprocess.run([*link, '-O0', '-flto=4'], cwd=build, check=True)
env = dict(os.environ, SDL_VIDEODRIVER='offscreen', SDL_AUDIODRIVER='dummy',
           PIKMIN_RENDER_SCALE='1', XDG_CONFIG_HOME=str(output / 'config'))
subprocess.run([str(exe), str(output), str(args.width), str(args.height)],
               cwd=root, env=env, check=True, timeout=30)
try:
    from PIL import Image, ImageChops
except ImportError:
    print('Pillow unavailable; PPM captures are ready.')
else:
    for path in output.glob('page-*.ppm'):
        Image.open(path).save(path.with_suffix('.png'))
    initial = Image.open(output / 'page-00.ppm')
    reopened = Image.open(output / 'page-16.ppm')
    if ImageChops.difference(initial, reopened).getbbox():
        raise RuntimeError('Reopening F1 left stale menu state in the render.')
    closed = Image.open(output / 'page-15.ppm')
    if closed.getcolors(maxcolors=2) is None or len(closed.getcolors(maxcolors=2)) != 1:
        raise RuntimeError('Closed F1 still drew over the background.')
    print('Visual lifecycle checks passed: closed and reopened F1.')
print(f'Captures: {output}')
