#!/usr/bin/env python3
"""Generates the basic sample's assets without any external tool or download.

Writes, under apps/samples/basic/assets:
  models/crate.glb   a textured box and a metallic sphere, embedded PNG textures, two nodes
  models/reed.glb    a skinned stem on a chain of four joints, swaying in the clip "Sway"
  models/beacon.glb  a lamp spinning around its own axis with a bobbing halo, the clip "Pulse"
  textures/checker.png   a checker for the ground material
  sky.hdr            a gradient sky with a sun, as an uncompressed Radiance file
  ground.material.json   the ground's material over the checker
  sounds/hum.wav     a two-second drone that loops seamlessly
  sounds/chime.wav   a short bell

Run from the repository root: python3 tools/generate_sample_assets.py
The files are plain data; re-running overwrites them identically.
"""
import json
import math
import struct
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent / "apps" / "samples" / "basic" / "assets"


def png(width: int, height: int, rgba: bytes) -> bytes:
    def chunk(kind: bytes, data: bytes) -> bytes:
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    rows = b"".join(b"\x00" + rgba[y * width * 4 : (y + 1) * width * 4] for y in range(height))
    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b"")


def checker(size: int, a: tuple, b: tuple, cells: int) -> bytes:
    out = bytearray()
    for y in range(size):
        for x in range(size):
            c = a if ((x * cells // size) + (y * cells // size)) % 2 == 0 else b
            out += bytes(c)
    return bytes(out)


def planks(size: int) -> bytes:
    # Wood-like planks: bands of warm browns with a grain from a few sines.
    out = bytearray()
    for y in range(size):
        for x in range(size):
            plank = (y * 4 // size)
            seam = 1.0 if (y * 4) % size < 3 else 0.0
            grain = 0.5 + 0.5 * math.sin(x * 0.35 + plank * 1.7 + math.sin(y * 0.11) * 2.0)
            base = 0.42 + 0.08 * plank + 0.12 * grain
            r = min(255, int(255 * (base * 1.00 - seam * 0.3)))
            g = min(255, int(255 * (base * 0.66 - seam * 0.3)))
            b = min(255, int(255 * (base * 0.38 - seam * 0.2)))
            out += bytes((max(r, 0), max(g, 0), max(b, 0), 255))
    return bytes(out)


def roughness_metallic(size: int) -> bytes:
    # glTF packs roughness in green and metallic in blue; the crate is rough and not metallic,
    # with slightly shinier plank centres.
    out = bytearray()
    for y in range(size):
        for x in range(size):
            centre = abs(((y * 4) % size) / size - 0.5) * 2.0
            roughness = int(255 * (0.55 + 0.35 * centre))
            out += bytes((0, roughness, 0, 255))
    return bytes(out)


def box_mesh():
    positions, normals, uvs, indices = [], [], [], []
    faces = [
        ((0, 0, 1), (1, 0, 0), (0, 1, 0)),
        ((0, 0, -1), (-1, 0, 0), (0, 1, 0)),
        ((1, 0, 0), (0, 0, -1), (0, 1, 0)),
        ((-1, 0, 0), (0, 0, 1), (0, 1, 0)),
        ((0, 1, 0), (1, 0, 0), (0, 0, -1)),
        ((0, -1, 0), (1, 0, 0), (0, 0, 1)),
    ]
    for normal, right, up in faces:
        base = len(positions)
        for u, v in ((0, 0), (1, 0), (1, 1), (0, 1)):
            p = [0.5 * normal[i] + (u - 0.5) * right[i] + (v - 0.5) * up[i] for i in range(3)]
            positions.append(p)
            normals.append(list(normal))
            uvs.append([u, 1.0 - v])
        indices += [base, base + 1, base + 2, base, base + 2, base + 3]
    return positions, normals, uvs, indices


def sphere_mesh(slices=32, stacks=16, radius=0.5):
    positions, normals, uvs, indices = [], [], [], []
    for stack in range(stacks + 1):
        v = stack / stacks
        phi = v * math.pi
        for s in range(slices + 1):
            u = s / slices
            theta = u * 2 * math.pi
            n = (math.sin(phi) * math.cos(theta), math.cos(phi), -math.sin(phi) * math.sin(theta))
            positions.append([radius * c for c in n])
            normals.append(list(n))
            uvs.append([u, v])
    for stack in range(stacks):
        for s in range(slices):
            a = stack * (slices + 1) + s
            b = a + slices + 1
            if stack != stacks - 1:
                indices += [a, b, b + 1]
            if stack != 0:
                indices += [a, b + 1, a + 1]
    return positions, normals, uvs, indices


def glb(path: Path):
    buffer = bytearray()
    views, accessors, images = [], [], []

    def add_view(data: bytes, target=None):
        while len(buffer) % 4:
            buffer.append(0)
        views.append({"buffer": 0, "byteOffset": len(buffer), "byteLength": len(data), **({"target": target} if target else {})})
        buffer.extend(data)
        return len(views) - 1

    def add_accessor(view, count, kind, component, minimum=None, maximum=None):
        accessor = {"bufferView": view, "componentType": component, "count": count, "type": kind}
        if minimum is not None:
            accessor["min"], accessor["max"] = minimum, maximum
        accessors.append(accessor)
        return len(accessors) - 1

    def add_image(data: bytes):
        images.append({"bufferView": add_view(data), "mimeType": "image/png"})
        return len(images) - 1

    def add_mesh(positions, normals, uvs, indices):
        floats = lambda rows: b"".join(struct.pack("<%df" % len(r), *r) for r in rows)
        minimum = [min(p[i] for p in positions) for i in range(3)]
        maximum = [max(p[i] for p in positions) for i in range(3)]
        p = add_accessor(add_view(floats(positions), 34962), len(positions), "VEC3", 5126, minimum, maximum)
        n = add_accessor(add_view(floats(normals), 34962), len(normals), "VEC3", 5126)
        t = add_accessor(add_view(floats(uvs), 34962), len(uvs), "VEC2", 5126)
        i = add_accessor(add_view(struct.pack("<%dI" % len(indices), *indices), 34963), len(indices), "SCALAR", 5125)
        return {"POSITION": p, "NORMAL": n, "TEXCOORD_0": t}, i

    wood = add_image(png(128, 128, planks(128)))
    rough = add_image(png(128, 128, roughness_metallic(128)))
    box_attributes, box_indices = add_mesh(*box_mesh())
    sphere_attributes, sphere_indices = add_mesh(*sphere_mesh())
    document = {
        "asset": {"version": "2.0", "generator": "sonnet tools/generate_sample_assets.py"},
        "scene": 0,
        "scenes": [{"name": "Crate", "nodes": [0, 1]}],
        "nodes": [
            {"name": "Crate", "mesh": 0},
            {"name": "Ball", "mesh": 1, "translation": [0.0, 1.0, 0.0]},
        ],
        "meshes": [
            {"name": "CrateMesh", "primitives": [{"attributes": box_attributes, "indices": box_indices, "material": 0}]},
            {"name": "BallMesh", "primitives": [{"attributes": sphere_attributes, "indices": sphere_indices, "material": 1}]},
        ],
        "materials": [
            {
                "name": "Planks",
                "pbrMetallicRoughness": {
                    "baseColorTexture": {"index": 0},
                    "metallicRoughnessTexture": {"index": 1},
                    "metallicFactor": 1.0,
                    "roughnessFactor": 1.0,
                },
            },
            {
                "name": "Brass",
                "pbrMetallicRoughness": {"baseColorFactor": [0.95, 0.78, 0.45, 1.0], "metallicFactor": 1.0, "roughnessFactor": 0.25},
            },
        ],
        "textures": [{"source": wood, "sampler": 0}, {"source": rough, "sampler": 0}],
        "samplers": [{"magFilter": 9729, "minFilter": 9987, "wrapS": 10497, "wrapT": 10497}],
        "images": images,
        "bufferViews": views,
        "accessors": accessors,
        "buffers": [{"byteLength": len(buffer)}],
    }
    text = json.dumps(document, separators=(",", ":")).encode()
    while len(text) % 4:
        text += b" "
    while len(buffer) % 4:
        buffer.append(0)
    body = struct.pack("<II", len(text), 0x4E4F534A) + text + struct.pack("<II", len(buffer), 0x004E4942) + bytes(buffer)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b"glTF" + struct.pack("<II", 2, 12 + len(body)) + body)


def rgbe(r: float, g: float, b: float) -> bytes:
    m = max(r, g, b)
    if m < 1e-32:
        return b"\x00\x00\x00\x00"
    mantissa, exponent = math.frexp(m)
    scale = mantissa * 256.0 / m
    return bytes((min(255, int(r * scale)), min(255, int(g * scale)), min(255, int(b * scale)), exponent + 128))


def hdr(path: Path, width=256, height=128):
    # Flat scanlines: stb_image reads them as long as the first pixel is not the RLE marker.
    header = f"#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y {height} +X {width}\n".encode()
    pixels = bytearray()
    sun_dir = (math.cos(math.radians(35)) * math.cos(math.radians(50)), math.sin(math.radians(35)), -math.cos(math.radians(35)) * math.sin(math.radians(50)))
    for y in range(height):
        elevation = (0.5 - y / height) * math.pi
        for x in range(width):
            azimuth = (x / width) * 2 * math.pi - math.pi
            direction = (math.cos(elevation) * math.cos(azimuth), math.sin(elevation), math.cos(elevation) * math.sin(azimuth))
            if direction[1] >= 0.0:
                t = direction[1]
                r, g, b = 0.55 + 0.05 * (1 - t), 0.70 + 0.10 * (1 - t), 1.0 + 0.5 * (1 - t)
                r, g, b = r * (1 - 0.6 * t) + 0.08, g * (1 - 0.6 * t) + 0.16, b * (1 - 0.4 * t) + 0.35
            else:
                t = -direction[1]
                r, g, b = 0.28 - 0.1 * t, 0.26 - 0.1 * t, 0.22 - 0.08 * t
            cosine = sum(a * b for a, b in zip(direction, sun_dir))
            if cosine > 0.9995:
                r, g, b = 60.0, 55.0, 45.0
            elif cosine > 0.98:
                glow = (cosine - 0.98) / 0.0195
                r, g, b = r + 6.0 * glow, g + 5.0 * glow, b + 3.5 * glow
            pixels += rgbe(r, g, b)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(header + bytes(pixels))


class Glb:
    """A glTF binary under construction: buffer views, accessors and the JSON around them."""

    def __init__(self):
        self.buffer = bytearray()
        self.views = []
        self.accessors = []

    def view(self, data: bytes, target=None):
        while len(self.buffer) % 4:
            self.buffer.append(0)
        entry = {"buffer": 0, "byteOffset": len(self.buffer), "byteLength": len(data)}
        if target:
            entry["target"] = target
        self.views.append(entry)
        self.buffer.extend(data)
        return len(self.views) - 1

    def accessor(self, view, count, kind, component, minimum=None, maximum=None):
        entry = {"bufferView": view, "componentType": component, "count": count, "type": kind}
        if minimum is not None:
            entry["min"], entry["max"] = minimum, maximum
        self.accessors.append(entry)
        return len(self.accessors) - 1

    def floats(self, rows, kind, target=None):
        flat = b"".join(struct.pack("<%df" % len(r), *r) for r in rows)
        minimum = [min(r[i] for r in rows) for i in range(len(rows[0]))]
        maximum = [max(r[i] for r in rows) for i in range(len(rows[0]))]
        return self.accessor(self.view(flat, target), len(rows), kind, 5126, minimum, maximum)

    def scalars(self, values):
        data = struct.pack("<%df" % len(values), *values)
        return self.accessor(self.view(data), len(values), "SCALAR", 5126, [min(values)], [max(values)])

    def indices(self, values):
        data = struct.pack("<%dI" % len(values), *values)
        return self.accessor(self.view(data, 34963), len(values), "SCALAR", 5125)

    def joints(self, rows):
        data = b"".join(struct.pack("<4H", *r) for r in rows)
        return self.accessor(self.view(data, 34962), len(rows), "VEC4", 5123)

    def write(self, path: Path, document: dict):
        document.update({
            "asset": {"version": "2.0", "generator": "sonnet tools/generate_sample_assets.py"},
            "bufferViews": self.views,
            "accessors": self.accessors,
            "buffers": [{"byteLength": len(self.buffer)}],
        })
        text = json.dumps(document, separators=(",", ":")).encode()
        while len(text) % 4:
            text += b" "
        while len(self.buffer) % 4:
            self.buffer.append(0)
        body = struct.pack("<II", len(text), 0x4E4F534A) + text + struct.pack("<II", len(self.buffer), 0x004E4942) + bytes(self.buffer)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(b"glTF" + struct.pack("<II", 2, 12 + len(body)) + body)


def quaternion_z(angle: float):
    return [0.0, 0.0, math.sin(angle / 2), math.cos(angle / 2)]


def quaternion_y(angle: float):
    return [0.0, math.sin(angle / 2), 0.0, math.cos(angle / 2)]


def reed_glb(path: Path):
    """A tapered stem skinned to a chain of four joints, one per metre, swaying in a loop."""
    segments, rings, height = 12, 6, 3.0
    positions, normals, uvs, joints, weights, indices = [], [], [], [], [], []
    for ring in range(rings + 1):
        v = ring / rings
        y = v * height
        radius = 0.25 * (1.0 - v) + 0.07 * v
        # Two joints share every vertex, by the metre it sits in: the stem bends smoothly.
        lower = min(int(y), 3)
        upper = min(lower + 1, 3)
        blend = min(max(y - lower, 0.0), 1.0)
        for s in range(segments + 1):
            u = s / segments
            theta = u * 2 * math.pi
            normal = (math.cos(theta), 0.0, math.sin(theta))
            positions.append([radius * normal[0], y, radius * normal[2]])
            normals.append(list(normal))
            uvs.append([u, 1.0 - v])
            joints.append((lower, upper, 0, 0))
            weights.append([1.0 - blend, blend, 0.0, 0.0])
    for ring in range(rings):
        for s in range(segments):
            a = ring * (segments + 1) + s
            b = a + segments + 1
            indices += [a, b, b + 1, a, b + 1, a + 1]

    glb = Glb()
    attributes = {
        "POSITION": glb.floats(positions, "VEC3", 34962),
        "NORMAL": glb.floats(normals, "VEC3", 34962),
        "TEXCOORD_0": glb.floats(uvs, "VEC2", 34962),
        "JOINTS_0": glb.joints(joints),
        "WEIGHTS_0": glb.floats(weights, "VEC4", 34962),
    }
    triangles = glb.indices(indices)
    # Joint 0 sits at the stem's foot, the others one metre above the one below.
    inverse_binds = []
    for joint in range(4):
        matrix = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -float(joint), 0, 1]
        inverse_binds.append(matrix)
    inverse_bind = glb.accessor(glb.view(b"".join(struct.pack("<16f", *m) for m in inverse_binds)), 4, "MAT4", 5126)

    # Sway: every joint above the foot turns about Z, each a little further and later than the
    # one below, over two seconds that end where they began so the loop is seamless.
    samplers, channels = [], []
    times = [i * 0.25 for i in range(9)]
    time = glb.scalars(times)
    for joint, (amplitude, phase) in enumerate((( 0.10, 0.0), (0.16, 0.6), (0.22, 1.2)), start=2):
        rotations = [quaternion_z(amplitude * math.sin(2 * math.pi * t / 2.0 + phase)) for t in times]
        samplers.append({"input": time, "output": glb.floats(rotations, "VEC4"), "interpolation": "LINEAR"})
        channels.append({"sampler": len(samplers) - 1, "target": {"node": joint, "path": "rotation"}})

    glb.write(path, {
        "scene": 0,
        "scenes": [{"name": "Reed", "nodes": [0]}],
        "nodes": [
            {"name": "Reed", "children": [1, 5]},
            {"name": "Bone0", "children": [2]},
            {"name": "Bone1", "children": [3], "translation": [0.0, 1.0, 0.0]},
            {"name": "Bone2", "children": [4], "translation": [0.0, 1.0, 0.0]},
            {"name": "Bone3", "translation": [0.0, 1.0, 0.0]},
            {"name": "Stem", "mesh": 0, "skin": 0},
        ],
        "meshes": [{"name": "StemMesh", "primitives": [{"attributes": attributes, "indices": triangles, "material": 0}]}],
        "skins": [{"name": "StemSkin", "joints": [1, 2, 3, 4], "skeleton": 1, "inverseBindMatrices": inverse_bind}],
        "animations": [{"name": "Sway", "samplers": samplers, "channels": channels}],
        "materials": [{
            "name": "Stem",
            "pbrMetallicRoughness": {"baseColorFactor": [0.35, 0.60, 0.28, 1.0], "metallicFactor": 0.0, "roughnessFactor": 0.75},
            "doubleSided": True,
        }],
    })


def beacon_glb(path: Path):
    """A lamp that turns about its axis while its halo bobs: an animation without a skin."""
    glb = Glb()

    def mesh(data):
        positions, normals, uvs, indices = data
        attributes = {
            "POSITION": glb.floats(positions, "VEC3", 34962),
            "NORMAL": glb.floats(normals, "VEC3", 34962),
            "TEXCOORD_0": glb.floats(uvs, "VEC2", 34962),
        }
        return attributes, glb.indices(indices)

    lamp_attributes, lamp_indices = mesh(box_mesh())
    halo_attributes, halo_indices = mesh(sphere_mesh(24, 12, 0.5))

    # A full turn in four seconds, in quarters: a slerp takes the short way between neighbours.
    spin_times = [0.0, 1.0, 2.0, 3.0, 4.0]
    spins = [quaternion_y(i * math.pi / 2) for i in range(5)]
    bob_times = [i * 0.25 for i in range(9)]
    bobs = [[0.8, 0.6 + 0.18 * math.sin(2 * math.pi * t / 2.0), 0.0] for t in bob_times]
    samplers = [
        {"input": glb.scalars(spin_times), "output": glb.floats(spins, "VEC4"), "interpolation": "LINEAR"},
        {"input": glb.scalars(bob_times), "output": glb.floats(bobs, "VEC3"), "interpolation": "LINEAR"},
    ]
    glb.write(path, {
        "scene": 0,
        "scenes": [{"name": "Beacon", "nodes": [0]}],
        # The halo is a sibling of the lamp, not its child: the lamp's scale is its own, and the
        # root's spin carries the halo around it.
        "nodes": [
            {"name": "Beacon", "children": [1, 2]},
            {"name": "Lamp", "mesh": 0, "translation": [0.0, 0.45, 0.0], "scale": [0.4, 0.9, 0.4]},
            {"name": "Halo", "mesh": 1, "translation": [0.8, 0.6, 0.0], "scale": [0.36, 0.36, 0.36]},
        ],
        "meshes": [
            {"name": "LampMesh", "primitives": [{"attributes": lamp_attributes, "indices": lamp_indices, "material": 0}]},
            {"name": "HaloMesh", "primitives": [{"attributes": halo_attributes, "indices": halo_indices, "material": 1}]},
        ],
        "animations": [{"name": "Pulse", "samplers": samplers, "channels": [
            {"sampler": 0, "target": {"node": 0, "path": "rotation"}},
            {"sampler": 1, "target": {"node": 2, "path": "translation"}},
        ]}],
        "materials": [
            {"name": "Lamp", "pbrMetallicRoughness": {"baseColorFactor": [0.20, 0.22, 0.26, 1.0], "metallicFactor": 0.9, "roughnessFactor": 0.35}},
            {"name": "Glow", "pbrMetallicRoughness": {"baseColorFactor": [0.95, 0.85, 0.55, 1.0], "metallicFactor": 0.0, "roughnessFactor": 0.4}, "emissiveFactor": [1.0, 0.75, 0.25]},
        ],
    })


def wav(path: Path, samples, rate=48000):
    """A mono 16-bit WAV of samples in [-1, 1]."""
    frames = len(samples)
    data = b"".join(struct.pack("<h", max(-32767, min(32767, int(s * 32767)))) for s in samples)
    header = b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVEfmt " + struct.pack("<IHHIIHH", 16, 1, 1, rate, rate * 2, 2, 16)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(header + b"data" + struct.pack("<I", len(data)) + data)
    return frames


def hum(seconds=2.0, rate=48000):
    """A drone whose partials fit whole cycles into its length, so it loops without a click."""
    frames = int(seconds * rate)
    partials = ((110.0, 0.5), (220.0, 0.18), (330.0, 0.10), (55.0, 0.22))
    out = []
    for i in range(frames):
        t = i / rate
        value = sum(gain * math.sin(2 * math.pi * frequency * t) for frequency, gain in partials)
        # A slow tremolo, also a whole number of cycles long.
        out.append(value * (0.85 + 0.15 * math.sin(2 * math.pi * t / seconds)))
    return out


def chime(seconds=1.2, rate=48000):
    """A struck bell: a few partials decaying at their own rates."""
    frames = int(seconds * rate)
    partials = ((880.0, 0.5, 3.0), (1320.0, 0.25, 4.5), (1760.0, 0.15, 6.0), (2640.0, 0.08, 9.0))
    out = []
    for i in range(frames):
        t = i / rate
        value = sum(gain * math.exp(-decay * t) * math.sin(2 * math.pi * frequency * t) for frequency, gain, decay in partials)
        out.append(value * min(1.0, t * 400.0))  # a short attack, so the start does not click
    return out



def main():
    glb(ROOT / "models" / "crate.glb")
    reed_glb(ROOT / "models" / "reed.glb")
    beacon_glb(ROOT / "models" / "beacon.glb")
    wav(ROOT / "sounds" / "hum.wav", hum())
    wav(ROOT / "sounds" / "chime.wav", chime())
    (ROOT / "textures").mkdir(parents=True, exist_ok=True)
    (ROOT / "textures" / "checker.png").write_bytes(png(64, 64, checker(64, (150, 152, 158, 255), (110, 112, 118, 255), 8)))
    hdr(ROOT / "sky.hdr")
    print("wrote", ROOT)


if __name__ == "__main__":
    main()
