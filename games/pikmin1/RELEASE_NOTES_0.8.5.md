# Open Nectar 0.8.5

The headline: **memory cards now work with the real game.** A save exported
from a GameCube memory card or from Dolphin loads straight into Open Nectar,
and a save made here can be taken back the other way. Around it: gyro aiming
on pads and phones, a rebuilt settings menu that explains every option, a
first-person view that finally works properly, and sixteen new gameplay
options, including Lock-On and Charge borrowed from Pikmin 3.

Downloads: `open_nectar_0.8.5.apk` (Android, arm64), `nectar-windows.zip`,
`nectar-linux.tar.gz`. Saves and settings from 0.8 carry over.

## Memory cards

- **Genuine GameCube saves are readable at last.** The checksum that guards
  each section of the card was reading its words in the host's byte order.
  On a PC that is the opposite of a GameCube, so every real save looked
  corrupt, the game offered to repair it, and repairing it destroyed the
  progress. The sum is now computed the way the console computes it.
- **Cards written by older builds still load.** They carry the old sum, so
  it is accepted on read and quietly rewritten correctly the next time the
  game saves. Nothing to do and nothing to convert.
- Together this means a card is the same card everywhere: Open Nectar,
  Dolphin and real hardware can all read each other's saves. To bring a
  `.gci` in, drop it in the game's `save/card0/` folder without its 64-byte
  header.

## Gyro aiming

Aim the cursor by turning the controller, as in the Switch release.

- **Works with gyro pads and with the phone itself.** Switch Pro Controller,
  Joy-Con, DualShock 4 and DualSense are picked up automatically. On Android,
  with no gyro pad connected, the phone's own sensor is used, and it follows
  the screen's orientation.
- **Third person:** the gyro moves the cursor, on top of the stick or mouse,
  in either control scheme. **First person:** it looks around instead.
- **Settings** (*Controls*): on/off, sensitivity, and invert, each axis on
  its own (none, horizontal, vertical or both).
- **Calibrate.** Put the pad or phone down, keep it still and press A. Two
  seconds later the resting drift is measured and subtracted from then on, so
  the cursor stops creeping. The result is saved with your settings.
- **Gyro Recenter** is a new bindable action. It brings the cursor back in
  front of Olimar, and in first person it levels the view. It has no default
  button, because every pad button is already taken: assign one from the
  *Gyro Recenter Button* row in *Controls*.

## Settings menu

The F1 menu and the title screen's *Advanced Options* had grown one row at a
time. They are now organised the same way:

| Group | What is in it |
|---|---|
| **Display** | Display mode, resolution, aspect ratio, 3D resolution, refresh rate, VSync, FPS mode, language |
| **Graphics** | Antialiasing, filtering, lighting, shadows, fog, bloom, AO, depth of field, colour grading, texture packs, HD models |
| **Controls** | Control scheme, mouse, sticks, gyro, keyboard and gamepad bindings |
| **Camera** | Free Camera, First Person, Lock-On, Charge |
| **Gameplay** | Pikmin limit, day length, health, Pikmin behaviour, co-op |
| **Data** | Export and import saves, reset settings |

- **Every row explains itself.** A help line shows what the selected option
  does. In F1 it appears at the bottom of the panel; on the title screen it
  appears on a plate under the list.
- **Options that depend on another are dimmed** until that one is on, and
  the help line says which one. Examples: Charge needs Lock-On, Gamma needs
  Colour Grading, Mouse Sensitivity needs the Mouse Cursor scheme, and
  Resolution does nothing in borderless mode. Rows locked by Hard mode say
  so.
- **Bindings have their own pages.** On the title screen, *Controls* used to
  be a single list of 56 rows in a six-row window. The keyboard and gamepad
  bindings now each open their own list.
- **Leaving the menu saves.** Closing F1 (or pressing Esc/B) and *Back* on
  the title screen both keep your changes. There are no separate Save rows
  any more. The one exception is a display change you have not confirmed yet,
  which is reverted on its own. Before, closing F1 without choosing Save left
  the changes working for the rest of the session but lost them on restart.
- The old *Mods* page is gone. Its options now live in the group they belong
  to, as the table shows.

## New gameplay options

Sixteen new options, all off (or at 100%) by default.

- **Better Pathfinding.** A carrying party wedged against scenery used to
  push at the same waypoint forever, because the route was only rebuilt
  when a waypoint closed, never when the party simply stopped moving. With
  it on, a party that covers no ground for three seconds rebuilds its
  route from where it actually is.
- **Blues Only In Water.** A non-blue Pikmin that walks into water under its
  own steam steps back to the last dry ground it stood on instead of
  drowning. Thrown in, or knocked in by an enemy, still drowns, so water
  keeps its teeth. Whistling them across water no longer feeds them to it
  either: they gather at the edge and follow you back once you are on land.
- **Idle Pikmin Counter.** Shows how many Pikmin are standing around doing
  nothing, above the field total, and only when there are any — a permanent
  zero is noise you stop reading.
- **Olimar Health** and **Enemy Health.** Anywhere from a quarter to five
  times the original, in steps. Applied as a divisor on incoming damage
  rather than by resizing the health bar, so the gauge and every
  "below a quarter" warning keep reading correctly.
- **Infinite Day.** The playable clock stops. The title screen keeps its own
  clock, so its sky still moves. Note that sunset never arrives, so on
  Android there is currently no way to end a day with this on.
- **Free Camera.** Hold Shift and the mouse orbits instead of aiming; the
  cursor waits where you left it. On a pad the right stick orbits and the
  squad moves to D-pad Down. Horizontal only — this game's camera pitch is
  stepped, not analogue.
