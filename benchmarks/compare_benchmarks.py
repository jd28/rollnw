#!/usr/bin/env python3

import argparse
import json
import math
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


def load_config(path: Path | None) -> dict:
    if not path:
        return {}
    return json.loads(path.read_text())


def resolve_profile(config: dict, profile_name: str | None) -> dict:
    if not profile_name:
        return {}
    profiles = config.get("profiles", {})
    if profile_name not in profiles:
        raise SystemExit(f"profile '{profile_name}' not found in config")
    return profiles[profile_name]


def profile_selection(profile: dict) -> tuple[set[str], set[str]]:
    hard = set(profile.get("hard", []))
    advisory = set(profile.get("advisory", []))
    return hard, advisory


def benchmark_meta(config: dict, name: str) -> dict:
    meta = config.get("benchmarks", {})
    if name in meta:
        return meta[name]
    if "/" in name:
        base = name.split("/", 1)[0]
        if base in meta:
            return meta[base]
    return {}


def threshold_for(name: str, profile: dict, config: dict, default_threshold: float) -> float:
    thresholds = profile.get("thresholds", {})
    bench_map = thresholds.get("benchmarks", {})
    if name in bench_map:
        return float(bench_map[name])

    meta = benchmark_meta(config, name)
    group = meta.get("group")
    if group and group in thresholds.get("groups", {}):
        return float(thresholds["groups"][group])

    domain = meta.get("domain")
    if domain and domain in thresholds.get("domains", {}):
        return float(thresholds["domains"][domain])

    if "default" in thresholds:
        return float(thresholds["default"])
    return default_threshold


def build_pairs(config: dict, baseline: dict[str, float], contender: dict[str, float]) -> list[dict]:
    out = []
    for pair in config.get("pairs", []):
        cpp_name = pair.get("cpp")
        smalls_name = pair.get("smalls")
        if not cpp_name or not smalls_name:
            continue
        if cpp_name not in baseline or cpp_name not in contender:
            continue
        if smalls_name not in baseline or smalls_name not in contender:
            continue

        cpp_base = baseline[cpp_name]
        smalls_base = baseline[smalls_name]
        cpp_cont = contender[cpp_name]
        smalls_cont = contender[smalls_name]
        if cpp_base <= 0 or cpp_cont <= 0:
            continue

        ratio_base = smalls_base / cpp_base
        ratio_cont = smalls_cont / cpp_cont
        ratio_delta = ((ratio_cont - ratio_base) / ratio_base) * 100.0 if ratio_base > 0 else math.inf

        out.append(
            {
                "name": pair.get("name", f"{cpp_name}__{smalls_name}"),
                "cpp": cpp_name,
                "smalls": smalls_name,
                "cpp_baseline_ns": cpp_base,
                "smalls_baseline_ns": smalls_base,
                "cpp_contender_ns": cpp_cont,
                "smalls_contender_ns": smalls_cont,
                "ratio_baseline": ratio_base,
                "ratio_contender": ratio_cont,
                "ratio_delta_pct": ratio_delta,
            }
        )

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
    parser.add_argument(
        "--config",
        type=Path,
        default=None,
        help="Optional benchmark profiles config JSON",
    )
    parser.add_argument(
        "--profile",
        type=str,
        default=None,
        help="Optional profile name from config (bench-pr, bench-nightly, ...)",
    )
    parser.add_argument(
        "--out-json",
        type=Path,
        default=None,
        help="Optional path to write machine-readable comparison summary",
    )
    args = parser.parse_args()

    baseline = load_means(args.baseline)
    contender = load_means(args.contender)

    config = load_config(args.config)
    profile = resolve_profile(config, args.profile)
    selected_hard, selected_advisory = profile_selection(profile)
    selected_all = selected_hard | selected_advisory

    common = sorted(set(baseline).intersection(contender))
    if selected_all:
        common = [name for name in common if name in selected_all]

    if not common:
        print("No overlapping benchmarks to compare.")
        return 2

    regressions = []
    improvements = []
    severe = []

    for name in common:
        b = baseline[name]
        c = contender[name]
        if b <= 0:
            continue

        pct = ((c - b) / b) * 100.0
        threshold = threshold_for(name, profile, config, args.warn_threshold)
        advisory = name in selected_advisory
        row = (pct, name, b, c, threshold, advisory)
        if pct > 0:
            regressions.append(row)
            if (
                not advisory
                and pct >= threshold
                and b >= args.min_baseline_ns
            ):
                severe.append(row)
        elif pct < 0:
            improvements.append(row)

    regressions.sort(reverse=True)
    improvements.sort()
    pairs = build_pairs(config, baseline, contender)
    pairs.sort(key=lambda p: p["ratio_delta_pct"], reverse=True)

    print(f"Compared benchmarks: {len(common)}")
    print(f"Regressions: {len(regressions)} | Improvements: {len(improvements)}")
    if args.profile:
        print(f"Profile: {args.profile}")
    print(f"Default threshold: +{args.warn_threshold:.2f}% (baseline >= {args.min_baseline_ns:.1f} ns)")

    if regressions:
        print("\nTop regressions:")
        for pct, name, b, c, threshold, advisory in regressions[: args.top]:
            mode = "advisory" if advisory else "hard"
            print(f"  {pct:+7.2f}%  {name}  ({b:.2f} -> {c:.2f} ns, threshold {threshold:.2f}%, {mode})")

    if improvements:
        print("\nTop improvements:")
        for pct, name, b, c, threshold, advisory in improvements[: args.top]:
            mode = "advisory" if advisory else "hard"
            print(f"  {pct:+7.2f}%  {name}  ({b:.2f} -> {c:.2f} ns, threshold {threshold:.2f}%, {mode})")

    if pairs:
        print("\nMigration ratios (smalls/cpp):")
        for p in pairs[: args.top]:
            print(
                "  {name}: {rb:.3f} -> {rc:.3f} ({dp:+6.2f}%)".format(
                    name=p["name"],
                    rb=p["ratio_baseline"],
                    rc=p["ratio_contender"],
                    dp=p["ratio_delta_pct"],
                )
            )

    summary = {
        "compared": len(common),
        "profile": args.profile,
        "regressions": [
            {
                "name": name,
                "pct": pct,
                "baseline_ns": b,
                "contender_ns": c,
                "threshold_pct": threshold,
                "advisory": advisory,
            }
            for pct, name, b, c, threshold, advisory in regressions
        ],
        "improvements": [
            {
                "name": name,
                "pct": pct,
                "baseline_ns": b,
                "contender_ns": c,
                "threshold_pct": threshold,
                "advisory": advisory,
            }
            for pct, name, b, c, threshold, advisory in improvements
        ],
        "severe": [
            {
                "name": name,
                "pct": pct,
                "baseline_ns": b,
                "contender_ns": c,
                "threshold_pct": threshold,
                "advisory": advisory,
            }
            for pct, name, b, c, threshold, advisory in severe
        ],
        "pairs": pairs,
    }

    if args.out_json:
        args.out_json.parent.mkdir(parents=True, exist_ok=True)
        args.out_json.write_text(json.dumps(summary, indent=2, sort_keys=True))

    if severe:
        print(f"\nFAIL: {len(severe)} benchmark(s) exceed regression threshold.")
        return 1

    print("\nPASS: no benchmark regressions exceeded threshold.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
