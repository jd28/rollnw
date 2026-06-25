#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Any


@dataclass(frozen=True)
class BenchmarkCase:
    name: str
    args: tuple[str, ...]


def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def slug(value: str) -> str:
    out = []
    for ch in value:
        if ch.isalnum() or ch in ("-", "_"):
            out.append(ch)
        else:
            out.append("_")
    return "".join(out).strip("_") or "area"


def benchmark_cases(variant_set: str) -> list[BenchmarkCase]:
    full = BenchmarkCase("full", ())
    if variant_set == "minimal":
        return [full]

    cases = [
        full,
        BenchmarkCase("no_shadows", ("--no-shadows",)),
        BenchmarkCase("no_lights", ("--no-lights",)),
        BenchmarkCase("visibility_radius_8", ("--visible-tile-radius", "8")),
    ]
    if variant_set == "default":
        return cases
    if variant_set == "all":
        cases.extend(
            [
                BenchmarkCase("no_lights_no_shadows", ("--no-lights", "--no-shadows")),
                BenchmarkCase("visibility_cone_8", ("--visible-tile-cone", "8")),
                BenchmarkCase("forward_plus_clusters", ("--forward-plus-debug", "cluster-lights")),
                BenchmarkCase("forward_plus_depth", ("--forward-plus-debug", "depth-slices")),
            ]
        )
        return cases
    raise SystemExit(f"invalid --variants '{variant_set}', expected minimal, default, or all")


def run_command(cmd: list[str], log_path: Path | None = None) -> int:
    print("+ " + " ".join(cmd), flush=True)
    if log_path:
        with log_path.open("w") as log:
            return subprocess.run(cmd, check=False, stdout=log, stderr=subprocess.STDOUT).returncode
    return subprocess.run(cmd, check=False).returncode


def executable_arg(path: Path) -> str:
    value = str(path)
    if path.is_absolute() or value.startswith("./") or value.startswith("../"):
        return value
    return f"./{value}"


def read_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text())


def nested_avg(root: dict[str, Any], *keys: str) -> float:
    node: Any = root
    for key in keys:
        if not isinstance(node, dict):
            return 0.0
        node = node.get(key)
    if isinstance(node, dict):
        value = node.get("avg", 0.0)
    else:
        value = node if node is not None else 0.0
    try:
        return float(value)
    except (TypeError, ValueError):
        return 0.0


def summarize_benchmark(path: Path) -> dict[str, Any]:
    report = read_json(path)
    summary = report.get("summary", {})
    scene = report.get("scene", {})
    return {
        "area": report.get("area"),
        "path": str(path),
        "total_render_ms_avg": nested_avg(summary, "timings_ms", "total_render"),
        "setup_ms_avg": nested_avg(summary, "timings_ms", "setup"),
        "shadow_ms_avg": nested_avg(summary, "timings_ms", "shadow"),
        "opaque_ms_avg": nested_avg(summary, "timings_ms", "opaque"),
        "water_ms_avg": nested_avg(summary, "timings_ms", "water"),
        "transparent_ms_avg": nested_avg(summary, "timings_ms", "transparent"),
        "particles_ms_avg": nested_avg(summary, "timings_ms", "particles"),
        "forward_plus_prepare_ms_avg": nested_avg(summary, "timings_ms", "forward_plus_prepare"),
        "draws_avg": nested_avg(summary, "commands", "draws"),
        "indirect_draw_calls_avg": nested_avg(summary, "commands", "indirect_draw_calls"),
        "forward_plus_lights_avg": nested_avg(summary, "forward_plus", "lights"),
        "visible_records_avg": nested_avg(summary, "area_frame", "visible_records"),
        "scene": {
            "models": scene.get("models", 0),
            "local_lights": scene.get("local_lights", 0),
            "has_water": scene.get("has_water", False),
        },
    }


def print_table(cases: list[dict[str, Any]]) -> None:
    headers = [
        "case",
        "total",
        "setup",
        "shadow",
        "opaque",
        "fplus",
        "draws",
        "indirect",
        "lights",
        "visible",
    ]
    rows = []
    for case in cases:
        summary = case["summary"]
        rows.append(
            [
                case["name"],
                f'{summary["total_render_ms_avg"]:.3f}',
                f'{summary["setup_ms_avg"]:.3f}',
                f'{summary["shadow_ms_avg"]:.3f}',
                f'{summary["opaque_ms_avg"]:.3f}',
                f'{summary["forward_plus_prepare_ms_avg"]:.3f}',
                f'{summary["draws_avg"]:.0f}',
                f'{summary["indirect_draw_calls_avg"]:.0f}',
                f'{summary["forward_plus_lights_avg"]:.0f}',
                f'{summary["visible_records_avg"]:.0f}',
            ]
        )

    widths = [len(header) for header in headers]
    for row in rows:
        for idx, value in enumerate(row):
            widths[idx] = max(widths[idx], len(value))

    def line(values: list[str]) -> str:
        return "  ".join(value.rjust(widths[idx]) for idx, value in enumerate(values))

    print(line(headers))
    print(line(["-" * width for width in widths]))
    for row in rows:
        print(line(row))


