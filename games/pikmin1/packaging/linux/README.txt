OPEN NECTAR — NATIVE PIKMIN PORT FOR LINUX (x86-64)

A native Linux build of Pikmin: no emulator. It runs the game's original
JAudio sound engine, so music, sound effects and cinematic audio all play.

The ROM and any Nintendo proprietary resources are not included. Your disc
image is read to extract the game data; it is never copied or modified.


-------------------------------------------------------------------
WHAT YOU NEED
-------------------------------------------------------------------

  - Linux x86-64 with glibc 2.29 or newer. That is any current distro:
    Ubuntu 20.04+, Linux Mint 20+, Pop!_OS, Debian 11+, Fedora 30+,
    openSUSE Leap 15.3+ and Tumbleweed, Arch, Manjaro, EndeavourOS,
    SteamOS (Steam Deck desktop mode).
  - A graphics driver with OpenGL 3.3 (every Intel, AMD and NVIDIA driver),
    in an X11 or Wayland session.
  - zenity or kdialog, for the file and folder pickers. GNOME, KDE and most
    desktops already have one. If not:
        Debian/Ubuntu/Mint:  sudo apt install zenity
        Fedora:              sudo dnf install zenity
        Arch/Manjaro:        sudo pacman -S zenity
        openSUSE:            sudo zypper install zenity
  - Your own copy of Pikmin USA Rev. 1 (GPIE01) or Pikmin Europe (GPIP01),
    as ISO or GCM. RVZ/WIA/GCZ also work if dolphin-tool from a Dolphin
    installation is on PATH or next to the launcher.
  - About 1 GB of free space.

Everything else (SDL2, the C++ runtime) is built into the executables.


-------------------------------------------------------------------
TWO DOWNLOADS, SAME CONTENT
-------------------------------------------------------------------

Open_Nectar-x86_64.AppImage
    One file. Make it executable (right click > Properties > "Allow
    executing as program", or: chmod +x Open_Nectar-x86_64.AppImage) and
    open it.

nectar-linux.tar.gz
    A folder. Extract it (tar -xzf nectar-linux.tar.gz) and open
    nectar-launcher inside. It holds nectar (USA), nectar-pal (Europe) and
    nectar-launcher.

Either one opens the Open Nectar launcher, which installs and updates the
game. You only need it once: after installing, the launcher that stays in
the game's folder takes over.


-------------------------------------------------------------------
INSTALLING
-------------------------------------------------------------------

1. Open the AppImage or nectar-launcher.
2. Press Install under the Pikmin cover.
3. Choose your disc image and the folder to install to.
4. The disc is checked and the game data extracted (a few minutes).
5. Press "Play now".

The folder you chose ends up with the game data (assets), your saves and
settings, and two programs: nectar (the game, for your disc's region) and
nectar-launcher.

To play later, open nectar-launcher in that folder and click the cover.


-------------------------------------------------------------------
THE LAUNCHER
-------------------------------------------------------------------

Games        Click the cover to play. The button underneath updates it.
Settings     Every option of the in-game F1 menu, plus texture packs, HD
             models and language. Changes are saved at once and apply the
             next time the game starts.
Move Install Moves the whole installation (saves and settings included)
             to another folder or disk.

When a new version is published, the launcher says so ("Open Nectar X is
available") and the Update button downloads and installs it by itself.


-------------------------------------------------------------------
UPDATING
-------------------------------------------------------------------

From the installed launcher: press Update. It downloads the latest release
from GitHub, installs it and restarts. Your saves, settings, texture packs
and the extracted game data are not touched.

From a new download: open the new AppImage or nectar-launcher, press
Update and choose the folder where the game is installed. The disc image is
not needed.

Installations from older versions (the ones with nectar.real and a lib
folder) are converted to the new layout when updated.


-------------------------------------------------------------------
COMMAND LINE
-------------------------------------------------------------------

Install without any window (works over SSH, and without zenity/kdialog):

  ./nectar-launcher --rom /path/to/pikmin.iso --install-dir ~/Games/OpenNectar

Options:
  --rom FILE         Disc image: Pikmin USA Rev. 1 or Europe, ISO or GCM.
  --install-dir DIR  Folder to install to (created if needed).
  --extract-only     Install and exit, without starting the game.
  --skip-verify      Skip the disc checks (see below).
  --dolphin-tool P   dolphin-tool to convert RVZ/WIA/GCZ.
  --help             Show help.

The installer checks the image against a known-good dump before extracting
and re-reads every file it writes. That catches damaged copies and failing
drives, the usual reason a game installs fine and then fails strangely. It
adds about a minute; --skip-verify skips it.


-------------------------------------------------------------------
IF SOMETHING GOES WRONG
-------------------------------------------------------------------

The AppImage does not open
    Your system has no FUSE. Run it once from a terminal as:
      ./Open_Nectar-x86_64.AppImage --appimage-extract-and-run
    or use nectar-linux.tar.gz instead.

"libGL.so.1: cannot open shared object file"
    No OpenGL driver is installed:
      Debian/Ubuntu: sudo apt install libgl1
      Fedora:        sudo dnf install mesa-libGL
      Arch:          sudo pacman -S libglvnd
      openSUSE:      sudo zypper install Mesa-libGL1

"GLIBC_2.29 not found" (or similar)
    The distro is older than the minimum (for example Debian 10, Ubuntu
    18.04, RHEL/Rocky/Alma 8). Upgrade, or use a newer distro.

Browse does nothing
    Neither zenity nor kdialog is installed (see WHAT YOU NEED), or use the
    command line.

The disc image is rejected
    It must be Pikmin USA Rev. 1 (GPIE01) or Europe (GPIP01). Convert
    RVZ/WIA/GCZ with: dolphin-tool convert -f iso -i game.rvz -o game.iso

Not enough space
    The extracted data takes about 650 MB. Leave 1 GB free.


-------------------------------------------------------------------
IN GAME
-------------------------------------------------------------------

F1 (or Select/View on a pad) opens the settings menu at any time. The
European disc starts in English; change the language in the launcher's
Settings, in F1 > Display, or in the game's own options.
