#!/usr/bin/env python3
"""Spike (#591): is there a seam in the strand direction at the mirror line of the hair's UV islands?

Reads the `--shading-term curvature` capture of the head from behind (a Hair draw shows its strand
direction as colour), decodes the world direction per pixel and reports, per column, the mean angle
between the pixel and its neighbour four pixels to the right over the head's rows. A seam shows as a
column far above its neighbours.

Usage: hair_seam_profile.py STRAND.png X0 Y0 X1 Y1
"""
import sys

import numpy as np
from PIL import Image

img = np.asarray(Image.open(sys.argv[1]).convert("RGB")).astype(np.float64) / 255.0
x0, y0, x1, y1 = (int(a) for a in sys.argv[2:6])
v = img[y0:y1, x0:x1] * 2 - 1
mask = (img[y0:y1, x0:x1, 2] > 0.12) & (img[y0:y1, x0:x1, 1] > 0.02)
n = np.maximum(np.linalg.norm(v, axis=2, keepdims=True), 1e-9)
u = v / n
step = 4
cos = np.clip((u[:, :-step] * u[:, step:]).sum(axis=2), -1, 1)
ang = np.degrees(np.arccos(cos))
both = mask[:, :-step] & mask[:, step:]
profile = np.array([ang[:, c][both[:, c]].mean() if both[:, c].sum() > 20 else np.nan for c in range(ang.shape[1])])
valid = ~np.isnan(profile)
med = np.nanmedian(profile)
print(f"columns: {valid.sum()}, median neighbour angle {med:.1f} deg, p95 {np.nanpercentile(profile, 95):.1f}, max {np.nanmax(profile):.1f} "
      f"at column {x0 + int(np.nanargmax(profile))}")
for c in np.argsort(-np.nan_to_num(profile))[:5]:
    print(f"  column {x0 + c}: {profile[c]:.1f} deg")
