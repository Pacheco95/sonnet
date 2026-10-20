#!/usr/bin/env python3
"""Generates the TAA harness's project, apps/samples/taa (docs/rendering.md, "TAA checks").

The two procedural cases (a rough pane over a moving box, a masked pane translating over a pattern)
are built here from nothing but the standard library: the glTF files, their textures, the scripts,
the scenes and taa-cases.json. The two cases that use Khronos models (ChronographWatch,
TransmissionTest) only have their scenes here; tools/fetch_taa_fixtures.py downloads the models.

Every asset gets a sidecar with a fixed identity, so the scenes can name them without an editor
run in between. Run from the repository root; the output is deterministic.
"""
import base64
import json
import math
import struct
import uuid
import zlib
from pathlib import Path

PROJECT = Path(__file__).resolve().parent.parent / "apps" / "samples" / "taa"
ASSETS = PROJECT / "assets"
SIDECAR_VERSION = 2


def identity(name: str) -> str:
    return str(uuid.uuid5(uuid.NAMESPACE_URL, "https://github.com/Pacheco95/sonnet/taa/" + name))


# Identities of the downloaded models and the environment, which tools/fetch_taa_fixtures.py writes sidecars for.
WATCH = identity("ChronographWatch.gltf")
TRANSMISSION_TEST = identity("TransmissionTest.gltf")
COMMERCIAL_REFRIGERATOR = identity("CommercialRefrigerator.gltf")
ENVIRONMENT = identity("helipad.hdr")

# ---------------------------------------------------------------------------------------------
# Textures
# ---------------------------------------------------------------------------------------------


def png(width: int, height: int, rgba: bytes) -> bytes:
    def chunk(kind: bytes, data: bytes) -> bytes:
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))

    rows = b"".join(b"\x00" + rgba[y * width * 4:(y + 1) * width * 4] for y in range(height))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))


def pattern(size: int = 256) -> bytes:
    """A smooth, busy pattern that tiles: sines with periods of 8, 16 and 32 texels (about 15 to 60 px on
    screen) at moderate contrast. Hard edges are left out on purpose: a TAA of 8 samples and a
    reference of 16 resolve an edge pixel differently, so even opaque pixels would use up the budgets
    that are meant for the glass."""
    tau = 2 * math.pi
    out = bytearray()
    for y in range(size):
        for x in range(size):
            r = 128 + 70 * math.sin(tau * x / 8)
            g = 128 + 70 * math.sin(tau * (x + y) / 16)
            b = 128 + 70 * math.sin(tau * y / 32)
            out += bytes((int(r), int(g), int(b), 255))
    return png(size, size, bytes(out))


