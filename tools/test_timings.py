"""Summarise a test run's timings, optionally against an earlier run.

A results folder is build/<preset>/test-results, or a `test-results-*` artifact CI uploads
(docs/build.md, "Test timings"):

  build.json          compiler, configuration and options, written at configure time
  context.json        what CI adds: job, revision, run, runner image (--context below)
  ctest.xml           CTest's JUnit report, from `ctest --output-junit test-results/ctest.xml`:
                      each suite's result as CTest saw it, timeouts included, and its output
  catch2/<test>.xml   each Catch2 suite's own JUnit report, one test case or section per entry

The script prints Markdown: the context, one row per suite and the slowest cases. With --baseline
another results folder, each time also shows the change from it. It reports and never fails the
run; the test step's exit code is the result.

Usage: test_timings.py <results> [--baseline <results>] [--top N] [--context KEY=VALUE ...]
"""
import argparse
import json
import xml.etree.ElementTree as ET
from pathlib import Path


def read_json(path):
    try:
        return json.loads(path.read_text())
    except (OSError, ValueError):
        return {}


def case_result(case):
    if case.find("failure") is not None or case.find("error") is not None:
        return "failed"
    if case.find("skipped") is not None:
        return "skipped"
    return "passed"


def load(folder):
    """Return (context, suites); a suite is {"ctest": (result, seconds) | None, "cases": {name: (result, seconds)} | None}."""
    context = read_json(folder / "build.json") | read_json(folder / "context.json")
    suites = {}
    for report in sorted((folder / "catch2").glob("*.xml")):
        cases = None
        try:
            cases = {
                case.get("name"): (case_result(case), float(case.get("time", 0)))
                for case in ET.parse(report).iter("testcase")
            }
        except (ET.ParseError, ValueError):
            pass  # Catch2 writes the report when the run ends, so a crashed or killed suite leaves it empty.
        suites[report.stem] = {"ctest": None, "cases": cases}
    try:
        for case in ET.parse(folder / "ctest.xml").iter("testcase"):
            failure = case.find("failure")
            if failure is not None:
                result = (failure.get("message") or "failed").lower()
            elif case.get("status") == "run":
                result = "passed"
            else:
                result = case.get("status", "?")
            suite = suites.setdefault(case.get("name"), {"ctest": None, "cases": None})
            suite["ctest"] = (result, float(case.get("time", 0)))
    except (OSError, ET.ParseError):
        pass
    return context, suites


def seconds(value, before=None):
    text = f"{value:.2f}"
    if before is not None:
        text += f" ({value - before:+.2f})"
    return text


parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
parser.add_argument("results", type=Path)
parser.add_argument("--baseline", type=Path, help="an earlier results folder to compare with")
parser.add_argument("--top", type=int, default=15, help="how many of the slowest cases to list (default 15)")
parser.add_argument("--context", action="append", default=[], metavar="KEY=VALUE",
                    help="add to <results>/context.json before reading it")
args = parser.parse_args()

if args.context:
    path = args.results / "context.json"
    extra = dict(item.split("=", 1) for item in args.context)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(read_json(path) | extra, indent=2, sort_keys=True) + "\n")

context, suites = load(args.results)
base_context, base_suites = load(args.baseline) if args.baseline else ({}, {})

print(f"### Test timings, {context.get('job', args.results)}")
print()
if context:
    print(", ".join(f"{key} `{value}`" for key, value in sorted(context.items()) if value != ""))
    print()
if args.baseline:
    print(f"Compared with {base_context.get('job', args.baseline)} at `{base_context.get('revision', '?')}`.")
    print()
if not suites:
    print("No test results: the tests did not run.")
    raise SystemExit(0)

print("| Suite | Result | Seconds | Entries | Failed | Skipped |")
print("|---|---|--:|--:|--:|--:|")

def suite_time(suite):
    """CTest's wall time, which covers start-up and teardown; the cases' sum where CTest left no report."""
    if suite is None:
        return None
    if suite["ctest"]:
        return suite["ctest"][1]
    return sum(time for _, time in (suite["cases"] or {}).values())


for name, suite in sorted(suites.items(), key=lambda item: -suite_time(item[1])):
    result = suite["ctest"][0] if suite["ctest"] else "no CTest report"
    time = suite_time(suite)
    base = suite_time(base_suites.get(name))
    cases = suite["cases"]
    if cases is None:
        counts = "no Catch2 report | | "
    else:
        results = [r for r, _ in cases.values()]
        counts = f"{len(cases)} | {results.count('failed')} | {results.count('skipped')}"
    print(f"| {name} | {result} | {seconds(time, base)} | {counts} |")
print()

slowest = sorted(
    ((time, suite, case, result) for suite, data in suites.items() for case, (result, time) in (data["cases"] or {}).items()),
    reverse=True,
)[: args.top]
print(f"Slowest {len(slowest)} cases (a case with sections is one row per section path):")
print()
print("| Seconds | Suite | Case | Result |")
print("|--:|---|---|---|")


def base_case(suite, case):
    """The case in the baseline's suite of that name, or else in the one baseline suite that has it, so a suite
    split into shards (editor_tests into editor_tests_1 and _2), or merged back, still compares its cases."""
    found = (base_suites.get(suite, {}).get("cases") or {}).get(case)
    if found is None:
        matches = [data["cases"][case] for data in base_suites.values() if case in (data["cases"] or {})]
        found = matches[0] if len(matches) == 1 else None
    return found


for time, suite, case, result in slowest:
    base = base_case(suite, case)
    name = case.replace("|", "\\|")
    print(f"| {seconds(time, base[1] if base else None)} | {suite} | {name} | {result} |")
