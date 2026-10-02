# NOTICE

This repository combines work from several sources under different terms. This
file records who owns what, so anyone reusing the code knows exactly what they
are allowed to take and who to credit.

**Short version:** the NextOS Elite work is MIT — take it, use it commercially,
modify it, ship it, just keep the copyright notice. The decompilation beneath it
is CC0 and belongs to the projectPiki team. The game itself belongs to Nintendo
and is not distributed here in any form.

---

## Component map

| Component | Where | Copyright | License |
|---|---|---|---|
| NextOS Elite contributions | `src/port/`, and the commits on top of the imported decomp tree | 2026 NextOS Elite contributors | **MIT** — see [LICENSE](LICENSE) |
| Pikmin decompilation | `src/`, `include/` (everything outside `src/port/`) | The projectPiki contributors | **CC0 1.0** — see [projectPiki/pikmin](https://github.com/projectPiki/pikmin) |
| Reconstructed game code and data it describes | the decompiled sources; disc image never distributed | Nintendo | Not licensed to anyone here |
| Embedded font texture (`include/bigFont_*.h` upstream) | **removed here** — read from the player's disc at boot | Nintendo | Not licensed to anyone here |
| Aurora | `extern/aurora/` (submodule) | 2022 Luke Street | MIT — `extern/aurora/LICENSE` |
| Aurora GLES/GLES3 backend | `extern/aurora/lib/gl/`, `lib/gfx/`, `lib/gx/` | Brian Degenhardt (`bmdhacks`), for the Dusklight project | MIT, as part of Aurora |
| nod (disc image reader) | fetched by Aurora's build | Luke Street | MIT |
| SDL3 | linked at runtime, built from a Mali-fbdev tree | The SDL contributors | Zlib |
| Mali driver (`libMali.so`) | **not** in this repository — you supply it | ARM / Amlogic | Proprietary; not redistributed |

---

## What the NextOS Elite MIT grant covers

These are ours, and you may take them under the MIT terms in [LICENSE](LICENSE):

- **The port layer** (`src/port/`). The Pikmin decompilation targets the
  GameCube: it has no PC target, no CMake, no SDL and no Aurora. Everything that
  makes it a running Linux program is this layer — a cooperative scheduler that
  gives Dolphin OS threads the console's one-thread-at-a-time semantics, a
  retrace clock, the frame pump, the disc mount, input, and the shutdown path.
- **The software JAudio renderer** (`src/port/jaudio_host.cpp`,
  `jaudio_bank_host.cpp`, `audio_sink.cpp`). Unlike MusyX, JAudio has no PC
  abstraction to fill in: the GameCube build ships raw DSP microcode. This
  renders the 64 native voices in software — AFC and PCM decode, resampling,
  the filter chain, the bus layout — and feeds stereo PCM to SDL3.
- **The OpenGL ES 2.0 work for Mali-450 (Utgard).** Aurora's GLES backend, by
  Brian Degenhardt, targets newer Mali parts with OpenGL ES 3.x. Bringing this
  game down to ES 2.0 on a fixed-function Utgard GPU is our work, as are the
  Mali-specific correctness fixes (forward-Z on this driver, light-mask shader
  specialization, native vertex fetch for indexed arrays without declared
  extents).
- **The anamorphic 16:9 path** — widening the 3D frustum and squeezing the 2D
  layers by the real panel aspect read at runtime, so the game fills a widescreen
  panel without re-rendering at a higher cost.
- **Correctness fixes to the decompiled code under a 64-bit ABI**, where the
  original assumes a 32-bit pointer: allocator unit arithmetic, animation matrix
  caches, texture cache headers, and similar.
- **Packaging and launcher** for NextOS Elite.

## What it does not cover

- The decompilation itself. It is CC0 — you already have every freedom its
  authors could give you, directly from them.
- Aurora and its GLES backend, nod, and SDL. Each keeps its own license.
- Any Nintendo material. Pikmin, its code, art, music and names are Nintendo's.
  This repository contains no game data, and the port refuses to run without a
  disc image you supply yourself.
- `libMali.so`. It is proprietary ARM/Amlogic code, it is not in this
  repository, and the build expects you to copy it off your own device.
