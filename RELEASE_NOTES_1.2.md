# Open Nectar 1.2 — Pikmin 2 shadows, faithful controls and a big round of fixes

Pikmin 2 gets its original blob shadows back, the sticks and throws in
Pikmin 1 now feel like the GameCube, cutscene skipping is complete, and the
launcher, macOS and Windows builds get many fixes. Saves, settings, texture
packs and maps carry over.

**Updating:** press **Update** in your launcher. On Android, install the new
APK over the old one. **Do not uninstall first**, or your data is lost.

**macOS:** there is no ready-made macOS download. Each player builds it
from source with `scripts/build-macos.sh` (see the macOS section of the
README).

## Pikmin 2

* **Original shadows are back:** creatures and objects get the GameCube's
  blob shadows again on the GX renderer (#243). The option is now called
  **Original** in the shadow settings.
* **Cave radar:** the radar map in the pause menu lines up with the rooms
  again.
* **Raised Pikmin limit:** more than 110 Pikmin latching onto the same enemy
  or treasure no longer crashes the game (#258).
* **Touch (Android):** the touch controls no longer break after leaving a
  cave or area (#238).

## Pikmin 1

* **Your Pikmin 2 save is safe:** Pikmin 1 no longer erases the Pikmin 2 save
  that shares the memory card when the card has no Pikmin 1 save.
* **Sticks match the GameCube:** dead zone, octagon clipping and C-stick
  strength now follow the original, while a full diagonal still gives a full
  run.
* **Throwing:** releasing the button early waits for the grab like the
  original, and a Pikmin that walks over to you is thrown on its arrival, as
  on the disc.
* **Text windows:** Space, Enter and left click now show the whole page at
  once, like B; Shift bound to B works again.
* **Skipping cutscenes:** skipping an Onion discovery now opens the cone and
  the path and still fires the first seed; skipped cutscenes replay their
  events in the right order.
* **Randomness:** the eight places that draw two random numbers at once use
  the original GameCube order again.
* **No Day Limit:** no longer stops after 27 ordinary day ends, and the
  Day 29 diary goes on the right page.
* **Skip Tutorial:** the voyage log now has its Day 1 entry.
* **Gates:** destroyed wall sides stay destroyed on later days.
* **Permadeath:** the "expedition lost" notice only appears when a save was
  actually erased.
* **Whistle Pluck:** whistle plucks count toward the pluck camera's
  100-pluck limit.
* **Buried captain:** the escape timer runs at the right speed at any tick
  rate.
* **HD texture packs:** replacement textures get complete mipmaps, so they
  stay sharp and stable at a distance (#287).
* No more "PIKI BIRTH FAILED" crash when the Onion can't spawn a Pikmin (#236).

## Launcher and platforms

* **Install dialog:** the install folder is filled in for you (#107).
* **Narrow windows:** model rows and save buttons fit small panels (#288).
* **Pikmin 1 install check** now requires the plants archive on USA/JPN
  installs, instead of letting the game abort later.
* **Backtick key** (`` ` ``) opens the in-game settings, for Mac keyboards
  where F1 needs Fn.
* **macOS:** sturdier builds and better PAL support.
* **USB controllers:** they reconnect after being plugged in late or
  unplugged and plugged back, in both games (#229, #249).
* **Windows:** fixes a black screen on some drivers (seen under CrossOver).

## Stability

* The crash screen itself no longer crashes, so the real error is shown.
* Several fixes for bugs flagged by compiler warnings: audio memory, texture
  pack log, math helpers and Map Maker text (#290).

## Thanks

Thanks to Kyle Nakamura (knakamura13) and lucamaio-pixel for their pull
requests in this release.
