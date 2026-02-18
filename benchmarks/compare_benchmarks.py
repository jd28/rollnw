#!/usr/bin/env python3

import argparse
import json
from pathlib import Path


def load_means(path: Path) -> dict[str, float]:
    data = json.loads(path.read_text())
    out: dict[str, float] = {}

    for row in data.get("benchmarks", []):
        name = row.get("name")
        if not name:
            continue

        run_type = row.get("run_type")
        aggregate_name = row.get("aggregate_name")

        if run_type == "aggregate" and aggregate_name != "mean":
            continue

        cpu_time = row.get("cpu_time")
        if cpu_time is None:
            continue

        out[name] = float(cpu_time)

    return out


def main() -> int:
    parser = argparse.ArgumentParser(description="Compare two Google Benchmark JSON runs")
    parser.add_argument("baseline", type=Path, help="Baseline JSON path")
    parser.add_argument("contender", type=Path, help="Contender JSON path")
    parser.add_argument(
        "--warn-threshold",
        type=float,
        default=10.0,
        help="Warn/fail when contender is slower than this percent (default: 10.0)",
    )
    parser.add_argument(
        "--min-baseline-ns",
        type=float,
        default=50.0,
        help="Ignore regressions if baseline cpu_time is below this ns value (default: 50)",
    )
    parser.add_argument(
        "--top",
        type=int,
        default=20,
        help="Max rows to print in regression/improvement lists (default: 20)",
    )
    args = parser.parse_args()

    baseline = load_means(args.baseline)
    contender = load_means(args.contender)

    common = sorted(set(baseline).intersection(contender))
    if not common:
        print("No overlapping benchmarks to compare.")
        return 2

    regressions = []
    improvements = []

    for name in common:
        b = baseline[name]
        c = contender[name]
        if b <= 0:
            continue

        pct = ((c - b) / b) * 100.0
        row = (pct, name, b, c)
        if pct > 0:
            regressions.append(row)
        elif pct < 0:
            improvements.append(row)

    regressions.sort(reverse=True)
    improvements.sort()

    severe = [
        r
        for r in regressions
        if r[0] >= args.warn_threshold and r[2] >= args.min_baseline_ns
    ]

    print(f"Compared benchmarks: {len(common)}")
    print(f"Regressions: {len(regressions)} | Improvements: {len(improvements)}")
    print(f"Threshold: +{args.warn_threshold:.2f}% (baseline >= {args.min_baseline_ns:.1f} ns)")

    if regressions:
        print("\nTop regressions:")
        for pct, name, b, c in regressions[: args.top]:
            print(f"  {pct:+7.2f}%  {name}  ({b:.2f} -> {c:.2f} ns)")

    if improvements:
        print("\nTop improvements:")
        for pct, name, b, c in improvements[: args.top]:
            print(f"  {pct:+7.2f}%  {name}  ({b:.2f} -> {c:.2f} ns)")

    if severe:
        print(f"\nFAIL: {len(severe)} benchmark(s) exceed regression threshold.")
        return 1

    print("\nPASS: no benchmark regressions exceeded threshold.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
