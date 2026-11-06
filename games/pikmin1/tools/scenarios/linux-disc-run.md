# Disc scenarios on Linux x86-64 (Docker)

Recorded 2026-10-28 on an M3 Max Mac, Docker Desktop 29.9.2, `linux/amd64`
(emulated), tree `91132b88` (with `PIKMIN_SCENARIO_FLOAT_DET`, default ON in a
scenario build). Scenarios `boot_smoke`, `throw_release` and `day30_clock` at 30
and 60 Hz against a real disc install, then compared with macOS arm64 runs of the
same tree on the same install. Windows is in
[windows-disc-run.md](windows-disc-run.md).

## Recipe

Ubuntu 20.04's SDL 2.0.10 is too old (the game needs `SDL_GameControllerHasSensor`),
so the image is `ubuntu:24.04` with its `libsdl2-dev` (GCC 13.3):

```dockerfile
FROM --platform=linux/amd64 ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    cmake ninja-build ccache pkg-config g++ libsdl2-dev libgl1-mesa-dev libx11-dev \
    libgl1-mesa-dri libglx-mesa0 libegl-mesa0 xvfb xauth ca-certificates python3 \
 && rm -rf /var/lib/apt/lists/*
```

Build (source mounted read-only at `/src`, build output in a Docker volume). The
configure log prints `Scenario float determinism: -ffp-contract=off and the
software libm`:

```sh
docker build -t pikmin-linux-disc .
docker run --rm --platform linux/amd64 -v "$PWD":/src:ro -v pikmin-linux-build:/build \
  pikmin-linux-disc bash -c '
    cmake -S /src/games/pikmin1 -B /build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DPIKMIN_NATIVE_JAUDIO=OFF -DPIKMIN_BUILD_SCENARIOS=ON &&
    cmake --build /build --target pikmin_pc -j 4'
```

Run (the install is mounted read-only; the scratch folder the game changes into
holds a symlink to its `assets`, so nothing is written to the install):

```sh
docker run --rm --platform linux/amd64 -v "$HOME/<install>":/disc:ro -v "$PWD/out":/out \
  -v pikmin-linux-build:/build:ro pikmin-linux-disc bash -c '
    export LIBGL_ALWAYS_SOFTWARE=1
    mkdir -p /scratch && ln -sfn /disc/assets /scratch/assets
    for n in boot_smoke throw_release day30_clock; do for r in 30 60; do
      PIKMIN_SCENARIO_DATA_DIR=/scratch xvfb-run -a /build/bin/nectar \
        --scenario $n --scenario-tick-rate $r --state-hash-log /out/$n-$r.hash \
        > /out/$n-$r.out 2>&1; echo "$n $r exit $?"
    done; done'
```

Each run took 15 to 44 s under emulation.

The macOS logs come from the same tree, built natively with the same options
(`cmake -S games/pikmin1 -B <build> -DCMAKE_BUILD_TYPE=RelWithDebInfo
-DPIKMIN_NATIVE_JAUDIO=OFF -DPIKMIN_BUILD_SCENARIOS=ON`, Unix Makefiles, Apple
clang), run from a scratch folder whose `assets` is a symlink to the install, with
the same arguments and `PIKMIN_SCENARIO_DATA_DIR` set to that folder.

## Results

| Scenario | Rate | Linux exit | macOS exit | `throw_release` K (Linux / macOS) | Hash log |
|----------|------|-----------|-----------|-----------------------------------|----------|
| boot_smoke | 30 | 0 | 0 | | byte-identical (951 lines) |
| boot_smoke | 60 | 0 | 0 | | byte-identical (951 lines) |
| throw_release | 30 | 0 | 0 | 15 / 15 | byte-identical (421 lines) |
| throw_release | 60 | 0 | 0 | 18 / 18 | byte-identical (699 lines) |
| day30_clock | 30 | 0 | 0 | | byte-identical (329 lines) |
| day30_clock | 60 | 0 | 0 | | byte-identical (350 lines) |

All six pass on both, with K = 15 (30 Hz) and 18 (60 Hz), the same as the
stand-in. Each Linux `--state-hash-log` is byte-identical to the macOS one. A
second macOS pass of all six reproduced its own logs byte for byte. The navi,
teki, item, world and rng hashes change from tick to tick in every run, and the
piki hash does in `throw_release` (the other two have no Pikmin, so it stays
constant), so the comparison covers the live stage. The mode column is 0 (no
mode source in a scenario).

## Before the float change

The first run (tree `ccf3c90a`, before `PIKMIN_SCENARIO_FLOAT_DET`) had the
same exits and K, but none of the six logs matched macOS. The `item` hash
diverged first, at tick 6, in every run; then `navi` (tick 16 at 30 Hz, 9 at
60 Hz), `teki` (51) and in `throw_release` `piki` (52). `world` and `mode` never
differed. With the float change (no FMA contraction, the software libm; see
"Cross-platform floats" in [README.md](README.md)) that divergence is gone on
the disc tree too.

## Not verified

- The `ctest -R _disc` path on Linux (the runs above call `nectar` directly).
- Software GL was llvmpipe under xvfb in an emulated container; real GPU
  drivers and a native x86-64 host were not tried.
- A Release (`-O3`) build: all logs here are RelWithDebInfo on both OSes.
- Other scenarios on the disc (only the three with `_disc` entries were run).
