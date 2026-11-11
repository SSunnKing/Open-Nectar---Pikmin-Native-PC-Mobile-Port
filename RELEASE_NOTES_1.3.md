# Open Nectar 1.3 — Quick Save, an easier Map Maker and HD model downloads

Pikmin 1 gets an optional Quick Save and repairs saves that got stuck, the
Map Maker is redesigned to be easier for everyone, the launcher can fetch and
install HD character models for you, and HD texture packs are easier to switch
and remove. Saves, settings, texture packs and maps carry over.

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
* **Fewer crashes with HD packs on AMD:** missing mip levels of compressed HD
  textures are now built by the game instead of the graphics driver, the
  likely cause of crashes on AMD Windows drivers (#308, #313, #316).
* **Spotlights light only their cone:** the two spotlights in The Forest Navel
  no longer light the whole cave evenly, and lights fade with distance as on
  the console (#312, #314).
* **Better Pathfinding:** Pikmin thrown onto a ledge wait there, as on the
  disc, instead of jumping back to the group (#254).
* **Bomb Rock Respawn mod:** the bomb rocks lying on the map are now refilled
  too, not only the bomb piles (#311).

## Pikmin 2

* **No more "no down floor 20" crash:** a save moved to a later area with the
  Unlock All Zones and All Onions cheats no longer panics a few minutes into
  play (#305).
* **Co-op water:** water in two-player Story mode refracts the screen as in
  single player instead of turning almost black (#309).
* **Correct area names:** the heat-haze copy of the area title shows that
  area's own name. In the Awakening Wood, the Perplexing Pool and the Wistful
  Wild it used to show leftover titles from the disc (#270).
* **Original speed at 60 and 120 FPS:** the "Saving..." screen, its fades and
  chime, and the "% of debt repaid" banner no longer run 2x or 4x too long or
  too fast (#190, #227).
* **Randomizer:** treasures that need more than 100 Pikmin, such as the
  Doomsday Apparatus, are no longer placed in the Submerged Castle, where only
  blue Pikmin can go (#255).
* **"You appear lost" scene:** it is skipped when its data is missing instead
  of stopping the game.

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

## Map Maker

The Map Maker is redesigned so anyone can build a map, with Roblox Studio as
the reference.

* **New start screen:** pick what the map is for (VS, Solo, Co-op or
  Campaign), then start blank in a small, medium or large size, from a
  template or from a game area. **My maps** lists every map you have, newest
  first, with thumbnails and a search box.
* **New solo template, Dewdrop Hollow:** a one-day mission to find 9 of 10
  ship parts.
* **Editor layout like a 3D editor:** a File menu, undo and redo with a
  history list, a "Ready to play" indicator that lists what still needs
  fixing, and a Test in game menu that can start from where the camera looks.
  Tools sit in a left bar with their name under each icon, grouped into
  Select, Terrain and Place.
* **Drag and drop:** drag ship parts, enemies, bosses and decoration from the
  catalog straight onto the map, with a preview of where they land.
* **Ship parts look right:** parts show their real names, such as Main Engine
  or Positron Generator, and render with their metallic reflections instead
  of black or plain white. Catalog items are shown as cards with their name
  under the picture.
* **Easier on the eyes:** a sky gradient behind the map, a faint ground grid
  on maps without painted terrain, and calmer panel headings.
* **Model Maker:** the start screen buttons line up with the character cards.

## Platforms

* **macOS:** releases now include an Apple Silicon build. Holding a key no
  longer opens the accent picker in the game window (#206, #315).

## Thanks

Thanks to Kyle Nakamura (knakamura13) for the pull requests in this release.
