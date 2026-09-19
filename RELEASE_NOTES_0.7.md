# Open Nectar 0.7

The headline: **HD models from Pikmin 3.** Olimar, the red/yellow/blue Pikmin
(leaf, bud and flower included), the Bulborb and the Dwarf Bulborb can be
replaced with their Wii U meshes and textures, on every platform, by picking
the model zips you download yourself — the game converts them on the spot.
Alongside it, the throw cadence fix people asked for, mouse buttons and a
swarm key in the control remapper, and a batch of desktop fixes.

Downloads: `open_nectar_0.6.apk` (Android, arm64; the version string stays 0.6
for this build), `nectar-windows.zip`,
`nectar-linux.tar.gz`. Saves and settings from 0.6 carry over.

## HD models from Pikmin 3

The original `.mod` files keep driving animation, collision and everything
else; only the visible mesh is swapped, and each model falls back to the
original the moment its pack is missing. Nothing from Pikmin 3 ships with the
game: you bring the rips, the game builds its own packs from them.

**How to install (all platforms):**

1. Download the model zips from The Models Resource (Wii U → Pikmin 3). You
   need up to four, one per model, exactly as they come — do not unzip or
   rename them:
   - `Wii U - Pikmin 3 - Characters - Olimar.zip`
   - `Wii U - Pikmin 3 - Characters - Pikmin.zip`
   - `Wii U - Pikmin 3 - Creatures - Bulborb.zip`
   - `Wii U - Pikmin 3 - Creatures - Dwarf Bulborb.zip`
2. In game, open the settings menu (**F1** on desktop, the gear button on
   Android) → **Graphics** → **HD Models**.
3. Pick the row for the model you want (Olimar, Pikmin, Bulborb, Dwarf
   Bulborb) and press **A**. A file picker opens: choose the matching zip.
   Picking the wrong zip is harmless — the game tells you which model it is.
4. The row switches to *Installed*. Repeat for the other models.
5. Restart the game. That is it.

On desktop you can also just drop the zips into `Load/Models/` next to the
executable: they are converted the next time the HD Models menu is opened.
Linux needs `zenity` or `kdialog` for the file picker (the drop-in route works
without them). Android uses the system file picker, so the zips can live
anywhere the picker reaches.

The generated packs are plain `.nhm` files under `Load/Models/OlimarHD`,
`PikminHD` and `BulborbHD`; delete a folder to go back to the original model.

Under the hood: an in-game converter reads the Collada + PNG rips (a small zip
reader, an XML parser and stb_image — no external tools, no Python), skins the
meshes with the engine's own animation matrices, and bakes the details the
Wii U shaders provided (the see-through visor, the bulborb corneas, the
two-sided eye discs, mirrored Pikmin UVs). The Pikmin legs mapped crosswise in
the first prototype — Pikmin 3 puts `llegjnt` on +X and the GameCube rig has
that joint at −X — which is fixed here.

## Gameplay and controls

- **Pikmin throw cadence** (#37, #40). Two things capped how fast you could
  throw. Releasing the button while a nearby Pikmin was still waiting for the
  grab keyframe sat there until the whole ThrowWait animation had played, so
  every throw paid for a full grab; and a press landing during the throw
  wind-up was simply dropped, so mashing lost every other throw. The grab now
  completes on release, and a press during the wind-up is queued and fires
  the tick the Pikmin leaves the hand. Mashing throws as fast as on GameCube.
- **Mouse buttons are bindable** (#42). Controls → any action → press a mouse
  button: the middle button and the side buttons (Mouse 4 / Mouse 5) bind like
  keys. Left and right stay as the fixed cursor conveniences.
- **Swarm to cursor** (#29). New action, `C` by default, remappable to
  keyboard, mouse or gamepad: hold it and the squad heads for the cursor, the
  way Down on the Wii D-pad did. The C-stick still wins when you move it.
- **Cursor in borderless fullscreen** (#40). The absolute mouse cursor was
  measured against the stored *windowed* size instead of the desktop, so it
  sat off-centre in borderless mode. Fixed, and the mouse re-centres properly
  when leaving relative mode.

## Saving

- **Saves that vanished on Linux** (#34). The memory card was written beside
  the executable; on a read-only install (an AppImage mount, `/opt`, a Proton
  prefix over a read-only game folder) the write silently failed and the game
  said "saved" anyway. The game folder is now probed with a real write and,
  if it is not writable, the card lives in `~/.local/share/pikmin-native/save`
  (Windows: `%LOCALAPPDATA%\Nectar\save`). `NECTAR_SAVE_DIR` still overrides
  everything.
- **Save Data backup** (#36). Settings → Save Data exports the memory card to
  a zip and imports one back. On Android that goes through the system file
  picker, so saves can be moved between devices or kept somewhere safe.

## Fixes

- Crash on the second attract movie: globals were not reset between movies
  (#27).
- Texture packs on desktop: the menu now creates `Load/Textures` and shows its
  absolute path instead of doing nothing when there was no in-app extractor
  (#35).
- HD models leaked their cull/blend state into the next mesh drawn: the Onion
  turned see-through, Olimar's helmet vanished, the cursor ring misrendered.
  The pipeline state is saved and restored around every HD draw.
- Rendering: immediate-mode normals (`pc_gfx_normal`) and RGBA texture objects
  with a configurable wrap mode, needed by the HD meshes.

## Known issues

- The Nintendo logo in the intro and credits still renders wrong (#40).
- The mouse cursor can misbehave when checking ship parts at the Onion with
  the cursor mod on (#40).
