#!/usr/bin/env python3
"""Builds the humanoid of the character sample, apps/samples/character/assets/character (issue #494).

  python3 tools/generate_character_humanoid.py <downloads>

<downloads> is a folder holding, anywhere below it, the three files listed in apps/samples/character/LICENSES.md:
the MPFB 2 Blender extension (`add-on-mpfb-v2.0.18.zip`) and the MakeHuman asset packs
`makehuman_system_assets_cc0.zip` and `skins02_cc0.zip`. Blender 4.2 or later must be on the PATH, or named by
the BLENDER environment variable (`/snap/bin/blender` here); numpy and Pillow are needed too.

The script installs MPFB into a throwaway Blender user folder (the user's own Blender is not touched), unpacks
the two packs into MPFB's library there, and runs itself inside Blender (`blender -b --python <this> -- ...`),
which:

1. builds the human from HUMAN below: a young adult man, 1.80 m tall, with the `game_engine` rig (Unreal
   mannequin bone names), hair, eyebrows, eyelashes, eyes, a T-shirt with jeans, and leather shoes;
2. bakes the body's ambient occlusion into a 2048 x 2048 map, since the renderer has no ambient-occlusion pass;
3. removes the helper geometry and the skin under the clothes, joins every part into one skinned mesh, turns the
   character to face -Z (the engine's forward; Blender's exporter otherwise gives +Z), and lifts it so the soles
   rest on y = 0 with the `Root` bone at the origin;
4. adds `Stand`, a static clip (two equal keys) that lowers the arms from the rig's A pose, and exports glTF 2.0
   with tangents.

Then, outside Blender, it prepares the textures (base colour, normal, and occlusion-roughness-metalness at
2048 texels where the source has them), writes the materials into the glTF, packs everything into one binary
glTF (`humanoid.glb`, so the images are the model's sub-assets and not also textures of the project), and
writes the sidecar if there is none, its identity derived from the file's path as the course's are. Every step
is in LICENSES.md. Set KEEP_WORK=<folder> to keep the unpacked glTF, its textures and the baked occlusion.

Run from anywhere. With Blender 5.2.2 two runs wrote the same bytes; another Blender version may differ.
"""
import json
import math
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PROJECT = ROOT / "apps" / "samples" / "character"
OUT = PROJECT / "assets" / "character"
MODEL = "assets/character/humanoid.glb"

DOWNLOADS = {
    "mpfb": "add-on-mpfb-v2.0.18.zip",
    "system": "makehuman_system_assets_cc0.zip",
    "skins": "skins02_cc0.zip",
}

# MPFB's human description (its `.json` preset format). Phenotype values are MakeHuman's macro sliders, 0 to 1:
# gender 1 is male, age 0.5 is 25 years, height 0.55 gives 1.80 m; proportions towards 1 are the idealised
# ones. Asset names are MPFB's file names, found in the unpacked packs.
HUMAN = {
    "name": "Human",
    "phenotype": {"gender": 1.0, "age": 0.5, "muscle": 0.6, "weight": 0.5, "proportions": 0.7, "height": 0.55,
                  "cupsize": 0.5, "firmness": 0.5, "race": {"asian": 0.0, "caucasian": 1.0, "african": 0.0}},
    "rig": "game_engine",
    "eyes": "high-poly.mhclo", "eyebrows": "eyebrow001.mhclo", "eyelashes": "eyelashes01.mhclo",
    "tongue": "", "teeth": "", "hair": "short02.mhclo", "proxy": "", "targets": [],
    "clothes": ["male_casualsuit06.mhclo", "shoes01.mhclo"],
    "skin_mhmat": "young_caucasian_male.mhmat", "skin_material_type": "GAMEENGINE",
    "eyes_material_type": "MAKESKIN", "clothes_material_type": "GAMEENGINE",
}

# MPFB object name suffix -> the engine material it gets. The order is the submesh order of the joined mesh.
PARTS = {
    "body": "Skin", "high-poly": "Eyes", "eyebrow001": "Eyebrows", "eyelashes01": "Eyelashes",
    "short02": "Hair", "male_casualsuit06": "Clothes", "shoes01": "Shoes",
}

