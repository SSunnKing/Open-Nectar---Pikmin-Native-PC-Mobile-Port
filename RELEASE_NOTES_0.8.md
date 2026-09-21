# Open Nectar 0.8

The headline: **local co-op with Olimar and Louie.** Two captains, two
controllers, split screen that can merge into a single camera when you stay
together, and Louie from Pikmin 2 as a playable model. Around it, the port's
settings moved into a menu that looks like the game's own, a texture pack
installer on desktop, and a handful of fixes to the new-game flow.

Downloads: `open_nectar_0.8.apk` (Android, arm64), `nectar-windows.zip`,
`nectar-linux.tar.gz`. Saves and settings from 0.7 carry over. The desktop
builds now carry the Open Nectar icon (executables and installer on Windows;
window icon plus a `.desktop` file and PNG on Linux).

## Local co-op

- **Co-op from the title screen.** The main menu now reads *Start / Co-op /
  Options / Advanced Options* (plus *Challenge Mode* when unlocked). *Start*
  is single player as always; *Co-op* goes straight to the controller
  prompt, so the old 1P/2P question is gone.
- **Controllers and captains.** Each player presses a button on their own
  controller, then picks a captain — Olimar or Louie — with that same
  controller (sprites from *Pikmin 2-e*). Both can pick the same one: with
  two Olimars player 2 is tinted blue, with two Louies player 2 is tinted
  red, and Olimar + Louie needs no tint. The tint colours the suit only; head
  and visor stay as they are. Cursor ring, cursor trail, antenna light and
  the HUD follow the same colour.
- **Louie.** Playable through the Pikmin 2 rip, same route as the HD models:
  *Advanced Options → Graphics → HD Models → Louie* and pick
  `GameCube - Pikmin 2 - Characters - Louie.zip` from The Models Resource
  (or drop it into `Load/Models/`). The rig is the GameCube one, so he
  animates with the original Olimar animations; helmet glass matches Olimar
  HD, antenna light is blue, and the HUD shows his Pikmin 2 portrait. If the
  model is not installed the picker says so and keeps Olimar.
- **Louie HD.** The Pikmin 3 rip works too: *HD Models → Louie HD* with
  `Wii U - Pikmin 3 - Characters - Louie.zip`. When both are installed the
  Pikmin 3 one is used, the same way Olimar HD replaces the original.
- **Single player picks a captain too.** After *Start* a *Captain* prompt
  asks Olimar or Louie (keyboard, gamepad or a tap), so Louie is not limited
  to co-op. Challenge Mode stays Olimar.
- **Split screen that merges.** New option *Mods → Co-op Merged Camera* (off
  by default). On, both halves show one camera while the captains are close
  and split smoothly as they walk apart; each half's side follows where that
  player is on screen (left/right, or top/bottom with the horizontal split).
  Controls follow the camera each player actually sees, so nothing inverts
  when the shared camera turns. Off keeps the static split from 0.7.
- **HUD per player** in each player's half, whichever side it is on.
- Player 2's Olimar keeps the blue tint from 0.7 when both play Olimar.

## Advanced Options (new settings menu)

- *Advanced Options* on the title screen opens the port's settings inside the
  game's own glass panels, with the same font, cursor and highlight as
  *Options*: **Display, Controls, Graphics, Mods, Save Data**, then *Save* and
  *Back*. Long lists (resolutions, key and button bindings) scroll with a
  side bar, held Up/Down keeps scrolling, and on touch screens you can drag
  the list up and down with a finger. The background behind each panel is
  blurred so the text reads over the title screen.
- Display changes still ask for confirmation with a countdown; the dialog is
  now a proper panel and reverts on its own when the time runs out.
- Resolution opens a full list instead of cycling.
- **F1 in game is unchanged**; it is the same settings underneath.

## Graphics

- **Per-pixel lighting** (*Graphics → Lighting*). The GX lighting equation is
  evaluated per fragment instead of per vertex, so highlights and light
  falloff follow surfaces instead of triangle corners. Same equation, same
  colours; off keeps the vertex lighting from GameCube.
- **Shadow maps** (*Graphics → Shadows*: Off / Soft / Normal / Strong). A depth
  map rendered from the sun (light 0) gives captains, Pikmin, creatures and
  the ship real cast shadows, filtered with hardware PCF. Cursor, marker
  rings and particles do not cast. With shadow maps on, the original blob
  shadows are skipped.

## Controls

- **Pikmin colour on the D-pad** (#43). Left/Right on the D-pad (or the arrow
  keys) cycles through the colours in your squad, like the Pikmin 2 shortcut,
  and also while holding A: the Pikmin in hand goes back to the squad and one
  of the new colour is picked up. Works for both players in co-op; the mouse
  wheel and the HUD tap keep working as before.
- **Mouse buttons are bindable** (#42). Any keyboard action can take a mouse
  button, including Mouse 4 / Mouse 5 (back / forward).

## Installers

- **Texture packs on desktop** now open a file picker (Windows native,
  `zenity`/`kdialog` on Linux) and extract the zip into `Load/Textures`
  themselves, following the same path rule as Android. No more manual unzip.
- HD Models gained the *Louie* and *Louie HD* rows; the converters accept the
  Pikmin 2 and Pikmin 3 Louie rips on desktop and Android, and
  `build-hd-model-pack.py louie` / `louie_hd` do the same offline. Packs
  built from now on flag the captain's head and visor so the co-op tint
  colours the suit only (Olimar HD too, once re-converted).

## Fixes

- Choosing *Normal / Permadeath* and then *Normal / Hard* with a gamepad
  auto-confirmed the second question with the same press. Fixed.
- The new-game prompt now keeps the file-select background (stars and
  gradient) instead of a black screen, and empty slots no longer play the
  bubble zoom before it.
- Particles with a tint (antenna glow, cursor trail) were only partly tinted;
  child particles and textured particles now take the colour too.
- **NVIDIA PRIME on Linux** (#44). Running through `prime-run` still landed
  on the integrated GPU under Wayland: it exports the GLX vendor but not the
  EGL one, and SDL uses EGL there. The port now completes the route the
  environment already asked for (EGL vendor on Wayland, GLX vendor on X11)
  without touching anything the user set.
- **Test mode.** `PIKMIN_UNLOCK_ALL=1` starts a new game with every story and
  challenge stage open, no tutorial and intro cutscenes skipped.
- Menu panels built from the game's layouts can gain extra entries at run
  time (this is how the title and Advanced panels grow); the glass, the
  title plate and the cursor icons follow along.

## Known issues

- The Onion's light beam is cut short in the port compared to GameCube
  (tracked in `docs/PLAN_GRAPHICS.md`).
- The Nintendo logo in the intro and credits still renders wrong (#40).
