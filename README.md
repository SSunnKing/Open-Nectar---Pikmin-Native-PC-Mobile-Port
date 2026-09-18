# Pikmin VR

Pikmin (GameCube, 2001) in a VR headset, through OpenXR. A fork of
[Open Nectar](https://github.com/SSunnKing/Open-Nectar---Pikmin-Native-PC-Mobile-Port),
the native PC/Android port of the
[projectPiki](https://github.com/projectPiki/pikmin) decompilation.

Not an emulator: the game's own code is compiled for your machine, with the
console's hardware replaced by ordinary equivalents. VR then replaces the
camera, draws the world once per eye, and puts the interface on a panel that
floats in front of you.

**No game data is included.** You supply your own copy of the disc.

## What runs today

| | |
|---|---|
| Windows + PC headset | Quest over Link/Air Link/Virtual Desktop, or any OpenXR runtime with OpenGL |
| Quest 2 / Quest 3 standalone | sideloaded APK, 52-72 FPS measured on a Quest 2 |
| Flat | unchanged; with no headset the game runs exactly as Open Nectar does |

Two ways to play, swapped at any time with **left grip + Y**:

- **Third-person** — you float behind and above the captain, looking where you
  look rather than where the game's camera pointed.
- **Tabletop** — the level is a miniature in front of you with the captain at
  its centre. Nothing moves you, so it is the comfortable one, and it suits a
  game about directing a crowd from above.

## Controls (Quest Touch)

| Control | Action |
|---|---|
| Left stick | Move the captain, relative to where you are looking |
| Right stick | Swarm the Pikmin (the C-stick) |
| **Left stick click** | **Snap the view behind the captain**, looking the way he faces |
| Right trigger | A: pluck; hold to pick a Pikmin up, release **or flick your hand forward** to throw |
| Right grip | Whistle, centred where you point |
| Point the right controller | Moves the cursor; a laser shows where it lands |
| Right A / B | A / B |
| Left X / Y | Disband / map |
| Left menu | Start |
| Left trigger | L |
| Right stick click | R |

The left grip is the **VR button**. While it is held:

| Chord | Action |
|---|---|
| Right stick left/right | Snap turn, or spin the table |
| Right stick up/down | Move the view closer or further, or resize the table |
| Move the left hand | Drag the table (tabletop) |
| X | Re-anchor your play space where you are sitting or standing now |
| Y | Swap third-person and tabletop |
| Menu | Open the port's settings menu |
| Left stick | D-pad |
| Left stick click | Z |

Throwing works either way: let go of the trigger, or flick your hand forward
while holding it. A flick while the trigger stays down picks up the next
Pikmin, so you can throw a line of them without releasing.

## Settings

`pikmin_vr.ini`, next to the game (on Android, in the game's folder), re-read at
startup:

| Key | Default | What it does |
|---|---|---|
| `mode` | `third_person` | `third_person` or `tabletop` |
| `third_person_scale` | 100 | World units per metre: lower makes the world bigger around you |
| `third_person_distance` | 300 | How far behind the captain the view sits |
| `third_person_height` | 220 | How far above him |
| `tabletop_scale` | 1000 | World units per metre on the table: higher shrinks the level |
| `snap_turn_degrees` | 30 | Turn step |
| `follow_seconds` | 0.25 | How lazily the view follows the captain |
| `supersample` | 1.0 desktop, 0.75 standalone | Multiplies the runtime's recommended eye resolution; takes effect on restart |
| `left_handed` | 0 | Swaps the pointing and off hands |
| `right_stick_turns` | 0 | 1 puts turning on the right stick and swarming on the VR button |

`PIKMIN_VR=0` turns VR off. Diagnostics: `PIKMIN_VR_MODE=tabletop`,
`PIKMIN_VR_INPUT_DEBUG=1`, `PIKMIN_VR_DUMP=<dir>`, `PIKMIN_VR_CLEAR_DEBUG=1`,
`PIKMIN_VR_NO_CULL=1`. On Android these go in `env.txt` in the game's folder,
one per line.

## Playing on a Quest

```sh
cd android && ./gradlew assembleDebug
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

Then give it the game's data, either by opening the installer on the headset
with your own disc image:

```sh
adb push Pikmin.iso /sdcard/Download/
adb shell am start -n org.opennectar/.InstallerActivity
```

or by extracting on a PC (`nectar-launcher --extract-only`) and copying the
result in, which on Android 11 and newer has to go through `run-as`:

```sh
adb push game/assets /data/local/tmp/nectar-assets
adb shell run-as org.opennectar sh -c 'mkdir -p files && cp -r /data/local/tmp/nectar-assets files/assets'
adb shell rm -r /data/local/tmp/nectar-assets
```

## Playing on a PC

Windows needs the MSYS2 MinGW-w64 toolchain and the OpenXR loader:

```sh
pacman -S --needed mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja \
    mingw-w64-x86_64-pkgconf mingw-w64-x86_64-SDL2 mingw-w64-x86_64-openxr-sdk
tools/build_windows_native.sh
```

`build-win/bin/nectar-launcher.exe` installs the game data from your disc image
once; after that run `nectar.exe` from the folder it installed into. Start your
headset's runtime first. `-DPIKMIN_VR=OFF` builds without VR.

Linux builds as upstream documents; the VR mode is Windows and Android only so
far, because the session binds to WGL or EGL.

## Notes and known issues

- **Water renders as a dark, opaque sheet** instead of translucent, and
  anything below the surface (including the captain's submerged half) goes dark
  with it. Under investigation; it is not the eye projection, the clear colour
  or the blend override, all of which have been ruled out by testing.
- **There is no sky.** The flat camera never looks above the treeline, so the
  game draws nothing there and you see black. A tinted dome would fix it.
- **Screen-space interface pieces sit on the panel, not in the world**: enemy
  life gauges and carry counts are positioned from a flat projection, so they
  do not line up with what they label. Moving them into the world is the fix.
- **Cutscenes put you where the director's camera is.** Comfortable in
  tabletop, where the table stays put; in third-person the camera moves you.
- Post-processing (bloom, depth of field, ambient occlusion) is off in VR: it
  assumes one symmetric view, and the eyes are neither.
- If the standalone build flickers, close Link/Air Link on the headset: the
  streaming client and the app compete for the immersive slot.
- The settings menu is still the desktop one. A VR panel for the values above
  belongs there and is not written yet.

## How it works

Three hooks, described in [pc_port/vr/README.md](pc_port/vr/README.md):

- **The camera** is replaced with the headset's head pose before the world is
  drawn, so culling, billboards and the stick's movement direction follow your
  head for free.
- **The world** is drawn once per eye into one side-by-side render target, with
  each eye's offset and lens folded into the projection uniform. Game code runs
  once per frame: the world simulation lives inside the draw and cannot be run
  twice. Both eyes share one framebuffer because swapping targets per draw
  makes a tile-based GPU resolve its tiles every time, which cost a Quest 2 more
  than half its frame rate.
- **The interface** lands on a floating panel, which is also how whole flat
  screens (title, file select, results) are shown.

`pc_port/vr/pc_vr_rig.*` is plain maths with no OpenXR or GL in it, so
`ctest -R pc_vr_rig_test` checks the conventions -- eye offsets, the GameCube
frustum, snap turns, the table -- without a headset.

## Credits

[projectPiki](https://github.com/projectPiki/pikmin) for the decompilation,
[Open Nectar](https://github.com/SSunnKing/Open-Nectar---Pikmin-Native-PC-Mobile-Port)
for the native port this forks, and the GameCube decompilation community. The
idea of a VR fork of a decompilation port follows
[TPVR](https://github.com/JoeyAW/TPVR).

## Legal

Independent, unaffiliated with Nintendo. Pikmin and Nintendo's characters,
audio, artwork and names belong to their owners. See [LEGAL.md](LEGAL.md):
no disc images, extracted assets or game data belong in this repository or in
anything built from it.
