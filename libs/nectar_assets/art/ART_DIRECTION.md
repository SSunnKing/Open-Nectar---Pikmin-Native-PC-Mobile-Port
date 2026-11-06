# Disc-free stand-in art direction

This is the art bible for the stand-in models and textures that the disc-free
build draws (fork issue #115). Every design here is original. None of it
recreates, traces or approximates a Nintendo design, and nothing is taken from
the game disc. The roles are the game's (captain, Pikmin, Onion, ship, pellets,
enemies); the look is ours.

## The world: Hollowmere

A damp garden-floor world of river pebbles, moss and mineral crystals. Living
things here grow crystals the way plants grow leaves. The captain is a lantern
shepherd who herds small pebble folk, and the pebble folk grow a crystal crest
as they mature. Their homes are geode hives cracked open at the top.

## Shape language

- **Soft and chunky.** Pebble shapes: squashed spheres, eggs and rounded
  boxes. Every hard edge gets a bevel; nothing reads as a raw cube.
- **Crystals are the only sharp shapes.** Points, facets and spikes mean
  "mineral": maturity crests, hive spires, the ship's lamp. That keeps the
  sharp/soft contrast meaningful.
- **Silhouette first.** Each role must read from its silhouette alone at
  gameplay distance (a Pikmin is about 20 pixels tall in a 640x480 frame).
  Big, simple masses; small details only where they carry a cue.
- **Bottom-heavy.** Creatures and props sit low and wide on the ground.
- **GameCube-era budget.** Hundreds of triangles per creature, low thousands
  for the hive and the ship. Shading is baked into vertex colours by the export
  script (sky/ground ambient, one warm key light from above-front-left,
  ambient occlusion and a soft highlight), so the models look lit even though
  the stand-in draw is unlit.

## Palette

| Use | Colour | Notes |
|-----|--------|-------|
| Red folk | `#E8412F` coral red | gameplay cue, always saturated; in game the body takes the game's own red (`Piki::pikiColors`) |
| Yellow folk | `#F5C518` sunflower | gameplay cue; game colour in game, as above |
| Blue folk | `#2F6FE0` cobalt | gameplay cue; game colour in game, as above |
| Crest, tier 1 | `#D9D4C7` pale pebble | matte |
| Crest, tier 2 | `#7FE3F0` ice cyan | glassy |
| Crest, tier 3 | `#FFEC9E` pale star gold | glows (no shading); paler than the yellow folk |
| Captain coat | `#1E7F86` deep teal | the only teal creature |
| Captain lantern | `#FFA630` warm amber | glows |
| Puffmin folk | `#A48CCB` pale violet | the hostile folk; violet is the threat colour. In game the body takes the game's deep purple (`Piki::kinokoColors`) |
| Stone, hives, gates | `#8C8577` warm grey, `#5E574C` dark | |
| Moss and grass | `#6E9E3A`, `#4F7A2A` | |
| Soil and paths | `#8A6A45`, `#6B4F33` | |
| Enemy shell | `#5B3C7A` dusk violet | threat colour, not red/yellow/blue |
| Enemy belly and eyes | `#F2A65A` amber | eyes glow |
| Enemy claw tips | `#E8D6BC` bone | marks the biting end |
| Pellet body | `#F2D06B` honey gold | number embossed in dark brown |

Red, yellow and blue are reserved for the three folk types and their hives.
Nothing else in the world uses those saturated hues, so colour always means
"which type".

## Roles

Each entry gives the design, the gameplay cue it must carry, and how it differs
from the Nintendo design it replaces.

### Captain (`pikis/nv3Model.mod`)

A stocky lantern shepherd: a rounded pear-shaped body in a deep teal hooded
coat, a round face with big dark eyes under the hood, cream mittens and boots,
a satchel on the back, and a shepherd's crook rising from the satchel behind
his left shoulder that curves forward, with an amber lantern hanging from the
hook beside the hood.

The crook first arched over the middle of the hood. Seen from the front, the
arch was edge-on and read as a stick on the head with a glowing ball on it: an
antenna. Beside the hood it reads as a staff carried over the shoulder.

- **Cue:** must read as "the one you control" and never as a folk: teal, twice
  a folk's height, and the lantern glow marks him from far away. The lantern
  hangs from the leaf-tip joint (6), where the game puts its `ante` sphere.
  The right mitten is joint 7, on +x like the rig's `rhnd` sphere; the crook
  is on the other side.
- **Differs from Olimar:** no helmet, no glass dome, no antenna. A hooded coat
  and a crook-and-lantern instead of a spacesuit with a bulb on the helmet.

### Pebble folk: the three Pikmin types (`pikis/redModel.mod`, `yelModel.mod`, `bluModel.mod`)

Round pebble-shaped creatures: one egg-shaped body (no separate neck), two big
eyes with white rims, stubby nub arms and two short round feet. Each type adds
one small trait of its own:

- **Red:** three rounded ember scales running down the back.
- **Yellow:** round cheek pouches and a darker zig-zag band across the belly.
- **Blue:** a wide flat paddle tail.

- **Puffmin** (`kinModel.mod`): the same body with no trait, in the game's
  Puffmin colour (deep purple).

- **Cue:** body colour is the type. The trait backs it up for colour-blind
  players. The folk face +z like the rest of the rig.
- **Colour:** the game paints the body with the type colour on the model's
  first material (`ViewPiki::demoDraw`), and with it the idle pastel and the
  white damage flash. So the body, arms and feet are authored white (shaded by
  the vertex colours) and the bindings draw them with that material; the eyes
  and the trait keep their own colours.
