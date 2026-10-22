#!/usr/bin/env python3
"""Make the report's alpha-shadow scene variants in a new temporary directory.

Run from the repository root: python3 docs/reports/kosmickrisp-macos/prepare.py DEST
"""
import json
from pathlib import Path
import shutil
import sys
import uuid

project = Path(sys.argv[1])
shutil.copytree("apps/samples/basic", project, ignore=shutil.ignore_patterns(".sonnet"))
for name, alpha in (("clear", 0), ("glass", 0.5), ("solid", 1)):
    identity = str(uuid.uuid5(uuid.NAMESPACE_URL, "sonnet-368/" + name))
    material = {
        "version": 1, "baseColor": [0.8, 0.9, 1, alpha], "metallic": 0,
        "roughness": 0.1, "alphaMode": "Blend", "textures": {},
    }
    path = project / "assets" / f"{name}.material.json"
    path.write_text(json.dumps(material, indent=2) + "\n")
    Path(str(path) + ".meta").write_text(json.dumps({
        "version": 1, "uuid": identity, "type": "Material", "settings": {},
    }, indent=2) + "\n")
    scene = json.loads((project / "scenes/main.scene.json").read_text())
    for entity in scene["entities"]:
        if entity["name"] == "Sphere":
            entity["components"]["MeshRenderer"].update(material=identity, color=[1, 1, 1, 1])
    (project / "scenes" / f"{name}.scene.json").write_text(json.dumps(scene, indent=2) + "\n")
