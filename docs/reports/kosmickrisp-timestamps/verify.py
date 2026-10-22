"""Compare the raw Vulkan diagnostic's clock span with its Metal commit feedback."""

import argparse
from pathlib import Path
import re


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    args = parser.parse_args()
    log = args.log.read_text()
    queries = [
        tuple(map(int, match))
        for match in re.findall(r"query=(\d+) ticks=(\d+) available=(\d+)", log)
    ]
    feedback = re.findall(r"duration_ms=([0-9.]+) buffers=1 error=none", log)
    period = re.search(r"period_ns=([0-9.]+)", log)
    if len(queries) != 44 or len(feedback) != 1 or period is None:
        raise SystemExit("expected one raw diagnostic submission with 44 queries and Metal feedback")
    if [query[0] for query in queries] != list(range(44)):
        raise SystemExit("query indices are incomplete or duplicated")
    if any(query[2] == 0 for query in queries):
        raise SystemExit("unavailable query")
    if any(end[1] < start[1] for start, end in zip(queries, queries[1:])):
        raise SystemExit("clock regressed")
    period_ns = float(period[1])
    span_ms = (queries[-1][1] - queries[0][1]) * period_ns / 1e6
    gpu_ms = float(feedback[0])
    # One tick covers quantization; one printed nanosecond covers feedback's decimal rounding.
    if span_ms > gpu_ms + period_ns / 1e6 + 1e-6:
        raise SystemExit(f"Vulkan span {span_ms:.6f} ms exceeds Metal GPU execution {gpu_ms:.6f} ms")
    print(f"44 available, ordered queries; Vulkan span {span_ms:.6f} ms; Metal GPU execution {gpu_ms:.6f} ms")


if __name__ == "__main__":
    main()
