#!/usr/bin/env python3
"""Writes apps/samples/basic/scenes/stress.scene.json, the README's performance target as a scene.

The scene is a sun, a sky, a camera and one entity running scripts/stress.lua, which builds the
ten thousand draws and a hundred lights when it plays, so the file stays a few kilobytes. Run it
from anywhere; it overwrites the scene file.
"""
import json
import math
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "apps/samples/basic/scenes/stress.scene.json"
SKY = "0238c936-ba15-474c-a8a7-53a87650359e"
STRESS_SCRIPT = "5c0a1b2c-0003-4d4e-9f00-000000000008"


def vec(x, y, z):
    return {"x": x, "y": y, "z": z}


def pitch(degrees):
    half = math.radians(degrees) / 2
    return {"x": round(math.sin(half), 6), "y": 0.0, "z": 0.0, "w": round(math.cos(half), 6)}


def entity(name, position, components, rotation=None):
    transform = {"position": vec(*position), "rotation": rotation or {"x": 0.0, "y": 0.0, "z": 0.0, "w": 1.0},
                 "scale": vec(1, 1, 1)}
    return {"uuid": str(uuid.uuid5(uuid.NAMESPACE_URL, "sonnet/stress/" + name)), "name": name,
            "components": {"Transform": transform, **components}}


# The camera and the sun are the benchmark's: above the grid's near edge, pitched down 18 degrees.
entities = [
    entity("Camera", (0, 12, 40), {"Camera": {"fovY": 1.0, "nearPlane": 0.1}, "AudioListener": None}, pitch(-18)),
    entity("Sun", (0, 8, 0), {"DirectionalLight": {"color": vec(1.0, 0.96, 0.9), "intensity": 3.0}}, pitch(-50)),
    entity("Sky", (0, 0, 0), {"Environment": {"map": SKY, "intensity": 1.0, "exposure": 1.0}}),
    entity("Stress", (0, 0, 0), {"Scripts": {"slots": [{"script": STRESS_SCRIPT, "properties": {}}]}}),
]

OUT.write_text(json.dumps({"version": 3, "entities": entities}, indent=2) + "\n")
print(f"wrote {OUT.relative_to(ROOT)}: {len(entities)} entities")
