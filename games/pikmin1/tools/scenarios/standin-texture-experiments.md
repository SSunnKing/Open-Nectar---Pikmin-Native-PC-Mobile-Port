# Stand-in texture and bundle experiments (#67)

Each run copies the generated stand-in tree (`nectar_standin_generator`; 130 files when these runs were made, 314 on 2026-10-28) to a scratch directory,
deletes or replaces exactly one file (or the stated set), and runs
`SDL_AUDIODRIVER=dummy PIKMIN_SCENARIO_DATA_DIR=<copy> PIKMIN_SCENARIO_STAGE=stages/sandbox.ini nectar --scenario boot_smoke`
from an empty scratch working directory (30 ticks per second, hidden window). Build: RelWithDebInfo, macOS arm64,
2026-10-27. Baseline (nothing changed): exit 0, `PASS boot_smoke`, stage ready after 5 boot ticks, intro done after 92 (15 now that the stand-in cinemas exist; the rows below were not re-run).

Exit 134 is `[PANIC]` then `abort()` (SIGABRT). Exit 139 is SIGSEGV; the scenario run prints no crash block, so the
location below is from the open log (the last `DVDOpen` before the stop) plus the issue's reading.

## Check first 1: one missing file

| Change | Exit | Observed | Matches the issue's prediction |
|---|---|---|---|
| delete `consFont.bti` | 139 | `FAILED to open consFont.bti`, then SIGSEGV during `System::init` (before the stage loads) | yes |
| delete `effects/shadow.txe` | 139 | `FAILED to open effects/shadow.txe` after the stage is selected, then SIGSEGV | yes |
| delete `effects/tex/effects.bun` (no loose copies exist) | 134 | `[PANIC] system.cpp:1378: Cannot open file. [effects/tex/star8_i.bti]` | partly: the first texture asked for is `star8_i.bti`, not `ps_glow.bti` |
| `effects/tex/effects.bun` without `ps_glow.bti` | 134 | `[PANIC] Cannot open file. [effects/tex/ps_glow.bti]` | yes |
| `effects/tex/effects.bun` without `hokori4.bti` | 134 | `[PANIC] Cannot open file. [effects/tex/hokori4.bti]` | yes (abort; the message is the open failure, not `NO TEX`) |
| delete `effects/pcr/effects.bun` | 134 | `[PANIC] Cannot open file.[effects/pcr/ore_rec.pcr]` | yes (missing `.pcr` data) |
| delete `screen/eng_tex/screen.bun` | 134 | `[PANIC] Can't load texture [0_64.bti]` | yes |
| `screen.bun` without the `0_64.bti` entry | 134 | `[PANIC] Can't load texture [0_64.bti]` | yes |
| delete `screen/eng_blo/play09.blo` | 134 | `[PANIC] Cannot open file.[screen/eng_blo/play09.blo]` | yes |
| delete `intro/nintendo.bti` | 0 | FAILED line, `PASS boot_smoke` | yes (tolerated) |
| delete `intro/item_0_9.bti` | 0 | FAILED line, `PASS boot_smoke` | yes |
| delete `intro/map1.bti` | 0 | no FAILED line: the sandbox run never opens it (the manifest's `map1.bti` open comes from the disc boot's stage banner), `PASS` | yes (no effect) |
| delete the three `intro/` files together | 0 | `PASS boot_smoke` | yes |
| delete the 10 loose HUD textures (`a_btn`, `b_btn`, `dot_24`, `dot_32`, `w08_160`, `ws08_160`, `ws08_yel`, `x_btn`, `y_btn`, `z_btn`) | 0 | FAILED lines only, `PASS boot_smoke` | not predicted: these loose files are not needed by `boot_smoke` |
| delete `archives/blo_eng.dir` | 0 | FAILED line, `PASS boot_smoke` | not predicted (control: not a texture) |

## Check first 2: bundle entry names

The generated `effects/tex` directory holds only `effects.bun`, so the baseline boot already proves that the 54
entry names equal the lookup paths exactly (every texture comes from the bundle, no loose fallback exists).

