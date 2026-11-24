#!/usr/bin/env python3
"""Spike (#591, ADR-0046): the hair coverage candidates under TAA, measured in code.

`capture` runs the player once per frame wanted, each a fresh process whose history is cut at the start,
so frame n of one configuration is the same image whenever it is taken; `analyze` turns the frames into
the two metrics ADR-0046 names:

  stability  per-pixel temporal standard deviation over eight consecutive TAA phases of a camera at rest,
             over the hairline box (mean and 99th percentile, 8-bit sRGB levels);
  trail      after an orbit that stops, each frame's per-pixel difference from the converged frame of the
             same pose over the head box (mean / 99th percentile), against a floor: eight converged
             frames, one per phase, against the same reference.

The camera of `spike_orbit_stop` turns at 90 degrees a second for 0.5 s and stops; the pose is frozen
from 0.25 s so a still capture is the same pose (apps/samples/character/scripts).

  hair_taa_metrics.py capture OUTDIR [player flags...]
  hair_taa_metrics.py analyze OUTDIR [PLAIN_DIR STOCHASTIC_DIR]   (the two directories define the hairline band)
"""
import concurrent.futures
import os
import subprocess
import sys

import numpy as np
from PIL import Image

PLAYER = os.environ.get("SONNET_PLAYER", "build/linux-release/apps/player/sonnet_player")
PROJECT = "apps/samples/character"
SCENE = os.environ.get("HAIR_SCENE", "scenes/spike_orbit_stop.scene.json")
SIZE = "1280x720"
STOP = int(os.environ.get("HAIR_STOP_FRAME", "28"))  # the last frame the camera still moves, as a frame count
STABLE_FIRST = 200  # eight consecutive frames from here, well after the history settled
REFERENCE = 320
HEAD_BOX = tuple(int(v) for v in os.environ.get("HAIR_HEAD_BOX", "480,120,780,420").split(","))
HAIRLINE_BOX = tuple(int(v) for v in os.environ.get("HAIR_HAIRLINE_BOX", "510,160,740,320").split(","))
TRAIL_OFFSETS = (0, 1, 2, 3, 4, 6, 8, 12)


def frames_wanted():
    stable = [("st", STABLE_FIRST + j) for j in range(8)]
    trail = [("tr", STOP + k) for k in TRAIL_OFFSETS]
    floor = [("fl", REFERENCE + 8 + j) for j in range(8)]
    return stable + trail + floor + [("ref", REFERENCE)]


def run(outdir, label, n, flags):
    path = os.path.join(outdir, f"{label}-{n}.png")
    if os.path.exists(path):
        return
    # --play S draws ceil(S / (1/60)) frames: ask for n of them with half a frame to spare.
    cmd = [PLAYER, PROJECT, "--scene", SCENE, "--headless", "on", "--render-size", SIZE, "--play", f"{(n - 0.5) / 60:.6f}",
           "--screenshot", path, *flags]
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=300)


def capture(outdir, flags):
    os.makedirs(outdir, exist_ok=True)
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        list(pool.map(lambda w: run(outdir, w[0], w[1], flags), frames_wanted()))


def load(outdir, label, n, box):
    return np.asarray(Image.open(os.path.join(outdir, f"{label}-{n}.png")).convert("RGB").crop(box)).astype(np.float64)


def band_mask(plain_dir, stochastic_dir):
    """The hairline band: where a converged stochastic-coverage frame differs from the converged cutoff
    frame of the same pose (the partly covered fringe and silhouette), grown by two pixels, in HEAD_BOX."""
    a = load(plain_dir, "ref", REFERENCE, HEAD_BOX)
    b = load(stochastic_dir, "ref", REFERENCE, HEAD_BOX)
    band = (np.abs(a - b).max(axis=2) > 12.0).astype(np.float64)
    for _ in range(2):
        padded = np.pad(band, 1)
        band = np.max([padded[y : y + band.shape[0], x : x + band.shape[1]] for y in range(3) for x in range(3)], axis=0)
    return band > 0.5


def analyze(outdir, quiet=False, band=None):
    """Stability over the hairline band, trail and floor over the head box; with `band` (a mask in HEAD_BOX
    coordinates) the trail and floor are also given over the band alone."""
    box = HEAD_BOX
    stack = np.stack([load(outdir, "st", STABLE_FIRST + j, box) for j in range(8)])
    std = stack.std(axis=0).mean(axis=2)
    box_std = std[HAIRLINE_BOX[1] - box[1] : HAIRLINE_BOX[3] - box[1], HAIRLINE_BOX[0] - box[0] : HAIRLINE_BOX[2] - box[0]]
    result = {"stab_box_mean": float(box_std.mean()), "stab_box_p99": float(np.percentile(box_std, 99))}
    if band is not None:
        result["band_pixels"] = int(band.sum())
        result["stab_mean"] = float(std[band].mean())
        result["stab_p99"] = float(np.percentile(std[band], 99))
    else:
        result["stab_mean"], result["stab_p99"] = result["stab_box_mean"], result["stab_box_p99"]
    ref = load(outdir, "ref", REFERENCE, box)
    pick = (lambda d: d[band]) if band is not None else (lambda d: d)
    trail = {}
    for k in TRAIL_OFFSETS:
        diff = pick(np.abs(load(outdir, "tr", STOP + k, box) - ref).mean(axis=2))
        trail[k] = (float(diff.mean()), float(np.percentile(diff, 99)))
    floors = [pick(np.abs(load(outdir, "fl", REFERENCE + 8 + j, box) - ref).mean(axis=2)) for j in range(8)]
    result["trail"] = trail
    result["floor_mean"] = float(np.mean([f.mean() for f in floors]))
    result["floor_p99"] = float(np.mean([np.percentile(f, 99) for f in floors]))
    result["floor_p99_max"] = float(max(np.percentile(f, 99) for f in floors))
    if not quiet:
        print(f"stability ({'hairline band' if band is not None else 'hairline box'}, 8 phases at rest): mean {result['stab_mean']:.2f}, p99 {result['stab_p99']:.2f}")
        print("trail (frames after the stop: mean / p99): " + "  ".join(f"{k}: {m:.2f}/{p:.1f}" for k, (m, p) in trail.items()))
        print(f"floor over eight converged phases: mean {result['floor_mean']:.2f}, p99 {result['floor_p99']:.1f} (worst phase {result['floor_p99_max']:.1f})")
    return result


if __name__ == "__main__":
    if len(sys.argv) >= 3 and sys.argv[1] == "capture":
        capture(sys.argv[2], sys.argv[3:])
    elif len(sys.argv) in (3, 5) and sys.argv[1] == "analyze":
        analyze(sys.argv[2], band=band_mask(sys.argv[3], sys.argv[4]) if len(sys.argv) == 5 else None)
    else:
        sys.exit(__doc__)
