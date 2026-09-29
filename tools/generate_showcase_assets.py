#!/usr/bin/env python3
"""Generates the showcase sample: a sunset courtyard built from procedural assets, no downloads.

Two steps, because the editor writes the assets' `.meta` sidecars (and with them the UUIDs the scene refers to):

  python3 tools/generate_showcase_assets.py assets   # models, textures, materials, sky, scripts
  sonnet_editor apps/samples/showcase --screenshot /tmp/x.png   # any run writes the missing sidecars
  python3 tools/generate_showcase_assets.py scene    # scenes/main.scene.json from the sidecars

Writes under apps/samples/showcase:
  assets/models/kit.glb      the architecture and props: fluted columns, arcade walls, a gate, towers, lamp posts, trees
  assets/models/{crate,reed,beacon}.glb   the basic sample's props, reused
  assets/materials/*.material.json        emissive materials for bulbs and the gate's glow
  assets/sky.hdr             a sunset with clouds, 1024x512, sun disc included
  assets/sounds/hum.wav      the beacons' drone
  scripts/camera_drift.lua   a slow dolly through the courtyard in play mode
  scenes/main.scene.json     the layout

Run from the repository root. Everything is deterministic: re-running overwrites the files identically.
"""
import json
import math
import random
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import generate_sample_assets as basic  # noqa: E402

PROJECT = Path(__file__).resolve().parent.parent / "apps" / "samples" / "showcase"
ASSETS = PROJECT / "assets"

# ---------------------------------------------------------------------------------------------
# Small vector helpers
# ---------------------------------------------------------------------------------------------


def sub(a, b):
    return [a[0] - b[0], a[1] - b[1], a[2] - b[2]]


def cross(a, b):
    return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def norm(a):
    length = math.sqrt(dot(a, a))
    return [c / length for c in a] if length > 1e-12 else [0.0, 1.0, 0.0]


def clamp(x, lo=0.0, hi=1.0):
    return lo if x < lo else hi if x > hi else x


def smoothstep(a, b, x):
    t = clamp((x - a) / (b - a))
    return t * t * (3 - 2 * t)


def mix(a, b, t):
    return [x * (1 - t) + y * t for x, y in zip(a, b)]


# ---------------------------------------------------------------------------------------------
# Tileable noise and the texture generators
# ---------------------------------------------------------------------------------------------

_lattices = {}


def _lattice(period, seed):
    key = (period, seed)
    if key not in _lattices:
        rng = random.Random(seed * 7919 + period)
        _lattices[key] = [rng.random() for _ in range(period * period)]
    return _lattices[key]


def value_noise(x, y, period, seed):
    table = _lattice(period, seed)
    ix, iy = math.floor(x), math.floor(y)
    fx, fy = x - ix, y - iy
    fx, fy = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy)
    x0, x1, y0, y1 = ix % period, (ix + 1) % period, iy % period, (iy + 1) % period
    a = table[y0 * period + x0] * (1 - fx) + table[y0 * period + x1] * fx
    b = table[y1 * period + x0] * (1 - fx) + table[y1 * period + x1] * fx
    return a * (1 - fy) + b * fy


def fbm(u, v, base, octaves, seed):
    total, amplitude, weight = 0.0, 1.0, 0.0
    for octave in range(octaves):
        frequency = base << octave
        total += amplitude * value_noise(u * frequency, v * frequency, frequency, seed + octave)
        weight += amplitude
        amplitude *= 0.5
    return total / weight


def to_byte(x):
    return int(clamp(x) * 255 + 0.5)


def normal_map(height, size, strength):
    """Tangent-space normals of a height field that wraps; glTF's green points up the image."""
    out = bytearray()
    for y in range(size):
        up, down, row = height[(y - 1) % size], height[(y + 1) % size], height[y]
        for x in range(size):
            dx = (row[(x + 1) % size] - row[(x - 1) % size]) * 0.5 * strength
            dy = (down[x] - up[x]) * 0.5 * strength
            n = norm([-dx, dy, 1.0])
            out += bytes((to_byte(n[0] * 0.5 + 0.5), to_byte(n[1] * 0.5 + 0.5), to_byte(n[2] * 0.5 + 0.5), 255))
    return bytes(out)


def rgba(pixels):
    return b"".join(bytes((to_byte(r), to_byte(g), to_byte(b), 255)) for r, g, b in pixels)


def orm(rows):
    """Occlusion in red, roughness in green, metallic in blue: one image for both glTF slots."""
    return b"".join(bytes((to_byte(o), to_byte(r), to_byte(m), 255)) for o, r, m in rows)