def stripes(size: int = 256) -> bytes:
    """Orange and white bands, for the moving box."""
    out = bytearray()
    for y in range(size):
        for x in range(size):
            out += bytes((235, 120, 30, 255)) if (x // 8) % 2 == 0 else bytes((245, 245, 235, 255))
    return png(size, size, bytes(out))


def lattice(size: int = 256) -> bytes:
    """A frame with a cross, light-tinted: alpha 255 on the bars, 0 in the four windows. Few edges and
    a moderate contrast, since an edge pixel of the 8 sample TAA and of the 16 sample reference differ
    by up to a sixteenth of the contrast however well the history is kept."""
    out = bytearray()
    for y in range(size):
        for x in range(size):
            on_bar = x < 16 or x >= size - 16 or y < 16 or y >= size - 16 or abs(x - size // 2) < 8 or abs(y - size // 2) < 8
            out += bytes((150, 205, 225, 255)) if on_bar else bytes((255, 255, 255, 0))
    return png(size, size, bytes(out))


# ---------------------------------------------------------------------------------------------
# glTF
# ---------------------------------------------------------------------------------------------


def quad(width: float, height: float, tiles=(1, 1)):
    w, h = width / 2, height / 2
    positions = [(-w, -h, 0), (w, -h, 0), (w, h, 0), (-w, h, 0)]
    normals = [(0, 0, 1)] * 4
    uvs = [(0, tiles[1]), (tiles[0], tiles[1]), (tiles[0], 0), (0, 0)]
    return positions, normals, uvs, [0, 1, 2, 0, 2, 3]


def cube(side: float):
    h = side / 2
    faces = [((0, 0, 1), (1, 0, 0), (0, 1, 0)), ((0, 0, -1), (-1, 0, 0), (0, 1, 0)),
             ((1, 0, 0), (0, 0, -1), (0, 1, 0)), ((-1, 0, 0), (0, 0, 1), (0, 1, 0)),
             ((0, 1, 0), (1, 0, 0), (0, 0, -1)), ((0, -1, 0), (1, 0, 0), (0, 0, 1))]
    positions, normals, uvs, indices = [], [], [], []
    for normal, u, v in faces:
        base = len(positions)
        for su, sv in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
            positions.append(tuple(h * (normal[i] + su * u[i] + sv * v[i]) for i in range(3)))
            normals.append(normal)
            uvs.append(((su + 1) / 2, (1 - sv) / 2))
        indices += [base, base + 1, base + 2, base, base + 2, base + 3]
    return positions, normals, uvs, indices


def write_gltf(name: str, mesh, material: dict, textures: dict, extensions: list):
    positions, normals, uvs, indices = mesh
    blob = bytearray()
    views, accessors = [], []

    def add(data: bytes, target, count, kind, component, minimum=None, maximum=None):
        views.append({"buffer": 0, "byteOffset": len(blob), "byteLength": len(data), "target": target})
        blob.extend(data)
        while len(blob) % 4:
            blob.append(0)
        accessor = {"bufferView": len(views) - 1, "componentType": component, "count": count, "type": kind}
        if minimum is not None:
            accessor["min"], accessor["max"] = minimum, maximum
        accessors.append(accessor)
        return len(accessors) - 1

    flat = lambda rows: struct.pack(f"<{len(rows) * len(rows[0])}f", *[c for row in rows for c in row])
    position = add(flat(positions), 34962, len(positions), "VEC3", 5126,
                   [min(p[i] for p in positions) for i in range(3)], [max(p[i] for p in positions) for i in range(3)])
    normal = add(flat(normals), 34962, len(normals), "VEC3", 5126)
    uv = add(flat(uvs), 34962, len(uvs), "VEC2", 5126)
    index = add(struct.pack(f"<{len(indices)}H", *indices), 34963, len(indices), "SCALAR", 5123)
    document = {
        "asset": {"version": "2.0", "generator": "tools/generate_taa_fixtures.py"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"name": name, "mesh": 0}],
        "meshes": [{"name": name, "primitives": [{"attributes": {"POSITION": position, "NORMAL": normal,
                                                                  "TEXCOORD_0": uv}, "indices": index,
                                                  "material": 0}]}],
        "materials": [material],
        "buffers": [{"byteLength": len(blob),
                     "uri": "data:application/octet-stream;base64," + base64.b64encode(bytes(blob)).decode()}],
        "bufferViews": views,
        "accessors": accessors,
    }
    if textures:
        document["images"] = [{"uri": f"{image}.png"} for image in textures]
        document["textures"] = [{"source": i, "sampler": 0} for i in range(len(textures))]
        document["samplers"] = [{"magFilter": 9729, "minFilter": 9987, "wrapS": 10497, "wrapT": 10497}]
        for image, data in textures.items():
            (ASSETS / f"{image}.png").write_bytes(data)
    if extensions:
        document["extensionsUsed"] = extensions
    (ASSETS / f"{name}.gltf").write_text(json.dumps(document, indent=1) + "\n")
    sidecar(f"{name}.gltf")


def sidecar(file: str, **extra):
    """A sidecar with a fixed identity; the editor and the player fill in the rest when they scan."""
    document = {"version": SIDECAR_VERSION, "uuid": identity(file), "settings": {}}
    document.update(extra)
    (ASSETS / f"{file}.meta").write_text(json.dumps(document, indent=2) + "\n")


def assets():
    ASSETS.mkdir(parents=True, exist_ok=True)
    write_gltf("wall", quad(14, 8, (4, 2)), {
        "name": "Pattern", "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}, "metallicFactor": 0.0,
                                                    "roughnessFactor": 1.0},
        "extensions": {"KHR_materials_unlit": {}}}, {"pattern": pattern()}, ["KHR_materials_unlit"])
    write_gltf("box", cube(1.4), {
        "name": "Box", "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}, "metallicFactor": 0.0,
                                                "roughnessFactor": 0.6}}, {"stripes": stripes()}, [])
    write_gltf("pane_rough", quad(5.0, 3.0), {
        "name": "Rough pane", "doubleSided": True,
        "pbrMetallicRoughness": {"baseColorFactor": [0.85, 0.92, 1.0, 1.0], "metallicFactor": 0.0,
                                 "roughnessFactor": 0.5},
        "extensions": {"KHR_materials_transmission": {"transmissionFactor": 1.0}}}, {},
               ["KHR_materials_transmission"])
    write_gltf("pane_masked", quad(2.4, 1.8), {
        "name": "Masked lattice pane", "doubleSided": True, "alphaMode": "MASK", "alphaCutoff": 0.5,
        "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}, "metallicFactor": 0.0, "roughnessFactor": 0.2},
        "extensions": {"KHR_materials_transmission": {"transmissionFactor": 1.0}}}, {"lattice": lattice()},
               ["KHR_materials_transmission"])
    (PROJECT / "scripts").mkdir(parents=True, exist_ok=True)
    (PROJECT / "scripts" / "slide.lua").write_text("""-- Moves its entity along `velocity` (metres per second) from where it starts, and holds still from
-- `stopAt` seconds on (0: never). The TAA harness's boxes and panes use it (docs/rendering.md, "TAA checks").
local Slide = {
  properties = {
    velocity = { type = "vec3", default = { x = 0, y = 0, z = 0 } },
    stopAt = { type = "number", default = 0 },
  },
}

function Slide:start()
  self.origin = self.entity:get("Transform").position
  self.time = 0
end

function Slide:update(dt)
  self.time = self.time + dt
  local t = self.time
  if self.stopAt > 0 then
    t = math.min(t, self.stopAt)
  end
  local transform = self.entity:get("Transform")
  transform.position = self.origin + self.velocity * t
  self.entity:set("Transform", transform)
end

return Slide
""")
    sidecar("../scripts/slide.lua")