# The `Stand` clip turns each upper arm this far down from the A pose, about the axis that takes it straight down.
ARM_DROP_DEGREES = 40.0
AO_SIZE = 2048
AO_DISTANCE = 0.12  # metres: creases, ears, fingers and armpits, not the whole torso's shading
AO_SAMPLES = 128


def find(downloads: Path, name: str) -> Path:
    for path in downloads.rglob(name):
        return path
    sys.exit(f"{name} not found under {downloads}")


# ---------------------------------------------------------------------------------------------
# Inside Blender
# ---------------------------------------------------------------------------------------------


def blender_build(work: Path, gltf: Path):
    import bpy  # noqa: F401  (only inside Blender)
    from mathutils import Matrix, Vector
    from bl_ext.user_default.mpfb.services.humanservice import HumanService

    for o in list(bpy.data.objects):
        bpy.data.objects.remove(o)

    settings = HumanService.get_default_deserialization_settings()
    settings["subdiv_levels"] = 0
    body = HumanService.deserialize_from_dict(json.loads(json.dumps(HUMAN)), settings)
    rig = body.parent
    parts = [o for o in bpy.data.objects if o.type == 'MESH']

    def select(objects, active):
        bpy.ops.object.select_all(action='DESELECT')
        for o in objects:
            o.select_set(True)
        bpy.context.view_layer.objects.active = active

    def apply_modifier(obj, name):
        select([obj], obj)
        bpy.ops.object.modifier_apply(modifier=name)

    for o in parts:  # the phenotype's targets, baked into the mesh
        if o.data.shape_keys:
            select([o], o)
            bpy.ops.object.shape_key_remove(all=True, apply_mix=True)
    apply_modifier(body, "Hide helpers")

    # Ambient occlusion of the body against itself and the eyes, in the rest pose.
    scene = bpy.context.scene
    scene.render.engine = 'CYCLES'
    scene.cycles.device = 'CPU'
    scene.cycles.samples = AO_SAMPLES
    scene.world = scene.world or bpy.data.worlds.new("World")
    scene.world.light_settings.distance = AO_DISTANCE
    for o in parts:
        o.hide_render = o not in (body,) and not o.name.endswith("high-poly")
    image = bpy.data.images.new("SkinAO", AO_SIZE, AO_SIZE, float_buffer=False)
    material = body.material_slots[0].material
    node = material.node_tree.nodes.new('ShaderNodeTexImage')
    node.image = image
    material.node_tree.nodes.active = node
    select([body], body)
    bpy.ops.object.bake(type='AO', margin=16, use_clear=True)
    image.filepath_raw = str(work / "skin_ao.png")
    image.file_format = 'PNG'
    image.save()
    for o in parts:
        o.hide_render = False

    for modifier in [m.name for m in body.modifiers if m.type == 'MASK']:
        apply_modifier(body, modifier)

    # One material per part, named for the engine; the textures are written into the glTF afterwards.
    order = []
    for o in parts:
        suffix = o.name.split(".", 1)[1]
        name = PARTS[suffix]
        order.append((list(PARTS).index(suffix), o))
        m = bpy.data.materials.get(name) or bpy.data.materials.new(name)
        o.data.materials.clear()
        o.data.materials.append(m)
    parts = [o for _, o in sorted(order, key=lambda e: e[0])]

    # Face -Z in glTF: Blender's +Y. MPFB builds the human facing Blender's -Y.
    select([rig] + parts, rig)
    rig.rotation_euler = (0.0, 0.0, math.pi)
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)

    select(parts, parts[0])
    bpy.ops.object.join()
    mesh = parts[0]
    mesh.name = mesh.data.name = "Body"
    for group in [g for g in mesh.vertex_groups if g.name not in rig.data.bones]:
        mesh.vertex_groups.remove(group)
    rig.name = rig.data.name = "Armature"

    # Soles on the ground: everything but the Root bone moves up by the depth of the lowest vertex.
    lift = -min((mesh.matrix_world @ v.co).z for v in mesh.data.vertices)
    mesh.data.transform(Matrix.Translation((0.0, 0.0, lift)))
    select([rig], rig)
    bpy.ops.object.mode_set(mode='EDIT')
    for bone in rig.data.edit_bones:
        if bone.name != "Root":
            bone.head.z += lift
            bone.tail.z += lift
    bpy.ops.object.mode_set(mode='OBJECT')

    # Stand: the upper arms lowered towards the body.
    rig.animation_data_create()
    action = bpy.data.actions.new("Stand")
    rig.animation_data.action = action
    bpy.ops.object.mode_set(mode='POSE')
    for side in ("l", "r"):
        bone = rig.pose.bones[f"upperarm_{side}"]
        head = bone.head.copy()
        direction = (bone.tail - bone.head).normalized()
        axis = direction.cross(Vector((0.0, 0.0, -1.0))).normalized()
        turn = Matrix.Translation(head) @ Matrix.Rotation(math.radians(ARM_DROP_DEGREES), 4, axis) @ Matrix.Translation(-head)
        bone.matrix = turn @ bone.matrix
        bpy.context.view_layer.update()
    for bone in rig.pose.bones:
        for frame in (1, 2):
            bone.keyframe_insert("location", frame=frame)
            bone.keyframe_insert("rotation_quaternion", frame=frame)
            bone.keyframe_insert("scale", frame=frame)
    bpy.ops.object.mode_set(mode='OBJECT')
    scene.frame_start, scene.frame_end = 1, 2

    report = {
        "lift": lift,
        "bones": [{"name": b.name, "parent": b.parent.name if b.parent else None,
                   "head": [round(c, 4) for c in b.head_local], "tail": [round(c, 4) for c in b.tail_local]}
                  for b in rig.data.bones],
        "vertices": len(mesh.data.vertices), "triangles": sum(len(p.vertices) - 2 for p in mesh.data.polygons),
    }
    (work / "build.json").write_text(json.dumps(report, indent=1))

    bpy.ops.export_scene.gltf(
        filepath=str(gltf), export_format='GLTF_SEPARATE', export_image_format='NONE', export_yup=True,
        export_apply=False, export_texcoords=True, export_normals=True, export_tangents=True,
        export_skins=True, export_all_influences=False, export_def_bones=False, export_morph=False,
        export_animations=True, export_animation_mode='ACTIONS', export_force_sampling=True,
        export_optimize_animation_size=False, export_cameras=False, export_lights=False, export_extras=False,
        export_materials='EXPORT')