- **Rig:** the stand-in rig keeps every joint at the feet, and the game turns
  joints 2 to 6 when a folk looks around. So the body hangs from the root (0),
  the eyes and trait from the spine joint (1), the arms from 7 and 8 and the
  feet from 10 and 11; nothing hangs from the head joints.
- **Differs from Pikmin:** no stem, no leaf, bud or flower on the head, no
  long thin body or limbs, no pointed ears or nose. Squat pebbles instead of
  tall sprouts.

### Maturity crests (`pikis/happas/leaf.mod`, `bud.mod`, `flower.mod`)

The game draws one of three "happa" models at the folk's leaf-tip joint. Here
they are crystal crests sitting directly on the crown, with no stalk:

- **Tier 1 (leaf):** one small, matte pale-pebble bead. A dot.
- **Tier 2 (bud):** a pair of tall ice-cyan crystal shards. A spike.
- **Tier 3 (flower):** a large five-pointed star-gold crystal that glows. A star.
  It is paler than the bible's first gold (`#FFE27A`), which was too close to
  the yellow folk at gameplay distance.

- **Cue:** dot, spike and star are three different silhouettes and three
  different brightnesses, so the tier reads even at a distance and even
  without colour.
- **Differs from Pikmin:** no stalk and no plant parts. Crystal growth, not
  leaf, bud and flower.

### Buried folk (`objects/pikihead/pikihead.mod`)

The upper half of a pebble body with its eyes peeking out of a mound of loose
soil, waiting to be plucked. The game draws the crest at the buried folk's
joint 3, which the stand-in rig keeps at the model origin, so the crown has to
be at the walking folk's crown height for the crest to sit on it; showing only
the crown left the crest floating above the soil.

The game tints the model's first material with the folk colour, so the body
hangs from joint 0, which the binding draws with that material (white, shaded
by the vertex colours). The eyes and soil keep their own colours. The buried
folk and its crest are drawn unlit like the walking folk.

### Home hives, one per colour (`objects/goal/goal.mod`)

A squat geode hive: a wide rounded stone dome sitting on a ring of four short
stone buttresses, split open at the top in a jagged crack lined with crystals
of the hive's colour, and a tall crystal spire of that colour rising from the
crack. A ramp-like lip faces the front.

- **Cue:** the crystal colour names the type from across the map; the spire is
  the tallest thing near the landing site.
- **Differs from the Onion:** no bulb, no flower, no three thin legs and it
  does not hover. A cracked stone geode on the ground with a crystal spire.

### Ship (`objects/ufo/ufo0705.mod` and its parts)

The captain's lantern barge: a wide round teal-and-brass gondola like a squat
kettle, a riveted brass band around the middle, a big amber glass lamp on top
(the same amber as the captain's lantern), four stubby fan pods on short arms
and four landing feet.

- **Cue:** wide disc silhouette with a glowing top; the brass band marks the
  side where parts are delivered.
- **Differs from the Dolphin:** not elongated, no nose cone, no cockpit canopy,
  no fins or tail. A round flying lamp-kettle.

### Pellets (`objects/pellets/white1-4.mod`)

Thick rounded honey-gold seed tokens: a domed disc with a bevelled rim and a
groove around the edge, with the value (1, 5, 10, 20) embossed in raised dark
brown numerals on both faces. Larger values are larger tokens.

- **Cue:** the number is the carry weight and must be legible at gameplay
  distance; the token stands upright so the face is visible.
- **Differs from pellets:** not a flat coloured disc; a domed, grooved token
  with raised numerals.

### Nectar, bomb rocks

- **Nectar:** a cluster of three glowing pale-green dew beads.
- **Bomb rock:** a charcoal stone ball wrapped in a riveted iron band with a
  glowing hazard-orange crystal set on top.

### First enemy (Bulborb's role, `chappy`)

A dusk-violet shelled stilt-beetle: a high domed shell with three lighter ridge
plates, four thick legs bent at the knee, an amber underside, and a wide jaw of
two crab-like pincers with bone-pale tips in front, with two glowing amber eyes
on short stalks above them.

- **Cue:** the threat colour (violet) appears nowhere else; the pincers show
  which end bites, and the eye stalks show it from behind.
- **Rig:** each part hangs from the joint that means it: shell and belly on the
  body (1), tail plate on 2, neck on 3, head and pincers on the head (4, where
  the 'head' and 'slot' parts sit), one eye stalk on each of 5 and 6, the right
  legs on 7-9 and the left legs on 10-12 (the shins 8 and 11 are 'rleg' and
  'lleg'). Joints 13-16 carry nothing.
- **Size:** about 30 wide, 39 long (pincer tips to tail) and 20 tall, standing
  on the ground; the shell fits the 20-unit body sphere. The legs are bent
  stilts rather than short stumps, so the body rides high and the amber
  underside shows below the shell.
- **Differs from Bulborb:** no red spotted bulbous body, no huge mouth with
  small eyes. A shelled beetle with pincers and stalk eyes.

### Terrain, map edge, gates and bridges

- **Ground:** moss-green grass with soft clumps and scattered soil specks;
  paths of packed brown soil. Generated textures, tiled.
- **Map edge:** a cliff of layered warm-grey sandstone with darker strata
  lines, not a flat strip.
- **Gates:** a barricade of woven dark twigs between two rounded stone posts.
- **Bridges:** weathered planks with rope rails on stone footings.

## Process rules

- The `.blend` files under `art/blender/` are the source. The export script
  (`art/export_standins.py`) bakes the lighting and writes `art/export/*.inc`,
  which the C++ generator compiles in. See `art/README.md`.
- Never trace, copy or extract anything from the disc or from Nintendo art.
  Joint counts, parents and collision spheres stay the disc's numbers (they are
  structure, not art, and gameplay depends on them); geometry and colour are
  ours.
