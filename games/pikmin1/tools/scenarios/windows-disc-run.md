# Disc scenarios on Windows x86-64 (MinGW cross build, wine in Docker)

Recorded 2026-10-28 on an M3 Max Mac, tree `91132b88` (with
`PIKMIN_SCENARIO_FLOAT_DET`, default ON in a scenario build). A Windows
`nectar.exe` cross-built on macOS with MinGW-w64 ran `boot_smoke`,
`throw_release` and `day30_clock` at 30 and 60 Hz under wine against a real disc
install. Its state-hash logs were compared with macOS arm64 runs of the same tree
on the same install (the macOS and Linux runs are in
[linux-disc-run.md](linux-disc-run.md)).

## Build: MinGW-w64 on macOS

Homebrew's `mingw-w64` (14.0.0, GCC 16.2.0; its default C runtime is UCRT) and
SDL2 for MinGW from the SDL release `SDL2-devel-2.32.10-mingw.tar.gz` (sha256
`83a5d74012311edc3c0d40ea6faecbe57ad692aa033fa5dc273cc937e3938ff2`).

`cmake/toolchain-mingw64.cmake` picks SDL2 up from `third_party/SDL2-mingw64`
(ignored by git). Either extract the archive's `x86_64-w64-mingw32` folder
there, or, to keep the tree clean as in this run, use a toolchain file outside
the tree that includes the repository one and adds the SDL2 folder:

```cmake
include("<repo>/games/pikmin1/cmake/toolchain-mingw64.cmake")
set(_sdl "<extract>/SDL2-2.32.10/x86_64-w64-mingw32")
list(APPEND CMAKE_PREFIX_PATH "${_sdl}")
list(APPEND CMAKE_FIND_ROOT_PATH "${_sdl}")
```

```sh
cmake -S games/pikmin1 -B <build-win> -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_TOOLCHAIN_FILE=<toolchain> \
  -DPIKMIN_NATIVE_JAUDIO=OFF -DPIKMIN_BUILD_SCENARIOS=ON
cmake --build <build-win> --target pikmin_pc -j 4
```

The configure log prints `Scenario float determinism: -ffp-contract=off and the
software libm`. The result is `<build-win>/bin/nectar.exe`: a console (CUI)
executable with SDL2 and the C/C++ runtime linked in, so no DLL goes beside it.
It imports `OPENGL32.dll`, system DLLs and the UCRT `api-ms-win-crt-*` sets.
The only maths functions it imports from UCRT are exact ones (`ceil`, `floor`,
`round`, `lround`, `frexp`, `copysignf`, `fmaxf`, `fminf`, `nextafterf` and
their float forms). `sin`, `cos`, `tan`, their inverses, `atan2`, `exp`, `log`,
`pow`, `fmod`, `hypot` and `sqrt` (double and float) come from the software libm
object linked in.

## Run: wine in Docker `linux/amd64`

Homebrew's wine casks (`wine-stable`, `wine@devel`, `wine@staging`) have been
disabled since 2026-09-01 (they fail the Gatekeeper check), so wine on macOS
was not used. The run uses Ubuntu 24.04's wine 9.0 in an emulated
`linux/amd64` container, with Xvfb. GL is wine's own `opengl32.dll`, which goes
through GLX to the container's Mesa llvmpipe; no Windows Mesa DLLs are needed.
The game reports `4.5 (Compatibility Profile) Mesa 25.2.8` and
`llvmpipe (LLVM 20.1.2, 256 bits)`.

```dockerfile
FROM --platform=linux/amd64 ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    wine wine64 xvfb xauth libgl1 libglx-mesa0 libgl1-mesa-dri libegl-mesa0 mesa-utils \
    ca-certificates \
 && rm -rf /var/lib/apt/lists/*
ENV WINEDEBUG=-all WINEPREFIX=/wineprefix WINEARCH=win64
```

The install is mounted read-only and the exe folder too. Wine maps `/` to `Z:`,
so the data folder and the log paths are given as `Z:\...`. The scenario's own
save folder lands in the prefix's `C:\users\root\Temp`, inside the container.

```sh
docker build -t pikmin-wine-disc .
docker run --rm --platform linux/amd64 -v "$HOME/<install>":/disc:ro -v "$PWD/out":/out \
  -v <build-win>/bin:/exe:ro pikmin-wine-disc bash -c '
    export LIBGL_ALWAYS_SOFTWARE=1
    mkdir -p /scratch && ln -sfn /disc/assets /scratch/assets
    Xvfb :99 -screen 0 1280x720x24 -nolisten tcp & export DISPLAY=:99; sleep 2
    wineboot -i
    cd /out
    for n in boot_smoke throw_release day30_clock; do for r in 30 60; do
      PIKMIN_SCENARIO_DATA_DIR="Z:\\scratch" wine Z:\\exe\\nectar.exe \
        --scenario $n --scenario-tick-rate $r --state-hash-log "Z:\\out\\$n-$r.hash" \
        > /out/$n-$r.out 2>&1; echo "$n $r exit $?"
    done; done'
```

`wine` returns the exe's exit code. `wineboot -i` took 25 to 33 s and each run
21 to 110 s under emulation.

## Results

| Scenario | Rate | Windows (wine) exit | macOS exit | `throw_release` K (Windows / macOS) | Hash log against macOS |
|----------|------|---------------------|-----------|-------------------------------------|------------------------|
| boot_smoke | 30 | 0 | 0 | | identical after CRLF to LF (951 lines) |
| boot_smoke | 60 | 0 | 0 | | identical after CRLF to LF (951 lines) |
| throw_release | 30 | 0 | 0 | 15 / 15 | identical after CRLF to LF (421 lines) |
| throw_release | 60 | 0 | 0 | 18 / 18 | identical after CRLF to LF (699 lines) |
| day30_clock | 30 | 0 | 0 | | identical after CRLF to LF (329 lines) |
| day30_clock | 60 | 0 | 0 | | identical after CRLF to LF (350 lines) |

All six pass with K = 15 (30 Hz) and 18 (60 Hz), the same as macOS, Linux and
the stand-in. Every hash value on every line equals the macOS one, so Windows,
Linux and macOS give the same per-tick sim hashes on the disc tree.

The files are not byte-identical: `pc_state_hash.cpp` opens the log with
`fopen(path, "w")`, which is text mode on Windows, so every line ends in CRLF.
Strip the CR before comparing (`tr -d '\r' < win.hash | cmp - mac.hash`). A
second Windows `boot_smoke` 30 Hz run gave the same log.

## Not verified

- Real Windows. Only wine 9.0 under x86-64 emulation was run.
- The CI build: MSYS2 MINGW64 natively on Windows (MSYS2's GCC, msvcrt runtime)
  with Mesa's `opengl32.dll` and `libgallium_wgl.dll` beside the exe. This run
  is a Homebrew GCC 16.2 cross build on UCRT with wine's GL.
- A Release (`-O3`) build: all logs here are RelWithDebInfo.
- The `ctest -R _disc` path on Windows (the runs above call `nectar.exe`
  directly), and scenarios other than these three.
