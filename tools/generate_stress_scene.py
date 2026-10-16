#!/usr/bin/env python3
"""Writes the basic sample's two stress scenes, the performance targets as scenes.

scenes/stress.scene.json is the README's target: a sun, a sky, a camera and one entity running
scripts/stress.lua, which builds the ten thousand draws and a hundred lights when it plays.
scenes/hud_stress.scene.json is M15's HUD budget: a sky, a camera and a canvas running
scripts/hud_stress.lua, which builds two hundred elements, a third of them labels. Both build
their content in a script so the files stay a few kilobytes. Run it from anywhere; it overwrites
both scene files.
"""
import json
import math
import uuid
from pathlib import Path

import scene_json

ROOT = Path(__file__).resolve().parent.parent
SCENES = ROOT / "apps/samples/basic/scenes"
SKY = "0238c936-ba15-474c-a8a7-53a87650359e"
STRESS_SCRIPT = "5c0a1b2c-0003-4d4e-9f00-000000000008"
HUD_STRESS_SCRIPT = "5c0a1b2c-0003-4d4e-9f00-000000000070"


# Scene version 5 introduced vectors and quaternions as arrays, x, y, z, w (ADR-0029).
def vec(x, y, z):
    return [x, y, z]


def pitch(degrees):
    half = math.radians(degrees) / 2
    return [round(math.sin(half), 6), 0.0, 0.0, round(math.cos(half), 6)]


def entity(name, position, components, rotation=None, scene="stress"):
    transform = {"position": vec(*position), "rotation": rotation or [0.0, 0.0, 0.0, 1.0],
                 "scale": vec(1, 1, 1)}
    return {"uuid": str(uuid.uuid5(uuid.NAMESPACE_URL, f"sonnet/{scene}/" + name)), "name": name,
            "components": {"Transform": transform, **components}}


def write(name, entities):
    out = SCENES / f"{name}.scene.json"
    out.write_text(scene_json.dumps({"version": 6, "entities": entities}) + "\n")
    print(f"wrote {out.relative_to(ROOT)}: {len(entities)} entities")


# The camera and the sun are the benchmark's: above the grid's near edge, pitched down 18 degrees.
write("stress", [
    entity("Camera", (0, 12, 40), {"Camera": {"fovY": 1.0, "nearPlane": 0.1}, "AudioListener": None}, pitch(-18)),
    entity("Sun", (0, 8, 0), {"DirectionalLight": {"color": vec(1.0, 0.96, 0.9), "intensity": 3.0}}, pitch(-50)),
    entity("Sky", (0, 0, 0), {"Environment": {"map": SKY, "intensity": 1.0}}),
    entity("Stress", (0, 0, 0), {"Scripts": {"slots": [{"script": STRESS_SCRIPT, "properties": {}}]}}),
])

# The HUD's own cost is the overlay pass's, so the scene behind it is only a sky. The canvas scales
# by width, so the whole grid is on screen in portrait too, in the top third of a phone.
write("hud_stress", [
    entity("Camera", (0, 2, 6), {"Camera": {"fovY": 1.0, "nearPlane": 0.1}, "AudioListener": None}, scene="hud_stress"),
    entity("Sky", (0, 0, 0), {"Environment": {"map": SKY, "intensity": 1.0}}, scene="hud_stress"),
    entity("HUD", (0, 0, 0), {
        "GuiCanvas": {"referenceSize": [1920, 1080], "match": 0.0, "safeArea": False, "order": 0},
        "Scripts": {"slots": [{"script": HUD_STRESS_SCRIPT, "properties": {}}]},
    }, scene="hud_stress"),
])