def add_common_mudl_args(cmd: list[str], args: argparse.Namespace) -> None:
    if args.module:
        cmd.extend(["--module", str(args.module)])
    if args.user:
        cmd.extend(["--user", str(args.user)])


def main() -> int:
    root = repo_root()
    parser = argparse.ArgumentParser(
        description="Run a validation-gated mudl area benchmark matrix and write a summary JSON."
    )
    parser.add_argument("area", help="Area resref to benchmark")
    parser.add_argument("--mudl", type=Path, default=Path("build/tools/mudl/mudl"))
    parser.add_argument("--module", type=Path, default=None)
    parser.add_argument("--user", type=Path, default=None)
    parser.add_argument("--out-dir", type=Path, default=root / "tmp/mudl-area-baseline")
    parser.add_argument("--frames", type=int, default=60)
    parser.add_argument("--warmup", type=int, default=5)
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=720)
    parser.add_argument("--variants", choices=("minimal", "default", "all"), default="default")
    parser.add_argument("--quiet", action="store_true", help="Write mudl logs to files under --out-dir")
    parser.add_argument("--skip-validation", action="store_true")
    args = parser.parse_args()

    args.out_dir.mkdir(parents=True, exist_ok=True)
    area_slug = slug(args.area)
    validation_path = args.out_dir / f"{area_slug}-validation-sweep.json"
    summary_path = args.out_dir / f"{area_slug}-baseline-summary.json"

    validation_summary: dict[str, Any] | None = None
    if not args.skip_validation:
        validation_cmd = [
            executable_arg(args.mudl),
            "area-sweep",
            args.area,
            "--frames",
            "1",
            "--warmup",
            "0",
            "--width",
            str(args.width),
            "--height",
            str(args.height),
            "--variants",
            args.variants,
            "--validate",
            "--json",
            str(validation_path),
        ]
        add_common_mudl_args(validation_cmd, args)
        validation_log_path = args.out_dir / f"{area_slug}-validation-sweep.log"
        rc = run_command(validation_cmd, validation_log_path if args.quiet else None)
        if validation_path.exists():
            validation_summary = read_json(validation_path).get("summary", {})
        if rc != 0:
            print(f"validation sweep failed, see {validation_path}")
            return rc

    case_reports: list[dict[str, Any]] = []
    failed = False
    for case in benchmark_cases(args.variants):
        report_path = args.out_dir / f"{area_slug}-{case.name}.json"
        cmd = [
            executable_arg(args.mudl),
            "area-benchmark",
            args.area,
            "--frames",
            str(max(args.frames, 1)),
            "--warmup",
            str(max(args.warmup, 0)),
            "--width",
            str(max(args.width, 1)),
            "--height",
            str(max(args.height, 1)),
            "--camera",
            "gameplay",
            "--json",
            str(report_path),
        ]
        cmd.extend(case.args)
        add_common_mudl_args(cmd, args)

        log_path = args.out_dir / f"{area_slug}-{case.name}.log"
        rc = run_command(cmd, log_path if args.quiet else None)
        if rc != 0:
            failed = True
        if report_path.exists():
            case_reports.append(
                {
                    "name": case.name,
                    "log": str(log_path) if args.quiet else None,
                    "returncode": rc,
                    "summary": summarize_benchmark(report_path),
                }
            )
        else:
            failed = True
            case_reports.append({
                "name": case.name,
                "log": str(log_path) if args.quiet else None,
                "returncode": rc,
                "summary": None,
            })

    summary = {
        "command": "mudl-area-benchmark-baseline",
        "area": args.area,
        "module": str(args.module) if args.module else None,
        "user": str(args.user) if args.user else None,
        "frames": max(args.frames, 1),
        "warmup_frames": max(args.warmup, 0),
        "viewport": {"width": max(args.width, 1), "height": max(args.height, 1)},
        "variants": args.variants,
        "validation": {
            "skipped": args.skip_validation,
            "path": str(validation_path) if not args.skip_validation else None,
            "log": str(validation_log_path) if not args.skip_validation and args.quiet else None,
            "summary": validation_summary,
        },
        "cases": case_reports,
    }
    summary_path.write_text(json.dumps(summary, indent=2) + "\n")

    complete_cases = [case for case in case_reports if case.get("summary")]
    if complete_cases:
        print_table(complete_cases)
    print(f"wrote {summary_path}")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
