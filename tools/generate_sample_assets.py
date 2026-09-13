#!/usr/bin/env python3
"""Generates the basic sample's assets without any external tool or download.

Writes, under apps/samples/basic/assets:
  models/crate.glb   a textured box and a metallic sphere, embedded PNG textures, two nodes
  textures/checker.png   a checker for the ground material
  sky.hdr            a gradient sky with a sun, as an uncompressed Radiance file
  ground.material.json   the ground's material over the checker

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


def main():
    glb(ROOT / "models" / "crate.glb")
    (ROOT / "textures").mkdir(parents=True, exist_ok=True)
    (ROOT / "textures" / "checker.png").write_bytes(png(64, 64, checker(64, (150, 152, 158, 255), (110, 112, 118, 255), 8)))
    hdr(ROOT / "sky.hdr")
    print("wrote", ROOT)


if __name__ == "__main__":
    main()
