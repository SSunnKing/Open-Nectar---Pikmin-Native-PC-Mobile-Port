NECTAR — NATIVE PIKMIN PORT FOR LINUX (x86-64)

Self-contained package: includes glibc, SDL2, PulseAudio, ALSA and codecs.
You don't need to install dependencies for most of the program.

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

System requirements:
  - Linux x86-64 with kernel 3.2 or higher.
  - OpenGL driver with libglvnd (present in any distro since 2017).
  - X11 or Wayland session.
  - Your legal copy of Pikmin USA Rev. 1 (GPIE01) or Pikmin Europe (GPIP01),
    in ISO or GCM format. RVZ/WIA/GCZ work too if dolphin-tool from a
    Dolphin installation is on PATH or beside the launcher (about 1.4 GB
    of temporary space is needed for the conversion).

The ROM and any Nintendo proprietary resources are not included.
The ROM is not copied or modified during installation.


-------------------------------------------------------------------
1. EXTRACTION
-------------------------------------------------------------------

Use the .tar.gz. A ZIP loses execute permissions and the package
would fail before starting.

  tar -xzf nectar-linux.tar.gz
  cd nectar-linux

With tar.gz no chmod is needed. If you received it in ZIP format,
restore permissions with:

  chmod +x nectar nectar-launcher nectar.real nectar-launcher.real \
           lib/ld-linux-x86-64.so.2


-------------------------------------------------------------------
2. INSTALL MISSING DEPENDENCIES
-------------------------------------------------------------------

Only two things from the system are needed: OpenGL libraries and, if
you want the graphical installer, zenity.

Debian, Ubuntu, Linux Mint, Pop!_OS:

  sudo apt update
  sudo apt install zenity libopengl0 libglvnd0 libgbm1 libdrm2 \
                   libgl1-mesa-dri

Arch, Manjaro, EndeavourOS:

  sudo pacman -S --needed zenity libglvnd mesa

Fedora, Nobara:

  sudo dnf install zenity libglvnd libglvnd-glx libglvnd-egl \
                   mesa-dri-drivers

openSUSE:

  sudo zypper install zenity libglvnd Mesa-dri

If you use KDE and prefer kdialog, substitute zenity with kdialog in
any of the above commands. Either works.

zenity is OPTIONAL: without it, the installer works the same in text
mode from the terminal, or using the commands in section 4.


-------------------------------------------------------------------
3. VERIFY NOTHING IS MISSING
-------------------------------------------------------------------

Before installing, verify that the system resolves everything:

  ./lib/ld-linux-x86-64.so.2 --library-path ./lib --list ./nectar.real \
    | grep -i "not found"

If it prints nothing, everything is correct. If any library appears,
install the package from section 2 that contains it.


-------------------------------------------------------------------
4. COMMAND-LINE INSTALLATION (NO WINDOWS)
-------------------------------------------------------------------

This is the most reliable method and doesn't need zenity, kdialog or
graphical environment to install. Indicate the ROM and destination folder:

  ./nectar-launcher --rom /path/to/pikmin.iso --install-dir ~/Games/OpenNectar

This extracts resources, installs executables and libraries in that
folder, and launches the game when finished.

To install WITHOUT the game starting afterwards:

  ./nectar-launcher --rom /path/to/pikmin.iso \
                    --install-dir ~/Games/OpenNectar \
                    --extract-only

To play later, from the installation folder:

  cd ~/Games/OpenNectar
  ./nectar-launcher

Available options:

  --rom FILE         Pikmin USA Rev. 1 ISO or GCM image.
  --install-dir DIR  Folder to install to (created if it doesn't exist).
  --extract-only     Install and exit, without launching the game.
  --skip-verify      Skip integrity checks (see below).
  --help             Show help.


-------------------------------------------------------------------
4b. INTEGRITY CHECKS
-------------------------------------------------------------------

The installer checks two things on its own:

1. Before extracting, verifies that the image matches an intact dump
   of Pikmin USA Rev. 1. Detects copies damaged during transfer,
   which are the most common cause of the game installing correctly
   but then failing with incomprehensible errors.

2. When extracting, re-reads each written file and compares it with
   what came from the image. Detects faulty disks and USB drives,
   which produce files of correct size with wrong content.

This adds about a minute to installation. If you prefer to skip it,
use --skip-verify; but if the game fails afterwards, the first thing
you'll be asked is to install without that flag.

Note: when passing --install-dir the installer doesn't try to open
any window, so this method works via SSH and on machines without
zenity or kdialog.


-------------------------------------------------------------------
5. GRAPHICAL INSTALLATION
-------------------------------------------------------------------

If you have installed zenity or kdialog:

  ./nectar-launcher

It will ask for the ROM and destination folder with dialogs. If you
double-click without a terminal, it automatically reopens in one.

From a terminal without zenity or kdialog, the same command uses the
text-mode installer, which asks via keyboard.


-------------------------------------------------------------------
6. COMMON ISSUES DURING INSTALLATION
-------------------------------------------------------------------

"Zenity or KDialog is required for the graphical installer."
    You launched it with double-click on a system without those
    programs and without a terminal. Install zenity (section 2) or
    use the commands in section 4.

"The installer accepts ISO/GCM. Convert RVZ/WIA/GCZ to ISO..."
    Your image is compressed. Convert it:
      dolphin-tool convert -f iso -i game.rvz -o game.iso

"The existing lib directory does not belong to Nectar."
    You chose as destination a folder that already contained a
    different lib. The installer refuses to delete it for safety.
    Choose an empty or new folder.

"Permission denied" when running
    Permissions were lost during extraction. Apply the chmod from
    section 1, or extract again from the .tar.gz.

ROM is rejected
    It must be Pikmin USA Rev. 1 (GPIE01, revision 1), uncompressed.
    Other regions or revisions are not supported.

Insufficient space
    Extracted resources take about 650 MB, plus 30 MB of executables
    and libraries. Leave at least 1 GB free at the destination.
