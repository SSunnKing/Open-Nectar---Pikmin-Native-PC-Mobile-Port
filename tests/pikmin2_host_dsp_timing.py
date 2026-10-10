"""Exercise production updateDSP with host bursts and console interrupt timing."""
import argparse
import os
from pathlib import Path
import subprocess


def extract(source, signature):
    start = source.index(signature)
    at = source.index('{', start)
    end, depth = at + 1, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


PREFIX = r'''
#include <cstdint>
#include <cstdio>
using u32 = uint32_t; using OSTick = uint32_t; using f32 = float;
static u32 tick, updates, callbacks, drops;
static bool voice = true;
OSTick OSGetTick() { return tick; }
void JASReport(const char*) {}
namespace JASKernel { void probeStart(int, const char*) {} void probeFinish(int) {} }
namespace JASDsp { void invalChannelAll() {} }
namespace JASPortCmd { void execAllCommand() {} }
namespace JASAudioThread { u32 snIntCount; }
namespace JASDSPChannel {
void killActiveChannel() { ++drops; voice = false; }
void updateAll() { ++updates; }
}
void DSPSyncCallback() {}
int getSubFrames() { return 7; }
void subframeCallback() { ++callbacks; }
'''
SUFFIX = r'''
int main() {
    // Seven synchronous software updates per DMA. Eight DMA messages may be
    // consumed in a burst, followed by a presentation/network stall. Timings
    // deliberately include equal, faster and slower internal subframes.
    for (unsigned frame = 0; frame < 600; ++frame) {
        for (unsigned sub = 0; sub < 7; ++sub) {
            JASAudioThread::snIntCount = 7 - sub;
            tick += sub == 0 && frame % 8 == 0 ? 18000 : 100 + (sub % 3) * 20;
            updateDSP();
        }
    }
    if (updates != 4200 || callbacks != 4200) return 2;
#ifdef PIKI_PC_PORT
    if (drops || !voice) {
        std::printf("FAIL host burst dropped sustained voice: drops=%u\n", drops);
        return 1;
    }
#else
    if (!drops || voice) return 3; // Preserve the console overload protection.
#endif
    std::printf("PASS updates=%u callbacks=%u drops=%u\n", updates, callbacks, drops);
}
'''


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--compiler', default='g++')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    path = 'games/pikmin2/pikmin2-decomp/src/JSystem/JAudio/JAS/JASAiCtrl.cpp'
    current = (root / path).read_text()
    original = subprocess.check_output(['git', 'show', 'bb787017ef6243d0ab8fe32ed87ba9052d931c8e:' + path], cwd=root, text=True)
    out = root / 'output/host-dsp-timing'
    out.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ)
    env['PATH'] = str(Path(args.compiler).resolve().parent) + os.pathsep + env.get('PATH', '')
    for name, source, host, expected in [('original-host', original, True, 1),
                                         ('fixed-host', current, True, 0),
                                         ('console', current, False, 0)]:
        cpp = out / (name + '.cpp')
        cpp.write_text(PREFIX + extract(source, 'void updateDSP()') + SUFFIX)
        exe = out / (name + '.exe')
        command = [args.compiler, '-std=c++17', '-O2', str(cpp), '-o', str(exe)]
        if host:
            command.append('-DPIKI_PC_PORT')
        subprocess.run(command, check=True, env=env)
        result = subprocess.run([str(exe)], env=env)
        if result.returncode != expected:
            raise SystemExit(f'{name}: expected {expected}, got {result.returncode}')
    print('host DSP timing regression passed (original fails; host fixed; console retained)')
    # Link the real renderer and ARAM implementation, without SDL, sequence
    # mocks or game assets. Establish sustained sample/frame continuity apart
    # from the timing-triggered voice eviction tested above.
    audio = root / 'games/pikmin2/pc_port/audio'
    renderer = out / 'sustained-renderer.exe'
    subprocess.run([args.compiler, '-std=c++17', '-O2', '-Wno-register', '-DPIKI_PC_PORT',
                    '-I' + str(root / 'include'), '-I' + str(audio),
                    str(root / 'tests/pikmin2_sustained_renderer.cpp'),
                    str(audio / 'pc_dsp_host.cpp'), str(audio / 'pc_aram.cpp'),
                    '-o', str(renderer)], check=True, env=env)
    subprocess.run([str(renderer)], check=True, env=env)


if __name__ == '__main__':
    main()