def blender_install(extension: Path):
    import bpy
    bpy.ops.extensions.package_install_files(filepath=str(extension), repo="user_default", enable_on_install=True)
    bpy.ops.wm.save_userpref()


# ---------------------------------------------------------------------------------------------
# Textures and materials, outside Blender
# ---------------------------------------------------------------------------------------------


def blur(a, radius):
    """Three box passes per axis, close to a Gaussian of standard deviation `radius` texels."""
    import numpy as np
    width = max(1, int(round(math.sqrt(12 * radius * radius / 3 + 1))) | 1)
    half = width // 2
    a = np.asarray(a, np.float64)
    for axis in (0, 1):
        for _ in range(3):
            padded = np.pad(a, [(half + 1, half) if k == axis else (0, 0) for k in range(a.ndim)], mode="edge")
            summed = np.cumsum(padded, axis=axis)
            a = (np.take(summed, range(width, summed.shape[axis]), axis=axis)
                 - np.take(summed, range(0, summed.shape[axis] - width), axis=axis)) / width
    return a.astype(np.float32)


# The T-shirt's prints (the MakeHuman logo on the chest, back and sleeve), as boxes of the 2048-texel map:
# x0, y0, x1, y1 with y down. Inside them, every texel that is not plain white cotton is a print.
PRINTS = [(380, 220, 620, 345), (880, 200, 1310, 430), (1480, 290, 1570, 380)]


def remove_prints(rgb):
    """Fills the prints with the cotton around them: a normalised blur of the texels outside the print mask."""
    import numpy as np
    lum = rgb.mean(axis=2)
    saturation = rgb.max(axis=2) - rgb.min(axis=2)
    box = np.zeros(lum.shape, bool)
    for x0, y0, x1, y1 in PRINTS:
        box[y0:y1, x0:x1] = True
    mask = box & ((saturation > 0.06) | (lum < 0.78))
    mask = blur(mask.astype(np.float32), 3) > 0.02  # grown over the print's antialiased edge
    keep = (~mask).astype(np.float32)
    filled = rgb.copy()
    for radius in (4, 12, 32):  # small holes from near texels, the logo's middle from farther ones
        weight = blur(keep, radius)
        colour = blur(rgb * keep[..., None], radius) / np.maximum(weight, 1e-4)[..., None]
        holes = mask & (weight > 0.05)
        filled[holes] = colour[holes]
        rgb = np.where(holes[..., None], filled, rgb)
        keep = np.maximum(keep, holes.astype(np.float32))
        mask = mask & ~holes
    return rgb


