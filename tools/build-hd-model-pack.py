#!/usr/bin/env python3
"""Build Open Nectar's external HD model packs (NHM1) from Pikmin 3 rips.

    build-hd-model-pack.py olimar  <Olimar.zip> <out.nhm>
    build-hd-model-pack.py pikmin  <Pikmin.zip> <out dir>
    build-hd-model-pack.py bulborb <Dwarf Bulborb.zip> <Bulborb.zip> <out dir>

Each NHM file holds expanded, skinned triangles plus the diffuse texture of
every part.  Nintendo's original .mod files are neither copied nor modified:
the game keeps loading them for animation and collision and only swaps the
visible mesh (see pc_port/mods/pc_hd_models.cpp).

File layout (little endian):
    "NHM1" u32 version  u32 bones  u32 parts
    bones * ( u8 len, name[len], 16 f32 inverse bind, row-major )
    parts * ( u32 vertices, u32 width, u32 height, u32 rgbaBytes,
              [version >= 2: u32 flags  (bit 0: repeat texture wrap, bit 1: no culling)],
              vertices * ( 3 f32 pos, 3 f32 nrm, 2 f32 uv, 4 u8 bone, 4 f32 weight ),
              rgba bytes )
"""

from __future__ import annotations

import argparse
import io
import struct
import zipfile
import xml.etree.ElementTree as ET
from pathlib import Path

from PIL import Image

NS = {"c": "http://www.collada.org/2005/11/COLLADASchema"}
JOINTS = [
    "kosinull", "legcentre", "llegjnt", "rlegjnt", "sebonjnt", "headjnt",
    "happajnt1", "happajnt2", "happajnt3", "lhandjnt", "rhandjnt",
]
IDENTITY = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]

# playerE_body is fully opaque; Pikmin 3 makes the visor see-through in its
# shader, so a constant alpha is baked into the glass part's texture copy.
GLASS_ALPHA = 72


def floats(node: ET.Element) -> list[float]:
    return [float(x) for x in (node.text or "").split()]


def source_data(parent: ET.Element) -> dict[str, tuple[list, int]]:
    result = {}
    for source in parent.findall("c:source", NS):
        acc = source.find("c:technique_common/c:accessor", NS)
        stride = int(acc.get("stride", "1"))
        arr = source.find("c:float_array", NS)
        if arr is not None:
            values = floats(arr)
        else:
            names = source.find("c:Name_array", NS)
            values = (names.text or "").split()
        result[source.get("id")] = (values, stride)
    return result


def rgba(zip_file: zipfile.ZipFile, name: str) -> tuple[int, int, bytes]:
    with Image.open(io.BytesIO(zip_file.read(name))) as image:
        image = image.convert("RGBA")
        return image.width, image.height, image.tobytes()


def wrap_clamp(t: float) -> float:
    """Fold into [0, 1): the game samples with clamp, so islands that live in
    a neighbouring tile (Olimar's head sits in [-1, 0]) must be moved."""
    return t % 1.0


def wrap_repeat(t: float) -> float:
    """Left untouched: the part is flagged so the game samples with repeat."""
    return t


PART_FLAG_REPEAT = 1   # texture wraps instead of clamping
PART_FLAG_NOCULL = 2   # draw both faces (thin eye discs)
CORNEA_ALPHA = 64


