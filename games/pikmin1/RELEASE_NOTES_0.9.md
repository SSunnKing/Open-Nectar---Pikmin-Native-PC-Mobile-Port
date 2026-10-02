# Open Nectar 0.9

A big one: a brand-new 1-vs-1 Versus mode, 44 achievements, playable Pikmin
captains, a rebuilt F1 settings page with mouse support and a round of
fixes. Saves and settings carry over.

## New

- **Versus mode: "Parts Race" (1 vs 1).** A new entry in the main menu
  (Start / Co-op / VS / Options / Advanced Options / Challenge Mode). It
  revives the versus mode Nintendo left unfinished and always plays with two
  captains.
  - **Its own arena**, built in memory from the game's own Impact Site data:
    a flat walled field with a base at each end, two ponds and the centre
    free for the big part. No extra files needed.
  - Each player gets their own three Onions, 15 Pikmin (5 of each colour)
    and their own rocket. Ship parts are picked by weight; deliver them to
    your rocket to score.
  - **Rocket siege (optional):** your free Pikmin near the rival rocket wear
    it down; destroying it wins the match. Each part delivered repairs it.
    If both rockets fall at once, it's a draw.
  - Bomb-rock gate at each base, with bombs nearby to open it.
  - **Rules menu before the match:** duration, rocket siege on/off, rocket
    health, when the big part appears, Pikmin per player and pellet respawn.
    Rules are saved with your settings.
  - 3-2-1-START countdown (nobody moves until it ends), per-player scoreboard
    and HUD, reinforcements for a player left with no Pikmin, and a final
    screen with rematch or back to title. In the pause menu, "Quit match"
    goes back to the title. Versus does not touch your save.
  - Rival sprouts can't be plucked; Pikmin fight the rival captain's squad.
  - Shortest-path routing on the arena so carriers don't take detours.
- **Achievements.** 44 achievements (444 points) with their own tab in F1:
  every ship part, each Pikmin colour, Goolix, the three endings, "Allergic
  to Blue", "Hocotate Speed Demon" and the five Challenge Mode scores.
  Unlocked ones show in colour, locked ones in grey, and selecting one tells
  you how to get it. A pop-up with the icon appears when you unlock one.
  - Icons come from your own game files (the ship parts list, the Pikmin
    faces, the area photos), so nothing extra is shipped.
  - Titles come from SlashTangent's RetroAchievements set for Pikmin.
  - Progress is kept apart from your save (`pikmin_achievements.txt`).
  - Achievements stay off in Versus mode and while cheats are on (Pikmin
    limit over 100, infinite day, speed or health changes...). Quality of life
    mods like Quick Grab or Throw Speed don't count as cheats.
- **Pikmin captains.** In the captain picker you can now choose Olimar,
  Louie or a Red, Yellow or Blue Pikmin. A Pikmin captain looks the part
  (model, leaf light, portrait), is as tall as Olimar and plays exactly like
  him. The picker shows each Pikmin with new pixel-art sprites.
- **Quick Grab** (F1 > Controls, off by default): the Pikmin you throw
  appears in Olimar's hand at once, so throwing is just as fast with the
  squad behind him as around him.
- **Mouse in the F1 menu:** left click selects tabs, rows and options (click
  again to use), the wheel moves up and down and right click goes back.
- **Per-player camera in Co-op/VS:** each controller's right stick turns its
  own player's camera; mouse and touch stay on Player 1.
- **New cheats:** "Invincible Pikmin" and "Unlock All Zones" (this one is
  saved to the file). Hard mode turns every cheat off.
- **Player 2 can open F1 in Co-op/VS** with Select on their own controller,
  navigate it with that controller and remap their own gamepad buttons
  (saved separately from Player 1's).

## Changes

- **F1 settings rebuilt as a tabbed page** (Display, Graphics, Controls,
  Camera, Gameplay, Cheats, Data, Achievements): rows with section headers
  on the left, the row's options and an explanation on the right. Works with
  mouse, controller, keyboard and touch, and scales up on phones.
- **Glass settings menu on the title screen** gets the same layout, with a
  scrollbar, a right-hand options column and shrinking long values.
- **Bigger squads keep their shape:** with the Pikmin limit above 100 the
  formation grows in length as well as width, instead of squashing into a
  wide oval. At 100 or less it behaves exactly like the original.
- The Free Camera explanation now says how to use it: hold Left Shift and
  move the mouse, or use the right stick.
- "Infinite Day" is now the "Infinite" stop at the end of the day-length list.
- More full-width screens (#46): file select, title, world map, course
  select, high scores and slide-in menus no longer clip to 4:3, and elements
  the original layout parks off-screen stay hidden.

## Fixes

- **World map touch (Android):** tapping an area selects it again; taps were
  landing off to the side since the map went full width.
- **Save export/import (Android):** exporting a backup no longer gets
  picked up by the HD models installer; backups hold only the two memory
  cards (the shader cache made our own ZIP fail the restore check); no more
  empty .zip when there is nothing to copy; exporting works on storage
  providers that reject overwrite mode.
- **Co-op:** Pikmin taken out of an Onion by Player 2 now go to Player 2
  instead of Player 1.
- **Co-op/VS geysers:** a geyser only launches the captain standing on it;
  the other captain was being flung into the sky and landing on it.
- **Co-op split screen:** enemy health circles no longer vanish in Player
  2's view when Player 1 isn't looking at that enemy.
- **Controller reconnect:** a controller that disconnects and reconnects goes
  back to the same player (it stopped responding, or swapped players).
- **First person per player:** in Co-op/VS each player toggles their own
  first-person view (keyboard owner with V, or each controller's bound
  button, Left Stick click by default) instead of both switching at once.
- **VS:** Pikmin attacking a rocket can be killed again by the rival's
  Pikmin; after the first hit they became invulnerable.
- **Captain picker art** is now built into the game, so the pixel-art
  portraits show on every install (they were missing on Linux installs).
- **Accents and ñ in menus:** Spanish text (e.g. the VS rules and
  explanation screen) shows accented letters, ñ, ¿ and ¡ correctly.
- **F1 and glass menus:** a dark backing behind the panels makes the text
  easier to read; long achievement titles shrink or are cut with "..."
  instead of overlapping, and the achievement help no longer shows a stray
  "Locked." outside the panel.
- Menu key-repeat and window/resolution handling tidied up.