def textures(library: Path, work: Path):
    import numpy as np
    from PIL import Image

    out = work / "textures"
    out.mkdir(parents=True, exist_ok=True)

    def load(relative, mode="RGB"):
        return Image.open(library / relative).convert(mode)

    def jpeg(image, name):
        image.save(out / name, quality=90, subsampling=0, optimize=True)

    def orm(occlusion, roughness, size):
        o = np.full((size, size), 255, np.uint8) if occlusion is None else occlusion
        r = np.broadcast_to(np.asarray(np.clip(roughness * 255 + 0.5, 0, 255), np.uint8), (size, size))
        return Image.fromarray(np.dstack([o, r, np.zeros((size, size), np.uint8)]))


    # Skin. Base colour: MakeHuman's young light-skinned male. Normal: Mindfront's Aksel skin (same UV layout) on the
    # body; its face carries injury make-up (a swollen eye, cuts), so the face island gets a fine normal from the
    # base colour's own detail instead, and so does the neck at the top of the torso island, whose tendons Aksel
    # carves deeper than this body's shape has them. Roughness: Aksel's specular detail around a skin value of 0.5.
    base = load("skins/young_caucasian_male/young_lightskinned_male_diffuse.png")
    jpeg(base, "Skin_BaseColor.jpg")
    size = base.size[0]
    v, u = np.mgrid[0:size, 0:size] / size
    face = ((u >= 0.63) & (v >= 0.17) & (v <= 0.85)) | ((u >= 0.2) & (u <= 0.56) & (v <= 0.22))
    face = face.astype(np.float32)
    face = blur(face * 255, 12) / 255
    aksel = np.asarray(load("skins/mindfront_aksel_skin/Aksel_Skin_NRM.png"), np.float32) / 127.5 - 1
    lum = np.asarray(base.convert("L"), np.float32) / 255
    height = lum - blur(lum, 6)
    gx = np.roll(height, -1, 1) - np.roll(height, 1, 1)
    gy = np.roll(height, -1, 0) - np.roll(height, 1, 0)
    strength = 2.5
    # Image rows run down while V runs up, so +V is -row: the green channel is +dh/dv = -gy.
    detail = np.dstack([-gx * strength, gy * strength, np.ones_like(gx)])
    detail /= np.linalg.norm(detail, axis=2, keepdims=True)
    normal = aksel * (1 - face[..., None]) + detail * face[..., None]
    normal /= np.linalg.norm(normal, axis=2, keepdims=True)
    jpeg(Image.fromarray(np.uint8(np.clip(normal * 127.5 + 128, 0, 255))), "Skin_Normal.jpg")
    spec = np.asarray(load("skins/mindfront_aksel_skin/Aksel_Skin_SPEC.png", "L"), np.float32) / 255
    roughness = np.clip(0.5 - 0.8 * (spec - blur(spec, 24)), 0.38, 0.65) * (1 - face) + 0.48 * face
    ao = np.asarray(Image.open(work / "skin_ao.png").convert("L"), np.uint8)
    if ao.shape[0] != size:
        ao = np.asarray(Image.fromarray(ao).resize((size, size), Image.LANCZOS))
    jpeg(orm(ao, roughness, size), "Skin_ORM.jpg")

    # Clothes: the casual suit's own colour, normal and occlusion maps; cotton and denim are rough.
    suit = "clothes/male_casualsuit06/male_casualsuit06"
    shirt = remove_prints(np.asarray(load(suit + "_diffuse.png"), np.float32) / 255)
    # The T-shirt (the map's upper 42 %) averages a linear albedo of 0.61, which under the course's sun and exposure
    # (tuned for concrete) lands at the top of the tone curve and loses its folds; 0.8 of it keeps them.
    top = int(0.42 * shirt.shape[0])
    linear = np.where(shirt[:top] <= 0.04045, shirt[:top] / 12.92, ((shirt[:top] + 0.055) / 1.055) ** 2.4) * 0.8
    shirt[:top] = np.where(linear <= 0.0031308, linear * 12.92, 1.055 * linear ** (1 / 2.4) - 0.055)
    jpeg(Image.fromarray(np.uint8(np.clip(shirt * 255 + 0.5, 0, 255))), "Clothes_BaseColor.jpg")
    jpeg(load(suit + "_normal.png"), "Clothes_Normal.jpg")
    occlusion = load(suit + "_ao.png", "L")
    jpeg(orm(np.asarray(occlusion), 0.88, occlusion.size[0]), "Clothes_ORM.jpg")

    # Shoes: leather, 1024 texels as the source; no occlusion map in the source.
    jpeg(load("clothes/shoes01/shoes01_diffuse.png"), "Shoes_BaseColor.jpg")
    jpeg(load("clothes/shoes01/shoes01_normal.png"), "Shoes_Normal.jpg")


    # Hair cards, eyebrows and eyelashes keep their alpha: palettised PNG, which keeps it per palette entry.
    def cards(relative, name):
        image = load(relative, "RGBA")
        image.quantize(256, method=Image.Quantize.FASTOCTREE, dither=Image.Dither.NONE).save(out / name, optimize=True)

    cards("hair/short02/short02_diffuse.png", "Hair_BaseColor.png")
    jpeg(load("hair/short02/short02_normal.png"), "Hair_Normal.jpg")
    cards("eyebrows/eyebrow001/eyebrow001.png", "Eyebrows_BaseColor.png")
    cards("eyelashes/eyelashes01/eyelashes01.png", "Eyelashes_BaseColor.png")
    # Eyes: the iris and sclera map. Its alpha is zero only where the cornea's shell maps, which MPFB draws as
    # clear glass; masked out here, so the iris shows.
    cards("eyes/materials/brown_eye.png", "Eyes_BaseColor.png")


