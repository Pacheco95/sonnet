#!/usr/bin/env python3
"""Spike (#591): does the shader's strand direction follow the painted strands on the screen?

Reads three captures of one view of the head from behind (`--shading-term albedo`, `normal` and
`curvature`, which shows the strand direction of a Hair draw as colour), takes the dominant orientation
of the painted base colour on the screen (a structure tensor of the albedo's luminance), and the strand
direction the shader derived, projected on the screen, and reports the mean |cos| of the angle between
the two axes: about 0.64 for unrelated axes, towards 1 when they agree.

Usage: hair_strand_alignment.py ALBEDO.png NORMAL.png STRAND.png [X0 Y0 X1 Y1]   (the box holds the head)
"""
import sys

import numpy as np
from PIL import Image


def box(a, r, axis):
    n = a.shape[axis]
    pad = [(0, 0)] * a.ndim
    pad[axis] = (r + 1, r)
    c = np.cumsum(np.pad(a, pad, mode="edge"), axis=axis, dtype=np.float64)
    return (np.take(c, range(2 * r + 1, 2 * r + 1 + n), axis=axis) - np.take(c, range(0, n), axis=axis)) / (2 * r + 1)


def blur(a, r):
    for _ in range(3):
        a = box(box(a, r, 0), r, 1)
    return a


def load(path):
    return np.asarray(Image.open(path).convert("RGB")).astype(np.float64) / 255.0


albedo, normal, strand = (load(p) for p in sys.argv[1:4])
h, w, _ = strand.shape
mask = (strand[..., 2] > 0.12) & (strand[..., 0] > 0.0) & (strand.sum(axis=2) > 0.4)
# Hair only: the curvature term paints skin in red alone, and everything else black.
mask &= strand[..., 1] > 0.02
if len(sys.argv) >= 8:
    x0, y0, x1, y1 = (int(a) for a in sys.argv[4:8])
    box_mask = np.zeros_like(mask)
    box_mask[y0:y1, x0:x1] = True
    mask &= box_mask
ys, xs = np.nonzero(mask)
print(f"hair pixels: {mask.sum()}")

# Which world direction is the screen's right: the world normal's x grows across the head.
nx = normal[..., 0] * 2 - 1
sx = np.sign(np.corrcoef(xs, nx[ys, xs])[0, 1]) or 1.0
print(f"screen right is world {'+' if sx > 0 else '-'}x")

world = strand * 2 - 1
sv = np.stack([sx * world[..., 0], -world[..., 1]], axis=-1)  # screen x right, y down
theta_strand = np.arctan2(sv[..., 1], sv[..., 0])

luma = albedo @ np.array([0.2126, 0.7152, 0.0722])
s = blur(luma, 1)
gx = (np.roll(s, -1, 1) - np.roll(s, 1, 1)) * 0.5
gy = (np.roll(s, -1, 0) - np.roll(s, 1, 0)) * 0.5
m = mask.astype(np.float64)
win = 5
jxx, jxy, jyy = (blur(a * m, win) / np.maximum(blur(m, win), 1e-6) for a in (gx * gx, gx * gy, gy * gy))
coherence = np.sqrt((jxx - jyy) ** 2 + 4 * jxy**2) / (jxx + jyy + 1e-12)
theta_paint = 0.5 * np.arctan2(2 * jxy, jxx - jyy) + np.pi / 2

for lo in (0.0, 0.3, 0.5):
    sel = mask & (coherence > lo)
    c = np.abs(np.cos(theta_strand[sel] - theta_paint[sel]))
    print(f"coherence > {lo:.1f}: {sel.sum():6d} pixels, mean |cos| {c.mean():.3f}, share within 30 degrees {np.mean(c > np.cos(np.pi / 6)):.2f}")
