#!/usr/bin/env python3
"""Generates the obstacle course of the character sample, apps/samples/character (issue #490).

Two steps:

  python3 tools/generate_character_level.py prepare <downloads>   # textures and sky from the CC0 downloads
  python3 tools/generate_character_level.py level                 # meshes, materials, sidecars and the scene

`prepare` reads the source files listed in apps/samples/character/LICENSES.md from <downloads>, a folder
holding the extracted ambientCG archives (each `<AssetId>_1K-JPG.zip`, anywhere below it) and the Poly Haven
HDRI. It re-encodes the colour and normal maps, packs occlusion, roughness and metalness into one map the way
glTF reads them (red, green, blue), and takes the sun out of the sky: the disc's energy above the sky around
it becomes the scene's directional light, printed as SUN_DIRECTION and SUN_IRRADIANCE for the constants below.
Its outputs are committed, so only someone replacing a source runs it. Needs numpy and Pillow.

`level` needs nothing but the standard library and writes the rest. The identities are derived from fixed
names with the engine's own `Uuid::derive`, so the scene refers to the meshes and textures without the
editor writing their sidecars first; an existing sidecar is kept, since the editor completes it on open.

Run from anywhere; everything is deterministic and re-running overwrites the files identically.
"""
import json
import math
import sys
import uuid
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import generate_sample_assets as basic  # noqa: E402
import scene_json  # noqa: E402

PROJECT = Path(__file__).resolve().parent.parent / "apps" / "samples" / "character"
LEVEL = PROJECT / "assets" / "level"
TEXTURES = LEVEL / "textures"
SKY_SOURCE = "kloofendal_48d_partly_cloudy_2k.hdr"
SKY = LEVEL / "kloofendal_48d_partly_cloudy_2k.hdr"

# Every texture is 1024 texels across and covers `tile` metres of surface, so the course reads at one density,
# 512 texels per metre. ambientCG's colour maps are darker than the surfaces they were shot from (a mean linear
# albedo of 0.08 to 0.13 for concrete, which measures 0.25 to 0.35), so `prepare` scales each map, in linear
# light, to the mean luminance `albedo` gives: plain concrete, weathered timber and a red primer.
MATERIALS = {
    "concrete_floor": {"source": "Concrete032", "tile": (2.0, 2.0), "albedo": 0.28},
    "concrete_wall": {"source": "Concrete031", "tile": (2.0, 2.0), "albedo": 0.30},
    "painted_metal": {"source": "PaintedMetal004", "tile": (2.0, 2.0), "albedo": 0.16},
    "worn_planks": {"source": "Planks037A", "tile": (2.0, 2.0), "albedo": 0.10},
}

# From `prepare`: towards the sun in the sky file's convention (ibl.slang's equirectUv), and the irradiance
# its disc delivered on a surface facing it, in the map's units, which the Environment uses at intensity 1.
SUN_DIRECTION = (0.5435, 0.7417, 0.3931)
SUN_IRRADIANCE = (4.50, 4.54, 4.14)

NIL = "00000000-0000-0000-0000-000000000000"


# ---------------------------------------------------------------------------------------------
# Identities
# ---------------------------------------------------------------------------------------------


