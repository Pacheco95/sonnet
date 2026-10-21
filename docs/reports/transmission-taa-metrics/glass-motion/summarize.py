#!/usr/bin/env python3
"""Prints the worst measured frame of each case for `background/` and `glass/` (#352): the region's mean and
99th percentile, the pixels not recovered, the split of the region by what the glass covers now, and the
reference matching residual of the covered pixels. This is the historical, last-substep-motion archive;
its floor_* column names are retained as recorded, but are not an error bound. The first row is unavailable."""
import csv, glob, os, sys

here = os.path.dirname(os.path.abspath(__file__))
for variant in ("background", "glass"):
    print(variant)
    for path in sorted(glob.glob(os.path.join(here, variant, "*.csv"))):
        rows = list(csv.DictReader(open(path)))
        residual = rows[1:]
        worst = lambda key, kind=float, source=rows: max(kind(r[key]) for r in source)
        print("  %-24s mean %5.2f p99 %3d unrec %4d pass %2d/%2d | covered %5.2f/%3d uncovered %5.2f/%3d | residual unshifted %5.2f/%3d matched %5.2f/%3d" % (
            os.path.basename(path)[:-4], worst("glass_mean"), worst("glass_p99", int), worst("unrecovered", int),
            sum(int(r["pass"]) for r in rows), len(rows), worst("covered_mean"), worst("covered_p99", int),
            worst("uncovered_mean"), worst("uncovered_p99", int), worst("floor_still_mean", float, residual),
            worst("floor_still_p99", int, residual), worst("floor_best_mean", float, residual), worst("floor_best_p99", int, residual)))
