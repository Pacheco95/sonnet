#!/usr/bin/env python3
"""Prints worst-frame mean / p99 / unrecovered for every variant directory next to this script."""
import json, pathlib
root = pathlib.Path(__file__).parent
for variant in sorted(p for p in root.iterdir() if p.is_dir()):
    for summary in sorted(variant.glob("*/summary.json")):
        s = json.loads(summary.read_text())
        print(f"{variant.name:20} {s['case']:26} {s['worstMean']:6.2f} / {s['worstPercentile99']:3.0f} / {s['worstUnrecovered']:3.0f}")
