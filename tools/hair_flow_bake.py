#!/usr/bin/env python3
"""Spike (#591, ADR-0046): bakes a strand flow map from a painted hair texture.

Throwaway tool, not for merge. Reads the base colour (RGBA), writes an RGBA8 data texture:
  R, G  the strand axis as a doubled angle, (cos 2a, sin 2a) * 0.5 + 0.5, in the texture's own UV frame
  B     a strand shift in [-1, 1] * 0.5 + 0.5 from the painted brightness (Scheuermann's shift noise)
  A     the coherence of the measured orientation, which the shader does not read
The direction is an axis, not an arrow: the doubled angle makes bilinear filtering and block
compression safe, and the shader flips it so the strand's tip points down in world space.

The orientation is the structure tensor of the base colour's luminance, smoothed over a window.
Where its coherence is low the field falls back to a combed one: per fan-shaped UV island, the
point the coherent strands' lines cross most nearly is the crown, and the strands run away from it.

Usage: hair_flow_bake.py BASE_COLOR.png OUT.png [--window 6] [--lo 0.18] [--hi 0.45]
"""
import argparse
import sys

import numpy as np
from PIL import Image


def box(a, r, axis):
    """A box blur of radius r along an axis, by a running sum, edges clamped."""
    n = a.shape[axis]
    pad = [(0, 0)] * a.ndim
    pad[axis] = (r + 1, r)
    p = np.pad(a, pad, mode="edge")
    c = np.cumsum(p, axis=axis, dtype=np.float64)
    hi = np.take(c, range(2 * r + 1, 2 * r + 1 + n), axis=axis)
    lo = np.take(c, range(0, n), axis=axis)
    return (hi - lo) / (2 * r + 1)


def blur(a, r):
    """Three box passes per axis: close to a Gaussian of sigma about r * 0.8."""
    for _ in range(3):
        a = box(box(a, r, 0), r, 1)
    return a


