#!/usr/bin/env python3
"""Writes apps/samples/basic/scenes/lighting.scene.json, the lighting and shadows showcase.

The scene uses the built-in primitives and the basic sample's ground material, so it needs no new
assets. Run it from anywhere; it overwrites the scene file. Entity UUIDs are derived from names so
the file is stable between runs.
"""
import json
import math
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "apps/samples/basic/scenes/lighting.scene.json"

BOX = "a464f023-844b-8938-9df4-75487155e36d"
SPHERE = "fe3d3b79-c4ee-8f48-8944-6b1596b2c621"
CYLINDER = "4982c24f-bd36-8c92-a2c1-47e047fbe1b6"
CAPSULE = "663df5e4-bf07-8eb4-9364-2db2a5a3e28d"
PLANE = "bb8714b8-63f4-81c4-86ef-ad99a6ff9d40"
GROUND_MATERIAL = "60abe124-b44f-4f02-a0c3-6d2d3c94ee6d"
SKY = "0238c936-ba15-474c-a8a7-53a87650359e"
NO_MATERIAL = "00000000-0000-0000-0000-000000000000"


def vec(x, y, z):
    return {"x": x, "y": y, "z": z}


def look(forward):
    """Quaternion that turns -Z onto `forward`, keeping +Y up."""
    f = [c / math.sqrt(sum(k * k for k in forward)) for c in forward]
    up = (0.0, 1.0, 0.0) if abs(f[1]) < 0.999 else (0.0, 0.0, 1.0)
    r = [up[1] * f[2] - up[2] * f[1], up[2] * f[0] - up[0] * f[2], up[0] * f[1] - up[1] * f[0]]
    n = math.sqrt(sum(k * k for k in r))
    r = [k / n for k in r]
    u = [f[1] * r[2] - f[2] * r[1], f[2] * r[0] - f[0] * r[2], f[0] * r[1] - f[1] * r[0]]
    z = [-k for k in f]
    m = [[r[0], u[0], z[0]], [r[1], u[1], z[1]], [r[2], u[2], z[2]]]
    t = m[0][0] + m[1][1] + m[2][2]
    if t > 0:
        s = math.sqrt(t + 1) * 2
        q = ((m[2][1] - m[1][2]) / s, (m[0][2] - m[2][0]) / s, (m[1][0] - m[0][1]) / s, s / 4)
    elif m[0][0] > m[1][1] and m[0][0] > m[2][2]:
        s = math.sqrt(1 + m[0][0] - m[1][1] - m[2][2]) * 2
        q = (s / 4, (m[0][1] + m[1][0]) / s, (m[0][2] + m[2][0]) / s, (m[2][1] - m[1][2]) / s)
    elif m[1][1] > m[2][2]:
        s = math.sqrt(1 + m[1][1] - m[0][0] - m[2][2]) * 2
        q = ((m[0][1] + m[1][0]) / s, s / 4, (m[1][2] + m[2][1]) / s, (m[0][2] - m[2][0]) / s)
    else:
        s = math.sqrt(1 + m[2][2] - m[0][0] - m[1][1]) * 2
        q = ((m[0][2] + m[2][0]) / s, (m[1][2] + m[2][1]) / s, s / 4, (m[1][0] - m[0][1]) / s)
    return {"x": round(q[0], 6), "y": round(q[1], 6), "z": round(q[2], 6), "w": round(q[3], 6)}


entities = []


def add(name, position, components, forward=None, scale=(1, 1, 1)):
    rotation = look(forward) if forward else {"x": 0.0, "y": 0.0, "z": 0.0, "w": 1.0}
    components = {"Transform": {"position": vec(*position), "rotation": rotation, "scale": vec(*scale)}, **components}
    entities.append({"uuid": str(uuid.uuid5(uuid.NAMESPACE_URL, "sonnet/lighting/" + name)), "name": name,
                     "components": components})


def mesh(kind, color, material=NO_MATERIAL, static=True):
    c = {"MeshRenderer": {"mesh": kind, "material": material, "color": {"x": color[0], "y": color[1], "z": color[2],
                                                                       "w": 1.0}, "visible": True}}
    if static:
        c["Static"] = None
    return c


