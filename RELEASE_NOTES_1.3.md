# Open Nectar 1.3 — Quick Save, HD model downloads and stuck-save repair

Pikmin 1 gets an optional Quick Save and repairs saves that got stuck, the
launcher can fetch and install HD character models for you, and HD texture
packs are easier to switch and remove. Saves, settings, texture packs and maps
carry over.

**Updating:** press **Update** in your launcher. On Android, install the new
APK over the old one. **Do not uninstall first**, or your data is lost.

## Pikmin 1

* **Quick Save (experimental, off by default):** save and return to a point
  inside a day. Turn it on in F1 > Data > **Quick Save (experimental)** and
  restart. **F6** saves, **F7** loads (it asks first), and **Esc** cancels a
  load in progress. Loading replays your day behind a progress screen. It works
  in Story and Versus, not in netplay, Speedrun, Permadeath, Randomizer or
  Challenge Mode. Ordinary saves are unchanged. While it is on, the mouse does
  not click menus, so a recorded day cannot go out of sync. Keyboard and
  controller work as usual (#212, #304).
* **Stuck saves are repaired:** a save that remembers a cutscene but not what
  it gave you, such as a missing first yellow or blue seed or a locked Onion,
  is fixed when it loads. The game tells you what it fixed. It never hands out
  free days, Pikmin or ship parts. The original file is copied to a
  `progression-repair` folder before your next save overwrites it (#293).
* **Area names show while loading:** the area name is now always shown before
  gameplay loads. This adds about two seconds to each affected transition
  (#298).
* **HD texture packs:** keep both HD packs installed and switch between Off,
  1080p and 4K, or delete a pack you no longer want to free its space. The
  installer warns you if the disk is short on room (#291). Small text on the
  title screen and menus, such as PRESS START, is sharper with a pack on
  (#306).

## Pikmin 2

* **No more "no down floor 20" crash:** a save moved to a later area with the
  Unlock All Zones and All Onions cheats no longer panics a few minutes into
  play (#305).

## Launcher

* **Get HD models:** a new button on the HD Models page lists every model zip
  Open Nectar can convert: Olimar, Louie from Pikmin 2, Louie from Pikmin 3,
  Pikmin, Red Bulborb and Dwarf Bulborb. **Download** opens that character's
  page in your browser. The launcher then watches your Downloads folder and
  installs the zip by itself when it finishes. It never downloads the files
  itself. You can also use **Import a zip** for a file saved elsewhere (#303).
* **Uninstall HD models:** each installed model has an **Uninstall** button
  that shows the space it frees (#303).
* **Settings text fits:** setting values are shown in full, with the
  "In game: F1 or backtick" hint on its own line, and long setting names wrap
  instead of ending in "..." (#302).

## Platforms

* **macOS:** releases now include an Apple Silicon build.

## Thanks

Thanks to Kyle Nakamura (knakamura13) for the pull requests in this release.