# name: (base colour, normal, ORM, normal scale, roughness, alpha mode, cutoff, double sided)
MATERIALS = {
    "Skin": ("Skin_BaseColor.jpg", "Skin_Normal.jpg", "Skin_ORM.jpg", 0.5, 1.0, "OPAQUE", None, False),
    "Eyes": ("Eyes_BaseColor.png", None, None, 1.0, 0.08, "MASK", 0.5, False),
    "Eyebrows": ("Eyebrows_BaseColor.png", None, None, 1.0, 0.7, "MASK", 0.35, True),
    "Eyelashes": ("Eyelashes_BaseColor.png", None, None, 1.0, 0.7, "MASK", 0.35, True),
    "Hair": ("Hair_BaseColor.png", "Hair_Normal.jpg", None, 1.0, 0.62, "MASK", 0.35, True),
    "Clothes": ("Clothes_BaseColor.jpg", "Clothes_Normal.jpg", "Clothes_ORM.jpg", 1.0, 1.0, "OPAQUE", None, False),
    "Shoes": ("Shoes_BaseColor.jpg", "Shoes_Normal.jpg", None, 1.0, 0.55, "OPAQUE", None, False),
}


def write_materials(gltf: Path):
    document = json.loads(gltf.read_text())
    images, textures = [], []

    def texture(name):
        images.append({"name": Path(name).stem, "uri": f"textures/{name}"})
        textures.append({"sampler": 0, "source": len(images) - 1})
        return {"index": len(textures) - 1}

    materials = []
    for material in document["materials"]:
        base, normal, orm, normal_scale, roughness, alpha, cutoff, double = MATERIALS[material["name"]]
        pbr = {"baseColorTexture": texture(base), "metallicFactor": 0.0, "roughnessFactor": roughness}
        out = {"name": material["name"], "pbrMetallicRoughness": pbr}
        if orm:
            pbr["metallicFactor"] = 1.0
            pbr["metallicRoughnessTexture"] = texture(orm)
            out["occlusionTexture"] = dict(pbr["metallicRoughnessTexture"])
        if normal:
            out["normalTexture"] = dict(texture(normal), scale=normal_scale)
        if alpha != "OPAQUE":
            out["alphaMode"] = alpha
            out["alphaCutoff"] = cutoff
        if double:
            out["doubleSided"] = True
        materials.append(out)
    document["materials"] = materials
    document["images"] = images
    document["textures"] = textures
    document["samplers"] = [{"magFilter": 9729, "minFilter": 9987, "wrapS": 10497, "wrapT": 10497}]
    gltf.write_text(json.dumps(document, indent=1) + "\n")


