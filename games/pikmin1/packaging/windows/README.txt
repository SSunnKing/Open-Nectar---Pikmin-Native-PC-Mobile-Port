OPEN NECTAR — NATIVE PIKMIN PORT FOR WINDOWS (x86-64)

A native Windows build of Pikmin: no emulator. It runs the game's original
JAudio sound engine, so music, sound effects and cinematic audio all play.

The ROM and any Nintendo proprietary resources are not included. Your disc
image is read to extract the game data; it is never copied or modified.


WHAT YOU NEED
-------------
  - Windows 10 (version 1803 or later) or Windows 11, 64-bit.
  - GPU drivers with OpenGL 3.3 or newer. Intel, NVIDIA and AMD drivers all
    have it. Windows' generic "Basic Display Adapter" driver does NOT, and
    the game will not start on it.
  - Your own copy of Pikmin USA Rev. 1 (GPIE01) or Pikmin Europe (GPIP01),
    as ISO or GCM. RVZ/WIA/GCZ also work if DolphinTool.exe from a Dolphin
    installation is on PATH or next to the launcher.
  - About 1 GB of free space.

Nothing else: SDL2 and the C++ runtime are built into the .exe files. There
are no DLLs to keep next to them.


WHAT IS IN THE ZIP
------------------
  nectar-launcher.exe   installs, updates and starts the game
  nectar.exe            the game, USA
  nectar-pal.exe        the game, Europe
  README.txt            this file

The launcher installs only the game for your disc's region, named
nectar.exe.


INSTALLING
----------
1. Extract the zip anywhere. Nothing is installed system-wide.
2. Open nectar-launcher.exe.
3. Press Install under the Pikmin cover.
4. Choose your disc image and the folder to install to.
5. The disc is checked and the game data extracted (a few minutes).
6. Press "Play now".

The folder you chose ends up with the game data (assets), your saves and
settings, nectar.exe and nectar-launcher.exe. To play later, open
nectar-launcher.exe in that folder and click the cover.


THE LAUNCHER
------------
Games         Click the cover to play. The button underneath updates it.
Settings      Every option of the in-game F1 menu, plus texture packs, HD
              models and language. Changes are saved at once and apply the
              next time the game starts.
Move Install  Moves the whole installation (saves and settings included)
              to another folder or drive.

When a new version is published, the launcher says so ("Open Nectar X is
available") and the Update button downloads and installs it by itself.


UPDATING
--------
From the installed launcher: press Update. It downloads the latest release
from GitHub, installs it and restarts. Your saves, settings, texture packs
and the extracted game data are not touched.

From a new download: extract the new zip, open its nectar-launcher.exe,
press Update and choose the folder where the game is installed. The disc
image is not needed.

Installations from older versions (with SDL2.dll next to the game) are
tidied up when updated: the DLLs are no longer needed.


COMMAND LINE
------------
Install without any window, from cmd or PowerShell:

  nectar-launcher.exe --rom C:\path\to\pikmin.iso --install-dir C:\Games\OpenNectar

Options: --extract-only (install without starting the game), --skip-verify
(skip the disc checks), --dolphin-tool PATH, --help.

The installer checks the image against a known-good dump before extracting
and re-reads every file it writes. That catches damaged copies and failing
drives, the usual reason a game installs fine and then fails strangely. It
adds about a minute; --skip-verify skips it.


THE GAME'S CONSOLE WINDOW IS DELIBERATE
---------------------------------------
The game opens a console window with diagnostic messages. It is not a bug:
it is kept so that a failed launch says why instead of vanishing silently.
To keep that text for a bug report, run the game from its folder with:

  nectar.exe > log.txt 2>&1


IF SOMETHING GOES WRONG
-----------------------
  Windows SmartScreen warns about the launcher
      The .exe files are not signed. Choose "More info" > "Run anyway".

  "Could not initialize window/OpenGL"
      Your GPU drivers do not offer OpenGL 3.3. Update them from the
      manufacturer's website, not from Windows Update.

  Update says it cannot reach GitHub
      Check the connection. You can also download the new zip by hand,
      open its launcher and press Update.

  The installer rejects the image
      It must be Pikmin USA Rev. 1 (GPIE01) or Europe (GPIP01). Convert
      RVZ/WIA/GCZ to ISO with DolphinTool.

  No sound, but the game runs
      The default playback device was busy at launch. The game keeps
      trying, so sound starts on its own once a device is free.


IN GAME
-------
F1 (or Select/View on a pad) opens the settings menu at any time. The
European disc starts in English; change the language in the launcher's
Settings, in F1 > Display, or in the game's own options.
