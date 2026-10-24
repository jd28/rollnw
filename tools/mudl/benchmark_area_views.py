#!/usr/bin/env python3
"""Measure explicit overview/player camera views of the prepared 32x32 city."""

import argparse
import hashlib
import json
import math
import os
import statistics
import subprocess
from pathlib import Path


# Inspected views of the fixed prepare_max_city.py fixture, not camera defaults.
VIEWS = {
    "full-area-overview": ("160,-40,410", "160,160,9.22"),
    "elevated-plaza": ("65,45,30", "80,60,0"),
}


def distribution(values):
    values = list(values)
    if not values or not all(v is not None and math.isfinite(v) and v >= 0 for v in values):
        raise ValueError("missing or invalid timing")
    values.sort()
    return {"median": statistics.median(values), "p95": values[math.ceil(len(values) * .95) - 1]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--module", type=Path, required=True)
    parser.add_argument("--user", type=Path, required=True, help="Prepared max-city user directory")
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--stage", choices=["capture", "timing"], default="capture")
    args = parser.parse_args()
    out = args.out.resolve()
    repo = Path(__file__).resolve().parents[2]
    if out.is_relative_to(repo):
        parser.error("generated evidence must be outside the repository")
    out.mkdir(parents=True, exist_ok=False)
    binary = args.binary.resolve()
    env = {k: v for k, v in os.environ.items() if not k.startswith("ROLLNW_PROBE_")}
    env.pop("ROLLNW_GFX_TRACE_RESOURCES", None)
    frames, warmup = (60, 12) if args.stage == "capture" else (1200, 180)
    result = {"scope": "standalone renderer viewport; no toolset UI or actor activity",
              "stage": args.stage, "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(), "runs": []}
    for rep in range(1, 2 if args.stage == "capture" else 4):
        names = list(VIEWS) if rep % 2 else list(reversed(VIEWS))
        for view in names:
            name = f"{rep}-{view}"
            position, target = VIEWS[view]
            report = out / f"{name}.json"
            command = [str(binary), "area-benchmark", "max_city", "--module", str(args.module.resolve()),
                       "--user", str(args.user.resolve()), "--camera", "custom", "--camera-position", position,
                       "--camera-target", target, "--camera-fov", "50", "--width", "1920", "--height", "1080",
                       "--step-ms", "16", "--warmup", str(warmup), "--frames", str(frames), "--json", str(report)]
            if args.stage == "capture":
                command += ["--validate", "--screenshot", str(out / f"{name}.png")]
            with (out / f"{name}.log").open("w") as log:
                subprocess.run(command, env=env, check=True, stdout=log, stderr=subprocess.STDOUT)
            data = json.loads(report.read_text())
            if tuple(data["scene"][k] for k in ("area_width", "area_height", "models", "particle_systems")) != (32, 32, 1680, 600):
                raise ValueError(f"{name}: workload mismatch")
            if len(data["samples"]) != frames or data["validation"] != {"errors": 0, "warnings": 0}:
                raise ValueError(f"{name}: incomplete run or validation failure")
            for row in data["samples"]:
                if any(v for k, v in row["commands"].items() if k.endswith("failures") or k.startswith("dropped_")):
                    raise ValueError(f"{name}: failed graphics commands")
                if row["counts"]["render_model_animation_failed_samples"]:
                    raise ValueError(f"{name}: failed animation samples")
                if view == "full-area-overview" and row["counts"]["area_frame_visible_chunks"] != 1024:
                    raise ValueError(f"{name}: overview does not retain the full area")
            sample = data["samples"][-1]
            run = {"name": name, "view": view, "command": command, "camera": data["camera"],
                   "report_sha256": hashlib.sha256(report.read_bytes()).hexdigest(),
                   "retained_chunks": sample["counts"]["area_frame_visible_chunks"],
                   "retained_records": sample["counts"]["area_frame_visible_records"],
                   "pass_draws": {k: v["draws"] for k, v in sample["pass_commands"].items()},
                   "draws": sample["commands"]["draws"]}
            if args.stage == "timing":
                run["timings"] = {section: {k: distribution(r[section][k] for r in data["samples"])
                                             for k in data["samples"][0][section]}
                                  for section in ("host_timings_ms", "timings_ms", "gpu_timings_ms")}
            result["runs"].append(run)
            (out / "summary.json").write_text(json.dumps(result, indent=2) + "\n")
            print(name, "draws", run["draws"], run.get("timings", {}).get("host_timings_ms", {}), flush=True)


if __name__ == "__main__":
    main()
