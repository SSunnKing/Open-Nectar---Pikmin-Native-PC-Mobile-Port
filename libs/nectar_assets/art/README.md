# Stand-in art sources

The disc-free build's stand-in models are made in Blender. This folder holds
their sources and the script that turns them into data the C++ stand-in
generator compiles in. Building the game or the generator never needs Blender:
the exports are committed. See [ART_DIRECTION.md](ART_DIRECTION.md) for the
designs.

| Path | What |
|------|------|
| `blender/*.blend` | The sources. One file per family (pellets, folk, ...); one collection per model. |
| `export_standins.py` | Blender script: bakes the lighting into vertex colours and writes `export/`. |
| `export/*.inc` | The committed exports, one per model, plus `all.inc`. Generated: do not edit. |
| `check_export.py` | Exports to a temporary folder and compares with `export/` (tolerant to last-digit rounding). |
| `authoring/*.py` | The seed scripts that built the first version of each `.blend` from Blender primitives. Re-running one overwrites its `.blend`. |

## Re-export everything

From the repository root, with Blender 5.2.3 (other versions may evaluate
modifiers slightly differently and fail the check):

```sh
Blender --background --factory-startup --python libs/nectar_assets/art/export_standins.py
```

On macOS `Blender` is `/Applications/Blender.app/Contents/MacOS/Blender`. Then
rebuild `nectar_standin_generator` and commit `export/` together with the
`.blend` change. Check that the exports are current:

```sh
python3 libs/nectar_assets/art/check_export.py [--blender PATH]
```

CI runs the same check (`.github/workflows/standin-art-export.yml`) when
anything under `art/` changes.

## Rules for a model in a `.blend`

- A collection with the custom properties `nectar_model` (the art name the
  C++ binding uses), `nectar_joints` (the joint count of the `.mod` it feeds:
  the generator refuses art made for another rig) and optionally
  `nectar_ground` (darken what touches the ground).
- Mesh objects inside it, modifiers allowed. `nectar_joint` on an object is the
  joint it hangs from (default 0). The stand-in animations keep every joint at
  the model origin, so author every part in place, in game units.
- Blender is Z up and the front of a model faces -Y. The export turns that into
  the game's Y up, front +z.
- Colour comes from each material's viewport colour, or a colour attribute
  named `Base`. Material properties `nectar_emit` (0 to 1: glowing parts ignore
  the lighting) and `nectar_gloss` (highlight strength).
- Objects hidden from render are not exported (use them for references).

## How the C++ side uses it

`src/standin_art_meshes.cpp` includes `export/all.inc`. `standInArtBindings()`
in `src/standin_art.cpp` maps a stand-in `.mod` path to an art model and a
scale; `buildArtStandInModel` then puts the art on the spec's own skeleton, so
joint counts, parents, joint bounds and the collision text (`collinfo`) stay
exactly what they were. Every mesh is vertex-coloured on one white material;
the scenario build's unlit stand-in draw multiplies the material colour by the
vertex colour (`dgxGraphics.cpp`). The lit draw ignores vertex colours (the
model draws white), so the object's draw call must turn the unlit draw on
(`pc_scenario_push_creature_unlit_draw`): pellets, hives, the ship and the
captain do, and enemies do when their shape has vertex colours
(`BTeki::drawTekiShape`). A binding's `tintJoints` put those joints' meshes on
material 0 instead, the material the game recolours at run time (the walking
and buried folk take their folk colour, idle pastel and damage flash that way),
so their art is authored white.
`nectar_mod_writer_test` checks the bindings of the 45 boot models and
`nectar_standin_objects_test` those of the object models.

### Why C++ includes and not OBJ, glTF or JSON

- The rigs need a joint per mesh and a colour per corner; OBJ has neither
  without extensions.
- glTF keeps its geometry in binary buffers, so a change would not diff, and
  reading it needs a library the stand-in tools do not have.
- A text format read at run time needs a parser, a way to find the files from
  every test binary, and (embedded) a code-generation step; MSVC also caps
  string literals near 64 KB.
- Static tables of `{ x, y, z, nx, ny, nz, r, g, b }` need none of that: one
  vertex per line diffs cleanly, the compiler rejects a malformed file, and
  nothing new is linked.
