#!/usr/bin/env python3
"""Archive and compare #352's background/glass TAA harness runs (NumPy and Pillow).

Both inputs must contain the seven unchanged cases and --keep-frames output at 1280x720.
The ADR region/budgets remain those of metrics.csv. Extra diagnostic masks split
moving content from stationary content and the masked pane's lattice edges from its interior;
they do not replace the acceptance region. See docs/reports/transmission-taa-motion-corrected.md.
"""
import argparse
import csv
import hashlib
import json
import math
import shutil
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

CASES = ("rough-pane", "masked-pane", "masked-pane-fast", "watch", "watch-fast",
         "transmission-test", "transmission-test-still")
WIDTH, HEIGHT = 1280, 720


def read_frame(directory, frame):
    image = np.asarray(Image.open(directory / f"frame-{frame:03}.png").convert("RGB"))
    if image.shape != (HEIGHT, 3 * WIDTH, 3):
        raise ValueError(f"unexpected capture dimensions in {directory}")
    return image[:, :WIDTH], image[:, WIDTH:2 * WIDTH]


def dilate(mask, radius=2):
    padded = np.pad(mask, radius)
    result = np.zeros_like(mask)
    for y in range(2 * radius + 1):
        for x in range(2 * radius + 1):
            result |= padded[y:y + HEIGHT, x:x + WIDTH]
    return result


def error_statistics(errors, mask):
    values = errors[mask]
    if values.size == 0:
        raise ValueError("empty diagnostic region")
    histogram = np.bincount(values, minlength=256)
    p99 = int(np.searchsorted(np.cumsum(histogram), math.ceil(0.99 * values.size)))
    return {"pixels": int(values.size), "mean": float(values.mean()), "p99": p99,
            "maximum": int(values.max())}


def diagnostic_masks(case, frame, references):
    y, x = np.mgrid[:HEIGHT, :WIDTH]
    if case.startswith("watch") or case == "rough-pane":
        if case.startswith("watch"):
            interior = (x - 640) ** 2 + (y - 350) ** 2 < 140 ** 2
            threshold = 8
        else:
            interior = (x >= 180) & (x < 1100) & (y >= 100) & (y < 620)
            threshold = 4
        low = np.minimum.reduce(references)
        high = np.maximum.reduce(references)
        moving = np.max(high.astype(np.int16) - low.astype(np.int16), axis=2) >= threshold
        moving = dilate(moving) & interior
        return {"moving-content": moving, "stationary-interior": interior & ~moving}
    if case.startswith("masked-pane"):
        # Same camera, quad, alpha texture and stop time as generate_taa_fixtures.py. A frame
        # has already advanced (frame + 1) / 60 seconds. Coordinates are unjittered pixel centres.
        speed = 2.4 if case.endswith("fast") else 0.9
        center = -0.8 + speed * min((frame + 1) / 60, 40 / 60)
        pixels_per_metre = HEIGHT / (2 * math.tan(math.radians(20)) * 5)
        u = ((x + 0.5 - WIDTH / 2) / pixels_per_metre - center) / 2.4 + 0.5
        v = (HEIGHT / 2 - y - 0.5) / pixels_per_metre / 1.8 + 0.5
        coverage = ((u >= 0) & (u < 1) & (v >= 0) & (v < 1) &
                    ((u < 16 / 256) | (u >= 240 / 256) | (v < 16 / 256) | (v >= 240 / 256) |
                     ((u > 120 / 256) & (u < 136 / 256)) | ((v > 120 / 256) & (v < 136 / 256))))
        core = coverage & ~dilate(~coverage)
        return {"lattice-edges": dilate(coverage) & ~core, "transmitted-pattern": core}
    # Clear and rough sphere rows, including their rims and reflections. This is a composite
    # diagnostic crop, not a separation of the shader's lobes; the metallic row is outside it.
    return {"glass-detail-rows": (x >= 545) & (x < 735) & (y >= 286) & (y < 382)}