def block_textures(size=256):
    """Sandstone ashlar: four courses of two blocks over two metres, mortar joints, chamfered edges."""
    rng = random.Random(11)
    rows, per_row = 4, 2
    block_h = size // rows
    block_w = size // per_row
    tints = [[[rng.random() for _ in range(3)] for _ in range(per_row)] for _ in range(rows)]
    offsets = [0, block_w // 2, 0, block_w // 2]
    albedo, occlusion, height = [], [], []
    for y in range(size):
        row = y // block_h
        ly = y % block_h
        hrow = []
        for x in range(size):
            shifted = (x + offsets[row]) % size
            col = shifted // block_w
            lx = shifted % block_w
            edge = min(lx, block_w - 1 - lx, ly, block_h - 1 - ly)
            u, v = x / size, y / size
            wear = fbm(u, v, 8, 4, 3)
            grain = fbm(u, v, 32, 2, 9)
            t = tints[row][col]
            shade = 0.78 + 0.35 * wear + 0.08 * (t[0] - 0.5)
            colour = [0.80 * shade * (0.96 + 0.08 * t[1]), 0.66 * shade * (0.96 + 0.08 * t[2]), 0.48 * shade * (0.92 + 0.14 * t[0])]
            mortar = 1.0 - smoothstep(1.5, 3.5, edge)
            chamfer = smoothstep(0.0, 5.0, edge)
            dirt = mix(colour, [0.30, 0.27, 0.23], 0.85) if mortar > 0 else colour
            colour = mix(colour, dirt, mortar)
            colour = [c * (0.92 + 0.16 * grain) for c in colour]
            h = 0.55 * chamfer + 0.30 * wear + 0.15 * grain - 0.35 * mortar
            albedo.append(colour)
            height_value = h
            hrow.append(height_value)
            ao = 1.0 - 0.55 * mortar - 0.15 * (1 - chamfer)
            occlusion.append((ao, 0.72 + 0.2 * (1 - wear) + 0.1 * mortar, 0.0))
        height.append(hrow)
    return rgba(albedo), normal_map(height, size, 5.0), orm(occlusion)


def cobble_textures(size=256):
    """Small setts in staggered courses: sixteen rows, irregular widths, dark joints, worn tops."""
    rng = random.Random(5)
    rows = 16
    row_h = size // rows
    stones = []  # per row: list of (start, width, tint)
    for r in range(rows):
        widths, total = [], 0
        while total < size:
            w = rng.randint(13, 24)
            widths.append(w)
            total += w
        scale = size / total
        widths = [max(12, int(w * scale)) for w in widths]
        widths[-1] += size - sum(widths)
        start, row = rng.randint(0, size), []
        for w in widths:
            row.append((start % size, w, rng.random(), rng.random()))
            start += w
        stones.append(row)
    lookup = []
    for r in range(rows):
        table = [None] * size
        for start, w, tint, warmth in stones[r]:
            for i in range(w):
                table[(start + i) % size] = (i, w, tint, warmth)
        lookup.append(table)
    albedo, surface, height = [], [], []
    for y in range(size):
        r = y // row_h
        ly = y % row_h
        hrow = []
        for x in range(size):
            i, w, tint, warmth = lookup[r][x]
            edge = min(i, w - 1 - i, ly, row_h - 1 - ly)
            u, v = x / size, y / size
            speck = fbm(u, v, 64, 2, 21)
            wear = fbm(u, v, 6, 3, 17)
            base = 0.36 + 0.20 * tint + 0.10 * wear
            colour = [base * (1.0 + 0.12 * warmth), base * (0.96 + 0.05 * warmth), base * (0.92 - 0.06 * warmth)]
            joint = 1.0 - smoothstep(1.0, 2.6, edge)
            colour = mix(colour, [0.10, 0.09, 0.08], joint * 0.85)
            colour = [c * (0.85 + 0.3 * speck) for c in colour]
            albedo.append(colour)
            dome = smoothstep(0.0, 4.0, edge)
            hrow.append(0.65 * dome + 0.2 * speck + 0.15 * wear - 0.4 * joint)
            surface.append((1.0 - 0.6 * joint, 0.68 + 0.25 * speck + 0.07 * joint, 0.0))
        height.append(hrow)
    return rgba(albedo), normal_map(height, size, 6.0), orm(surface)


def marble_texture(size=256):
    albedo = []
    for y in range(size):
        for x in range(size):
            u, v = x / size, y / size
            warp = fbm(u, v, 4, 4, 31)
            vein = abs(math.sin((u * 2.0 + v * 1.0 + warp * 2.2) * math.pi))
            thin = 1.0 - smoothstep(0.0, 0.06, vein)
            fine = 1.0 - smoothstep(0.0, 0.02, abs(math.sin((u * 5.0 - v * 3.0 + warp * 4.0) * math.pi)))
            cloud = fbm(u, v, 8, 3, 37)
            base = [0.93 - 0.05 * cloud, 0.92 - 0.05 * cloud, 0.89 - 0.04 * cloud]
            colour = mix(base, [0.42, 0.45, 0.50], 0.55 * thin + 0.25 * fine)
            albedo.append(colour)
    return rgba(albedo)


def leaf_textures(size=256):
    albedo, height = [], []
    for y in range(size):
        hrow = []
        for x in range(size):
            u, v = x / size, y / size
            blob = fbm(u, v, 16, 3, 41)
            mid = fbm(u, v, 6, 3, 43)
            light = smoothstep(0.35, 0.75, blob)
            colour = mix([0.05, 0.13, 0.05], [0.22, 0.42, 0.10], light)
            colour = mix(colour, [0.42, 0.50, 0.14], 0.25 * smoothstep(0.55, 0.85, mid))
            albedo.append(colour)
            hrow.append(blob)
        height.append(hrow)
    return rgba(albedo), normal_map(height, size, 6.0)


def bark_textures(size=256):
    albedo, height = [], []
    for y in range(size):
        hrow = []
        for x in range(size):
            u, v = x / size, y / size
            ridge = fbm(u * 1.0, v * 0.25, 16, 3, 51)
            fine = fbm(u, v, 32, 2, 53)
            shade = 0.35 + 0.65 * ridge
            colour = [0.20 * shade + 0.03 * fine, 0.14 * shade + 0.02 * fine, 0.09 * shade]
            albedo.append(colour)
            hrow.append(ridge)
        height.append(hrow)
    return rgba(albedo), normal_map(height, size, 8.0)


def grass_textures(size=256):
    """Dry summer grass: streaks of gold and olive."""
    albedo, height = [], []
    for y in range(size):
        hrow = []
        for x in range(size):
            u, v = x / size, y / size
            blade = fbm(u, v, 48, 2, 71)
            patch = fbm(u, v, 4, 4, 73)
            colour = mix([0.13, 0.17, 0.05], [0.48, 0.42, 0.17], smoothstep(0.25, 0.75, patch * 0.6 + blade * 0.5))
            albedo.append(colour)
            hrow.append(blade)
        height.append(hrow)
    return rgba(albedo), normal_map(height, size, 3.0)


def cloth_texture(size=128):
    """A weave: fine crossed threads, so the banners catch the light unevenly."""
    albedo, height = [], []
    for y in range(size):
        hrow = []
        for x in range(size):
            warp = 0.5 + 0.5 * math.sin(x * math.pi / 2)
            weft = 0.5 + 0.5 * math.sin(y * math.pi / 2)
            over = ((x // 2) + (y // 2)) % 2
            h = warp if over else weft
            shade = 0.8 + 0.2 * h
            albedo.append((shade, shade, shade))
            hrow.append(h)
        height.append(hrow)
    return rgba(albedo), normal_map(height, size, 2.0)


# ---------------------------------------------------------------------------------------------
# Meshes
# ---------------------------------------------------------------------------------------------


class Mesh:
    def __init__(self):
        self.p, self.n, self.t, self.i = [], [], [], []

    def vertex(self, p, n, t):
        self.p.append(list(p))
        self.n.append(list(n))
        self.t.append(list(t))
        return len(self.p) - 1

    def tri(self, a, b, c):
        g = cross(sub(self.p[b], self.p[a]), sub(self.p[c], self.p[a]))
        if dot(g, g) < 1e-14:
            return
        n = [self.n[a][k] + self.n[b][k] + self.n[c][k] for k in range(3)]
        if dot(g, n) < 0:
            b, c = c, b
        self.i += [a, b, c]

    def quad(self, a, b, c, d):
        self.tri(a, b, c)
        self.tri(a, c, d)

    def add(self, other, offset=(0.0, 0.0, 0.0), yaw=0.0):
        """Appends a copy of `other`, turned about Y and moved."""
        base = len(self.p)
        c, s = math.cos(yaw), math.sin(yaw)
        for p, n, t in zip(other.p, other.n, other.t):
            self.p.append([c * p[0] + s * p[2] + offset[0], p[1] + offset[1], -s * p[0] + c * p[2] + offset[2]])
            self.n.append([c * n[0] + s * n[2], n[1], -s * n[0] + c * n[2]])
            self.t.append(list(t))
        self.i += [base + k for k in other.i]
        return self


def box(sx, sy, sz, tile=2.0):
    """A box standing on y = 0, centred in x and z, with UVs in metres over `tile`."""
    m = Mesh()
    hx, hz = sx / 2, sz / 2
    faces = [
        ((0, 0, 1), (-hx, 0, hz), (1, 0, 0), sx, sy),
        ((0, 0, -1), (hx, 0, -hz), (-1, 0, 0), sx, sy),
        ((1, 0, 0), (hx, 0, hz), (0, 0, -1), sz, sy),
        ((-1, 0, 0), (-hx, 0, -hz), (0, 0, 1), sz, sy),
        ((0, 1, 0), (-hx, sy, hz), (1, 0, 0), sx, sz),
        ((0, -1, 0), (-hx, 0, -hz), (1, 0, 0), sx, sz),
    ]
    for n, origin, right, width, height in faces:
        up = (0, 1, 0) if n[1] == 0 else ((0, 0, -1) if n[1] > 0 else (0, 0, 1))
        ids = []
        for u, v in ((0, 0), (1, 0), (1, 1), (0, 1)):
            p = [origin[k] + right[k] * u * width + up[k] * v * height for k in range(3)]
            ids.append(m.vertex(p, n, (u * width / tile, (1 - v) * height / tile)))
        m.quad(*ids)
    return m


def rect(width, depth, tile=None, cx=0.0, cz=0.0):
    """A horizontal rectangle at y = 0 facing up. UVs cover 0..1, or with `tile` they follow world metres
    (for a rectangle that will stand at cx, cz) so neighbouring rectangles tile seamlessly."""
    m = Mesh()
    hx, hz = width / 2, depth / 2

    def uv(x, z):
        return ((x + cx) / tile, (z + cz) / tile) if tile else ((x + hx) / width, (z + hz) / depth)

    ids = [m.vertex((x, 0, z), (0, 1, 0), uv(x, z)) for x, z in ((-hx, hz), (hx, hz), (hx, -hz), (-hx, -hz))]
    m.quad(*ids)
    return m


def window_panel(width=1.2, height=2.6, segments=16):
    """An arched window's glass: a rectangle topped by a half circle, facing +Z, standing on y = 0."""
    m = Mesh()
    r = width / 2
    body = height - r
    n = (0, 0, 1)
    ids = [m.vertex((x, y, 0), n, (0.5 + x / width, 1 - y / height)) for x, y in ((-r, 0), (r, 0), (r, body), (-r, body))]
    m.quad(*ids)
    centre = m.vertex((0, body, 0), n, (0.5, 1 - body / height))
    for i in range(segments):
        a0, a1 = math.pi * i / segments, math.pi * (i + 1) / segments
        p0 = m.vertex((r * math.cos(a0), body + r * math.sin(a0), 0), n, (0.5, 0.0))
        p1 = m.vertex((r * math.cos(a1), body + r * math.sin(a1), 0), n, (0.5, 0.0))
        m.tri(centre, p0, p1)
    return m


def vertical_rect(width, height):
    """A panel in the XY plane facing +Z with its bottom edge on y = 0."""
    m = Mesh()
    hx = width / 2
    ids = [m.vertex((-hx, 0, 0), (0, 0, 1), (0, 1)), m.vertex((hx, 0, 0), (0, 0, 1), (1, 1)),
           m.vertex((hx, height, 0), (0, 0, 1), (1, 0)), m.vertex((-hx, height, 0), (0, 0, 1), (0, 0))]
    m.quad(*ids)
    return m


def lathe(strips, segments=32, tile=1.0, flutes=None):
    """Turns profiles about Y. A strip is a list of (radius, y); it should run so that the outside is on
    the left when seen from outside: up the sides, inward across the tops. `flutes` is (count, depth, y0, y1)."""
    m = Mesh()

    def radius(r, y, theta):
        if flutes and flutes[2] <= y <= flutes[3]:
            count, depth = flutes[0], flutes[1]
            return r * (1.0 - depth * (1.0 - abs(math.cos(count * theta / 2.0))))
        return r

    for strip in strips:
        rows = []
        length = 0.0
        along = [0.0]
        for k in range(1, len(strip)):
            length += math.hypot(strip[k][0] - strip[k - 1][0], strip[k][1] - strip[k - 1][1])
            along.append(length)
        for k, (r, y) in enumerate(strip):
            prev, nxt = strip[max(k - 1, 0)], strip[min(k + 1, len(strip) - 1)]
            ds = [nxt[0] - prev[0], nxt[1] - prev[1]]
            row = []
            for s in range(segments + 1):
                theta = s / segments * 2 * math.pi
                d = 1e-3
                rr = radius(r, y, theta)
                p = [rr * math.cos(theta), y, -rr * math.sin(theta)]
                pa = [radius(r, y, theta + d) * math.cos(theta + d), y, -radius(r, y, theta + d) * math.sin(theta + d)]
                pb = [radius(r, y, theta - d) * math.cos(theta - d), y, -radius(r, y, theta - d) * math.sin(theta - d)]
                dtheta = sub(pa, pb)
                dprofile = [ds[0] * math.cos(theta), ds[1], -ds[0] * math.sin(theta)]
                n = norm(cross(dtheta, dprofile))
                row.append((p, n, (s / segments * 2 * math.pi * max(r, 0.05) / tile, along[k] / tile)))
            rows.append(row)
        ids = [[m.vertex(*v) for v in row] for row in rows]
        for k in range(len(ids) - 1):
            for s in range(segments):
                m.quad(ids[k][s], ids[k][s + 1], ids[k + 1][s + 1], ids[k + 1][s])
    return m


def sphere(radius=0.5, slices=32, stacks=16, bumps=0.0, seed=1, tile=1.0):
    m = Mesh()
    grid = []
    for stack in range(stacks + 1):
        phi = stack / stacks * math.pi
        row = []
        for s in range(slices + 1):
            theta = s / slices * 2 * math.pi
            n = (math.sin(phi) * math.cos(theta), math.cos(phi), -math.sin(phi) * math.sin(theta))
            r = radius
            if bumps:
                r *= 1.0 + bumps * (fbm((math.atan2(n[2], n[0]) / math.pi + 1) * 0.5, n[1] * 0.5 + 0.5, 3, 3, seed) - 0.5) * 2.0
            row.append(m.vertex([r * c for c in n], n, (s / slices * 2 * math.pi * radius / tile, phi * radius / tile)))
        grid.append(row)
    for stack in range(stacks):
        for s in range(slices):
            m.quad(grid[stack][s], grid[stack + 1][s], grid[stack + 1][s + 1], grid[stack][s + 1])
    return m


def torus(major, minor, segments=64, sides=16):
    m = Mesh()
    grid = []
    for i in range(segments + 1):
        a = i / segments * 2 * math.pi
        row = []
        for j in range(sides + 1):
            b = j / sides * 2 * math.pi
            centre = (major * math.cos(a), 0.0, -major * math.sin(a))
            n = (math.cos(b) * math.cos(a), math.sin(b), -math.cos(b) * math.sin(a))
            row.append(m.vertex([centre[k] + minor * n[k] for k in range(3)], n, (i / segments * 8, j / sides)))
        grid.append(row)
    for i in range(segments):
        for j in range(sides):
            m.quad(grid[i][j], grid[i + 1][j], grid[i + 1][j + 1], grid[i][j + 1])
    return m


def arch_wall(r, pier, extra, depth, segments=20, tile=2.0):
    """A wall panel with a round-headed opening: the opening's radius is `r`, its springing line is y = 0,
    the piers on either side are `pier` wide and the wall stands `extra` above the crown."""
    m = Mesh()
    half, top = r + pier, r + extra
    for z, nz in ((depth / 2, 1.0), (-depth / 2, -1.0)):
        n = (0, 0, nz)

        def v(x, y):
            return m.vertex((x, y, z), n, (x / tile, -y / tile))

        for i in range(segments):
            a0, a1 = math.pi * i / segments, math.pi * (i + 1) / segments
            m.quad(v(r * math.cos(a0), r * math.sin(a0)), v(r * math.cos(a1), r * math.sin(a1)),
                   v(r * math.cos(a1), top), v(r * math.cos(a0), top))
        m.quad(v(r, 0), v(half, 0), v(half, top), v(r, top))
        m.quad(v(-half, 0), v(-r, 0), v(-r, top), v(-half, top))
    for i in range(segments):
        a0, a1 = math.pi * i / segments, math.pi * (i + 1) / segments
        ids = []
        for a, z in ((a0, depth / 2), (a1, depth / 2), (a1, -depth / 2), (a0, -depth / 2)):
            n = (-math.cos(a), -math.sin(a), 0)
            ids.append(m.vertex((r * math.cos(a), r * math.sin(a), z), n, (a * r / tile, z / tile)))
        m.quad(*ids)
    for n, x in (((1, 0, 0), half), ((-1, 0, 0), -half)):
        ids = [m.vertex((x, y, z), n, (z / tile, -y / tile)) for y, z in ((0, -depth / 2), (0, depth / 2), (top, depth / 2), (top, -depth / 2))]
        m.quad(*ids)
    ids = [m.vertex((x, top, z), (0, 1, 0), (x / tile, z / tile)) for x, z in ((-half, depth / 2), (half, depth / 2), (half, -depth / 2), (-half, -depth / 2))]
    m.quad(*ids)
    return m


def column():
    strips = [
        [(0.62, 0.0), (0.62, 0.16), (0.56, 0.2), (0.50, 0.24), (0.46, 0.27)],
        [(0.46, 0.27), (0.45, 0.6), (0.41, 2.0), (0.385, 3.5), (0.38, 3.7)],
        [(0.38, 3.7), (0.45, 3.76), (0.56, 3.88), (0.61, 4.0)],
        [(0.61, 4.0), (0.62, 4.2)],
        [(0.62, 4.2), (0.0, 4.2)],
    ]
    return lathe(strips, segments=96, tile=2.0, flutes=(16, 0.035, 0.3, 3.65))


def lamp_post():
    strips = [
        [(0.20, 0.0), (0.20, 0.25), (0.11, 0.42), (0.07, 0.6)],
        [(0.055, 0.6), (0.045, 2.2)],
        [(0.045, 2.2), (0.10, 2.32), (0.17, 2.4), (0.17, 2.45)],
        [(0.17, 2.45), (0.05, 2.45)],
        [(0.24, 2.9), (0.10, 3.06), (0.0, 3.14)],
        [(0.0, 3.14), (0.0, 3.14)],
    ]
    m = lathe(strips[:4] + strips[4:5], segments=24, tile=1.0)
    for angle in range(4):  # cage bars around the bulb
        a = angle * math.pi / 2 + math.pi / 4
        m.add(lathe([[(0.014, 2.45), (0.014, 2.9)]], segments=6), (0.155 * math.cos(a), 0.0, -0.155 * math.sin(a)))
    return m


def hanging_lantern():
    m = lathe([[(0.012, 0.6), (0.012, 0.0)], [(0.16, -0.05), (0.06, 0.0), (0.0, 0.03)]], segments=16)
    m.add(lathe([[(0.0, -0.36), (0.09, -0.36), (0.12, -0.3)]], segments=16))
    for angle in range(4):
        a = angle * math.pi / 2 + math.pi / 4
        m.add(lathe([[(0.012, -0.34), (0.012, -0.05)]], segments=6), (0.13 * math.cos(a), 0.0, -0.13 * math.sin(a)))
    return m


def pedestal():
    strips = [
        [(1.7, 0.0), (1.7, 0.35), (1.55, 0.45), (1.35, 0.5)],
        [(1.35, 0.5), (1.0, 0.55), (0.7, 0.8), (0.55, 1.1), (0.55, 1.9)],
        [(0.55, 1.9), (0.8, 2.0), (0.9, 2.15)],
        [(0.9, 2.15), (0.0, 2.15)],
    ]
    return lathe(strips, segments=64, tile=2.0)


def tower():
    strips = [
        [(2.4, 0.0), (2.4, 0.5)],
        [(2.3, 0.5), (2.3, 11.4)],
        [(2.3, 11.4), (2.55, 11.6), (2.6, 12.2), (2.4, 12.4)],
        [(2.4, 12.4), (2.4, 13.0)],
        [(2.4, 13.0), (0.0, 13.0)],
    ]
    return lathe(strips, segments=48, tile=2.0)


def tower_roof():
    return lathe([[(2.9, 0.0), (2.85, 0.25), (2.0, 1.6), (1.0, 3.5), (0.35, 5.5), (0.0, 6.4)]], segments=48, tile=2.0)


def planter():
    return lathe([[(0.0, 0.0), (0.4, 0.0), (0.42, 0.05)], [(0.42, 0.05), (0.55, 0.45), (0.6, 0.62), (0.66, 0.66), (0.66, 0.72)],
                  [(0.66, 0.72), (0.5, 0.72), (0.5, 0.65)]], segments=32, tile=1.0)


def trunk():
    return lathe([[(0.28, 0.0), (0.2, 0.6), (0.14, 2.0), (0.11, 3.0)], [(0.11, 3.0), (0.0, 3.0)]], segments=16, tile=1.0)


def cypress():
    return lathe([[(0.02, 0.0), (0.5, 1.0), (0.85, 3.0), (0.85, 5.0), (0.5, 7.4), (0.0, 9.0)]], segments=14, tile=0.9)


def bench():
    m = box(2.2, 0.12, 0.6)
    m = Mesh().add(m, (0, 0.44, 0))
    for x in (-0.8, 0.8):
        m.add(box(0.14, 0.44, 0.5), (x, 0, 0))
    return m


def banner():
    m = Mesh()
    columns, rows, width, height = 6, 16, 1.0, 2.8
    grid = []
    for j in range(rows + 1):
        v = j / rows
        row = []
        for i in range(columns + 1):
            u = i / columns
            wave = 0.05 * math.sin(v * 5.0 + u * 2.0) * v
            row.append(m.vertex(((u - 0.5) * width, height * (1 - v), wave), (0, 0, 1), (u * 1.0, v * 2.6)))
        grid.append(row)
    for j in range(rows):
        for i in range(columns):
            m.quad(grid[j][i], grid[j + 1][i], grid[j + 1][i + 1], grid[j][i + 1])
    return m


# ---------------------------------------------------------------------------------------------
# glTF writer for the kit
# ---------------------------------------------------------------------------------------------


def write_kit(path):
    g = basic.Glb()
    images, textures = [], []

    def image(data):
        images.append({"bufferView": g.view(data), "mimeType": "image/png"})
        textures.append({"source": len(images) - 1, "sampler": 0})
        return len(textures) - 1

    def png(size, data):
        return basic.png(size, size, data)

    blocks_albedo, blocks_normal, blocks_orm = block_textures()
    cobbles_albedo, cobbles_normal, cobbles_orm = cobble_textures()
    leaf_albedo, leaf_normal = leaf_textures()
    bark_albedo, bark_normal = bark_textures()
    cloth_albedo, cloth_normal = cloth_texture()
    grass_albedo, grass_normal = grass_textures()
    tex = {
        "blocks": image(png(256, blocks_albedo)), "blocks_n": image(png(256, blocks_normal)), "blocks_orm": image(png(256, blocks_orm)),
        "cobble": image(png(256, cobbles_albedo)), "cobble_n": image(png(256, cobbles_normal)), "cobble_orm": image(png(256, cobbles_orm)),
        "marble": image(png(256, marble_texture())),
        "leaf": image(png(256, leaf_albedo)), "leaf_n": image(png(256, leaf_normal)),
        "bark": image(png(256, bark_albedo)), "bark_n": image(png(256, bark_normal)),
        "cloth": image(png(128, cloth_albedo)), "cloth_n": image(png(128, cloth_normal)),
        "grass": image(png(256, grass_albedo)), "grass_n": image(png(256, grass_normal)),
    }

    def pbr(name, base=(1, 1, 1, 1), metallic=0.0, roughness=1.0, albedo=None, normal=None, mr=None, double_sided=False, normal_scale=1.0, blend=False):
        pbr_block = {"baseColorFactor": list(base), "metallicFactor": metallic, "roughnessFactor": roughness}
        if albedo is not None:
            pbr_block["baseColorTexture"] = {"index": tex[albedo]}
        if mr is not None:
            pbr_block["metallicRoughnessTexture"] = {"index": tex[mr]}
        material = {"name": name, "pbrMetallicRoughness": pbr_block}
        if normal is not None:
            material["normalTexture"] = {"index": tex[normal], "scale": normal_scale}
        if mr is not None:
            material["occlusionTexture"] = {"index": tex[mr], "strength": 1.0}
        if double_sided:
            material["doubleSided"] = True
        if blend:
            material["alphaMode"] = "BLEND"
        return material

    materials = [
        pbr("Sandstone", albedo="blocks", normal="blocks_n", mr="blocks_orm", normal_scale=1.2),
        pbr("Cobbles", albedo="cobble", normal="cobble_n", mr="cobble_orm", normal_scale=1.3),
        pbr("Marble", albedo="marble", roughness=0.12),
        pbr("Gold", base=(1.0, 0.77, 0.34, 1), metallic=1.0, roughness=0.18),
        pbr("Water", base=(0.012, 0.05, 0.06, 0.62), roughness=0.02, blend=True),
        pbr("Iron", base=(0.05, 0.05, 0.055, 1), metallic=1.0, roughness=0.42),
        pbr("Terracotta", base=(0.62, 0.27, 0.16, 1), roughness=0.62),
        pbr("Leaves", albedo="leaf", normal="leaf_n", roughness=0.7, double_sided=True),
        pbr("Bark", albedo="bark", normal="bark_n", roughness=0.9),
        pbr("Cloth", albedo="cloth", normal="cloth_n", roughness=0.85, double_sided=True),
        pbr("Grass", albedo="grass", normal="grass_n", roughness=0.95),
        pbr("Basin", base=(0.02, 0.025, 0.03, 1), roughness=0.5),
    ]
    slot = {m["name"]: i for i, m in enumerate(materials)}

    meshes = [
        ("PavingWest", rect(21.25, 49.0, tile=3.0, cx=-13.375, cz=-2.5), "Cobbles"),
        ("PavingEast", rect(21.25, 49.0, tile=3.0, cx=13.375, cz=-2.5), "Cobbles"),
        ("PavingNorth", rect(5.5, 14.75, tile=3.0, cx=0.0, cz=-19.625), "Cobbles"),
        ("PavingSouth", rect(5.5, 13.75, tile=3.0, cx=0.0, cz=15.125), "Cobbles"),
        ("GrassWest", rect(176, 400, tile=2.5, cx=-112.0, cz=0.0), "Grass"),
        ("GrassEast", rect(176, 400, tile=2.5, cx=112.0, cz=0.0), "Grass"),
        ("GrassNorth", rect(48, 173, tile=2.5, cx=0.0, cz=-113.5), "Grass"),
        ("GrassSouth", rect(48, 178, tile=2.5, cx=0.0, cz=111.0), "Grass"),
        ("BasinFloor", rect(5.5, 20.5), "Basin"),
        ("WindowPanel", window_panel(), "Iron"),
        ("Sill", box(1.7, 0.16, 0.5), "Marble"),
        ("RimLong", box(0.5, 0.4, 20.5, tile=1.5), "Marble"),
        ("RimShort", box(5.5, 0.4, 0.5, tile=1.5), "Marble"),
        ("Water", rect(4.5, 19.5), "Water"),
        ("Pedestal", pedestal(), "Marble"),
        ("GoldSphere", sphere(0.72, 48, 32), "Gold"),
        ("RingA", torus(1.35, 0.05), "Gold"),
        ("RingB", torus(1.75, 0.05), "Gold"),
        ("RingC", torus(2.15, 0.05), "Gold"),
        ("Column", column(), "Marble"),
        ("ArcadeArch", arch_wall(1.3, 0.7, 0.55, 1.2), "Sandstone"),
        ("Gallery", box(2.6, 0.45, 28.4), "Sandstone"),
        ("GateArch", arch_wall(3.0, 3.6, 1.2, 2.4, segments=32), "Sandstone"),
        ("GateJamb", box(3.6, 5.0, 2.4), "Sandstone"),
        ("Wing", box(9.0, 9.0, 2.4), "Sandstone"),
        ("Cornice", box(31.0, 0.9, 3.2), "Sandstone"),
        ("StepLow", box(20.0, 0.3, 7.0), "Marble"),
        ("StepMid", box(18.0, 0.3, 5.2), "Marble"),
        ("StepHigh", box(16.0, 0.3, 3.4), "Marble"),
        ("Tower", tower(), "Sandstone"),
        ("TowerRoof", tower_roof(), "Terracotta"),
        ("LampPost", lamp_post(), "Iron"),
        ("HangingLantern", hanging_lantern(), "Iron"),
        ("Bulb", sphere(0.15, 16, 10), "Iron"),
        ("BulbSmall", sphere(0.06, 10, 6), "Iron"),
        ("Planter", planter(), "Terracotta"),
        ("Trunk", trunk(), "Bark"),
        ("Canopy", sphere(1.7, 28, 18, bumps=0.28, seed=7, tile=1.5), "Leaves"),
        ("CanopySmall", sphere(1.1, 24, 16, bumps=0.3, seed=13, tile=1.5), "Leaves"),
        ("Cypress", cypress(), "Leaves"),
        ("Bench", bench(), "Marble"),
        ("Banner", banner(), "Cloth"),
    ]

    gltf_meshes, nodes = [], []
    for index, (name, m, material) in enumerate(meshes):
        attributes = {
            "POSITION": g.floats(m.p, "VEC3", 34962),
            "NORMAL": g.floats(m.n, "VEC3", 34962),
            "TEXCOORD_0": g.floats(m.t, "VEC2", 34962),
        }
        gltf_meshes.append({"name": name, "primitives": [{"attributes": attributes, "indices": g.indices(m.i), "material": slot[material]}]})
        nodes.append({"name": name, "mesh": index})

    g.write(path, {
        "scene": 0,
        "scenes": [{"name": "Kit", "nodes": list(range(len(nodes)))}],
        "nodes": nodes,
        "meshes": gltf_meshes,
        "materials": materials,
        "textures": textures,
        "samplers": [{"magFilter": 9729, "minFilter": 9987, "wrapS": 10497, "wrapT": 10497}],
        "images": images,
    })
    print(f"kit.glb: {len(meshes)} meshes, {sum(len(m.p) for _, m, _ in meshes)} vertices")


# ---------------------------------------------------------------------------------------------
# Sky
# ---------------------------------------------------------------------------------------------

SUN = norm([-0.75, 0.22, 0.45])  # towards the sun, in the sky file's convention


def sky(path, width=1024, height=512):
    header = f"#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y {height} +X {width}\n".encode()
    pixels = bytearray()
    horizon, band, zenith = [1.7, 0.72, 0.34], [0.48, 0.28, 0.46], [0.02, 0.07, 0.30]
    for y in range(height):
        elevation = (0.5 - (y + 0.5) / height) * math.pi
        for x in range(width):
            azimuth = ((x + 0.5) / width) * 2 * math.pi - math.pi
            d = (math.cos(elevation) * math.cos(azimuth), math.sin(elevation), math.cos(elevation) * math.sin(azimuth))
            e = d[1]
            cs = dot(d, SUN)
            if e >= 0.0:
                a, b = smoothstep(0.0, 0.22, e), smoothstep(0.08, 0.85, e)
                colour = mix(mix(horizon, band, a), zenith, b)
                colour = [c + w * (max(cs, 0.0) ** 6 * 0.9 + max(cs, 0.0) ** 48 * 2.5) for c, w in zip(colour, (1.6, 0.8, 0.3))]
                # Clouds: streaky layers lit from below by the low sun.
                density = clamp((fbm(x / width, y / height * 0.6 + 0.2, 6, 5, 61) - 0.5) * 2.6 + 0.5)
                cover = smoothstep(0.42, 0.70, density) * smoothstep(0.0, 0.05, e) * (1.0 - smoothstep(0.55, 0.95, e))
                lit = max(cs, 0.0) ** 2.0
                cloud = mix([0.34, 0.19, 0.34], [3.0, 1.2, 0.5], lit * 0.9 + 0.3 * (1.0 - smoothstep(0.0, 0.35, e)))
                colour = mix(colour, cloud, cover * 0.85)
                if cs > 0.99985:
                    colour = [420.0, 330.0, 210.0]
            else:
                a = smoothstep(0.0, -0.25, e)
                colour = mix([0.55, 0.30, 0.16], [0.10, 0.075, 0.07], a)
            pixels += basic.rgbe(*colour)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(header + bytes(pixels))
    print("sky.hdr")


# ---------------------------------------------------------------------------------------------
# Materials and scripts
# ---------------------------------------------------------------------------------------------

EMISSIVE = {
    "bulb_warm": ([1.0, 0.75, 0.4, 1.0], [16.0, 7.5, 2.4]),
    "bulb_fairy": ([1.0, 0.8, 0.5, 1.0], [12.0, 6.5, 2.2]),
    "bulb_cool": ([0.6, 0.8, 1.0, 1.0], [2.5, 6.0, 15.0]),
    "gate_glow": ([1.0, 0.6, 0.3, 1.0], [7.0, 3.1, 1.0]),
    "window_glow": ([1.0, 0.6, 0.3, 1.0], [2.2, 0.9, 0.26]),
}

CAMERA_DRIFT = """-- A slow dolly down the pool towards the gate, swaying a little, for the showcase's play mode.
local CameraDrift = {
  speed = 0.32,  -- metres per second
  fromZ = 12.5,   -- z at t = 0
  toZ = 0.5,       -- z the dolly stops at
}

function CameraDrift:start()
  self.time = 0
end

function CameraDrift:update(dt)
  self.time = self.time + dt
  local transform = self.entity:get("Transform")
  local z = math.max(self.fromZ - self.speed * self.time, self.toZ)
  local sway = math.sin(self.time * 0.11)
  transform.position = vec3(1.6 * sway, 1.75 + 0.18 * math.sin(self.time * 0.23), z)
  transform.rotation = quat.euler(0.045 + 0.02 * math.sin(self.time * 0.17), -0.16 * sway, 0)
  self.entity:set("Transform", transform)
end

return CameraDrift
"""


def write_assets():
    (ASSETS / "models").mkdir(parents=True, exist_ok=True)
    write_kit(ASSETS / "models" / "kit.glb")
    basic.glb(ASSETS / "models" / "crate.glb")
    basic.reed_glb(ASSETS / "models" / "reed.glb")
    basic.beacon_glb(ASSETS / "models" / "beacon.glb")
    basic.wav(ASSETS / "sounds" / "hum.wav", basic.hum())
    sky(ASSETS / "sky.hdr")
    (ASSETS / "materials").mkdir(parents=True, exist_ok=True)
    for name, (base, emissive) in EMISSIVE.items():
        document = {"version": 1, "baseColor": base, "emissive": emissive, "metallic": 0.0, "roughness": 0.4, "normalScale": 1.0,
                    "occlusionStrength": 1.0, "alphaCutoff": 0.5, "alphaMode": "Opaque", "wrap": "Repeat", "doubleSided": True, "textures": {}}
        (ASSETS / "materials" / f"{name}.material.json").write_text(json.dumps(document, indent=2) + "\n")
    (PROJECT / "scripts").mkdir(parents=True, exist_ok=True)
    (PROJECT / "scripts" / "camera_drift.lua").write_text(CAMERA_DRIFT)
    (PROJECT / "scenes").mkdir(parents=True, exist_ok=True)
    (PROJECT / "project.json").write_text(json.dumps({"name": "Showcase", "engineVersion": engine_version(), "startScene": "scenes/main.scene.json",
                                                      "assetRoots": ["assets", "scripts"]}, indent=2) + "\n")
    print("wrote", PROJECT)


def engine_version():
    text = (PROJECT.parent.parent.parent / "CMakeLists.txt").read_text()
    marker = "VERSION "
    start = text.index(marker, text.index("project(")) + len(marker)
    return text[start:text.index(" ", start)].strip()


# ---------------------------------------------------------------------------------------------
# Scene
# ---------------------------------------------------------------------------------------------

BUILTIN_BOX = "a464f023-844b-8938-9df4-75487155e36d"
NIL = "00000000-0000-0000-0000-000000000000"


def quat_yaw_pitch(yaw, pitch=0.0):
    """Yaw about Y, then pitch about X: the entity's -Z ends up along (-sin y cos p, sin p, -cos y cos p)."""
    cy, sy, cp, sp = math.cos(yaw / 2), math.sin(yaw / 2), math.cos(pitch / 2), math.sin(pitch / 2)
    return {"x": cy * sp, "y": sy * cp, "z": -sy * sp, "w": cy * cp}


def quat_axis(axis, angle):
    a = norm(axis)
    s = math.sin(angle / 2)
    return {"x": a[0] * s, "y": a[1] * s, "z": a[2] * s, "w": math.cos(angle / 2)}


def look_along(direction):
    f = norm(direction)
    return quat_yaw_pitch(math.atan2(-f[0], -f[2]), math.asin(f[1]))


def vec(x, y, z):
    return {"x": x, "y": y, "z": z}


class Sidecars:
    def __init__(self):
        self.cache = {}

    def load(self, relative):
        if relative not in self.cache:
            path = ASSETS.parent / relative if not relative.startswith("assets") else PROJECT / relative
            self.cache[relative] = json.loads((path.parent / (path.name + ".meta")).read_text())
        return self.cache[relative]

    def uuid(self, relative):
        return self.load(relative)["uuid"]

    def mesh(self, relative, name):
        for sub_asset in self.load(relative)["subAssets"]:
            if sub_asset["type"] == "Mesh" and sub_asset["name"] == name:
                return sub_asset["uuid"]
        raise KeyError(f"{relative} has no mesh {name}")


def write_scene():
    meta = Sidecars()
    kit = "assets/models/kit.glb"
    entities = []
    counter = [0]

    def new_uuid():
        counter[0] += 1
        return f"5a0c0000-0000-4000-8000-{counter[0]:012x}"

    def entity(name, position=(0, 0, 0), rotation=None, scale=(1, 1, 1), parent=None, prefab=None, **components):
        uuid = new_uuid()
        record = {"uuid": uuid, "name": name}
        if parent:
            record["parent"] = parent
        if prefab:
            record["prefab"] = prefab
        body = {"Transform": {"position": vec(*position), "rotation": rotation or {"x": 0, "y": 0, "z": 0, "w": 1}, "scale": vec(*scale)}}
        body.update(components)
        record["components"] = body
        entities.append(record)
        return uuid

    def group(name):
        return entity(name)

    def mesh_components(mesh, material=None, color=(1, 1, 1, 1), static=True):
        out = {"MeshRenderer": {"mesh": meta.mesh(kit, mesh), "material": meta.uuid(f"assets/materials/{material}.material.json") if material else NIL,
                                "color": {"x": color[0], "y": color[1], "z": color[2], "w": color[3]}, "visible": True}}
        if static:
            out["Static"] = None
        return out

    water_y = 0.27
    reflections = []

    def mirror_quat(q):
        return {"x": -q["x"], "y": q["y"], "z": -q["z"], "w": q["w"]}

    def reflect(name, mesh, position, rotation, scale, material=None, color=(1, 1, 1, 1), spin=None):
        """A copy mirrored in the water's plane, seen through the blended surface: the engine has no
        screen-space reflections, so the pool reflects what stands on it by geometry."""
        extra = {}
        if spin:
            extra["Spin"] = {"axis": vec(-spin["axis"]["x"], spin["axis"]["y"], -spin["axis"]["z"]), "speed": spin["speed"]}
        components = mesh_components(mesh, material, color, static=spin is None)
        components.update(extra)
        reflections.append((f"{name} (reflection)", (position[0], 2 * water_y - position[1], position[2]), mirror_quat(rotation),
                            (scale[0], -scale[1], scale[2]), components))

    def piece(name, mesh, position, parent, yaw=0.0, material=None, color=(1, 1, 1, 1), scale=(1, 1, 1), static=True, mirrored=False, **extra):
        components = mesh_components(mesh, material, color, static)
        components.update(extra)
        if mirrored:
            reflect(name, mesh, position, quat_yaw_pitch(yaw), scale, material, color)
        return entity(name, position, quat_yaw_pitch(yaw), scale, parent, **components)

    def point(name, position, parent, color, intensity, radius):
        return entity(name, position, None, (1, 1, 1), parent, PointLight={"color": vec(*color), "intensity": intensity, "range": radius})

    def spot(name, position, target, parent, color, intensity, radius, inner, outer):
        direction = sub(target, position)
        return entity(name, position, look_along(direction), (1, 1, 1), parent,
                      SpotLight={"color": vec(*color), "intensity": intensity, "range": radius, "innerAngle": inner, "outerAngle": outer})

    # Sky, sun and camera ---------------------------------------------------------------------
    entity("Sky", Environment={"map": meta.uuid("assets/sky.hdr"), "intensity": 1.0, "exposure": 1.0})
    entity("Sun", (0, 20, 0), look_along([-c for c in SUN]), DirectionalLight={"color": vec(1.0, 0.60, 0.34), "intensity": 4.6})
    entity("Camera", (0, 1.75, 12.5), quat_yaw_pitch(0.0, 0.045), Camera={"fovY": math.radians(62), "nearPlane": 0.1}, AudioListener=None,
           Script={"script": meta.uuid("scripts/camera_drift.lua")})

    # Ground, pool, steps ---------------------------------------------------------------------
    ground = group("Ground")
    piece("Paving west", "PavingWest", (-13.375, 0, -2.5), ground)
    piece("Paving east", "PavingEast", (13.375, 0, -2.5), ground)
    piece("Paving north", "PavingNorth", (0, 0, -19.625), ground)
    piece("Paving south", "PavingSouth", (0, 0, 15.125), ground)
    piece("Grass west", "GrassWest", (-112.0, -0.03, 0), ground)
    piece("Grass east", "GrassEast", (112.0, -0.03, 0), ground)
    piece("Grass north", "GrassNorth", (0, -0.03, -113.5), ground)
    piece("Grass south", "GrassSouth", (0, -0.03, 111.0), ground)
    piece("Basin floor", "BasinFloor", (0, -13.0, -2.0), ground)
    pool = group("Pool")
    piece("Rim west", "RimLong", (-2.5, 0, -2.0), pool)
    piece("Rim east", "RimLong", (2.5, 0, -2.0), pool)
    piece("Rim north", "RimShort", (0, 0, -12.25), pool)
    piece("Rim south", "RimShort", (0, 0, 8.25), pool)
    piece("Water", "Water", (0, water_y, -2.0), pool)
    steps = group("Steps")
    piece("Step low", "StepLow", (0, 0, -15.4), steps)
    piece("Step mid", "StepMid", (0, 0.3, -16.3), steps)
    piece("Step high", "StepHigh", (0, 0.6, -17.2), steps)

    # Armillary on its pedestal ---------------------------------------------------------------
    armillary = group("Armillary")
    piece("Pedestal", "Pedestal", (0, 0, -3.0), armillary, mirrored=True)
    piece("Gold sphere", "GoldSphere", (0, 3.35, -3.0), armillary, static=False, mirrored=True)
    for name, axis, speed, tilt in (("Ring A", (0, 1, 0), 0.5, 0.0), ("Ring B", (1, 0, 0.3), -0.35, 0.0), ("Ring C", (0.3, 0, 1), 0.22, 0.0)):
        spin = {"axis": vec(*norm(axis)), "speed": speed}
        reflect(name, "Ring" + name[-1], (0, 3.35, -3.0), quat_axis(axis, 0.6), (1, 1, 1), spin=spin)
        entity(name, (0, 3.35, -3.0), quat_axis(axis, 0.6), (1, 1, 1), armillary,
               Spin=spin, **mesh_components("Ring" + name[-1], static=False))
    point("Armillary glow", (0, 3.35, -3.0), armillary, (1.0, 0.72, 0.4), 22.0, 9.0)

    # Colonnades -------------------------------------------------------------------------------
    colonnade_z = [-14 + 4 * i for i in range(7)]
    warm, cool = (1.0, 0.70, 0.38), (0.55, 0.75, 1.0)
    banners = group("Banners")
    for side, x, yaw in (("West", -8.5, math.pi / 2), ("East", 8.5, math.pi / 2)):
        arcade = group(f"{side} colonnade")
        for i, z in enumerate(colonnade_z):
            piece(f"Column {i + 1}", "Column", (x, 0, z), arcade)
        for i in range(len(colonnade_z) - 1):
            zc = (colonnade_z[i] + colonnade_z[i + 1]) / 2
            piece(f"Arch {i + 1}", "ArcadeArch", (x, 4.2, zc), arcade, yaw=yaw)
        piece("Gallery roof", "Gallery", (x, 6.15, -2.0), arcade, yaw=0.0)
    lanterns = group("Hanging lanterns")
    for side, x, sign in (("West", -8.5, 1), ("East", 8.5, -1)):
        for i in range(len(colonnade_z) - 1):
            zc = (colonnade_z[i] + colonnade_z[i + 1]) / 2
            lx = x + sign * 0.85
            piece(f"Lantern {side} {i + 1}", "HangingLantern", (lx, 5.15, zc), lanterns)
            piece(f"Lantern {side} {i + 1} bulb", "Bulb", (lx, 4.82, zc), lanterns, material="bulb_warm")
            point(f"Lantern {side} {i + 1} light", (lx, 4.7, zc), lanterns, warm, 9.0, 7.5)
            colour = (0.85, 0.12, 0.10, 1.0) if i % 2 == 0 else (0.10, 0.45, 0.55, 1.0)
            piece(f"Banner {side} {i + 1}", "Banner", (x + sign * 0.66, 2.9, zc - 1.85), banners, yaw=math.pi / 2 * sign * -1 + (0 if sign > 0 else math.pi), color=colour, static=False)
    fairy = group("String lights")
    for side, x in (("West", -8.5), ("East", 8.5)):
        for i in range(27):
            z = -14.0 + i
            y = 6.75 + 0.05 * math.sin(i * 0.9)
            piece(f"Bulb {side} {i}", "BulbSmall", (x + (0.9 if side == "West" else -0.9), y, z), fairy, material="bulb_fairy")
            if i % 3 == 0:
                point(f"Fairy light {side} {i}", (x + (0.9 if side == "West" else -0.9), y, z), fairy, (1.0, 0.75, 0.45), 2.4, 4.0)

    # Lamp posts, benches ---------------------------------------------------------------------
    posts = group("Lamp posts")
    for side, x in (("West", -3.7), ("East", 3.7)):
        for i in range(5):
            z = -9.0 + 3.8 * i
            piece(f"Lamp {side} {i + 1}", "LampPost", (x, 0, z), posts, mirrored=abs(x) < 4)
            piece(f"Lamp {side} {i + 1} bulb", "Bulb", (x, 2.65, z), posts, material="bulb_warm", mirrored=abs(x) < 4)
            point(f"Lamp {side} {i + 1} light", (x, 2.6, z), posts, warm, 10.0, 6.5)
    benches = group("Benches")
    for side, x, yaw in (("West", -5.6, math.pi / 2), ("East", 5.6, -math.pi / 2)):
        for i, z in enumerate((-6.5, 0.0, 4.5)):
            piece(f"Bench {side} {i + 1}", "Bench", (x, 0, z), benches, yaw=yaw)

    # Candles along the rim ------------------------------------------------------------------
    candles = group("Rim candles")
    for side, x in (("West", -2.5), ("East", 2.5)):
        for i in range(21):
            z = -11.5 + 0.95 * i
            piece(f"Candle {side} {i + 1}", "BulbSmall", (x, 0.46, z), candles, material="bulb_fairy", mirrored=True)
            if i % 2 == 0:
                point(f"Candle {side} {i + 1} light", (x, 0.6, z), candles, (1.0, 0.68, 0.34), 1.6, 3.0)

    # Gate, wings, towers ---------------------------------------------------------------------
    gate = group("Gate")
    fz = -20.0
    piece("Gate arch", "GateArch", (0, 5.0, fz), gate)
    piece("Jamb west", "GateJamb", (-(3.0 + 1.8), 0, fz), gate)
    piece("Jamb east", "GateJamb", (3.0 + 1.8, 0, fz), gate)
    piece("Wing west", "Wing", (-(6.6 + 4.5), 0, fz), gate)
    piece("Wing east", "Wing", (6.6 + 4.5, 0, fz), gate)
    piece("Cornice", "Cornice", (0, 9.0, fz), gate)
    entity("Glow", (0, 0.05, fz - 1.4), None, (1, 1, 1), gate, MeshRenderer={
        "mesh": BUILTIN_BOX, "material": meta.uuid("assets/materials/gate_glow.material.json"), "color": {"x": 1, "y": 1, "z": 1, "w": 1}, "visible": True},
        Static=None)
    entities[-1]["components"]["Transform"]["position"] = vec(0, 4.2, fz - 1.4)
    entities[-1]["components"]["Transform"]["scale"] = vec(6.4, 8.4, 0.2)
    point("Gate light low", (0, 1.5, fz + 0.2), gate, (1.0, 0.55, 0.28), 30.0, 12.0)
    point("Gate light high", (0, 6.0, fz + 0.2), gate, (1.0, 0.45, 0.30), 26.0, 12.0)
    windows = group("Windows")
    for side, sign in (("West", -1), ("East", 1)):
        for col, x in enumerate((8.6, 11.1, 13.6)):
            for row, y in enumerate((1.6, 5.4)):
                piece(f"Window {side} {col + 1}.{row + 1}", "WindowPanel", (sign * x, y, fz + 1.21), windows, material="window_glow")
                piece(f"Sill {side} {col + 1}.{row + 1}", "Sill", (sign * x, y - 0.14, fz + 1.4), windows)
    towers = group("Towers")
    for side, x in (("West", -16.5), ("East", 16.5)):
        piece(f"Tower {side}", "Tower", (x, 0, fz + 0.5), towers)
        piece(f"Tower {side} roof", "TowerRoof", (x, 13.0, fz + 0.5), towers, material=None)
        spot(f"Tower {side} spot", (x + (2.6 if side == "West" else -2.6), 10.0, fz + 3.0), (0, 3.0, -3.0), towers, cool, 260.0, 30.0, 0.10, 0.22)

    # Trees, planters, cypresses -------------------------------------------------------------
    trees = group("Trees")
    rng = random.Random(3)
    tree_spots = [(-13.5, -6.0), (13.5, -6.0), (-13.5, 3.0), (13.5, 3.0), (-12.0, 10.0), (12.0, 10.0), (-8.0, -19.0 + 8.0), (8.0, -11.0)]
    for i, (x, z) in enumerate(tree_spots):
        piece(f"Planter {i + 1}", "Planter", (x, 0, z), trees, yaw=rng.random() * 6)
        piece(f"Trunk {i + 1}", "Trunk", (x, 0.6, z), trees, yaw=rng.random() * 6)
        piece(f"Canopy {i + 1}", "Canopy", (x, 4.1, z), trees, yaw=rng.random() * 6, scale=(1.0, 0.9, 1.0), static=True)
        piece(f"Canopy {i + 1}b", "CanopySmall", (x + 0.9, 3.4, z + 0.5), trees, yaw=rng.random() * 6)
    skyline = group("Cypress ring")
    for i in range(70):
        angle = i / 70 * 2 * math.pi + rng.uniform(-0.03, 0.03)
        distance = rng.uniform(34, 52)
        x, z = distance * math.cos(angle), distance * math.sin(angle) - 8.0
        height = rng.uniform(0.9, 1.5)
        piece(f"Cypress {i + 1}", "Cypress", (x, 0, z), skyline, yaw=rng.random() * 6, scale=(height * 0.9, height, height * 0.9))

    # Props from the basic sample -------------------------------------------------------------
    props = group("Props")
    crate = meta.uuid("assets/models/crate.glb")
    for i, (x, y, z, yaw) in enumerate(((11.2, 0.5, 4.6, 0.4), (11.2, 0.5, 5.7, -0.2), (11.3, 1.5, 5.1, 0.9))):
        entity(f"Crate {i + 1}", (x, y, z), quat_yaw_pitch(yaw), (1, 1, 1), props, prefab=crate)
    beacon = meta.uuid("assets/models/beacon.glb")
    for side, x in (("West", -3.4), ("East", 3.4)):
        entity(f"Beacon {side}", (x, 0.9, -11.0), None, (1, 1, 1), props, prefab=beacon,
               AudioSource={"sound": meta.uuid("assets/sounds/hum.wav"), "volume": 0.4, "pitch": 1.0, "playing": True, "loop": True, "spatial": True,
                            "minDistance": 2.0, "maxDistance": 25.0})

    refl = group("Reflections")
    for name, position, rotation, scale, components in reflections:
        entity(name, position, rotation, scale, refl, **components)

    # Parents come first in the file: the loader resolves them in order.
    (PROJECT / "scenes" / "main.scene.json").write_text(json.dumps({"version": 2, "entities": entities}, indent=2) + "\n")
    lights = sum(1 for e in entities if "PointLight" in e["components"] or "SpotLight" in e["components"])
    print(f"main.scene.json: {len(entities)} entities, {lights} lights")


if __name__ == "__main__":
    step = sys.argv[1] if len(sys.argv) > 1 else ""
    if step == "assets":
        write_assets()
    elif step == "scene":
        write_scene()
    else:
        sys.exit(__doc__)
