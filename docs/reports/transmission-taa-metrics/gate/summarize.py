#!/usr/bin/env python3
"""Prints, for each case of `gate/` and `gate/ghost/` (#360), the worst measured frame against ADR-0034's gate: the
covered pixels' mean and 99th percentile with the budget each is held to (rest budget plus a multiple of the temporal
floor), the least slack to either, the most the pixels the glass left are worse than one unresolved sample (judged from
100 pixels), and the frames passing. Re-derives the verdict from the columns, so a change of constants is checked here."""
import csv, glob, os, sys

REST_MEAN, REST_P99, MEAN_PER_FLOOR, P99_PER_FLOOR = 4.0, 40, 4.0, 2.0
MIN_VACATED, VACATED_MARGIN = 100, 3.0

here = os.path.dirname(os.path.abspath(__file__))
for variant, pattern in (("gate", "*.csv"), ("ghost (history never dropped on a glass change)", "ghost/*.csv")):
    print(variant)
    for path in sorted(glob.glob(os.path.join(here, pattern))):
        rows = list(csv.DictReader(open(path)))
        mean_budget = [REST_MEAN + MEAN_PER_FLOOR * float(r["residual_matched_mean"]) for r in rows]
        p99_budget = [REST_P99 + P99_PER_FLOOR * int(r["residual_matched_p99"]) for r in rows]
        mean_slack = min(b - float(r["covered_mean"]) for b, r in zip(mean_budget, rows))
        p99_slack = min(b - int(r["covered_p99"]) for b, r in zip(p99_budget, rows))
        judged = [r for r in rows if int(r["vacated"]) >= MIN_VACATED]
        worse = max((float(r["vacated_mean"]) - float(r["vacated_sample_mean"]) for r in judged), default=None)
        passing = sum(
            1 for b, p, r in zip(mean_budget, p99_budget, rows)
            if float(r["covered_mean"]) <= b and int(r["covered_p99"]) <= p
            and (int(r["vacated"]) < MIN_VACATED or float(r["vacated_mean"]) <= float(r["vacated_sample_mean"]) + VACATED_MARGIN))
        print("  %-24s covered mean %5.2f p99 %3d | least slack mean %5.2f p99 %3d | vacated worse than a sample by %s | pass %2d/%2d" % (
            os.path.basename(path)[:-4], max(float(r["covered_mean"]) for r in rows), max(int(r["covered_p99"]) for r in rows),
            mean_slack, p99_slack, "-" if worse is None else "%.2f" % worse, passing, len(rows)))
