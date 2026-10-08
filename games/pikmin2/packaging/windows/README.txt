NECTAR — PIKMIN FOR WINDOWS (x86-64)

A native Windows build. No emulator, no Wine.

This build runs the game's original JAudio sound engine: music, sound
effects and cinematic audio all play.

New in 0.6: HD texture packs. Dolphin-format packs (DDS in BC7/BC1/BC3,
or PNG) load from Load/Textures/<GameID>/ next to pikmin_settings.conf;
enable them under F1 > Graphics > Texture packs, and they apply on the
next start. The renderer keeps static geometry resident on the GPU and
skins it in the vertex shader, and caches compiled shader binaries next
to your saves, so frames are cheaper and the first-sight stutter happens
once per machine. The same code now runs on Android; that package is
separate (open_nectar_<version>.apk).

From 0.5: a Hard mode alongside Permadeath (tougher enemies, shorter days,
80 Pikmin on the field), glossy surfaces on Olimar, the ship and the
Onions, compressed disc images (RVZ/WIA/GCZ) through a Dolphin converter
you already have, language selection from F1 on the European disc, the
F1 menu from the pad's Select/View button, and fixes for PAL saves not
being recognised. Saves now live in the game's own folder; an older card
is copied there on first launch.


WHAT YOU NEED
-------------
  - 64-bit Windows.
  - GPU drivers with OpenGL 3.3 or newer. Intel, NVIDIA and AMD drivers all
    have it. Windows' generic "Basic Display Adapter" driver does NOT, and
    the game will not start on it.
  - Your legal copy of Pikmin USA Rev. 1 (GPIE01) or Pikmin Europe
    (GPIP01), as ISO or GCM. RVZ/WIA/GCZ work too if DolphinTool.exe from
    a Dolphin installation is on PATH or beside the launcher (about 1.4 GB
    of temporary space is needed for the conversion). The package includes
    both game executables; the launcher copies the one your disc needs.

The ROM and any Nintendo proprietary resources are not included. Your image
is neither copied nor modified during installation.


INSTALLING
----------
1. Extract this folder anywhere you like. Nothing is installed system-wide.
2. Run nectar-launcher.exe (double-click).
3. It asks for the ISO/GCM image and a destination folder.
4. When it finishes, the game starts on its own.

To play later, go to the installation folder and run nectar-launcher.exe
again: it sees the game is already installed and launches it directly.

You can also install without dialogs, from cmd or PowerShell:

  nectar-launcher.exe --rom C:\path\to\pikmin.iso --install-dir C:\Games\Nectar

Options: --extract-only (install without launching), --skip-verify, --help.

The installer checks the image against a known-good dump of the matching
release (USA Rev. 1 or Europe) before extracting, and re-reads each file
it writes. That catches copies damaged in transfer and failing drives,
which are the usual reason a game installs fine and then fails strangely.
It adds about a minute; --skip-verify skips it.

A European disc needs nectar-pal.exe from this package. The installer
copies it as nectar.exe in the folder you chose. If that file is missing
from the zip, installation stops with an error instead of installing the
American build by mistake.


THE CONSOLE WINDOW IS DELIBERATE
--------------------------------
The game opens a console window with diagnostic messages. It is not a bug.
SDL2 would normally hide it, and with it every message the port prints. It
is kept so that a failed launch says why instead of vanishing silently.


IF SOMETHING GOES WRONG
-----------------------
The most useful thing you can send is the text from that console. To keep
it, run the game from cmd with the output redirected:

  cd C:\Games\Nectar
  nectar.exe > log.txt 2>&1

Common failures and what they mean:

  Closes instantly, no window
      SDL2.dll is missing from beside the .exe, or Windows blocked it. Check
      that SDL2.dll sits in the same folder as nectar.exe.

  "Could not initialize window/OpenGL"
      Your GPU drivers do not offer OpenGL 3.3. Update them from the
      manufacturer's website, not from Windows Update.

  Starts but cannot find the game data
      The game looks for the assets\ folder in the directory it is run from.
      Launch it from its installation folder, not by absolute path from
      somewhere else.

  The installer rejects the image
      It must be Pikmin USA Rev. 1 (GPIE01, revision 1) or Pikmin Europe
      (GPIP01, revision 0), uncompressed. Convert RVZ/WIA/GCZ to ISO with
      dolphin-tool.

  No sound, but the game runs
      The log says "no audio device yet; playing silently and retrying". The
      default playback device was busy or unavailable at launch. The game
      keeps trying, so sound starts on its own once a device is free.


IN-GAME SETTINGS — F1
---------------------
Press F1 at any time for resolution, display mode, render scale, VSync,
frame rate and controls.

Under Mods:
  - Mouse wheel: either picks which Pikmin colour to throw next, or zooms
    the camera.
  - Pikmin limit: how many Pikmin may be on the field at once, 50 to 999.
    The original is 100. It applies when a stage loads. High values do cost
    frame rate, and how much depends on your machine.
  - Day length: 5 to 30 minutes per in-game day, 10 being the original. It
    stretches the game's own clock, so nothing moves faster or slower --
    sunset simply arrives sooner or later.
  - Chain Pikmin actions: Pikmin look for more work after finishing a task.
    Off by default, to stay faithful to the original.

Permadeath is not in this menu. It belongs to a save file rather than to
the port, so it is chosen once, when you create the file, and the file
screen marks the files that carry it.

Photo mode is on F3. It freezes the world and hands you the camera: WASD
moves along the look direction, space and control lift and drop, the
arrows look, Q and E tilt and R levels, and shift or alt change pace.


KNOWN STATE
-----------
  - Sound: the original JAudio engine. Music, effects and cinematic audio.
  - Cinematic video: not implemented. The audio plays, the picture does not.
  - 30, 60 and 120 FPS selectable from the F1 menu.