def light(kind, color, intensity, **extra):
    return {kind: {"color": vec(*color), "intensity": intensity, **extra}}


# The stage: a ground, a back wall and a side wall for the shadows to land on.
add("Ground", (0, 0, 0), mesh(PLANE, (1, 1, 1), GROUND_MATERIAL), scale=(18, 1, 18))
add("Back wall", (0, 2.5, -6.5), mesh(BOX, (0.82, 0.8, 0.78)), scale=(18, 5, 0.4))
add("Side wall", (-8.8, 2.5, 0), mesh(BOX, (0.78, 0.8, 0.84)), scale=(0.4, 5, 13))

# Occluders of different shapes, near and far from the lights.
add("Pillar", (-5.5, 1.5, -2.5), mesh(BOX, (0.9, 0.9, 0.9)), scale=(0.9, 3, 0.9))
add("Column", (5.5, 1.25, -2.5), mesh(CYLINDER, (0.9, 0.85, 0.7)), scale=(1.0, 2.5, 1.0))
add("Big sphere", (-3, 0.9, 0.5), mesh(SPHERE, (0.9, 0.35, 0.3)), scale=(1.8, 1.8, 1.8))
add("Capsule", (3, 0.5, 0.5), mesh(CAPSULE, (0.3, 0.7, 0.9)))
add("Plinth", (0, 0.25, 1), mesh(BOX, (0.85, 0.85, 0.85)), scale=(2.4, 0.5, 2.4))
add("Spinning box", (0, 1.15, 1), {**mesh(BOX, (0.95, 0.75, 0.2), static=False),
                                   "Spin": {"axis": vec(0, 1, 0), "speed": 0.7}}, scale=(1.3, 1.3, 1.3))
add("Floating sphere", (-1.5, 3.2, -2.5), mesh(SPHERE, (0.5, 0.85, 0.5), static=False), scale=(0.8, 0.8, 0.8))
add("Bench", (3.5, 0.2, 3.2), mesh(BOX, (0.6, 0.45, 0.35)), scale=(3, 0.4, 0.8))
add("Step", (-4, 0.15, 3.5), mesh(BOX, (0.7, 0.7, 0.75)), scale=(2, 0.3, 2))

# The sun: low and warm, from the right and behind the camera, so the cascades cast long shadows.
add("Sun", (0, 8, 0), light("DirectionalLight", (1.0, 0.82, 0.62), 2.2), forward=(-0.55, -0.45, -0.7))
add("Sky", (0, 0, 0), {"Environment": {"map": SKY, "intensity": 0.25, "exposure": 1.0}})

# Three coloured spot lights from above, each aimed at the plinth's side, overlapping on the
# objects and casting three shadows apart from each other.
spots = [("Red spot", (-7, 5, 4), (0.95, 0.15, 0.1), (4.5, -4.0, -4.5)),
         ("Green spot", (0, 6, 8), (0.2, 0.9, 0.25), (0, -4.8, -7.0)),
         ("Blue spot", (7, 5, 4), (0.15, 0.3, 1.0), (-4.5, -4.0, -4.5))]
for name, position, color, forward in spots:
    add(name, position, light("SpotLight", color, 900.0, range=18.0, innerAngle=0.18, outerAngle=0.36,
                              castsShadows=True), forward=forward)

# Two point lights among the occluders: each casts a shadow in every direction.
add("Warm lamp", (-1.5, 1.6, 3.8), light("PointLight", (1.0, 0.65, 0.3), 30.0, range=9.0, castsShadows=True))
add("Cool lamp", (3.0, 2.8, -1.0), light("PointLight", (0.4, 0.7, 1.0), 35.0, range=9.0, castsShadows=True))

add("Camera", (0, 4.4, 12.5), {"Camera": {"fovY": 1.0, "nearPlane": 0.1}, "AudioListener": None},
    forward=(0, -0.28, -1))

OUT.write_text(json.dumps({"version": 2, "entities": entities}, indent=2) + "\n")
print(f"wrote {OUT.relative_to(ROOT)}: {len(entities)} entities")