def translucent(texture: tuple[int, int, bytes], alpha: int) -> tuple[int, int, bytes]:
    """Copy of a texture with a constant alpha, for glassy shells the source
    game renders with a shader (Olimar's visor, bulborb corneas)."""
    pixels = bytearray(texture[2])
    pixels[3::4] = bytes([alpha]) * (len(pixels) // 4)
    return texture[0], texture[1], bytes(pixels)


def wrap_mirror(t: float) -> float:
    """Mirrored repeat, for textures the rip notes as 'Mirror X/Y'."""
    t = t % 2.0
    return 2.0 - t if t > 1.0 else t


def read_skins(root: ET.Element) -> tuple[dict, dict]:
    """Returns ({geometry id: per-vertex [(joint name, weight)]}, {joint: inverse bind})."""
    controllers = {}
    bind_by_joint = {}
    for controller in root.findall("c:library_controllers/c:controller", NS):
        skin = controller.find("c:skin", NS)
        data = source_data(skin)
        joints_input = skin.find("c:joints/c:input[@semantic='JOINT']", NS)
        bind_input = skin.find("c:joints/c:input[@semantic='INV_BIND_MATRIX']", NS)
        names = data[joints_input.get("source")[1:]][0]
        bind_values = data[bind_input.get("source")[1:]][0]
        for i, name in enumerate(names):
            # Exporters used by these rips write matrices row-major (verified
            # by multiplying every global bind pose by its inverse bind).
            bind_by_joint.setdefault(name, bind_values[i * 16:(i + 1) * 16])

        vw = skin.find("c:vertex_weights", NS)
        weight_input = vw.find("c:input[@semantic='WEIGHT']", NS)
        weights = data[weight_input.get("source")[1:]][0]
        joint_offset = int(vw.find("c:input[@semantic='JOINT']", NS).get("offset"))
        weight_offset = int(weight_input.get("offset"))
        stride = 1 + max(int(x.get("offset")) for x in vw.findall("c:input", NS))
        vcount = [int(x) for x in vw.find("c:vcount", NS).text.split()]
        v = [int(x) for x in vw.find("c:v", NS).text.split()]
        cursor = 0
        influences = []
        for count in vcount:
            row = []
            for _ in range(count):
                row.append((names[v[cursor + joint_offset]], weights[v[cursor + weight_offset]]))
                cursor += stride
            row.sort(key=lambda x: -x[1])
            row = row[:4]
            total = sum(weight for _, weight in row) or 1.0
            influences.append([(name, weight / total) for name, weight in row])
        controllers[skin.get("source")[1:]] = influences
    return controllers, bind_by_joint


def read_triangles(mesh: ET.Element) -> tuple[list[dict], dict, dict, list[int], int, str]:
    """Resolves the <triangles> inputs, following <vertices> indirections
    (some exporters put POSITION/NORMAL/TEXCOORD all behind one VERTEX input)."""
    data = source_data(mesh)
    triangle = mesh.find("c:triangles", NS)
    vertices_node = mesh.find("c:vertices", NS)
    via_vertex = {x.get("semantic"): x.get("source")[1:] for x in vertices_node.findall("c:input", NS)}
    inputs = triangle.findall("c:input", NS)
    stride = 1 + max(int(x.get("offset")) for x in inputs)
    offsets: dict[str, int] = {}
    sources: dict[str, str] = {}
    for x in inputs:
        semantic = x.get("semantic")
        if semantic == "VERTEX":
            for sub_semantic, source in via_vertex.items():
                offsets[sub_semantic] = int(x.get("offset"))
                sources[sub_semantic] = source
        else:
            offsets[semantic] = int(x.get("offset"))
            sources[semantic] = x.get("source")[1:]
    indices = [int(x) for x in triangle.find("c:p", NS).text.split()]
    return data, offsets, sources, indices, stride, triangle.get("material")


def expand(root: ET.Element, geometry: ET.Element, controllers: dict, joint_names: list[str],
           wrap, transform=None) -> list[tuple]:
    """Expands one geometry into flat vertices: pos, nrm, uv, 4 bone ids, 4 weights."""
    mesh = geometry.find("c:mesh", NS)
    data, offsets, sources, indices, stride, _ = read_triangles(mesh)
    influences = controllers[geometry.get("id")]
    out = []
    for base in range(0, len(indices), stride):
        def value(semantic: str, size: int):
            values, src_stride = data[sources[semantic]]
            index = indices[base + offsets[semantic]]
            return values[index * src_stride:index * src_stride + size]
        pos = value("POSITION", 3)
        normal = value("NORMAL", 3)
        uv = value("TEXCOORD", 2)
        if transform:
            pos, normal = transform(pos), transform(normal)
        skin = influences[indices[base + offsets["POSITION"]]]
        bone_ids = [joint_names.index(name) for name, _ in skin] + [0] * (4 - len(skin))
        bone_weights = [w for _, w in skin] + [0.0] * (4 - len(skin))
        # Collada UV origin is bottom-left; the packs use top-left images.
        u, v = wrap(uv[0]), wrap(1.0 - uv[1])
        out.append((*pos, *normal, u, v, *bone_ids, *bone_weights))
    return out


def write_pack(output: Path, bones: list[tuple[str, list]], parts: list[tuple]) -> None:
    """parts: (vertices, texture) or (vertices, texture, flags). Version 1 is
    written when no part needs flags so older packs stay byte-identical."""
    output.parent.mkdir(parents=True, exist_ok=True)
    flags = [part[2] if len(part) > 2 else 0 for part in parts]
    version = 2 if any(flags) else 1
    payload = io.BytesIO()
    payload.write(struct.pack("<4sIII", b"NHM1", version, len(bones), len(parts)))
    for name, matrix in bones:
        encoded = name.encode("ascii")
        payload.write(struct.pack("<B", len(encoded)))
        payload.write(encoded)
        payload.write(struct.pack("<16f", *matrix))
    for part, part_flags in zip(parts, flags):
        vertices, (width, height, pixels) = part[0], part[1]
        payload.write(struct.pack("<IIII", len(vertices), width, height, len(pixels)))
        if version >= 2:
            payload.write(struct.pack("<I", part_flags))
        for vertex in vertices:
            payload.write(struct.pack("<8f4B4f", *vertex))
        payload.write(pixels)
    output.write_bytes(payload.getvalue())
    print(f"wrote {output}: {sum(len(part[0]) for part in parts)} vertices, {len(parts)} parts (v{version})")


def build_olimar(source_zip: Path, output: Path) -> None:
    with zipfile.ZipFile(source_zip) as archive:
        root = ET.fromstring(archive.read("playerE.dae"))
        controllers, bind_by_joint = read_skins(root)
        # Only head_m samples playerE_head; the suit, metal, light and both
        # helmet layers (naka = inner, soto = outer glass) sample playerE_body.
        # The glass is emitted as its own last part so it is drawn after the
        # face it covers and can blend over it.
        by_part = {"body": [], "head": [], "glass": []}
        for geometry in root.findall("c:library_geometries/c:geometry", NS):
            material = geometry.find("c:mesh/c:triangles", NS).get("material")
            key = {"head_m": "head", "naka_m": "glass", "soto_m": "glass"}.get(material, "body")
            by_part[key] += expand(root, geometry, controllers, JOINTS, wrap_clamp)
        body = rgba(archive, "playerE_body.png")
        glass_pixels = bytearray(body[2])
        glass_pixels[3::4] = bytes([GLASS_ALPHA]) * (len(glass_pixels) // 4)
        textures = [body, rgba(archive, "playerE_head.png"), (body[0], body[1], bytes(glass_pixels))]
    bones = [(joint, bind_by_joint.get(joint, IDENTITY)) for joint in JOINTS]
    write_pack(output, bones, list(zip((by_part["body"], by_part["head"], by_part["glass"]), textures)))


# Pikmin 3's rigid leaf/bud/flower meshes are Y-up-ish with the stem along -Z.
# Pikmin 1 draws pikis/happas/*.mod in joint happajnt3's space with the stem
# along +X and the blade spread along Z, so bake that rotation in and let the
# game place the mesh with the joint matrix alone.
def happa_to_joint_space(v: list[float]) -> list[float]:
    x, y, z = v
    return [-z, y, x]


def build_pikmin(source_zip: Path, out_dir: Path) -> None:
    bodies = {"red": "piki_p3_red.dae", "yellow": "piki_p3_yellow.dae", "blue": "piki_p3_blue.dae"}
    happas = {
        "leaf": ("leaf.dae", "piki_leaf_tex.png"),
        "bud": ("bud.dae", "piki_bud_tex.png"),
        "flower": ("flower.dae", "piki_flower_tex.png"),
    }
    with zipfile.ZipFile(source_zip) as archive:
        for colour, dae in bodies.items():
            root = ET.fromstring(archive.read(f"Pikmin/{dae}"))
            controllers, bind_by_joint = read_skins(root)
            vertices = []
            for geometry in root.findall("c:library_geometries/c:geometry", NS):
                # "Piki_PikminCOLOR_all requires Image mapping Mirror X/Y" (rip notes).
                vertices += expand(root, geometry, controllers, JOINTS, wrap_mirror)
            texture = rgba(archive, f"Pikmin/Textures/piki_{colour}_all.png")
            bones = [(joint, bind_by_joint.get(joint, IDENTITY)) for joint in JOINTS]
            write_pack(out_dir / f"piki_{colour}.nhm", bones, [(vertices, texture)])
        for name, (dae, png) in happas.items():
            root = ET.fromstring(archive.read(f"Pikmin/{dae}"))
            controllers, bind_by_joint = read_skins(root)
            joint_names = list(bind_by_joint)
            vertices = []
            for geometry in root.findall("c:library_geometries/c:geometry", NS):
                vertices += expand(root, geometry, controllers, joint_names, wrap_clamp,
                                   transform=happa_to_joint_space)
            texture = rgba(archive, f"Pikmin/Textures/{png}")
            # Already in the target joint's space: identity inverse bind.
            write_pack(out_dir / f"happa_{name}.nhm", [(name, IDENTITY)], [(vertices, texture)])


# Pikmin 3's bulborb rigs were rebuilt (different joint names, extra mouth
# bones), so their vertices are brought into Pikmin 1 model space with a
# similarity transform fitted on matching joints (eyes, legs, face, waist,
# root; residuals <= 2.4 units) and then skinned with the engine's own
# inverse bind matrices. Pikmin 1's swallow.mod (Spotty Bulborb) shares the
# dwarf's skeleton and is drawn at scale 3, hence the ~1/3 factor.
BULBORB_FITS = {
    "dwarf": (0.9463, (0.004, -0.07, 1.13)),
    "big": (0.3298, (0.004, 14.193, 3.691)),
}


def build_bulborb(dwarf_zip: Path, big_zip: Path, out_dir: Path) -> None:
    def aligned(scale: float, offset: tuple[float, float, float]):
        def transform_position(v: list[float]) -> list[float]:
            return [v[0] * scale + offset[0], v[1] * scale + offset[1], v[2] * scale + offset[2]]
        return transform_position

    def expand_aligned(root, geometry, controllers, joint_names, wrap, fit):
        scale, offset = fit
        # Uniform scale + translation: normals are unchanged, so expand with
        # the identity and move only the positions afterwards.
        vertices = expand(root, geometry, controllers, joint_names, wrap)
        move = aligned(scale, offset)
        return [(*move(v[0:3]), *v[3:]) for v in vertices]

    with zipfile.ZipFile(dwarf_zip) as archive:
        root = ET.fromstring(archive.read("Dwarf Bulborb/kochappy.dae"))
        controllers, bind_by_joint = read_skins(root)
        joint_names = list(bind_by_joint)
        # Body, eyes and the moyou (back spot) decals all sample opaque
        # textures; keep the decals last so they draw over the body.
        # eye_3_m is the iris/sclera disc and eye_m the highlight dot, both
        # thin pieces drawn two-sided; eye_2_m is the glassy cornea shell
        # (environment-mapped and see-through in Pikmin 3), drawn last with a
        # baked alpha so the iris shows through.
        by_part = {"body": [], "eyes": [], "moyou": [], "cornea": []}
        keys = {"moyou_m": "moyou", "eye_2_m": "cornea", "eye_3_m": "eyes", "eye_m": "eyes"}
        for geometry in root.findall("c:library_geometries/c:geometry", NS):
            material = geometry.find("c:mesh/c:triangles", NS).get("material")
            by_part[keys.get(material, "body")] += expand_aligned(
                root, geometry, controllers, joint_names, wrap_clamp, BULBORB_FITS["dwarf"])
        body_tex = rgba(archive, "Dwarf Bulborb/kochappy_tex.png")
        bones = [(name, IDENTITY) for name in joint_names]
        write_pack(out_dir / "bulborb_dwarf.nhm", bones, [
            (by_part["body"], body_tex),
            (by_part["eyes"], body_tex, PART_FLAG_NOCULL),
            (by_part["moyou"], rgba(archive, "Dwarf Bulborb/moyou_tex.png")),
            (by_part["cornea"], translucent(body_tex, CORNEA_ALPHA)),
        ])

    with zipfile.ZipFile(big_zip) as archive:
        root = ET.fromstring(archive.read("Red Bulborb/model.dae"))
        controllers, bind_by_joint = read_skins(root)
        joint_names = list(bind_by_joint)
        images = {i.get("id"): (i.find("c:init_from", NS).text or "").strip().lstrip("./")
                  for i in root.findall("c:library_images/c:image", NS)}
        effect_image = {}
        for effect in root.findall("c:library_effects/c:effect", NS):
            init = effect.find(".//c:init_from", NS)
            effect_image[effect.get("id")] = images.get((init.text or "").strip(), "") if init is not None else ""
        material_image = {m.get("id"): effect_image.get(m.find("c:instance_effect", NS).get("url")[1:], "")
                          for m in root.findall("c:library_materials/c:material", NS)}
        # <triangles material> holds a symbol that <bind_material> maps to the
        # real material id.
        symbol_material = {im.get("symbol"): im.get("target")[1:]
                           for im in root.findall(".//c:bind_material//c:instance_material", NS)}
        # Scene nodes name the pieces in geometry order: circle (spot layer),
        # eye_b (iris), eye_c (cornea shell), eye_w (eyeball), face (body).
        node_names = [n.get("name") or n.get("id") for n in root.findall("c:library_visual_scenes/c:visual_scene/c:node", NS)
                      if n.find("c:instance_controller", NS) is not None]
        by_part = {"body": [], "circle": [], "cornea": []}
        for node_name, geometry in zip(node_names, root.findall("c:library_geometries/c:geometry", NS)):
            symbol = geometry.find("c:mesh/c:triangles", NS).get("material")
            image = material_image.get(symbol_material.get(symbol, symbol), "face.0.png")
            if image.startswith("circle"):
                # The spot layer tiles a small texture across the back (UVs
                # span about -2.3..2.2): raw UVs with repeat wrapping.
                key, wrap = "circle", wrap_repeat
            elif node_name.endswith("eye_c_m"):
                key, wrap = "cornea", wrap_clamp
            else:
                key, wrap = "body", wrap_clamp
            by_part[key] += expand_aligned(root, geometry, controllers, joint_names, wrap, BULBORB_FITS["big"])
        face_tex = rgba(archive, "Red Bulborb/face.0.png")
        parts = [
            (by_part["body"], face_tex),
            (by_part["circle"], rgba(archive, "Red Bulborb/circle.0.png"), PART_FLAG_REPEAT),
            (by_part["cornea"], translucent(face_tex, CORNEA_ALPHA)),
        ]
        bones = [(name, IDENTITY) for name in joint_names]
        write_pack(out_dir / "bulborb.nhm", bones, parts)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("kind", choices=["olimar", "pikmin", "bulborb"])
    parser.add_argument("paths", nargs="+", type=Path, help="source zip(s) then the output NHM file or directory")
    args = parser.parse_args()
    if args.kind == "olimar":
        build_olimar(args.paths[0], args.paths[1])
    elif args.kind == "pikmin":
        build_pikmin(args.paths[0], args.paths[1])
    else:
        build_bulborb(args.paths[0], args.paths[1], args.paths[2])


if __name__ == "__main__":
    main()