- **Lock-On.** Press it with the cursor over an enemy and the cursor sticks
  to it: the ring, the trail and your throws all follow it, even while you
  run. Press again, walk far enough away, or kill it, and the lock drops.
  Thrown Pikmin no longer inherit the captain's momentum while locked, so
  a throw on the move still lands on the target.
- **Charge.** With a target locked, the swarm button sends every Pikmin in
  formation at it. Pikmin already working carry on with what they were
  doing. Needs Lock-On.
- **First Person.** The camera moves into Olimar's helmet. The option in
  *Camera* enables the mode; a separate bindable key steps in and out of it
  while playing. See below for how it plays.
- **Throw While Moving.** The captain only takes a Pikmin into his hand once
  it reaches him, and a walking captain is a target the Pikmin never quite
  catches, so throwing on the move stalled. With it on, the handoff
  range grows with the captain's speed and is measured flat, so a slope no
  longer counts as ground still to cover.
- **Whistle Radius** (*Gameplay*). How big the whistle circle grows at full
  charge, from 50% to 300% of the original. The circle you see and the area
  that calls Pikmin grow together.
- **Instant Whistle Response** (*Gameplay*). Whistled Pikmin join the squad
  the moment the whistle reaches them. Normally each one waits a random
  moment, turns to look at Olimar and only then comes running, which with
  idle Pikmin can feel like they ignored you.
- **Throw Speed** (*Gameplay*). How fast you can throw, from 50% to 200%.
  It scales Olimar's grab and throw animations. Above 100% he also reaches
  further for the next Pikmin, and the one he picks runs to his hand faster,
  so the squad trailing behind him keeps up with the faster pace.
- **Cancel Throw With B** (*Controls*). While holding a Pikmin with A, press B
  to put it back in the squad instead of throwing it. It does not start the
  whistle.
- **No Tripping** (*Gameplay*). Pikmin running in the squad never trip over
  and fall behind.
- **Onion: Y for Steps of 10** (*Controls*). In the Onion menu, hold Y while
  moving up or down to move Pikmin 10 at a time. The usual limits still apply:
  if fewer than 10 fit, it moves as many as it can and shows the usual message.

## First person

- **The mouse looks around, always.** In the Mouse Cursor control scheme,
  moving the mouse turns the view left and right and tilts it up and down,
  with no button to hold. Up and down stop short of flipping over. Gyro pads
  do the same.
- **The cursor follows your view.** It sits a fixed distance in front of
  where you are looking, so throws go where you face.
- **The triggers zoom:** R closes in, L pulls back, as field of view rather
  than distance.
- **The eye sits at helmet height** instead of at ground level.
- **Fixed: the view spinning out of control.** The camera smoothing fed the
  first-person position back into itself every frame, so the view drifted
  and spun, and often ended up looking at the underside of the map.
- **Fixed: the ground vanishing near the camera.** Everything closer than 100
  units to the eye was cut off, which at head height is most of the floor.
  The black void underneath showed through. The cut-off is much shorter in
  first person now.
- **Fixed: Onions could not be opened.** Hiding Olimar's model also stopped
  his collision from updating, so the game never saw him standing next to
  the Onion. Only the drawing is skipped now. This also fixes his antenna
  light staying behind.

## Controls

- **Lock-On**, **First Person** and **Gyro Recenter** are new bindable
  actions. Lock-On is `R` on the keyboard and right stick click on a pad.
  First Person is `V` and left stick click. Gyro Recenter is unassigned by
  default. All of them can be changed in *Controls*.
- **Charge** has no key of its own. With the option on, the swarm button does
  it instead, and stops steering the squad.
- **Two new touch buttons** on Android, one each for Lock-On and First Person,
  and they only appear while their option is on. The layout editor shows them
  either way, so they can be placed before being switched on.

## Fixes

- **The game closed when moving on to the next day** (#47, #48). After the
  day's report and the save prompt, whichever answer you gave, the game
  crashed on Windows, Linux and Android alike. Closing the end-of-day scene
  touched Olimar's manager after the day's stage had already been torn down.
  It now checks first.
- **The cursor jumped after talking to the ship** (#40). With the Mouse
  Cursor scheme, the mouse kept being read while a text box, a cutscene or the
  pause screen was up, and the whole movement landed on the cursor at once when
  play resumed. Mouse movement is now discarded while the game is not
  listening to it.
- **The area map spilled past the 4:3 frame in widescreen** (#46). Its
  background and the glass plate along the top showed at the sides of the
  screen. They are now kept inside the frame, with black bars, like the file
  select.
- In co-op, the banner shown when a player goes down was in Spanish when the
  game language was Spanish. It now reads **OLIMAR DOWN** in every language,
  like the rest of the port's text.
- The **idle counter** kept drawing over the menus after leaving a stage,
  because the count it reads is not cleared when the stage ends.

## Known issues

- With Infinite Day on, a day cannot be ended on Android.
- Lock-On marks its target by moving the cursor onto it; there is no ring or
  reticle drawn on the enemy itself yet.
- In first person, the mouse only looks around in the Mouse Cursor control
  scheme; in Classic it is not captured. There are no touch buttons for the
  zoom on Android, and none for Gyro Recenter.
- On some phones a gyro axis may turn the wrong way. The invert option in
  *Controls* corrects it.
- The Onion's light beam is cut short compared to GameCube
  (tracked in `docs/PLAN_GRAPHICS.md`).
- The Nintendo logo in the intro and credits still renders wrong (#40).
