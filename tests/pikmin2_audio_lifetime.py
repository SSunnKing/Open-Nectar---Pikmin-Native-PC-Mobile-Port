"""Compile asset-free command-publication and callback-lifetime regressions."""
import argparse
import os
from pathlib import Path
import subprocess

p = argparse.ArgumentParser()
p.add_argument('--compiler', default='g++')
args = p.parse_args()
root = Path(__file__).resolve().parents[1]
game = root / 'games/pikmin2'
out = root / 'output/audio-lifetime'
out.mkdir(parents=True, exist_ok=True)
env = dict(os.environ)
env['PATH'] = str(Path(args.compiler).resolve().parent) + os.pathsep + env.get('PATH', '')
flags = ['-std=c++17', '-O2', '-w', '-fpermissive', '-pthread', '-DPIKI_PC_PORT', '-DVERSION_GPVE01',
         '-include', str(game / 'pikmin2-decomp-adapter/pikmin2_pc_types.h'),
         '-include', str(game / 'pikmin2-decomp/include/Dolphin/os.h')]
flags += ['-I' + str(game / x) for x in ('pikmin2-decomp-adapter', 'pikmin2-decomp/include', 'pc_port')]
cases = [
    ('port-command', root / 'tests/pikmin2_audio_port_command.cpp',
     ['JSystem/JAudio/JAS/JASCmdStack.cpp', 'JSystem/JSupport/JSUList.cpp']),
    ('callback-lifetime', game / 'pc_port/pc_p2_audio_callback_lifetime_test.cpp',
     ['JSystem/JAudio/JAS/JASCallback.cpp']),
]
for name, test, sources in cases:
    exe = out / (name + '.exe')
    subprocess.run([args.compiler, *flags, str(test),
                    *[str(game / 'pikmin2-decomp/src' / s) for s in sources], '-o', str(exe)], check=True, env=env)
    subprocess.run([str(exe)], check=True, env=env, timeout=20)