# ---------------------------------------------------------------------------------------------
# Scenes
# ---------------------------------------------------------------------------------------------

IDENTITY_ROTATION = [0, 0, 0, 1]


def transform(position, rotation=IDENTITY_ROTATION, scale=(1, 1, 1)):
    return {"position": list(position), "rotation": list(rotation), "scale": list(scale)}


class Scene:
    def __init__(self, tag: int):
        self.tag = tag
        self.entities = []

    def add(self, name: str, components: dict, prefab: str | None = None):
        entity = {"uuid": f"7aa00000-0000-4000-8000-{self.tag:04d}{len(self.entities) + 1:08d}", "name": name,
                  "components": components}
        if prefab:
            entity["prefab"] = prefab
        self.entities.append(entity)

    def camera(self, position, fov, near=0.1):
        self.add("Camera", {"Transform": transform(position),
                            "Camera": {"fovY": fov, "nearPlane": near, "exposure": 1.0, "bloomStrength": 0.0}})

    def sun(self):
        # Pointing down and into the scene.
        angle = math.radians(-35)
        self.add("Sun", {"Transform": transform((0, 10, 0), (math.sin(angle / 2), 0, 0, math.cos(angle / 2))),
                         "DirectionalLight": {"color": [1.0, 0.96, 0.9], "intensity": 3.0}})

    def sky(self):
        self.add("Sky", {"Transform": transform((0, 0, 0)), "Environment": {"map": ENVIRONMENT, "intensity": 1.0}})

    def write(self, file: str):
        (PROJECT / "scenes").mkdir(parents=True, exist_ok=True)
        (PROJECT / "scenes" / file).write_text(json.dumps({"version": 6, "entities": self.entities}, indent=1) + "\n")


def slide(velocity, stop_at=0.0):
    properties = {"velocity": {"x": velocity[0], "y": velocity[1], "z": velocity[2]}}
    if stop_at:
        properties["stopAt"] = stop_at
    return {"slots": [{"script": identity("../scripts/slide.lua"), "properties": properties}]}


