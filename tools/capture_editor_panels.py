#!/usr/bin/env python3
"""Capture the editor's panels for docs/reports/editor-ui-widgets.md.

Each capture is one `--screenshot-window` run of the editor on the basic sample's playground scene, at the
default 1600x900 window a capture run opens, cropped to the panel it shows. The run opens a real window, so it
needs a desktop (X11 here: DISPLAY=:1); on this machine hold the desktop lock while it runs:

    flock ~/sonnet-measurements/.desktop.lock python3 tools/capture_editor_panels.py \
        --out ~/sonnet-measurements/572-inspector-captures/after

The window captures and their logs go to `--out`, and the crops to `--out`/crops. With `--before DIR`, a
directory an earlier run of this script wrote, each crop is the earlier one and this one side by side (the
inspector) or one above the other (the wide panels), before first, as the report shows them; the captures that
DIR does not have are cropped alone. Copy the crops the report uses into docs/reports/editor-ui-widgets/.

Needs Pillow.
"""

import argparse
import os
import subprocess
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
SCENE = ["apps/samples/basic", "--scene", "scenes/playground.scene.json"]

# name: (editor flags, crop box (left, top, width, height) in the 1600x900 window, how a pair is laid out)
INSPECTOR = (1260, 40, 340, 420)
BOTTOM = (0, 700, 1600, 200)
CAPTURES = {
    "inspector": (["--select", "Ball"], INSPECTOR, "side"),
    "inspector-script": (["--select", "Spawner"], INSPECTOR, "side"),
    "inspector-prefab-instance": (["--select", "Pallet/Crate A/Lid"], INSPECTOR, "side"),
    "log": (["--select", "Ball", "--front", "log"], BOTTOM, "stack"),
    "assets": (["--assets", "grid"], BOTTOM, "stack"),
}


def crop(path: Path, box: tuple[int, int, int, int]) -> Image.Image:
    left, top, width, height = box
    return Image.open(path).convert("RGBA").crop((left, top, left + width, top + height))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--editor", type=Path, default=ROOT / "build/linux-debug/apps/editor/sonnet_editor")
    parser.add_argument("--out", type=Path, required=True, help="directory for the window captures and logs")
    parser.add_argument("--before", type=Path, help="an earlier --out to put beside each crop")
    parser.add_argument("--only", nargs="+", choices=sorted(CAPTURES), help="capture only these")
    args = parser.parse_args()

    out = args.out.resolve()
    (out / "crops").mkdir(parents=True, exist_ok=True)
    env = dict(os.environ)
    env.setdefault("DISPLAY", ":1")
    failed = False
    for name in args.only or CAPTURES:
        flags, box, layout = CAPTURES[name]
        window = out / f"{name}.png"
        command = [str(args.editor.resolve()), *SCENE, *flags, "--screenshot-window", str(window)]
        with open(out / f"{name}.log", "w") as log:
            result = subprocess.run(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT)
        if result.returncode != 0:
            print(f"{name}: the editor exited with {result.returncode}, see {out / name}.log", file=sys.stderr)
            failed = True
            continue
        after = crop(window, box)
        earlier = args.before.resolve() / f"{name}.png" if args.before else None
        if earlier is not None and earlier.exists():
            before = crop(earlier, box)
            width, height = box[2], box[3]
            size = (2 * width, height) if layout == "side" else (width, 2 * height)
            pair = Image.new("RGBA", size)
            pair.paste(before, (0, 0))
            pair.paste(after, (width, 0) if layout == "side" else (0, height))
            after = pair
        after.save(out / "crops" / f"{name}-crop.png", optimize=True)
        print(out / "crops" / f"{name}-crop.png")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
