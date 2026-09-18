# VR mode

Pikmin in a headset, through OpenXR. The game still runs once per frame,
unchanged; VR replaces the camera, draws the world once per eye, and puts
everything that is not the world on a floating panel.

Supported: Windows with a PC-connected headset (Quest over Link, Air Link or
Virtual Desktop; anything with an OpenXR runtime that speaks OpenGL), and
Quest 2/3 standalone through the Android build.

## The two rigs

**Third-person** — you float behind and above the captain, looking where you
look rather than where the game's camera pointed. The view follows him with a
little lag, and turning is snap turns rather than a slow sweep.

**Tabletop** — the level is a miniature in front of you, the captain at its
centre. Comfortable (nothing moves you), and it suits a game about directing a
crowd from above. Grab, spin and resize the table to taste.

Switch between them at any time: hold the VR button and press Y.

## Controls (Quest Touch)

| Control | Action |
|---|---|
| Left stick | Move the captain, relative to where you are looking |
| Right stick left/right | Turn (snap), or spin the table |
| Right stick up/down | Move the view closer or further, or resize the table |
| **Left stick click** | **Face where you are looking**: puts the captain (or the table) back in front of you |
| Right trigger | A: pluck; hold to pick a Pikmin up, release **or flick your hand forward** to throw |
| Right grip | Whistle, centred where you point |
| Point the right controller | Moves the cursor; the laser shows where it lands |
| Right A / B | A / B |
| Left X / Y | Disband / map |
| Left menu | Start |
| Left trigger | L |
| Right stick click | R |

Set `right_stick_turns=0` to go back to the GameCube arrangement, where the
right stick swarms and turning lives on the VR button.

The left grip is the **VR button**. While it is held:

| Chord | Action |
|---|---|
| Right stick | Swarm (the C-stick) |
| Move the left hand | Drag the table (tabletop) |
| X | Face where you are looking |
| Y | Swap third-person and tabletop |
| Menu | Open the port's own settings menu |
| Left stick | D-pad |
| Left stick click | Z |

## Settings

`pikmin_vr.ini`, written next to the game (on Android, in the game's folder) and
re-read at startup:

| Key | Default | What it does |
|---|---|---|
| `mode` | `third_person` | `third_person` or `tabletop` |
| `third_person_scale` | 100 | World units per metre: lower makes the world bigger around you |
| `third_person_distance` | 300 | How far behind the captain the view sits |
| `third_person_height` | 220 | How far above him |
| `tabletop_scale` | 1000 | World units per metre on the table: higher shrinks the level |
| `snap_turn_degrees` | 30 | Turn step |
| `follow_seconds` | 0.25 | How lazily the view follows the captain |
| `supersample` | 1.0 | Multiplies the runtime's recommended eye resolution |
| `left_handed` | 0 | Swaps the pointing and off hands |
| `right_stick_turns` | 1 | Pointing hand's stick turns the view; 0 puts swarming there instead |

`PIKMIN_VR=0` turns VR off entirely. `PIKMIN_VR_MODE=tabletop` overrides the
rig for one run. `PIKMIN_VR_INPUT_DEBUG=1` traces the controller buttons, and
`PIKMIN_VR_DUMP=<dir>` writes both eyes and the panel as PPM every few seconds.

## Building

**Windows.** Needs the OpenXR loader; MSYS2 has it:

```sh
pacman -S --needed mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja \
    mingw-w64-x86_64-pkgconf mingw-w64-x86_64-SDL2 mingw-w64-x86_64-openxr-sdk
tools/build_windows_native.sh
```

`-DPIKMIN_VR=OFF` builds without it.

**Quest (standalone).** The Gradle build pulls the Khronos OpenXR loader for
Android and turns the game activity into an immersive one:

```sh
cd android && ./gradlew assembleDebug
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

The game data is not in the APK. Either open the installer on the headset with
your own disc image:

```sh
adb push Pikmin.iso /sdcard/Download/
adb shell am start -n org.opennectar/.InstallerActivity
```

or copy assets extracted on a PC (`nectar-launcher --extract-only`) straight
into the game's folder, which on Android 11 and newer has to go through
`run-as`:

```sh
adb push game/assets /data/local/tmp/nectar-assets
adb shell run-as org.opennectar sh -c 'mkdir -p files && cp -r /data/local/tmp/nectar-assets files/assets'
adb shell rm -r /data/local/tmp/nectar-assets
```

## How it works

`pc_vr.h` is the whole interface. Three hooks carry it:

- **The camera.** `newPikiGame.cpp` calls `pc_vr_scene_view` after the game has
  settled on its camera and before anything is drawn, and writes the headset's
  head pose into it. Because the game derives movement direction, billboards
  and culling from that camera, all of them follow the head without further
  changes. Frustum culling is switched off in VR: the game's frustum is far
  narrower than what a player can look at.
- **The world.** `pc_gfx_vr_world_begin/end` bracket the 3D world. In between,
  every GL draw is issued once per eye, into that eye's target, with the eye
  offset and lens folded into the projection uniform -- the one matrix every
  path goes through. Game code runs once: the world simulation lives inside the
  draw and cannot be run twice.
- **The interface.** Anything drawn outside that span lands in the ordinary
  render target over transparency and is shown as a quad layer, which is also
  how whole flat screens (title, file select, results) are presented.

`pc_vr_rig.*` holds the mapping between tracking space and the world, and is
plain maths with no OpenXR or GL in it, so `pc_vr_rig_test` can check the
conventions -- eye offsets, the GameCube-style frustum, snap turns, the table --
without a headset. Run it with `ctest -R pc_vr_rig_test`.

`pc_vr_xr.cpp` is everything platform: session, swapchains, frame submission,
Touch bindings, haptics. It has a Windows/WGL branch and an Android/EGL one.