def scenes():
    fov = math.radians(40)
    # A rough pane in front of a box that crosses a depth edge: the box at 1.2 m in front of the
    # wall, moving 3.6 m/s sideways (about 8 px per frame at 1280x720).
    scene = Scene(1)
    scene.camera((0, 0, 6), fov)
    scene.sun()
    scene.add("Wall", {"Transform": transform((0, 0, -3))}, identity("wall.gltf"))
    scene.add("Box", {"Transform": transform((-1.5, 0, -1.2), (0, 0.2588, 0, 0.9659)),
                      "Scripts": slide((3.6, 0, 0))}, identity("box.gltf"))
    scene.add("Pane", {"Transform": transform((0, 0, 1))}, identity("pane_rough.gltf"))
    scene.write("rough_pane.scene.json")

    # A masked lattice pane translating over a stationary pattern, then stopping after 40 frames:
    # at 0.9 m/s (about 3 px per frame at 1280x720), and 2.4 m/s (about 8 px per frame) as a stress
    # case that is reported and not judged.
    for name, speed in (("masked_pane", 0.9), ("masked_pane_fast", 2.4)):
        scene = Scene(2 if speed < 1 else 5)
        scene.camera((0, 0, 6), fov)
        scene.sun()
        scene.add("Wall", {"Transform": transform((0, 0, -3))}, identity("wall.gltf"))
        scene.add("Pane", {"Transform": transform((-0.8, 0, 1)), "Scripts": slide((speed, 0, 0), 40 / 60)},
                  identity("pane_masked.gltf"))
        scene.write(f"{name}.scene.json")

    # ChronographWatch from the glTF-Sample-Assets, framed by its fit-to-bounds camera, with the
    # second hand's 60 s clip played 30 times faster (3 degrees a frame), and 90 times faster (9
    # degrees) as a stress case that is reported and not judged.
    for name, speed in (("watch", 30.0), ("watch_fast", 90.0)):
        scene = Scene(3 if speed < 60 else 6)
        scene.sky()
        scene.camera((0.0006470680236816406, -0.08905792236328125, 8.970009803771973), math.pi / 4, 0.0107)
        scene.add("Watch", {"Transform": transform((0, 0, 0)), "Animator": {"speed": speed}}, WATCH)
        scene.write(f"{name}.scene.json")

    # TransmissionTest on a turntable, 0.5 rad/s (half a degree a frame), and still, as a case that
    # reports what TAA reaches without any motion.
    for name, speed in (("transmission_test", 0.5), ("transmission_test_still", 0.0)):
        scene = Scene(4 if speed else 7)
        scene.sky()
        scene.camera((0, 0, 4.855337619781494), math.pi / 4, 0.0047)
        scene.add("Board", {"Transform": transform((0, 0, 0)), "Spin": {"axis": [0.0, 1.0, 0.0], "speed": speed}},
                  TRANSMISSION_TEST)
        scene.write(f"{name}.scene.json")

    # The two acceptance scenes of the phone measurements (docs/player.md, "Transmission on a
    # phone"), framed for a portrait screen and still: the camera is the same at the S25's 1080x2340
    # and the iPad's 1640x2360, and nothing moves, so each frame costs what the last did.
    # TransmissionTest stands on its end, 1.8 m of board along the screen's long side.
    quarter = math.sqrt(0.5)
    scene = Scene(8)
    scene.sky()
    scene.camera((0, 0, 2.8), math.pi / 4, 0.01)
    scene.add("Board", {"Transform": transform((0, 0, 0), (0, 0, quarter, quarter))}, TRANSMISSION_TEST)
    scene.write("transmission_test_phone.scene.json")
    # CommercialRefrigerator is 2.2 m tall: the camera looks at its middle from 2.9 m in front of it.
    scene = Scene(9)
    scene.sky()
    scene.camera((0, 1.09, 3.4), math.pi / 4, 0.01)
    scene.add("Refrigerator", {"Transform": transform((0, 0, 0))}, COMMERCIAL_REFRIGERATOR)
    scene.write("commercial_refrigerator_phone.scene.json")


def project():
    (PROJECT / "project.json").write_text(json.dumps({
        "version": 1, "name": "TAA harness", "engineVersion": "0.20.0", "startScene": "scenes/rough_pane.scene.json",
        "assetRoots": ["assets", "scripts"]}, indent=2) + "\n")
    cases = [
        {"name": "rough-pane", "scene": "scenes/rough_pane.scene.json",
         "description": "a rough pane (roughness 0.5) still in front of a box moving across a depth edge"},
        {"name": "masked-pane", "scene": "scenes/masked_pane.scene.json",
         "description": "a textured, masked pane translating over a stationary pattern at 3 px a frame, then stopping",
         "warmup": 32, "measured": 24},
        {"name": "masked-pane-fast", "scene": "scenes/masked_pane_fast.scene.json", "diagnostic": True,
         "description": "the same pane at 8 px a frame: a stress case, reported and not judged",
         "warmup": 32, "measured": 24},
        {"name": "watch", "scene": "scenes/watch.scene.json",
         "description": "ChronographWatch with its second hand moving behind still glass, 30 times faster than real"},
        {"name": "watch-fast", "scene": "scenes/watch_fast.scene.json", "diagnostic": True,
         "description": "the same at 90 times: a stress case, reported and not judged"},
        {"name": "transmission-test", "scene": "scenes/transmission_test.scene.json",
         "description": "TransmissionTest on a turntable, half a degree a frame"},
        {"name": "transmission-test-still", "scene": "scenes/transmission_test_still.scene.json", "diagnostic": True,
         "description": "the same without the turntable: what TAA reaches on this glass at rest"},
    ]
    (PROJECT / "taa-cases.json").write_text(json.dumps({"cases": cases}, indent=2) + "\n")


if __name__ == "__main__":
    assets()
    scenes()
    project()