def smoothstep(lo, hi, x):
    t = np.clip((x - lo) / (hi - lo), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def islands(mask):
    """Labels the connected components of a mask by flood fill on a coarse grid."""
    small = mask[::8, ::8]
    labels = np.zeros(small.shape, np.int32)
    count = 0
    for y, x in zip(*np.nonzero(small)):
        if labels[y, x]:
            continue
        count += 1
        stack = [(y, x)]
        labels[y, x] = count
        while stack:
            cy, cx = stack.pop()
            for ny, nx in ((cy + 1, cx), (cy - 1, cx), (cy, cx + 1), (cy, cx - 1)):
                if 0 <= ny < small.shape[0] and 0 <= nx < small.shape[1] and small[ny, nx] and not labels[ny, nx]:
                    labels[ny, nx] = count
                    stack.append((ny, nx))
    return np.kron(labels, np.ones((8, 8), np.int32))[: mask.shape[0], : mask.shape[1]], count


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("base")
    ap.add_argument("out")
    ap.add_argument("--window", type=int, default=6, help="structure tensor window radius, texels")
    ap.add_argument("--lo", type=float, default=0.18, help="coherence below which the combed field takes over")
    ap.add_argument("--hi", type=float, default=0.45, help="coherence above which only the measured one is used")
    ap.add_argument("--report", default=None)
    args = ap.parse_args()

    rgba = np.asarray(Image.open(args.base).convert("RGBA")).astype(np.float64) / 255.0
    alpha = rgba[..., 3]
    luma = 0.2126 * rgba[..., 0] + 0.7152 * rgba[..., 1] + 0.0722 * rgba[..., 2]
    solid = alpha > 0.5
    h, w = luma.shape

    # Luminance gradient on a lightly smoothed image (Sobel); x to the right, y down in the file.
    s = blur(luma, 1)
    gx = (np.roll(s, -1, 1) - np.roll(s, 1, 1)) * 0.5
    gy = (np.roll(s, -1, 0) - np.roll(s, 1, 0)) * 0.5
    weight = solid.astype(np.float64)
    jxx = blur(gx * gx * weight, args.window)
    jxy = blur(gx * gy * weight, args.window)
    jyy = blur(gy * gy * weight, args.window)
    norm = np.maximum(blur(weight, args.window), 1e-6)
    jxx, jxy, jyy = jxx / norm, jxy / norm, jyy / norm
    trace = jxx + jyy + 1e-9
    radius = np.sqrt((jxx - jyy) ** 2 + 4 * jxy**2)
    coherence = np.clip(radius / trace, 0.0, 1.0)
    # The gradient's doubled angle is (jxx - jyy, 2 jxy); the strand runs across it, so negate.
    mx, my = -(jxx - jyy), -2 * jxy
    ml = np.maximum(np.hypot(mx, my), 1e-12)
    mx, my = mx / ml, my / ml

    # The combed field: per island, the crown is where the coherent strands' lines cross.
    labels, count = islands(solid)
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float64)
    cx, cy = np.zeros((h, w)), np.zeros((h, w))
    info = []
    for label in range(1, count + 1):
        sel = (labels == label) & solid & (coherence > args.hi)
        sel[::1, ::1] &= (np.arange(h)[:, None] % 4 == 0) & (np.arange(w)[None, :] % 4 == 0)
        if sel.sum() < 200:
            continue
        # Strand direction d = (cos a, sin a) from the doubled angle; the line's normal n = (-sin a, cos a).
        a = 0.5 * np.arctan2(my[sel], mx[sel])
        nxv, nyv = -np.sin(a), np.cos(a)
        px, py = xx[sel], yy[sel]
        wgt = coherence[sel]
        # Minimise sum w (n . (p - c))^2: the normal equations of the least-squares point.
        A = np.array([[np.sum(wgt * nxv * nxv), np.sum(wgt * nxv * nyv)], [np.sum(wgt * nxv * nyv), np.sum(wgt * nyv * nyv)]])
        b = np.array([np.sum(wgt * nxv * (nxv * px + nyv * py)), np.sum(wgt * nyv * (nxv * px + nyv * py))])
        pivot = np.linalg.solve(A, b)
        members = labels == label
        cx[members], cy[members] = pivot
        info.append((label, pivot, int(sel.sum())))
    dx, dy = xx - cx, yy - cy
    dl = np.maximum(np.hypot(dx, dy), 1e-9)
    # The radial direction as a doubled angle.
    ax, ay = (dx * dx - dy * dy) / dl**2, (2 * dx * dy) / dl**2

    mix = smoothstep(args.lo, args.hi, coherence)
    fx = mix * mx + (1 - mix) * ax
    fy = mix * my + (1 - mix) * ay
    fl = np.maximum(np.hypot(fx, fy), 1e-9)
    fx, fy = fx / fl, fy / fl

    # Shift noise from the painted brightness: lighter strands shift one way, darker the other.
    shift = np.clip((luma - blur(luma, 8)) * 6.0, -1.0, 1.0)

    # Bleed the field outwards past the island's edge so filtering and mips never read empty texels.
    valid = (blur(weight, 2) > 0.2).astype(np.float64)
    fields = [fx * valid, fy * valid, shift * valid, coherence * valid]
    for _ in range(12):
        v = blur(valid, 3)
        grown = [blur(f, 3) / np.maximum(v, 1e-6) for f in fields]
        new = (valid < 0.5) & (v > 1e-4)
        for f, g in zip(fields, grown):
            f[new] = g[new]
        valid = np.where(new, 1.0, valid)
    fx, fy, shift, coherence_out = fields
    fl = np.maximum(np.hypot(fx, fy), 1e-9)
    fx, fy = fx / fl, fy / fl

    out = np.zeros((h, w, 4), np.uint8)
    out[..., 0] = np.round((fx * 0.5 + 0.5) * 255)
    out[..., 1] = np.round((fy * 0.5 + 0.5) * 255)
    out[..., 2] = np.round((np.clip(shift, -1, 1) * 0.5 + 0.5) * 255)
    out[..., 3] = np.round(np.clip(coherence_out, 0, 1) * 255)
    Image.fromarray(out, "RGBA").save(args.out)

    lines = [
        f"islands: {count}",
        *[f"island {l}: crown at ({p[0]:.0f}, {p[1]:.0f}) from {n} coherent samples" for l, p, n in info],
        f"coherence over solid texels: median {np.median(coherence[solid]):.2f}, p10 {np.percentile(coherence[solid], 10):.2f}, "
        f"p90 {np.percentile(coherence[solid], 90):.2f}",
        f"share of solid texels with coherence >= {args.hi}: {np.mean(coherence[solid] >= args.hi):.2f}; "
        f"below {args.lo}: {np.mean(coherence[solid] < args.lo):.2f}",
    ]
    print("\n".join(lines))
    if args.report:
        open(args.report, "w").write("\n".join(lines) + "\n")


if __name__ == "__main__":
    sys.exit(main())
