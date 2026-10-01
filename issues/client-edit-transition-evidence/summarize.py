"""Recompute the recorded desktop results from raw trace/action rows."""

import collections
import gzip
import json
import math
from pathlib import Path
import statistics
import sys


def stats(values):
    return {
        "median": statistics.median(values),
        "p95": sorted(values)[math.ceil(len(values) * 0.95) - 1],
    }


root = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).resolve().parent
report = json.loads((root / "measurements.json").read_text())
groups = collections.defaultdict(list)
for run in report["run_order"]:
    with gzip.open(root / (run + ".jsonl.gz"), "rt") as source:
        frames = {row["epoch_after"]: row for row in map(json.loads, source)}
    events = list(map(json.loads, (root / (run + ".actions.jsonl")).read_text().splitlines()))
    assert events.pop() == {"complete": True}
    assert len(events) == len(frames) == 322
    variant = "before" if "before" in run else "after"
    for event in events:
        row = frames[event["epoch"]]
        assert row["present_returned"] and row["dispatch_to_present_ms"] is not None
        if event["sample"] >= 0:
            groups[variant, event["scenario"], event["action"]].append(row)

results = []
for (variant, scenario, action), rows in sorted(groups.items()):
    results.append({
        "variant": variant,
        "scenario": scenario,
        "action": action,
        "samples": len(rows),
        "dispatch_to_present_ms": stats([row["dispatch_to_present_ms"] for row in rows]),
        "inclusive_ms": {
            phase: stats([row["inclusive_ms"][phase] for row in rows])
            for phase in rows[0]["inclusive_ms"]
        },
        "calls": {
            phase: sorted({row["calls"][phase] for row in rows})
            for phase in rows[0]["calls"]
        },
    })

assert results == report["results"], "Stored summary differs from raw measurements"
print("variant,scenario,action,samples,median_ms,p95_ms")
for row in results:
    timing = row["dispatch_to_present_ms"]
    print(f'{row["variant"]},{row["scenario"]},{row["action"]},{row["samples"]},'
          f'{timing["median"]:.3f},{timing["p95"]:.3f}')
