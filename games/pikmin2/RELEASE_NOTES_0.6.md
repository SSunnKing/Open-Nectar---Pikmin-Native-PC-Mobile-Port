# Open Nectar 0.6

The headline: **Open Nectar now runs on Android.** Same code, same disc
images, same saves format — natively on your phone, at 60 FPS on a current
flagship, with touch controls designed for the game rather than a virtual
GameCube pad glued on top. Alongside it, HD texture packs on every platform,
a much faster renderer, and a batch of desktop fixes.

Downloads: `open_nectar_0.6.apk` (Android, arm64), `nectar-windows.zip`,
`nectar-linux.tar.gz`. Saves and settings from 0.5 carry over on desktop.

## Android

One APK, both discs. It bundles the USA Rev. 1 and European builds and picks
the one your image needs. Requirements: Android 10 or newer, 64-bit device,
OpenGL ES 3.0, about 1 GB free. ISO/GCM only — convert compressed images on
a computer first, there is no Dolphin on the phone.

**First run.** The app opens with a single button: pick your disc image with
the system file picker (internal storage, SD card, USB — anything the picker
reaches). The disc is verified and its assets extracted into the app's private
storage; on a recent device that takes well under ten seconds. The image is
read once, never copied, and can be deleted afterwards. From then on the app
opens straight into the game.

**Performance.** The first build that reached the title screen ran at 2 FPS.
This one holds 60 FPS with a hundred Pikmin on screen on a Snapdragon 8 Gen 2
(OnePlus CPH2449) and it got there through the renderer, not by cutting
content:

- Static geometry lives on the GPU. Every model's display list is parsed once,
  its vertices uploaded to a resident buffer, and skinned in the vertex
  shader. Draws that used to be re-decoded from the GameCube FIFO format every
  frame are now a single `glDrawArrays`.
- The vertex ring buffer no longer stalls on Adreno. `glBufferSubData` on a
  buffer the GPU is still reading forced a full CPU↔GPU sync per draw (~300 ms
  of a 360 ms frame). GLES now uses a fenced ring that never orphans storage.
- Shader binaries are cached to disk (`shader_cache/`), so the stutter of
  compiling on first sight of each material happens once per device, not once
  per launch.
- Depth and colour attachments are invalidated when they are no longer needed,
  which is what a tiled mobile GPU wants; depth is 24-bit without a stencil
  the port never used.
- Native heap dropped by roughly 500 MB: two GameCube heaps the port does not
  use were being zeroed page by page at startup. They are now lazily mapped.
- The existing 60/30 FPS option is the knob for mid-range devices.

**Touch controls.** Drawn only when you touch the screen; they vanish as soon
as a Bluetooth or USB controller is used.

- Floating stick on the left half; throw, whistle and disband buttons where a
  thumb rests. Hold disband and drag to move the squad (C-stick).
- Pinch to zoom, camera-angle and centre-camera buttons; a colour icon in the
  HUD cycles which Pikmin you throw. Photo mode has a button too, with the
  stick and a drag driving the free camera.
- Every button prompt the game draws — the blinking A on dialogue boxes, X/Y
  on copy/delete, the whole controls page of the pause menu — is replaced by
  the matching touch icon, and that button appears in the layout while the
  prompt is on screen. Inline prompts inside tutorial text get the icon too.
- Menus are operated by touching what the game draws: file slots (one tap
  selects, a second confirms), yes/no prompts, world-map areas, option rows.
  Swipe left/right flips pause-menu pages. Back on the world map returns to
  the file selector.
- Layout editor: move and resize every button, snap to a grid, saved with the
  rest of the settings. A settings button opens the same menu as F1 on
  desktop.

**Under the hood, for people who build it.** `android/` is a Gradle project
that compiles the game from the same root `CMakeLists.txt` (now usable as a
subdirectory) with SDL2 vendored in `third_party/SDL2-android`. The tree
compiles under Clang without `-fpermissive` tricks; the handful of `src/`
edits that took are explicit casts, not behaviour changes. Release builds are
signed with a project key kept out of the repository; see
`packaging/android/`.

## HD texture packs (all platforms)

Dolphin-format texture packs load in Open Nectar. Drop a pack under
`Load/Textures/<GameID>/` next to `pikmin_settings.conf` (desktop) or install
it from a ZIP/RAR through the F1 menu (Android), then enable it under
**Graphics → Texture packs**. Applies on the next start; the menu says so.

- DDS in BC7, BC1/BC3, RGBA8 and BGRA8, with `_mipN` chains, plus PNG.
- Compressed formats upload directly where the driver supports them and are
  decoded on the CPU (bcdec) where it does not — which is how BC7 packs work
  on Android GPUs that have never heard of BPTC.
- Packs are matched by Dolphin's texture-name hashing, so existing packs made
  for the emulator work unchanged. Tested with the Pikmin 4K pack's 1080p
  and Android editions.
- Nothing is redistributed: you download the pack from its author.

## Desktop

- The renderer work above is not Android-only: the resident-mesh path, GPU
  skinning and the lazy batch-state key are on for desktop OpenGL as well,
  and the shader-binary cache lands in the save folder.
- `PIKMIN_MESH_CACHE=0`, `PIKMIN_SHADER_CACHE=0`, `PIKMIN_GPU_SKINNING=0`,
  `PIKMIN_FB_INVALIDATE=0` and `PIKMIN_FRAME_DUMP=<dir>` are there for A/B
  testing and bug reports. `PIKMIN_TICK_STATS=1` prints the per-frame
  profiler; on Android the same variables go in `env.txt` in the game folder.
- New tools: `tools/bti2png.py` (view the disc's 2D art), `tools/texpack-check.py`
  (validate a pack before installing it), `tools/android_perf_capture.sh`.

## Known limits

- Android supports ISO/GCM only. RVZ/WIA/GCZ still need Dolphin's converter,
  which exists only on desktop.
- The 4K edition of texture packs is for desktop; on a phone use the 1080p or
  Android editions. The loader keeps the original texture for anything that
  exceeds the GPU's limits and says so in the log.
- The Android build is arm64 only. 32-bit devices are not supported and will
  not be.
- Save data on Android lives in the app's private storage: uninstalling the
  app deletes it. Update by installing the new APK over the old one.

## Thanks

To everyone who ran early Android builds and sent logcat captures, and to the
authors of the Pikmin 4K texture pack, whose work made the texture-pack
feature worth building.
