# Open Nectar 0.9.2

A Speedrun mode with its own timer, splits and records, menus redrawn in the
game's own style, cutscenes you can skip, and a batch of fixes: no more crash
on the map with over 100 Pikmin, the squad keeps all its Pikmin when you
swarm, and the camera behaves when you bring back the Whimsical Radar.
Saves, settings and texture packs carry over.

## New

- **Speedrun mode.** A new entry on the title screen, under Start. It opens a
  small menu: **Start Run**, **Best Times** and **How It Works** (shown by
  itself the first time).
  - **Pure GameCube Pikmin.** Every cheat, mod and port option that changes
    play is switched off for the run: original day length, health, speeds
    and Pikmin limit, 30 FPS simulation, classic controls (cursor on the
    stick, no gyro), no Quick Grab, Lock-On, free camera and so on. Your own
    settings come back as soon as you leave. Graphics, texture packs and
    button mapping stay as you have them.
  - **Timed like speedrun.com.** Real time with milliseconds, loads and
    cutscenes included. It starts the moment the new game starts and stops
    when the Secret Safe is collected. Start Run goes straight into a new
    game, with Olimar and no file select. The opening crash can't be skipped,
    as on the console.
  - **Splits.** One per day, named after the area and visit
    ("Forest of Hope 2"), listed under the timer with your total and the
    difference to your best run: green when you're ahead, red when behind,
    gold for your best ever segment.
  - **Best Times.** Your personal best (time, days, date and splits), your
    Sum of Best and your last five runs. Records save themselves when a run
    finishes; X erases them.
  - **Input display.** Under the timer, a see-through view of what you press:
    the controller you are using (Xbox, PlayStation or Switch labels, sticks
    and triggers included), or a full keyboard and mouse.
  - Speedrun runs use their own memory card, so your normal saves are never
    touched.
- **Skip cutscenes.** In normal play, Start skips any cutscene: arriving at an
  area, finding an Onion, and the rest. Leaving an area was already
  skippable.
- **Hide Olimar's Texts** (optional, off by default): no text boxes while you
  play, such as the first Pikmin, every ship part or tips. The ending texts
  still show. F1 > Gameplay.
- **Bomb Control** (optional, off by default): a Bomb button (B on the
  keyboard; assign one in Gamepad Bindings). A Yellow Pikmin with a bomb rock
  throws it at the cursor, or drops it lit at its feet when the cursor is
  too close. F1 > Controls.
- **Menus in the game's own style.** New Game, Captain, Controllers, the VS
  rules and results, and the restart notices now use the glass windows of
  Pikmin's own menus. Bigger text, the spinning cursor beside the chosen
  option, the real controller button icons in the help line, and the starry
  background of the file select behind them.
- **A message when a Permadeath file is lost.** After Olimar falls on a
  Permadeath file you are told the file has been erased, instead of finding
  the slot empty.

## Changes

- **Advanced Options moved into Options** on the title screen.
- **No more 1P/2P question** before file select: Co-op has its own button on
  the title screen.
- **Original day length by default** (13.5 minutes of daylight) instead of
  10 minutes.
- **Idle Pikmin counter** uses the same lettering and size as the rest of
  the game's text.
- **Carried-object counters** show the right colours for the carriers and
  the Onion they are heading to. Thanks to 4laric (#63).
- **Co-op:** the two captains' swarm sounds no longer cancel each other.
  Thanks to 4laric (#64).

## Fixes

- **The map no longer crashes with more than 100 Pikmin** (Pikmin and
  sprouts) on the field.
- **Swarming keeps all your Pikmin.** With the Pikmin limit above 100, a
  full C-stick swarm stretched the squad so far that the Pikmin at the back
  dropped out of it. A regression in 0.9.
- **The Whimsical Radar cutscene** no longer spins the camera nonstop. Any
  cutscene that looks straight down now holds its orientation.
- **The end-of-day report** is no longer stretched on wide screens such as
  21:9 (#62).
- **Infinite Day** no longer stops day 1 from ending.
- **Insta Kill and Enemy Health** now apply to bosses too.
