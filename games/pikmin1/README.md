# Open Nectar — Pikmin Native PC/Android Port

<img width="2172" height="476" alt="opennectarlogo (1)" src="https://github.com/user-attachments/assets/71283101-1be5-4ca4-9b16-488320343cc8" />

Native, experimental, and open-source port of *Pikmin* (GameCube, 2001) for **Linux, Windows and Android**. Runs the game code directly on the host system and translates GX to OpenGL; does not use Dolphin or any emulator.

This project builds upon the decompilation by [projectPiki/pikmin](https://github.com/projectPiki/pikmin) and adds a native PC port layer.

## Project Status

**Functional:**
- Native builds for Linux x86-64 and Windows x86-64, Android from the same source
- Most of the game playable from start to finish
- 30, 60 or 120 FPS gameplay, selectable in-game
- Full audio: the game's original JAudio engine, with a software DSP
- TEV specialization for optimal performance
- Controller, keyboard and mouse support, touch controls
- **Both retail discs**: Pikmin USA Rev 1 and Pikmin Europe. The European disc
  carries five languages — English, French, German, Spanish and Italian —
  switchable from the launcher, F1 or the game's own options
- **Pre-rendered movies**: the attract movies play, with sound
- **Widescreen**: the HUD is laid out for 16:9 rather than stretched, and the
  3D view culls to the same shape, so nothing pops in and out at the sides
- Post-processing: antialiasing, restored fog, bloom, ambient occlusion, depth
  of field, texture filtering and colour grading — every one of them optional
- Custom texture packs
- **Local co-op**: Olimar and Louie, two controllers, split screen that can
  merge into a single camera; Louie also playable in single player
- HD character models from Pikmin 3 rips
- Per-pixel lighting and real-time shadow maps (Off/Soft/Normal/Strong),
  both optional
- **Launcher**: installs, updates itself from GitHub releases, and exposes every
  F1 setting, texture packs and HD models outside the game
- **Speedrun mode**: pure GameCube rules, a real-time timer, per-day splits
  against your best run, and an input display
- **Skippable cutscenes**: Start skips any cutscene in normal play

**In development:**
- Some minor graphical differences

## Play directly (without compiling)

Grab the package for your system from [Releases](../../releases).

### Linux

Two downloads with the same content; pick one:

- **`Open_Nectar-x86_64.AppImage`** — one file. Make it executable
  (`chmod +x Open_Nectar-x86_64.AppImage`, or *Allow executing as program* in
  its properties) and open it.
- **`nectar-linux.tar.gz`** — a folder: `tar -xzf nectar-linux.tar.gz`, then
  open `nectar-linux/nectar-launcher`.

It runs on any x86-64 distribution with glibc 2.29 or newer — Ubuntu 20.04+,
Mint 20+, Debian 11+, Fedora, openSUSE Leap 15.3+/Tumbleweed, Arch, Manjaro,
SteamOS. SDL2 and the C++ runtime are built into the executables; the system
only has to provide an OpenGL driver (any Intel/AMD/NVIDIA one) and, for the
file pickers, `zenity` or `kdialog`, which GNOME and KDE already have.

Press **Install** under the Pikmin cover, choose your disc image and a folder.
That folder ends up with the game data, your saves and settings, and two
programs: `nectar` (the game, for your disc's region) and `nectar-launcher`.
Open that launcher to play from then on. `packaging/linux/README.txt` in the
download covers troubleshooting.

### Windows

**What you need**

- 64-bit Windows
- Graphics drivers with OpenGL 3.3 or newer. Intel, NVIDIA and AMD drivers all
  provide it. Windows' generic "Basic Display Adapter" driver does not, and the
  game will not start with it — install your GPU vendor's driver, not the one
  Windows Update supplies
- About 1 GB free where you install it: the extracted assets take roughly
  650 MB, plus the executables

**Installing**

1. Extract `nectar-windows.zip` anywhere. There is no installer to run and
   nothing is written outside the folder you choose.
2. Run `nectar-launcher.exe`.
3. It asks for your Pikmin disc image, and then for a folder to install into.
   Any folder works.
4. It verifies the image, extracts the assets and starts the game.

Installation takes a minute or two, most of it verifying that the disc image is
intact and that every extracted file came out right. That check catches damaged
copies and failing drives, which are the usual reason a game installs fine and
then misbehaves later.

ISO/GCM works without additional tools. For RVZ/WIA/GCZ, the launcher uses
**DolphinTool.exe** on Windows or **dolphin-tool** on Linux from an existing
[Dolphin installation](https://dolphin-emu.org/download/). It looks beside the
launcher and on PATH, then offers a file picker if the tool was not found.
Keep the tool with the rest of its Dolphin installation. It is not downloaded
or included by Open Nectar.

Conversion needs about **1.4 GiB extra free space in your system's temporary
folder**. The source image is unchanged; a separate temporary ISO is verified,
extracted, and removed when the attempt finishes. An interrupted conversion is
never reused. Forced termination may leave a `nectar-disc-*` temporary folder;
remove it only after the launcher and converter have stopped.

The progress display identifies conversion, disc verification, extraction and
finishing separately. If setup fails, **Back to setup** keeps your selections
so you can correct the image, converter or destination. Temporary file locks
at the final extraction step are retried for up to 2.5 seconds; persistent
failures show the preserved extraction path instead of silently starting over.

Once the game starts, **F1** opens graphics, controls and gameplay settings.

**Playing afterwards**

Go to the folder you installed into and run `nectar-launcher.exe` again, then
click the Pikmin cover.

You can also run `nectar.exe` directly, but only from inside that folder: the
game looks for its `assets` folder relative to the current directory.

**Installing without dialogs**

From `cmd` or PowerShell:

```
nectar-launcher.exe --rom C:\path\to\Pikmin.iso --install-dir C:\Games\OpenNectar
```

Add `--extract-only` to install without launching the game afterwards. This
works over Remote Desktop and on machines with no desktop session.
For a compressed image, add `--dolphin-tool C:\path\to\DolphinTool.exe` if
the converter is not beside the launcher or on PATH. The same option accepts
the path to `dolphin-tool` on Linux.

**The console window is intentional**

The game opens a console window alongside it, printing what it is doing. It is
not an error. This is a young port and those messages are the only thing that
explains a failure, so they are left visible on purpose. To keep them:

```
cd C:\Games\OpenNectar
nectar.exe > log.txt 2>&1
```

**Where your files live**

Everything stays in the installation folder:

- `save\card0\` — your save files, as ordinary files on disk
- `pikmin_settings.conf` — the F1 menu settings
- `assets\` — the extracted game data

To move the installation elsewhere, use **Move Install** in the launcher (or
copy the folder). To remove it, delete it.

**If something goes wrong**

| Symptom | Cause |
|---|---|
| SmartScreen warns about the launcher | The .exe files are not signed: *More info* → *Run anyway* |
| "Could not initialize window/OpenGL" | Graphics drivers too old, or the generic Windows display driver |
| Starts but finds no data | Run it from the installation folder, not from elsewhere |
| The image is rejected | It must be Pikmin USA Rev 1 or Pikmin Europe. RVZ/WIA/GCZ also needs the Dolphin converter; ISO/GCM does not |
| Disc conversion fails | Check the temporary folder's free space and select the converter from a complete Dolphin installation, or use ISO/GCM |

### Android

**What you need**

- Android 10 or newer on a 64-bit (arm64) device with OpenGL ES 3.0 — in
  practice, any phone or tablet from 2019 on
- About 1 GB free in internal storage
- Your disc image in ISO/GCM. Compressed images (RVZ/WIA/GCZ) are not
  supported on Android: convert them to ISO with Dolphin on a computer first

**Installing**

1. Download `open_nectar_<version>.apk` from [Releases](../../releases) and
   open it on the device (from the browser's downloads or the Files app).
   Android asks once to allow installs from that app.
2. Open Nectar. The first screen asks for your disc image: pick it with the
   system file picker from wherever it is (internal storage, SD card, USB).
3. It verifies the image, extracts the assets into the app's private storage
   and starts the game. From then on the app opens straight into the game.

One APK carries both the USA Rev. 1 and European builds; the installer picks
the one your disc needs. Touch controls are drawn on screen (every button
the game mentions appears in the layout, and the **layout** button lets you
move and resize them); Bluetooth and USB controllers work too. The
**settings** button opens the same menu as F1 on desktop.

To update, install the new APK over the old one — assets and saves stay.
Uninstalling deletes them.

### Both platforms

The launcher asks for your disc image, extracts the assets it needs and starts the
game. Your disc image is never copied or modified.

Supported discs:

| Disc | Game ID | Languages |
|---|---|---|
| Pikmin USA Rev 1 | `GPIE01`, revision 1 | English |
| Pikmin Europe | `GPIP01`, revision 0 | English, French, German, Spanish, Italian |

Each release needs its own executable — the game's code is compiled here, and it
differs between releases — so the package carries both and the installer picks
the one your disc needs. The European disc installs all five languages and
starts in English; change it in the launcher's **Settings**, in **F1 → Language**
or in the game's own options, without reinstalling.

### The launcher

After installing, the launcher in the game's folder is the way in:

- **Games** — click the cover to play. The button underneath updates the game.
- **Settings** — every option of the in-game F1 menu, plus texture packs, HD
  models and language, saved straight to `pikmin_settings.conf`.
- **Move Install** — moves the whole installation, saves included, to another
  folder or drive.

When a new release is published, the launcher says so, **What's new** shows that
release's notes, and **Update** downloads and installs it by itself: saves, settings, texture packs and the extracted
game data stay as they are. A newly downloaded package can also update an
existing installation: open its launcher, press **Update** and choose the
installed folder. The disc image is not needed either way.

### Launcher options

```sh
nectar-launcher --rom /path/to/Pikmin.iso --install-dir /path/to/installation
nectar-launcher --extract-only    # extract assets only, don't run
nectar-launcher --help
```

## Build from source

### Requirements

- CMake 3.16+
- C++17 compiler (GCC 10+ or Clang 12+)
- SDL2
- OpenGL
- zenity (optional on Linux, for the graphical dialog)

### Linux

```sh
# Debian/Ubuntu
sudo apt install build-essential cmake pkg-config libsdl2-dev libgl1-mesa-dev zenity

# Arch Linux
sudo pacman -S base-devel cmake sdl2 mesa zenity

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

### Choosing which release to build

The game's own code is compiled here, and it is conditional on which retail
release it came from in some four hundred places, so this is a build-time
choice rather than a setting. It defaults to USA Rev 1:

```sh
cmake -S . -B build-pal -DPIKMIN_GAME_VERSION=VERSION_GPIP01_00
cmake --build build-pal -j"$(nproc)"
```

The release packages carry both, and the launcher installs whichever the disc
asks for, named `nectar`. `packaging/linux/build-release.sh` and
`packaging/windows/package-standalone.sh` each build both executables; see
[docs/COMPILAR_RELEASES.md](docs/COMPILAR_RELEASES.md) for every release
package (Linux tar.gz and AppImage, Windows zip, Android APK).

### Windows (cross-compiled from Linux)

The Windows executable is built with MinGW-w64, cross-compiled from Linux.

SDL2 for MinGW is expected in `third_party/SDL2-mingw64`. It is not committed to
the repository; download `SDL2-devel-<version>-mingw.tar.gz` from the
[SDL releases](https://github.com/libsdl-org/SDL/releases) and extract its
`x86_64-w64-mingw32` directory there, so that
`third_party/SDL2-mingw64/lib/libSDL2.a` exists.

```sh
sudo apt install g++-mingw-w64-x86-64

cmake -S . -B build-windows \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw64.cmake \
      -DCMAKE_BUILD_TYPE=Release
cmake --build build-windows -j"$(nproc)"
```

The result is `build-windows/bin/nectar.exe`. SDL2 and the MinGW runtime are
linked into it (`PIKMIN_STATIC_RUNTIME`, on by default for Windows), so it
needs no DLLs beside it.

To ship a folder with both USA and PAL builds:

```sh
packaging/windows/package-standalone.sh
```

That writes `packaging/windows/out/nectar-windows/` with `nectar.exe`,
`nectar-pal.exe` and `nectar-launcher.exe`, and zips it as
`packaging/windows/out/nectar-windows.zip`.

### Run after building

```sh
./build/bin/nectar-launcher --rom /path/to/Pikmin.iso --install-dir ./my-installation
```

Or, for development directly against a checked-out asset tree in `./assets/`:

```sh
./build/bin/nectar
```

### Android (APK)

The Android project lives in `android/` and builds the game through the same
root `CMakeLists.txt` (both disc versions, GLES, arm64 only). It needs Android
Studio's JDK and SDK with NDK 28 and CMake 3.22; `android/local.properties`
points at the SDK.

```sh
cd android
JAVA_HOME=$HOME/Android/jdk ./gradlew assembleDebug   # debug-signed, for adb
```

A release APK is signed with the project key, which is kept outside the
repository (see `packaging/android/README-firma.txt`). With the key in place:

```sh
./packaging/android/package-apk.sh
```

produces `packaging/android/out/open_nectar_<version>.apk`, its SHA-256 and
the README that goes with it.

## Project structure

```
.
├── src/           # Decompiled game code from original
├── include/       # Game headers
├── pc_port/       # Native port layer (audio, video, input)
├── cmake/         # Toolchain files
├── config/        # Build configuration per region
├── packaging/     # Packaging scripts
├── third_party/   # Third-party code and its licences
├── tools/         # Development utilities
└── CMakeLists.txt
```

- `src/` and `include/` contain the decompiled code from the original game
- `pc_port/` is the native layer that translates GX→OpenGL, handles audio/input
- Port modifications go in `pc_port/`, not in `src/`

## Technical architecture

The port works as follows:

1. **Game code** (`src/`) is compiled as a static library
2. **Native layer** (`pc_port/`) implements GameCube APIs (GX, AI, PAD, etc.)
3. **GX→OpenGL translation**: GX display lists are translated to OpenGL shaders
4. **TEV specialization**: Optimized shaders are generated per material configuration
5. **Audio**: the game's original JAudio engine runs unchanged; only the boundary where the GameCube's DSP chip used to sit is replaced by a software renderer

## Controls and port features

This port includes significant improvements over the original GameCube game, designed to take advantage of keyboard, mouse, and PC capabilities.

### Keyboard and mouse

**Keyboard** (defaults; all of it remappable from the F1 menu):

| Action | Key |
|--------|-----|
| Movement (left stick) | W A S D |
| Pikmin formation (C-stick) | T (up) F (left) G (down) H (right) |
| D-Pad | Arrow keys |
| A — throw / confirm | Space |
| B — whistle / cancel | Left Shift |
| X — dismiss | X |
| Y — group | Y |
| Z — change camera | Z |
| L — rotate camera left | Q |
| R — rotate camera right | E |
| Start — pause / skip a cutscene | Enter |
| Bomb (with Bomb Control on) | B |
| Port settings menu | F1 |
| Switch control mode | F2 |
| Photo mode | F3 |

**Mouse:**
- In pointer mode, the mouse directly controls the game cursor
- Left click: A — throw
- Right click: B — whistle
- Middle click: Z — change camera
- Mouse wheel: picks the Pikmin colour to throw, or zooms the camera (see below)
- Mouse offers precision impossible with an analog stick

### F1 Menu — Port settings

Press **F1** at any time to open the configuration menu:

**Video:**
- **Resolution**: Any monitor resolution, including ultrawide
- **Display mode**: Windowed, fullscreen, or borderless
- **Render scale**: Internal resolution independent of output (improves performance on slower GPUs)
- **VSync**: Vertical synchronization on/off
- **Refresh rate**: Force a specific refresh rate

**Graphics:**

Every effect has an Off, and each applies as you move through the menu, against
the scene behind it. The reference machine for this project is a GTX 1050, so
none of this is mandatory.

- **Depth of field**: the focus plane follows the captain — what sits at his
  distance stays sharp, what is nearer or further falls away. Four steps. The
  sharp band is a fraction of the camera's distance to him, so it behaves the
  same in the close follow view and the far one. It stands down during cutscenes
- **Ambient occlusion**: contact shadows in creases, under leaves, where a
  Pikmin meets the ground
- **Bloom**: three named steps rather than a slider
- **Antialiasing**: FXAA, applied to the finished image. Deliberately not MSAA —
  Pikmin's undergrowth is alpha-tested quads, and MSAA does nothing for edges
  that are not geometry
- **Fog**: the game's own, which the port used to discard. On is the original
- **Texture filtering**: up to 16x anisotropic, with mipmaps the port never
  built. Changing it re-applies to everything already loaded
- **Colour grading**: gamma, brightness and saturation

**Language** (European disc only): switches between the five languages on the
disc. It takes effect the next time you start the game.

**Gameplay:**
- **FPS mode**:
  - `30 FPS (stable)`: Original game behavior
  - `60 FPS (experimental)`: 60 FPS gameplay
  - `120 FPS (experimental)`: 120 FPS gameplay, on a display that can show it
- **Chain Pikmin actions**: When active, Pikmin automatically look for more work after completing a task. Example: a Pikmin thrown at a flower destroys it and then automatically carries the pellet to the Onion. Disabled by default to maintain fidelity to the original

**Mods:**
- **Mouse wheel**: chooses what the wheel does, one or the other:
  - `Pikmin Colour`: cycles which colour to throw next, through the colours you actually have with you. With one colour it does nothing, with two it alternates, with three it cycles. If the chosen colour is out of reach, the captain still grabs the nearest Pikmin
  - `Camera Zoom`: pulls the camera between 0.45× and 2.50× of its normal distance
- **Pikmin limit**: how many Pikmin may be on the field at once, from 50 up to 999. The original is 100. The Onion, the field, the matrix pool and the shape cache are all sized from this number, so it applies when a stage loads rather than mid-day. Tested to 945 Pikmin on screen; high values do cost frame rate, and how much depends on your machine
- **Day length**: 5 to 30 minutes of play per in-game day, or Infinite. The original 13.5 minutes is the default. This stretches the game's own clock, so nothing moves faster or slower — sunset simply arrives sooner or later. Infinite never applies to day 1, whose tutorial ends at sunset
- **Bomb Control**: a Bomb button (B on the keyboard; assign one in Gamepad Bindings). A Yellow Pikmin with a bomb rock throws it at the cursor, or drops it lit at its feet when the cursor is too close. Ones already thrown and waiting go first
- **Hide Olimar's Texts**: no text boxes while you play (the first Pikmin, every ship part, tips). The cutscene that goes with each text is skipped too, as if you had closed it at once. The ending texts still show
- **Enemy health** (Cheats): a share of the original, down to Insta Kill. It applies to bosses as well

**Permadeath** is not in this menu, because it belongs to a save file rather than to the port. You choose it once, when you create the file, and the file screen marks the files that carry it. Lose Olimar and the run ends and that save is erased — through the game's own delete, so the slot ends up exactly as a manually deleted one does — and the file screen tells you so. A copy of a permadeath file is a permadeath file.

**Photo mode** is on **F3**. It freezes the world and hands you the camera:

| | |
|---|---|
| `WASD` | move along the look direction |
| `Space` / `Ctrl` | up / down |
| Arrow keys | look |
| `Q` / `E` | tilt; `R` levels |
| `Shift` / `Alt` | faster / slower |

**Controls:**
- **Control scheme**: Classic (GameCube) or Mouse pointer
- **Mouse sensitivity**: 0.1x to 5.0x
- **Stick dead zone**: 0-127
- **Invert sticks**: Options for main stick and C-stick
- **Hold to Pluck / Whistle Pluck**: keep the button held, or hold the whistle
  over sprouts, to pluck them one after another (both optional)

### Speedrun mode

**Speedrun** sits on the title screen under Start. It opens its own menu:
**Start Run**, **Best Times** and **How It Works** (shown by itself the first
time).

- **Vanilla rules.** For the run, every cheat, mod and port option that changes
  play returns its original value: day length, health, speeds, Pikmin limit,
  30 FPS simulation, classic controls (cursor on the stick, no gyro), no Quick
  Grab, Lock-On, free camera, cutscene skipping and so on. Graphics, texture
  packs and button mapping stay as they are. Leaving Speedrun brings your own
  settings back.
- **Timing as on speedrun.com** (All Parts): real time, loads and cutscenes
  included, from the new game to the Secret Safe. Start Run goes straight into
  a new game with Olimar; there is no file select.
- **Splits**: one per day, named after the area and visit ("Forest of Hope 2"),
  shown under the timer with the difference to your best run — green ahead,
  red behind, gold for your best segment ever.
- **Best Times**: personal best with its splits, Sum of Best and the last five
  runs. Records are saved when a run finishes.
- **Input display**: under the timer, the controller in use (Xbox, PlayStation
  or Switch labels) or a full keyboard and mouse, lighting up what you press.
- Speedrun has its own memory card and records in `save/speedrun/`, apart from
  your normal saves.

### Control modes

**Classic Mode (GameCube):**
- WASD controls cursor and movement simultaneously
- Identical behavior to the original
- Recommended for controllers

**Mouse Pointer Mode:**
- Mouse controls the cursor with absolute precision
- WASD available for independent movement
- Ideal for strategy with quick selection

### Controller support

The port supports any SDL2-compatible controller:
- Xbox, PlayStation, Nintendo Switch Pro
- Generic controllers with automatic mapping
- Customizable configuration from the F1 menu
- Vibration supported where available

### Port value-added features

**Improvements over the original:**
- **Arbitrary resolution**: From 480p up to 4K and ultrawide, no hacks needed
- **Widescreen that is not a stretch**: the HUD is laid out for the frame it is
  drawn in, and the 3D view culls to the same shape
- **60 and 120 FPS**: The original ran gameplay at 30
- **Mouse control**: Precision impossible on GameCube, including wheel shortcuts
- **Improved Pikmin AI**: Chain tasks automatically (optional)
- **Faster plucking**: hold the button or the whistle over sprouts (optional)
- **Skippable cutscenes**: Start skips any cutscene, including arriving at an area
- **Speedrun mode**: vanilla rules, timer, splits and best times built in
- **Superior performance**: TEV specialization generates optimal shaders per material
- **No emulation**: Native x86-64 code, no Dolphin overhead
- **Instant saves**: Save files are accessible on disk
- **Portable**: The release packages run without installing dependencies

## Performance

The validated configuration is **1920x1080 with native internal resolution** (`renderScale = 1`) on a GTX 1050 4GB with Mesa 26.0.

The game integrates by elapsed time, so raising the frame rate does not speed up gameplay. 120 FPS needs a display that can present it; on a 60 Hz panel with VSync the game still shows 60.

**Memory**: entering a stage used about 4 GB and kept climbing. It now settles
around 600 MB.

**Laptops with switchable graphics**: the game asks for the dedicated GPU on
both platforms, and only where that hardware is present. The log line
`[PC Port] GPU:` reports which one it got. `NECTAR_NO_PRIME=1` turns the request
off. Desktops with only an NVIDIA card are left alone: there is nothing to
offload to.

**`GPU: ... llvmpipe`** means OpenGL is running in software. On Linux with
NVIDIA this is almost always the system, not the game: after a driver update
the kernel module and the libraries no longer match until you **reboot**.
`glxinfo -B` failing or also showing llvmpipe confirms it.

## Debug and environment variables

```sh
PIKMIN_TEV_SPECIALIZE=0      # Disable specialization (slower)
PIKMIN_TEV_MAX_STAGES=N      # Limit shader complexity
PIKMIN_GL_CHECK=1            # Check OpenGL errors
PIKMIN_DUMP_SHADERS=1        # Dump generated shaders
PIKMIN_PERF_STATS=1          # GPU statistics
PIKMIN_TICK_STATS=1          # CPU statistics per tick
PIKMIN_AUDIO_STATS=1         # Audio mixer statistics
PIKMIN_WHEEL_TRACE=1         # Trace mouse wheel colour selection
PIKMIN_H4M_DEBUG=1           # Trace the pre-rendered movie player
PIKMIN_NO_H4M=1              # Skip the attract movies
PIKMIN_DOF_DEBUG=1           # Focus distance and scene depth
PIKMIN_PROJ_DEBUG=1          # One frame's projections, once a second
PIKMIN_MENU_PILLARBOX=1      # Menus boxed in 4:3 instead of widescreen
NECTAR_LANGUAGE=es           # Language (European disc), overrides the setting
NECTAR_NO_PRIME=1            # Do not ask for the dedicated GPU
NECTAR_PRIME_EGL=1           # Also pin EGL to NVIDIA (can fail on Wayland)
```

The game binary also accepts `--audio-self-test`, which walks scenes, stages,
boss transitions, muting, sound effects and cinematic streams without opening a
window, and reports whether each produced audio.

## AI disclosure

This project was developed with the assistance of AI tools. The generated code was reviewed, tested and integrated by me. I mention it because I'd rather be upfront about how this was built.

## Legal requirements

This repository **does not contain ROMs or Nintendo resources**.

To play you need to legally dump your own disc of one of:
- **Pikmin USA Rev. 1** (`GPIE01`, revision 1)
- **Pikmin Europe** (`GPIP01`, revision 0)

Do not upload ROMs, extracted assets, keys, or proprietary material to issues or pull requests. See [LEGAL.md](LEGAL.md).

## Contributing

Corrections, documentation, tests, and structural improvements are accepted. Before opening a PR:

1. Read [CONTRIBUTING.md](CONTRIBUTING.md)
2. Build in Release and run tests
3. Test in-game for changes that affect gameplay

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

## License

The code is offered under [CC0 1.0](LICENSE.MD). This license does not grant rights over *Pikmin*, its resources, trademarks, or any other material from Nintendo or other third parties.

## Credits

- Original decompilation: [projectPiki/pikmin](https://github.com/projectPiki/pikmin)
- Software DSP and sound bank loader: [NextOs-Ports/pikmin-nextos](https://github.com/NextOs-Ports/pikmin-nextos), adapted here from SDL3 to SDL2. Licence and attribution in [third_party/nextos-audio/](third_party/nextos-audio/)
- Native PC port: Open Nectar Community

## Links

- [LEGAL.md](LEGAL.md) - Legal notice
- [CONTRIBUTING.md](CONTRIBUTING.md) - Contribution guide