| Change | Exit | Observed |
|---|---|---|
| every entry renamed `X_effects/tex/...` | 134 | `Cannot open file. [effects/tex/star8_i.bti]` |
| every entry reduced to its basename (`star8_i.bti`) | 134 | same abort |
| bundle with count 0 | 134 | same abort (an empty bundle is valid to the loader, but then every effect texture is missing) |
| `screen.bun` entries renamed `X_...` | 134 | `Can't load texture [0_64.bti]` |
| `ps_glow.bti` entry replaced by a 6x6 BTI | 134 | `[PANIC] Texture effects/tex/effects.bun is not correct size 6 x 6` (the message names the bundle path, not the entry) |

So the bundle writer must use the full `effects/tex/*.bti` names, as it does, and a wrong-size entry aborts at boot.

## Check first 3: `consFont.bti`

The generator writes IA4 128x64 (`02 01 0080 0040`, alpha 0xFF); the baseline boot is the "IA4 128x64 alpha 0xFF
boots" result: exit 0, no crash. The negative-glyph-width claim is not observable in the log and was not measured.

| Replacement | Exit | Observed |
|---|---|---|
| IA4 128x64, alpha 0xFF (the stand-in) | 0 | `PASS` |
| RGB5A3 8x8 | 0 | `PASS` (no crash; the issue predicts wrong glyph metrics, not a crash) |
| IA4 128x64, all-zero alpha | 0 | `PASS` (no crash; the predicted negative widths are not visible in a headless boot) |
| IA4 128x4 (height under 8) | 0 | `PASS` (the predicted out-of-bounds read did not fault in this run; it is undefined behaviour, so the IA4 128x64 rule stays) |

## Wrong-size and malformed textures (loader rules)

| File | Change | Exit | Observed |
|---|---|---|---|
| `intro/nintendo.bti` | 6x6 RGB5A3 (not a 4x4 multiple) | 134 | `[PANIC] Texture intro/nintendo.bti is not correct size 6 x 6` |
| `intro/nintendo.bti` | zero-length file | 134 | `[PANIC] Bti file has imageData at offset 0!!!` |
| `intro/nintendo.bti` | 8x8 header, only 16 data bytes | 0 | `PASS` (a short read is not detected) |
| `intro/nintendo.bti` | 16x16 RGB5A3 | 0 | `PASS` (size is not checked against the stand-in's) |
| `intro/nintendo.bti` | format code 9 | 0 | `PASS` (`TEX_FMT_NULL`; the draw is not reached before the overlay ends) |
| `effects/shadow.txe` | 6x6, 16x16, 8x8 truncated, 8x8 I4 | 0 | `PASS` (`importTxe` has no size check) |

## Fidelity decision for the manifest textures and bundles

All 65 `.bti`, `.txe`, `.bun` and `.anm` paths marked OK in `boot-manifest.txt` were compared with the generated
tree. Every `.bti`, `.txe` and `.bun` path (18 files, including `effects/pcr/effects.bun`, `effects/tex/effects.bun`
and `screen/eng_tex/screen.bun`) is written; none of them is on `standin-open-allowlist.txt`. The only manifest
paths absent from the tree were then 46 of the 47 `.anm` per-model animation bundles (all 47 are in the tree on 2026-10-28). They are #66's domain (animation bundles), a missing
`.bun`/`.anm` is skipped silently by `StdSystem::loadBundle`, and the allowlisted ones are tolerated by the green
`scenario_*` runs, so no stand-in is added here. Stand-ins that boot_smoke does not need (the 10 loose HUD
textures, `intro/item_0_9.bti`, `intro/map1.bti`) stay because the disc boot opens them and the generator test
requires every manifest texture to exist; deleting them is tolerated (rows above).

## Since run

The comparison against the real install (APFS clones, one file or bundle entry removed per run), the `halowhit.txe`,
`rootRing.txe` and `intro/map1.bti` checks and the glyph-width measurement are in `verification-log.md`, item 3.
Still open: whether particle drawing tolerates a null child texture and null HUD textures generally (moot while every
texture has a stand-in).
