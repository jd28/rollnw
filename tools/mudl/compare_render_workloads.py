#!/usr/bin/env python3
"""Alternate two Release/Tracy-OFF binaries on the three rendering workloads."""

import argparse
import hashlib
import json
import math
import os
import statistics
import subprocess
from pathlib import Path


WORKLOADS = {
    "max_city": (32, 32, 1680, 600),
    "ms_4city": (16, 16, 420, 150),
    "pvp_area_2": (32, 16, 578, 16),
}


def distribution(values):
    values = sorted(v for v in values if v is not None)
    if not all(math.isfinite(v) and v >= 0 for v in values):
        raise ValueError("invalid timing")
    if not values:
        return {"available": 0}
    return {"available": len(values), "median": statistics.median(values),
            "p95": values[math.ceil(len(values) * .95) - 1],
            "p99": values[math.ceil(len(values) * .99) - 1]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("before", type=Path)
    parser.add_argument("after", type=Path)
    parser.add_argument("--module", type=Path, required=True)
    parser.add_argument("--user", type=Path, required=True, help="Prepared max-city user directory")
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--repetitions", type=int, default=3)
    args = parser.parse_args()
    if args.repetitions < 1:
        parser.error("repetitions must be positive")
    args.out.mkdir(parents=True, exist_ok=False)
    binaries = {k: getattr(args, k).resolve() for k in ("before", "after")}
    env = dict(os.environ)
    for key in ("ROLLNW_GFX_TRACE_RESOURCES", "ROLLNW_PROBE_MODEL_TEXTURES"):
        env.pop(key, None)
    results = {"binaries": {k: {"path": str(p), "sha256": hashlib.sha256(p.read_bytes()).hexdigest()}
                            for k, p in binaries.items()}, "runs": []}
    for repetition in range(1, args.repetitions + 1):
        for area, expected in WORKLOADS.items():
            order = ("before", "after") if repetition % 2 else ("after", "before")
            for version in order:
                name = f"{repetition}-{area}-{version}"
                report_path = (args.out / f"{name}.json").resolve()
                command = [str(binaries[version]), "area-benchmark", area,
                           "--module", str(args.module.resolve()), "--camera", "gameplay",
                           "--width", "1920", "--height", "1080", "--step-ms", "16",
                           "--warmup", "60", "--frames", "360", "--json", str(report_path)]
                if area == "max_city":
                    command += ["--user", str(args.user.resolve())]
                with (args.out / f"{name}.log").open("w") as log:
                    subprocess.run(command, env=env, check=True, stdout=log, stderr=subprocess.STDOUT)
                report = json.loads(report_path.read_text())
                scene = report["scene"]
                actual = tuple(scene[k] for k in ("area_width", "area_height", "models", "particle_systems"))
                if actual != expected or len(report["samples"]) != 360:
                    raise ValueError(f"{name}: workload mismatch: {actual}, expected {expected}")
                for sample in report["samples"]:
                    if any(value for key, value in sample["commands"].items()
                           if key.endswith("failures") or key.startswith("dropped_")):
                        raise ValueError(f"{name}: failed graphics commands")
                    if sample["counts"]["render_model_animation_failed_samples"]:
                        raise ValueError(f"{name}: failed animation samples")
                timings = {}
                for section in ("host_timings_ms", "timings_ms", "gpu_timings_ms"):
                    timings[section] = {key: distribution(s[section][key] for s in report["samples"])
                                        for key in report["samples"][0][section]}
                results["runs"].append({"name": name, "command": command, "timings": timings,
                                        "report_sha256": hashlib.sha256(report_path.read_bytes()).hexdigest()})
                (args.out / "summary.json").write_text(json.dumps(results, indent=2) + "\n")
                print(name, timings["host_timings_ms"]["frame"], flush=True)


if __name__ == "__main__":
    main()
