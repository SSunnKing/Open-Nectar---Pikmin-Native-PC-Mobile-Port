# Open Nectar 0.9.1

A new launcher, self-updating installs and smaller downloads that run on any
current Linux distro, plus a round of graphics fixes: see-through water,
a clear helmet for Olimar and smooth animations at 60/120 FPS. Saves,
settings and texture packs carry over.

**Updating from 0.9 or earlier:** download the new version, open its
launcher, press **Update** and choose the folder where the game is
installed. You don't need your disc image again. From now on, the installed
launcher updates itself.

## New

- **A brand-new launcher.** Covers for Pikmin and Pikmin 2 (downloaded from
  GameTDB, like Dolphin does) that grow when you hover them; click a cover to
  play. The background slowly cycles through screenshots of the game and the
  area photos from Olimar's log, taken from your own game files.
  - **Install** and **Update** under each cover. Pikmin 2 is shown as
    "Coming soon".
  - Installing now happens inside the launcher window: disc image and
    folder, progress, errors and "Play now", all in the same style.
  - The download you run from (package or AppImage) only shows Install and
    Update. The launcher that stays in the game's folder adds Settings and
    Move Install.
- **Settings outside the game.** The launcher's **Settings** tab has every
  option of the F1 menu, grouped the same way, with the same explanations.
  Changes are saved at once and apply the next time the game starts.
  - **Language** (European disc) can be picked there.
  - **Texture Packs:** choose the active pack or go back to the original
    textures, and install new packs from their .zip.
  - **HD Models:** install or replace each model from its .zip, and turn
    each one on or off on its own (turning one off keeps the files and
    brings back the original model).
  - Pikmin 2 will get its own section here.
- **Automatic updates.** The installed launcher checks GitHub when it
  opens. When a new version is out it says "Open Nectar X is available" and
  the button becomes "Update to X": it downloads, installs and restarts by
  itself. If you are offline, you can still update from a folder you
  downloaded by hand.
- **Update without the disc.** Any new download can update an existing
  installation: press Update and pick the installed folder. Saves,
  settings, packs and the extracted game data are left untouched.
- **Move Install.** Moves the whole installation (saves and settings
  included) to another folder or drive, then reopens the launcher from
  there.
- **Linux AppImage.** `Open_Nectar-x86_64.AppImage` is a single file: make
  it executable and open it. The `.tar.gz` is still there with the same
  content.
- **Whistle Pluck** (optional, off by default): hold the whistle over
  sprouts to pluck them one after another. F1 > Controls, next to Hold to
  Pluck. Thanks to 4laric (#56).

## Changes

- **Two files per installation.** An installed game is now just `nectar`
  (or `nectar.exe`) and `nectar-launcher`, next to the game data. No more
  `.real` files, launch scripts or `lib` folder on Linux, and no
  `SDL2.dll` on Windows: everything they needed is built into the
  executables. Older installations are tidied up when you update.
- **Linux: runs on any current distro.** The game only needs glibc 2.29 or
  newer (Ubuntu 20.04+, Mint 20+, Debian 11+, Fedora, openSUSE Leap 15.3+,
  Arch, Manjaro, SteamOS) plus the usual OpenGL driver. Tested on clean
  Ubuntu 20.04, Debian 12, Fedora 40, openSUSE Leap 15.6 and Arch.
- **Smaller downloads:** about 11.6 MB for Linux (was 19 MB).
- **No language question during install.** The European disc installs all
  five languages and starts in English; change it from the launcher,
  F1 > Display or the game's own options.

## Fixes

- **Water is see-through again.** You can see the ground under the water,
  as on the GameCube. The port was saturating a colour value the console
  only reads 8 bits of, which made the water fully opaque.
- **The Forest of Hope pond** no longer looks opaque: texture stages set
  without a coordinate now read the first one, as on the console. Thanks to
  4laric (#58).
- **Olimar's helmet** is clear glass with a light blue rim instead of a
  strong blue tint, and other lit translucent surfaces match the console.
- **Smooth animations at 60 and 120 FPS.** Character animations are stored
  at 30 frames per second; the game now blends between frames instead of
  holding each one, so Olimar, the Pikmin and enemies move smoothly at
  higher frame rates.
- **Language on the European disc:** the language chosen in F1 was ignored
  once a save existed, because the save's own language setting won. Both
  are now kept in sync: F1, the launcher and the game's options always
  show and use the same language.
- **Installing over a running game or launcher** no longer fails with
  "Text file busy" on Linux; on Windows the old program is set aside and
  removed the next time the launcher opens.
- **Playing from the installed folder with a European disc** no longer
  stops with "This disc needs nectar-pal".
- **Older installations without a region marker** are recognised as
  European from their data, so an update never installs the American
  build over a European disc.
- **Map screen on wide screens (Android):** the "Land" confirmation panel
  no longer sticks out on the right edge before it opens (#57).