def picture(output, case, frame, background, glass, reference, mask):
    bounds = {"watch": (470, 190, 810, 510), "watch-fast": (470, 190, 810, 510),
              "transmission-test": (450, 240, 760, 445), "transmission-test-still": (450, 240, 760, 445),
              "rough-pane": (360, 180, 980, 540)}
    left, top, right, bottom = bounds.get(case, (330, 165, 1010, 555))
    error_a = np.max(np.abs(background.astype(np.int16) - reference.astype(np.int16)), axis=2)
    error_b = np.max(np.abs(glass.astype(np.int16) - reference.astype(np.int16)), axis=2)
    overlay = reference.copy()
    overlay[mask] = (0.5 * overlay[mask] + 0.5 * np.array([255, 0, 255])).astype(np.uint8)
    panels = [reference, background, glass, overlay,
              np.repeat(np.minimum(error_a * 16, 255)[:, :, None], 3, axis=2).astype(np.uint8),
              np.repeat(np.minimum(error_b * 16, 255)[:, :, None], 3, axis=2).astype(np.uint8)]
    labels = ["16-sample reference", "Background motion", "Glass motion",
              "Diagnostic region", "Background error x16", "Glass error x16"]
    width, height = right - left, bottom - top
    canvas = Image.new("RGB", (3 * width, 2 * (height + 28)), "#20242a")
    draw = ImageDraw.Draw(canvas)
    font = ImageFont.load_default(size=16)
    for index, (panel, label) in enumerate(zip(panels, labels)):
        x, y = (index % 3) * width, (index // 3) * (height + 28)
        draw.text((x + 8, y + 4), label, font=font, fill="white")
        canvas.paste(Image.fromarray(panel[top:bottom, left:right]), (x, y + 28))
    canvas.save(output / f"{case}-{frame:03}.png")
    return [left, top, right, bottom]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--background", type=Path, required=True)
    parser.add_argument("--glass", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    images = args.out / "images"
    images.mkdir(exist_ok=True)
    comparison = {}
    diagnostics = []
    for case in CASES:
        paths = {"background": args.background / case, "glass": args.glass / case}
        summaries = {k: json.loads((v / "summary.json").read_text()) for k, v in paths.items()}
        if any(s.get("motionInterval") != "rendered-frame" for s in summaries.values()):
            raise ValueError("use the corrected rendered-frame harness for the final comparison")
        for key in ("motionInterval", "scene", "size", "frameSeconds", "substeps", "warmupFrames", "measuredFrames",
                    "referenceSamples", "referenceJitter", "referenceMotionSearchRadius", "diagnostic"):
            if summaries["background"][key] != summaries["glass"][key]:
                raise ValueError(f"{case}: comparison changes {key}")
        if summaries["background"]["glassMotion"] or not summaries["glass"]["glassMotion"]:
            raise ValueError("compare the background path with --glass-motion")
        if summaries["background"]["referenceSamples"] != 16:
            raise ValueError("the acceptance reference must stay at 16 samples")
        first = summaries["background"]["warmupFrames"]
        count = summaries["background"]["measuredFrames"]
        frames = range(first, first + count)
        reference_frames = [read_frame(paths["background"], f)[1] for f in frames]
        result = {}
        reference_hashes = {}
        rows_by_mode = {}
        for mode, path in paths.items():
            target = args.out / mode / case
            target.mkdir(parents=True, exist_ok=True)
            for name in ("metrics.csv", "summary.json"):
                shutil.copyfile(path / name, target / name)
            with (path / "metrics.csv").open() as source:
                rows = list(csv.DictReader(source))
            rows_by_mode[mode] = rows
            if [int(r["frame"]) for r in rows] != list(frames):
                raise ValueError(f"{case}: missing judged frames")
            s = summaries[mode]
            result[mode] = {key: s[key] for key in ("worstMean", "worstPercentile99", "worstVacatedMean",
                                                    "worstOpaqueMean", "worstOpaquePercentile99", "pass")}
            result[mode].update({"passingFrames": sum(int(r["pass"]) for r in rows), "measuredFrames": count,
                                 "legacyMeanBudgetFrames": sum(float(r["glass_mean"]) <= 2 for r in rows),
                                 "legacyP99BudgetFrames": sum(int(r["glass_p99"]) <= 8 for r in rows),
                                 "recoveryBudgetFrames": sum(
                                     int(r["vacated"]) < 100 or
                                     float(r["vacated_mean"]) <= float(r["vacated_sample_mean"]) + 3
                                     for r in rows)})
        residual_fields = ("residual_unshifted_mean", "residual_unshifted_p99",
                           "residual_matched_mean", "residual_matched_p99")
        same_fields = ("glass_pixels", "covered_pixels", "uncovered_pixels", "vacated", *residual_fields,
                       "vacated_sample_mean", "vacated_sample_p99", "single_covered_mean", "single_covered_p99")
        for index, (a, b) in enumerate(zip(rows_by_mode["background"], rows_by_mode["glass"])):
            if any(a[key] != b[key] for key in same_fields):
                raise ValueError(f"{case} frame {a['frame']}: coverage or reference residual changed")
            if any(a[key] == "" or b[key] == "" for key in residual_fields):
                raise ValueError(f"{case}: every measured frame needs its previous warm-up reference")
        result["referenceMotionResidual"] = {
            key: max(float(row[key]) for row in rows_by_mode["background"]) for key in residual_fields
        }
        for mode, rows in rows_by_mode.items():
            result[mode]["covered"] = {key: max(float(row[key]) for row in rows)
                                       for key in ("covered_mean", "covered_p99")}
            result[mode]["uncovered"] = {key: max(float(row[key]) for row in rows)
                                         for key in ("uncovered_mean", "uncovered_p99")}
        captures = [int(summaries["glass"]["worstFrame"])]
        if case.startswith("masked-pane"):
            captures.append(48)
        for frame, reference in zip(frames, reference_frames):
            background, _ = read_frame(paths["background"], frame)
            glass, other_reference = read_frame(paths["glass"], frame)
            if not np.array_equal(reference, other_reference):
                raise ValueError(f"{case} frame {frame}: reference changed between motion treatments")
            reference_hashes[str(frame)] = hashlib.sha256(reference.tobytes()).hexdigest()
            masks = diagnostic_masks(case, frame, reference_frames)
            for region, mask in masks.items():
                record = {"case": case, "frame": frame, "region": region}
                for mode, pixels in (("background", background), ("glass", glass)):
                    errors = np.max(np.abs(pixels.astype(np.int16) - reference.astype(np.int16)), axis=2).astype(np.uint8)
                    for key, value in error_statistics(errors, mask).items():
                        record[f"{mode}_{key}"] = value
                diagnostics.append(record)
            if frame in captures:
                crop = picture(images, case, frame, background, glass, reference, next(iter(masks.values())))
                result.setdefault("captures", []).append({"frame": frame, "crop": crop})
        result["referenceSha256"] = reference_hashes
        comparison[case] = result
        print(case, *[f"{mode}: {r['worstMean']:.3f}/{r['worstPercentile99']:g}/{r['worstVacatedMean']:.3f} "
                      f"({r['passingFrames']}/{count})" for mode, r in result.items() if mode in paths])
    (args.out / "comparison.json").write_text(json.dumps(comparison, indent=2) + "\n")
    with (args.out / "diagnostics.csv").open("w") as target:
        writer = csv.DictWriter(target, fieldnames=list(diagnostics[0]))
        writer.writeheader()
        writer.writerows(diagnostics)
    print("Every paired 16-sample reference is pixel-identical.")


if __name__ == "__main__":
    main()