def derive(parent: str, name: str) -> str:
    """core::Uuid::derive: two FNV-1a streams over the parent's bytes, a zero and the name."""
    data = uuid.UUID(parent).bytes + b"\0" + name.encode()

    def fnv(seed):
        h = seed
        for byte in data:
            h = ((h ^ byte) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
        return h

    words = (fnv(0xCBF29CE484222325), fnv(0x84222325CBF29CE4))
    raw = bytearray(16)
    for i in range(8):
        raw[i] = (words[0] >> (8 * i)) & 0xFF
        raw[i + 8] = (words[1] >> (8 * i)) & 0xFF
    raw[6] = (raw[6] & 0x0F) | 0x80
    raw[8] = (raw[8] & 0x3F) | 0x80
    return str(uuid.UUID(bytes=bytes(raw)))


def asset_uuid(relative: str) -> str:
    """The identity of a file of the project, from its path."""
    return derive(NIL, f"samples/character/{relative}")


def texture_path(source: str, kind: str) -> str:
    return f"assets/level/textures/{source}_{kind}.jpg"


# ---------------------------------------------------------------------------------------------
# prepare: textures and sky
# ---------------------------------------------------------------------------------------------


def read_hdr(path: Path):
    import numpy as np

    data = path.read_bytes()
    pos = 0
    while True:
        end = data.index(b"\n", pos)
        line = data[pos:end]
        pos = end + 1
        if line.startswith(b"-Y"):
            _, height, _, width = line.split()
            height, width = int(height), int(width)
            break
    rgbe = np.zeros((height, width, 4), np.uint8)
    for y in range(height):
        if data[pos] != 2 or data[pos + 1] != 2:
            raise ValueError(f"{path}: only run-length encoded scanlines are read")
        pos += 4
        for c in range(4):
            x = 0
            while x < width:
                count = data[pos]
                pos += 1
                if count > 128:
                    count -= 128
                    rgbe[y, x:x + count, c] = data[pos]
                    pos += 1
                else:
                    rgbe[y, x:x + count, c] = np.frombuffer(data[pos:pos + count], np.uint8)
                    pos += count
                x += count
    exponent = rgbe[..., 3].astype(np.int32)
    scale = np.where(exponent > 0, np.ldexp(1.0, exponent - 136), 0.0)
    return rgbe[..., :3].astype(np.float64) * scale[..., None]


def write_hdr(path: Path, image):
    import numpy as np

    height, width, _ = image.shape
    brightest = image.max(axis=2)
    mantissa, exponent = np.frexp(brightest)
    scale = np.where(brightest > 1e-32, mantissa * 256.0 / np.maximum(brightest, 1e-32), 0.0)
    rgbe = np.zeros((height, width, 4), np.uint8)
    rgbe[..., :3] = np.clip(image * scale[..., None], 0, 255).astype(np.uint8)
    rgbe[..., 3] = np.where(brightest > 1e-32, exponent + 128, 0).astype(np.uint8)
    out = bytearray(f"#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y {height} +X {width}\n".encode())
    for y in range(height):
        out += bytes((2, 2, width >> 8, width & 0xFF))
        for c in range(4):
            row = rgbe[y, :, c].tobytes()
            x = 0
            while x < width:
                run = 1
                while x + run < width and run < 127 and row[x + run] == row[x]:
                    run += 1
                if run >= 4:
                    out += bytes((128 + run, row[x]))
                    x += run
                    continue
                start = x
                while x < width and x - start < 128:
                    if x + 3 < width and row[x] == row[x + 1] == row[x + 2] == row[x + 3]:
                        break
                    x += 1
                out += bytes((x - start,)) + row[start:x]
    path.write_bytes(bytes(out))


def directions(height, width):
    """The direction of each texel's centre, inverting ibl.slang's equirectUv."""
    import numpy as np

    u = (np.arange(width) + 0.5) / width
    v = (np.arange(height) + 0.5) / height
    phi = (u - 0.5) * 2.0 * math.pi
    elevation = (0.5 - v) * math.pi
    cos_e = np.cos(elevation)[:, None]
    return np.stack([cos_e * np.cos(phi)[None, :], np.sin(elevation)[:, None].repeat(width, 1),
                     cos_e * np.sin(phi)[None, :]], axis=2), cos_e


def prepare_sky(source: Path):
    """Takes the sun's disc out of the map: its texels drop to the sky around it, and what they lose,
    integrated over their solid angle, is the irradiance the directional light gives instead. Left in, the
    disc would light every surface a second time through the irradiance and specular cubes."""
    import numpy as np

    image = read_hdr(source)
    height, width, _ = image.shape
    luminance = image @ np.array([0.2126, 0.7152, 0.0722])
    y, x = np.unravel_index(np.argmax(luminance), luminance.shape)
    dirs, cos_e = directions(height, width)
    peak = dirs[y, x]
    angle = np.degrees(np.arccos(np.clip(dirs @ peak, -1.0, 1.0)))
    ring = (angle > 4.0) & (angle < 6.0)
    sky = np.median(image[ring], axis=0)
    disc = (angle < 4.0) & (luminance > 4.0 * float(sky @ np.array([0.2126, 0.7152, 0.0722])))
    solid_angle = (2.0 * math.pi / width) * (math.pi / height) * cos_e
    weights = np.broadcast_to(solid_angle, luminance.shape)[disc]
    excess = image[disc] - sky
    irradiance = (excess * weights[:, None]).sum(axis=0)
    centre = (dirs[disc] * (luminance[disc] * weights)[:, None]).sum(axis=0)
    centre /= np.linalg.norm(centre)
    image[disc] = sky
    write_hdr(SKY, image)
    print(f"sky: {disc.sum()} texels of sun removed, sky around it {np.round(sky, 3).tolist()}")
    print(f"SUN_DIRECTION = ({centre[0]:.4f}, {centre[1]:.4f}, {centre[2]:.4f})  "
          f"elevation {math.degrees(math.asin(centre[1])):.1f} deg")
    print(f"SUN_IRRADIANCE = ({irradiance[0]:.2f}, {irradiance[1]:.2f}, {irradiance[2]:.2f})")


def find(downloads: Path, name: str) -> Path:
    found = sorted(downloads.rglob(name))
    if not found:
        raise FileNotFoundError(f"{name} is not under {downloads}")
    return found[0]


def prepare_textures(downloads: Path):
    """Colour and normal maps re-encoded at quality 90 (the archives' are near lossless and three times the
    size), and occlusion, roughness and metalness packed into one ORM map: white occlusion where the material
    has none, black metalness likewise. Normal and ORM maps keep full chroma, since every channel is data."""
    import numpy as np
    from PIL import Image

    TEXTURES.mkdir(parents=True, exist_ok=True)
    for spec in MATERIALS.values():
        source = spec["source"]
        prefix = f"{source}_1K-JPG"
        color = Image.open(find(downloads, f"{prefix}_Color.jpg")).convert("RGB")
        encoded = np.asarray(color, np.float64) / 255.0
        linear = np.where(encoded <= 0.04045, encoded / 12.92, ((encoded + 0.055) / 1.055) ** 2.4)
        mean = float(linear.reshape(-1, 3).mean(axis=0) @ np.array([0.2126, 0.7152, 0.0722]))
        linear = np.clip(linear * (spec["albedo"] / mean), 0.0, 1.0)
        clipped = float((linear >= 1.0).any(axis=2).mean())
        encoded = np.where(linear <= 0.0031308, linear * 12.92, 1.055 * linear ** (1 / 2.4) - 0.055)
        color = Image.fromarray(np.round(encoded * 255.0).astype(np.uint8), "RGB")
        color.save(PROJECT / texture_path(source, "Color"), quality=90, optimize=True)
        normal = Image.open(find(downloads, f"{prefix}_NormalGL.jpg")).convert("RGB")
        normal.save(PROJECT / texture_path(source, "NormalGL"), quality=90, subsampling=0, optimize=True)
        size = color.size

        def channel(kind, fill):
            try:
                return Image.open(find(downloads, f"{prefix}_{kind}.jpg")).convert("L").resize(size)
            except FileNotFoundError:
                return Image.new("L", size, fill)

        orm = Image.merge("RGB", (channel("AmbientOcclusion", 255), channel("Roughness", 128), channel("Metalness", 0)))
        orm.save(PROJECT / texture_path(source, "ORM"), quality=90, subsampling=0, optimize=True)
        print(f"{source}: {size[0]}x{size[1]}, mean albedo {mean:.3f} scaled to {spec['albedo']}, "
              f"{clipped:.2%} of texels clipped")


# ---------------------------------------------------------------------------------------------
# Meshes
# ---------------------------------------------------------------------------------------------


class Mesh:
    def __init__(self):
        self.p, self.n, self.t, self.i = [], [], [], []

    def quad(self, corners, normal, uvs):
        base = len(self.p)
        for p, t in zip(corners, uvs):
            self.p.append(list(p))
            self.n.append(list(normal))
            self.t.append(list(t))
        self.i += [base, base + 1, base + 2, base, base + 2, base + 3]

    def tri(self, corners, normal, uvs):
        base = len(self.p)
        for p, t in zip(corners, uvs):
            self.p.append(list(p))
            self.n.append(list(normal))
            self.t.append(list(t))
        self.i += [base, base + 1, base + 2]


def planar_uv(world, normal):
    """UVs in metres from the world position, projected along the face's axis so neighbouring pieces carry
    the texture across their joints; v runs down the world so the maps stand upright."""
    x, y, z = world
    ax = max(range(3), key=lambda k: abs(normal[k]))
    sign = 1.0 if normal[ax] > 0 else -1.0
    if ax == 1:
        return (x, z * sign)
    if ax == 0:
        return (-z * sign, -y)
    return (x * sign, -y)


def box_mesh(size, origin):
    """A box centred on its entity, textured from where it stands in the world (`origin`), faces counter-clockwise."""
    hx, hy, hz = (s / 2 for s in size)
    m = Mesh()
    faces = [
        ((1, 0, 0), [(hx, -hy, hz), (hx, -hy, -hz), (hx, hy, -hz), (hx, hy, hz)]),
        ((-1, 0, 0), [(-hx, -hy, -hz), (-hx, -hy, hz), (-hx, hy, hz), (-hx, hy, -hz)]),
        ((0, 1, 0), [(-hx, hy, hz), (hx, hy, hz), (hx, hy, -hz), (-hx, hy, -hz)]),
        ((0, -1, 0), [(-hx, -hy, -hz), (hx, -hy, -hz), (hx, -hy, hz), (-hx, -hy, hz)]),
        ((0, 0, 1), [(-hx, -hy, hz), (hx, -hy, hz), (hx, hy, hz), (-hx, hy, hz)]),
        ((0, 0, -1), [(hx, -hy, -hz), (-hx, -hy, -hz), (-hx, hy, -hz), (hx, hy, -hz)]),
    ]
    for normal, corners in faces:
        uvs = [planar_uv([c[k] + origin[k] for k in range(3)], normal) for c in corners]
        m.quad(corners, normal, uvs)
    return m


def crate_mesh(size):
    """A box with the whole of each face mapped from its own corner, as a crate's boards are cut to it."""
    hx, hy, hz = (s / 2 for s in size)
    m = Mesh()
    faces = [
        ((1, 0, 0), [(hx, -hy, hz), (hx, -hy, -hz), (hx, hy, -hz), (hx, hy, hz)], (size[2], size[1])),
        ((-1, 0, 0), [(-hx, -hy, -hz), (-hx, -hy, hz), (-hx, hy, hz), (-hx, hy, -hz)], (size[2], size[1])),
        ((0, 1, 0), [(-hx, hy, hz), (hx, hy, hz), (hx, hy, -hz), (-hx, hy, -hz)], (size[0], size[2])),
        ((0, -1, 0), [(-hx, -hy, -hz), (hx, -hy, -hz), (hx, -hy, hz), (-hx, -hy, hz)], (size[0], size[2])),
        ((0, 0, 1), [(-hx, -hy, hz), (hx, -hy, hz), (hx, hy, hz), (-hx, hy, hz)], (size[0], size[1])),
        ((0, 0, -1), [(hx, -hy, -hz), (-hx, -hy, -hz), (-hx, hy, -hz), (hx, hy, -hz)], (size[0], size[1])),
    ]
    for normal, corners, (w, h) in faces:
        m.quad(corners, normal, [(0, h), (w, h), (w, 0), (0, 0)])
    return m


def wedge_mesh(width, rise, run, origin):
    """A ramp centred on its bounding box, rising by `rise` over `run` towards -Z. The slope's v runs along
    the slope in metres, so the texture is not stretched on it."""
    hx, hy, hz = width / 2, rise / 2, run / 2
    m = Mesh()
    slope = math.hypot(rise, run)
    n = (0.0, run / slope, rise / slope)
    low = [(-hx, -hy, hz), (hx, -hy, hz)]
    high = [(hx, hy, -hz), (-hx, hy, -hz)]
    ox, oy, oz = origin
    m.quad([low[0], low[1], high[0], high[1]], n,
           [(ox - hx, oz), (ox + hx, oz), (ox + hx, oz - slope), (ox - hx, oz - slope)])
    back = [(hx, -hy, -hz), (-hx, -hy, -hz), (-hx, hy, -hz), (hx, hy, -hz)]
    m.quad(back, (0, 0, -1), [planar_uv((c[0] + ox, c[1] + oy, c[2] + oz), (0, 0, -1)) for c in back])
    bottom = [(-hx, -hy, -hz), (hx, -hy, -hz), (hx, -hy, hz), (-hx, -hy, hz)]
    m.quad(bottom, (0, -1, 0), [planar_uv((c[0] + ox, c[1] + oy, c[2] + oz), (0, -1, 0)) for c in bottom])
    for sx, normal in ((hx, (1, 0, 0)), (-hx, (-1, 0, 0))):
        corners = [(sx, -hy, hz), (sx, -hy, -hz), (sx, hy, -hz)]
        if sx < 0:
            corners = [corners[1], corners[0], corners[2]]
        m.tri(corners, normal, [planar_uv((c[0] + ox, c[1] + oy, c[2] + oz), normal) for c in corners])
    return m


def capsule_mesh(radius, height, slices=32, stacks=24):
    """A capsule centred on its entity along Y, as CapsuleCollider is, height counting both caps."""
    half = height / 2 - radius
    m = Mesh()
    rows = []
    for j in range(stacks + 1):
        theta = math.pi * j / stacks  # from the top pole
        y = radius * math.cos(theta) + (half if j <= stacks // 2 else -half)
        r = radius * math.sin(theta)
        ny = math.cos(theta)
        rows.append((y, r, ny, math.sin(theta)))
    # The equator is duplicated across the straight part.
    rows.insert(stacks // 2 + 1, (-half, radius, 0.0, 1.0))
    base = 0
    for j, (y, r, ny, nr) in enumerate(rows):
        for s in range(slices + 1):
            phi = 2 * math.pi * s / slices
            m.p.append([r * math.cos(phi), y, -r * math.sin(phi)])
            m.n.append([nr * math.cos(phi), ny, -nr * math.sin(phi)])
            m.t.append([s / slices, j / (len(rows) - 1)])
    for j in range(len(rows) - 1):
        for s in range(slices):
            a = base + j * (slices + 1) + s
            b = a + slices + 1
            # The poles' rows are one point repeated; their degenerate halves are left out.
            if j + 1 < len(rows) - 1:
                m.i += [a, b, b + 1]
            if j > 0:
                m.i += [a, b + 1, a + 1]
    return m


# ---------------------------------------------------------------------------------------------
# The course
# ---------------------------------------------------------------------------------------------

DECK_HEIGHT = 1.5
DECK = ((-6.0, 0.0, -3.0), (2.0, DECK_HEIGHT, 1.0))


def course():
    """The pieces as (name, kind, bounds or spec, material). Axis-aligned boxes are given by their corners.

    South (+Z) is where the player starts; the course runs north across the pit to the far floor. Real-world
    scale: a body is 1.8 m tall, doors are 2 m, walls 0.3 m thick."""
    pieces = []

    def box(name, lo, hi, material="concrete_wall"):
        pieces.append((name, "box", (lo, hi), material))

    # The two floors and the pit between them; the slabs are deep so the pit has walls.
    box("Floor south", (-14.0, -3.0, -4.0), (14.0, 0.0, 14.0), "concrete_floor")
    box("Floor north", (-14.0, -3.0, -24.0), (14.0, 0.0, -10.0), "concrete_floor")

    # 0.2 m steps: four of them, 0.3 m treads, onto a landing 0.8 m up whose north edge drops back down.
    for i in range(4):
        box(f"Step 0.2 m {i + 1}", (-13.0, 0.2 * i, 2.0), (-9.5, 0.2 * (i + 1), 8.0 - 0.3 * i))

    # The deck, 1.5 m up, reached by three 0.5 m steps, a 20 degree ramp; its 55 degree side is too steep
    # to walk and its north edge is the ledge, a 1.5 m drop.
    box("Deck", *DECK)
    for i in range(3):
        top = 0.5 * (i + 1)
        box(f"Step 0.5 m {i + 1}", (-8.0 + 0.6 * i, 0.5 * i, -3.0), (-6.0, top, 1.0))
    run20 = DECK_HEIGHT / math.tan(math.radians(20.0))
    pieces.append(("Ramp 20 deg", "wedge", {"x": (-3.0, 0.0), "rise": DECK_HEIGHT, "run": run20, "z": 1.0, "slope": 20.0},
                   "concrete_wall"))
    run55 = DECK_HEIGHT / math.tan(math.radians(55.0))
    pieces.append(("Ramp 55 deg", "wedge", {"x": (2.0, 5.0), "rise": DECK_HEIGHT, "run": run55, "z": -3.0, "slope": 55.0,
                                            "east": True}, "concrete_wall"))

    # Walls and corners, the narrow passage (0.8 m between its walls) and the low ceiling (2.0 m clear).
    box("Wall west", (6.0, 0.0, -3.0), (6.3, 3.0, 9.0))
    box("Wall north", (6.3, 0.0, -3.0), (13.5, 3.0, -2.7))
    box("Passage wall west", (7.8, 0.0, 1.5), (8.1, 2.6, 6.5))
    box("Passage wall east", (8.9, 0.0, 1.5), (9.2, 2.3, 6.5))
    box("Tunnel wall east", (12.0, 0.0, 1.5), (12.3, 2.3, 6.5))
    box("Low ceiling", (8.9, 2.0, 1.5), (12.3, 2.3, 6.5))

    # Camera obstructions: steel columns between the start and the course, and a beam over the walk.
    for i, z in enumerate((9.5, 5.0, 0.5)):
        box(f"Column {i + 1}", (3.6, 0.0, z - 0.2), (4.0, 4.2, z + 0.2), "painted_metal")
    box("Beam", (-14.0, 3.7, -3.6), (14.0, 4.2, -3.2), "painted_metal")
    box("Column pit west", (-14.0, 0.0, -3.6), (-13.6, 3.7, -3.2), "painted_metal")
    box("Column pit east", (13.6, 0.0, -3.6), (14.0, 3.7, -3.2), "painted_metal")

    # Beyond the pit: a 1.2 m ledge block to climb or drop from, and a wall with a corner.
    box("Ledge block", (-5.0, 0.0, -18.0), (1.0, 1.2, -14.0))
    box("Far wall", (-14.0, 0.0, -24.0), (14.0, 3.0, -23.7))
    box("Far corner wall", (6.0, 0.0, -23.7), (6.3, 3.0, -17.0))
    return pieces


PLATFORM = {"size": (3.0, 0.3, 2.6), "start": (0.0, -0.15, -5.45), "travel": (0.0, 0.0, -3.1), "period": 8.0, "pause": 1.5}
CRATE = {"size": (0.9, 0.9, 0.9), "position": (3.0, 0.45, 7.0), "mass": 30.0}
BODY = {"radius": 0.35, "height": 1.8, "position": (0.0, 0.92, 10.0), "mass": 80.0}
RESPAWN = (0.0, 0.0, 10.0)


def mesh_for(piece):
    name, kind, spec, _ = piece
    if kind == "box":
        lo, hi = spec
        size = [hi[k] - lo[k] for k in range(3)]
        centre = [(hi[k] + lo[k]) / 2 for k in range(3)]
        return box_mesh(size, centre), centre, size, [0, 0, 0, 1]
    width = spec["x"][1] - spec["x"][0]
    if spec.get("east"):
        # Along X: from the deck's east edge (x = 2) down to the floor at x = 2 + run, as wide as the deck in Z.
        width = DECK[1][2] - DECK[0][2]
        centre = [DECK[1][0] + spec["run"] / 2, spec["rise"] / 2, (DECK[0][2] + DECK[1][2]) / 2]
        rotation = [0.0, math.sin(math.pi / 4), 0.0, math.cos(math.pi / 4)]  # 90 deg about Y: -Z turns to -X
        return wedge_mesh(width, spec["rise"], spec["run"], centre), centre, [spec["run"], spec["rise"], width], rotation
    centre = [(spec["x"][0] + spec["x"][1]) / 2, spec["rise"] / 2, spec["z"] + spec["run"] / 2]
    return wedge_mesh(width, spec["rise"], spec["run"], centre), centre, [width, spec["rise"], spec["run"]], [0, 0, 0, 1]


# ---------------------------------------------------------------------------------------------
# level: files
# ---------------------------------------------------------------------------------------------

MODEL = "assets/level/course.glb"
SCRIPTS = {"platform": "scripts/platform.lua", "kill_volume": "scripts/kill_volume.lua"}


def write_sidecar(relative: str, kind: str, version: int, settings: dict):
    path = PROJECT / (relative + ".meta")
    if path.exists():
        return
    document = {"settings": settings, "type": kind, "uuid": asset_uuid(relative), "version": version}
    path.write_text(json.dumps(document, indent=2, sort_keys=True) + "\n")


def write_model(pieces):
    g = basic.Glb()
    meshes, nodes, names = [], [], []
    entries = [(piece[0], *mesh_for(piece)) for piece in pieces]
    entries.append(("Platform", box_mesh(PLATFORM["size"], PLATFORM["start"]), list(PLATFORM["start"]), None, [0, 0, 0, 1]))
    entries.append(("Crate", crate_mesh(CRATE["size"]), list(CRATE["position"]), None, [0, 0, 0, 1]))
    entries.append(("Body", capsule_mesh(BODY["radius"], BODY["height"]), list(BODY["position"]), None, [0, 0, 0, 1]))
    for index, (name, m, centre, _, rotation) in enumerate(entries):
        attributes = {
            "POSITION": g.floats(m.p, "VEC3", 34962),
            "NORMAL": g.floats(m.n, "VEC3", 34962),
            "TEXCOORD_0": g.floats(m.t, "VEC2", 34962),
        }
        meshes.append({"name": name, "primitives": [{"attributes": attributes, "indices": g.indices(m.i)}]})
        nodes.append({"name": name, "mesh": index, "translation": centre, "rotation": rotation})
        names.append(name)
    g.write(PROJECT / MODEL, {
        "scene": 0,
        "scenes": [{"name": "Course", "nodes": list(range(len(nodes)))}],
        "nodes": nodes,
        "meshes": meshes,
    })
    write_sidecar(MODEL, "Model", 2, {})
    model = asset_uuid(MODEL)
    return {name: derive(model, f"mesh/{i}") for i, name in enumerate(names)}


def write_materials():
    out = {}
    (LEVEL / "materials").mkdir(parents=True, exist_ok=True)
    for name, spec in MATERIALS.items():
        source = spec["source"]
        relative = f"assets/level/materials/{name}.material.json"
        scale = [1.0 / spec["tile"][0], 1.0 / spec["tile"][1]]
        slots = {"baseColor": "Color", "metallicRoughness": "ORM", "normal": "NormalGL", "occlusion": "ORM"}
        document = {
            "version": 6,
            "baseColor": [1.0, 1.0, 1.0, 1.0],
            "emissive": [0.0, 0.0, 0.0],
            "metallic": 1.0,
            "roughness": 1.0,
            "normalScale": 1.0,
            "occlusionStrength": 1.0,
            "alphaCutoff": 0.5,
            "transmission": 0.0,
            "alphaMode": "Opaque",
            "doubleSided": False,
            "unlit": False,
            "textures": {slot: asset_uuid(texture_path(source, kind)) for slot, kind in slots.items()},
            "transforms": {slot: {"scale": scale} for slot in slots},
        }
        (PROJECT / relative).write_text(json.dumps(document, indent=2) + "\n")
        write_sidecar(relative, "Material", 2, {})
        out[name] = asset_uuid(relative)
        for kind in ("Color", "NormalGL", "ORM"):
            write_sidecar(texture_path(source, kind), "Texture", 1, {"compress": True, "mipmaps": True, "srgb": kind == "Color"})
    relative = "assets/level/materials/placeholder.material.json"
    (PROJECT / relative).write_text(json.dumps({
        "version": 6, "baseColor": [0.4, 0.41, 0.43, 1.0], "emissive": [0.0, 0.0, 0.0], "metallic": 0.0,
        "roughness": 0.45, "normalScale": 1.0, "occlusionStrength": 1.0, "alphaCutoff": 0.5, "transmission": 0.0,
        "alphaMode": "Opaque", "doubleSided": False, "unlit": False, "textures": {},
    }, indent=2) + "\n")
    write_sidecar(relative, "Material", 2, {})
    out["placeholder"] = asset_uuid(relative)
    return out


def quat_yaw_pitch(yaw, pitch=0.0):
    """Yaw about Y, then pitch about X: the entity's -Z ends up along (-sin y cos p, sin p, -cos y cos p)."""
    cy, sy, cp, sp = math.cos(yaw / 2), math.sin(yaw / 2), math.cos(pitch / 2), math.sin(pitch / 2)
    return [cy * sp, sy * cp, -sy * sp, cy * cp]


def look_along(direction):
    length = math.sqrt(sum(c * c for c in direction))
    f = [c / length for c in direction]
    return quat_yaw_pitch(math.atan2(-f[0], -f[2]), math.asin(f[1]))


def engine_version():
    text = (PROJECT.parent.parent.parent / "CMakeLists.txt").read_text()
    start = text.index("VERSION ", text.index("project(")) + len("VERSION ")
    return text[start:text.index(" ", start)].strip()


def write_scene(meshes, materials):
    entities = []
    counter = [0]

    def entity(name, position=(0, 0, 0), rotation=None, parent=None, **components):
        counter[0] += 1
        identity = f"c4a20000-0000-4000-8000-{counter[0]:012x}"
        record = {"uuid": identity, "name": name}
        if parent:
            record["parent"] = parent
        body = {"Transform": {"position": list(position), "rotation": rotation or [0, 0, 0, 1], "scale": [1, 1, 1]}}
        body.update(components)
        record["components"] = body
        entities.append(record)
        return identity

    def renderer(mesh, material):
        return {"mesh": mesh, "material": material, "color": [1, 1, 1, 1], "visible": True}

    def rigid(kind, mass, friction=0.6, angular=0.05):
        return {"type": kind, "mass": mass, "friction": friction, "restitution": 0.0, "linearDamping": 0.05,
                "angularDamping": angular, "gravityScale": 1.0}

    sun = SUN_DIRECTION
    strength = max(SUN_IRRADIANCE)
    entity("Sky", Environment={"map": asset_uuid(SKY.relative_to(PROJECT).as_posix()), "intensity": 1.0})
    entity("Sun", (0, 30, 0), look_along([-c for c in sun]),
           DirectionalLight={"color": [round(c / strength, 4) for c in SUN_IRRADIANCE], "intensity": round(strength, 3)})
    eye, target = (-9.0, 6.5, 17.0), (1.0, 0.0, -6.0)
    entity("Camera", eye, look_along([target[k] - eye[k] for k in range(3)]),
           Camera={"fovY": math.radians(55.0), "nearPlane": 0.1, "exposure": CAMERA_EXPOSURE, "bloomStrength": 0.02})

    level = entity("Course")
    for piece in course():
        name, kind, spec, material = piece
        _, centre, size, rotation = mesh_for(piece)
        components = {"MeshRenderer": renderer(meshes[name], materials[material]), "Static": None}
        if kind == "box":
            components["BoxCollider"] = {"halfExtents": [s / 2 for s in size], "offset": [0, 0, 0]}
            entity(name, centre, rotation, level, **components)
            continue
        # A ramp's collider is a box whose top face is the slope, on a child turned to the slope's angle; it
        # runs past both ends into the floor and the deck, where nothing can reach it.
        ramp = entity(name, centre, rotation, level, **components)
        width, rise, run = (size[2], size[1], size[0]) if spec.get("east") else (size[0], size[1], size[2])
        angle = math.atan2(rise, run)
        slope = math.hypot(rise, run)
        thickness = 0.4
        half = [width / 2, thickness / 2, slope / 2 + 0.3]
        # In the ramp's own frame the slope runs from (y -rise/2, z +run/2) up to (+rise/2, -run/2); the box's
        # top face is on it, so its centre sits half the thickness below the slope's midpoint.
        offset = [0.0, -math.cos(angle) * thickness / 2, -math.sin(angle) * thickness / 2]
        tilt = [math.sin(angle / 2), 0.0, 0.0, math.cos(angle / 2)]
        entity(f"{name} collider", offset, tilt, ramp, Static=None, BoxCollider={"halfExtents": half, "offset": [0, 0, 0]})

    entity("Platform", PLATFORM["start"], None, level,
           MeshRenderer=renderer(meshes["Platform"], materials["painted_metal"]),
           RigidBody=rigid("Kinematic", 500.0, friction=0.9),
           BoxCollider={"halfExtents": [s / 2 for s in PLATFORM["size"]], "offset": [0, 0, 0]},
           Scripts={"slots": [{"script": asset_uuid(SCRIPTS["platform"]), "properties": {
               "travel": dict(zip("xyz", PLATFORM["travel"])), "period": PLATFORM["period"], "pause": PLATFORM["pause"]}}]})
    entity("Crate", CRATE["position"], None, level,
           MeshRenderer=renderer(meshes["Crate"], materials["worn_planks"]),
           RigidBody=rigid("Dynamic", CRATE["mass"]),
           BoxCollider={"halfExtents": [s / 2 for s in CRATE["size"]], "offset": [0, 0, 0]})
    respawn = entity("Respawn", RESPAWN)
    entity("Kill volume", (0.0, -12.0, -5.0), None, None,
           BoxCollider={"halfExtents": [80.0, 2.0, 80.0], "offset": [0, 0, 0]}, Trigger=None,
           Scripts={"slots": [{"script": asset_uuid(SCRIPTS["kill_volume"]), "properties": {"respawn": respawn}}]})
    entity("Placeholder body", BODY["position"], None, None,
           MeshRenderer=renderer(meshes["Body"], materials["placeholder"]),
           RigidBody=rigid("Dynamic", BODY["mass"], friction=0.5, angular=0.9),
           CapsuleCollider={"radius": BODY["radius"], "halfHeight": BODY["height"] / 2 - BODY["radius"], "offset": [0, 0, 0]})

    (PROJECT / "scenes").mkdir(parents=True, exist_ok=True)
    (PROJECT / "scenes" / "main.scene.json").write_text(scene_json.dumps({"version": 9, "entities": entities}) + "\n")
    print(f"main.scene.json: {len(entities)} entities")


# The map is relative, not absolute: sun and sky together put about 5 units of irradiance on the ground, so
# sunlit concrete (albedo about 0.6) leaves at about 1. This exposure lands it in the upper middle of the
# tone curve, as a camera metering a midday scene would, and keeps the clouds from clipping.
CAMERA_EXPOSURE = 0.5


def write_level():
    for relative in SCRIPTS.values():
        write_sidecar(relative, "Script", 2, {})
    write_sidecar(SKY.relative_to(PROJECT).as_posix(), "Environment", 2, {})
    pieces = course()
    meshes = write_model(pieces)
    materials = write_materials()
    write_scene(meshes, materials)
    project = {"version": 2, "name": "Character", "engineVersion": engine_version(), "startScene": "scenes/main.scene.json",
               "assetRoots": ["assets", "scripts"]}
    (PROJECT / "project.json").write_text(json.dumps(project, indent=2) + "\n")


def main():
    if len(sys.argv) == 3 and sys.argv[1] == "prepare":
        downloads = Path(sys.argv[2])
        TEXTURES.mkdir(parents=True, exist_ok=True)
        prepare_textures(downloads)
        prepare_sky(find(downloads, SKY_SOURCE))
    elif len(sys.argv) == 2 and sys.argv[1] == "level":
        write_level()
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