def pack_glb(gltf: Path, glb: Path):
    """One binary glTF: the exporter's buffer, then each image, every chunk padded to four bytes. Embedded,
    the images are the model's own sub-assets only, not also textures of their own in the project."""
    document = json.loads(gltf.read_text())
    binary = bytearray((gltf.parent / document["buffers"][0]["uri"]).read_bytes())
    for image in document["images"]:
        binary += b"\0" * (-len(binary) % 4)
        data = (gltf.parent / image.pop("uri")).read_bytes()
        document["bufferViews"].append({"buffer": 0, "byteOffset": len(binary), "byteLength": len(data)})
        binary += data
        image["bufferView"] = len(document["bufferViews"]) - 1
        image["mimeType"] = "image/png" if data[:4] == b"\x89PNG" else "image/jpeg"
    binary += b"\0" * (-len(binary) % 4)
    document["buffers"] = [{"byteLength": len(binary)}]
    text = json.dumps(document, separators=(",", ":")).encode()
    text += b" " * (-len(text) % 4)
    chunks = struct.pack("<II", len(text), 0x4E4F534A) + text + struct.pack("<II", len(binary), 0x004E4942) + binary
    glb.write_bytes(struct.pack("<III", 0x46546C67, 2, 12 + len(chunks)) + chunks)


def write_sidecar():
    sys.path.insert(0, str(ROOT / "tools"))
    import generate_character_level as level  # noqa: E402
    path = PROJECT / (MODEL + ".meta")
    if not path.exists():
        path.write_text(json.dumps({"settings": {}, "type": "Model", "uuid": level.asset_uuid(MODEL), "version": 2},
                                   indent=2, sort_keys=True) + "\n")


# ---------------------------------------------------------------------------------------------
# Driver
# ---------------------------------------------------------------------------------------------


def run_blender(blender, env, *args, factory=False):
    command = [blender, "-b"] + (["--factory-startup"] if factory else []) + ["--python", __file__, "--", *args]
    result = subprocess.run(command, env=env, capture_output=True, text=True)
    if result.returncode != 0 or "Traceback" in result.stdout + result.stderr:
        sys.exit(result.stdout[-4000:] + result.stderr[-4000:])


def main():
    if "--" in sys.argv:  # inside Blender
        args = sys.argv[sys.argv.index("--") + 1:]
        if args[0] == "install":
            blender_install(Path(args[1]))
        else:
            blender_build(Path(args[1]), Path(args[2]))
        return
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    downloads = Path(sys.argv[1]).resolve()
    blender = os.environ.get("BLENDER") or shutil.which("blender") or "/snap/bin/blender"
    with tempfile.TemporaryDirectory(prefix="sonnet-humanoid-") as temporary:
        temporary = Path(temporary)
        env = dict(os.environ, BLENDER_USER_RESOURCES=str(temporary / "blender"))
        run_blender(blender, env, "install", str(find(downloads, DOWNLOADS["mpfb"])), factory=True)
        library = temporary / "blender" / "extensions" / ".user" / "user_default" / "mpfb" / "data"
        for pack in ("system", "skins"):
            with zipfile.ZipFile(find(downloads, DOWNLOADS[pack])) as archive:
                archive.extractall(library)
        work = temporary / "work"
        work.mkdir()
        OUT.mkdir(parents=True, exist_ok=True)
        gltf = work / "humanoid.gltf"
        run_blender(blender, env, "build", str(work), str(gltf))
        textures(library, work)
        write_materials(gltf)
        pack_glb(gltf, PROJECT / MODEL)
        write_sidecar()
        if "KEEP_WORK" in os.environ:  # the textures and the glTF before packing, for inspection
            shutil.copytree(work, os.environ["KEEP_WORK"], dirs_exist_ok=True)
        report = json.loads((work / "build.json").read_text())
        for bone in report["bones"]:
            print(f"{bone['name']:<12} parent {bone['parent']}")
        print(f"humanoid.glb: {report['vertices']} vertices, {report['triangles']} triangles, "
              f"{len(report['bones'])} bones, lifted {report['lift'] * 1000:.1f} mm")


if __name__ == "__main__":
    main()
