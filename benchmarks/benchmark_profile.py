#!/usr/bin/env python3

import argparse
import json
import re
from pathlib import Path


def load_profile(config_path: Path, profile_name: str) -> dict:
    config = json.loads(config_path.read_text())
    profiles = config.get("profiles", {})
    if profile_name not in profiles:
        raise SystemExit(f"profile '{profile_name}' not found in {config_path}")
    return profiles[profile_name]


def merged_benchmarks(profile: dict) -> list[str]:
    hard = profile.get("hard", [])
    advisory = profile.get("advisory", [])
    seen = set()
    ordered = []
    for name in hard + advisory:
        if name in seen:
            continue
        ordered.append(name)
        seen.add(name)
    return ordered


def benchmark_filter_regex(names: list[str]) -> str:
    escaped = [re.escape(n) for n in names]
    return "^(" + "|".join(escaped) + ")$"


def main() -> int:
    parser = argparse.ArgumentParser(description="Emit benchmark profile data")
    parser.add_argument("--config", type=Path, default=Path("benchmarks/benchmark_profiles.json"))
    parser.add_argument("--profile", required=True)
    parser.add_argument(
        "--format",
        choices=("filter", "list", "json"),
        default="filter",
        help="Output benchmark filter regex, newline-delimited list, or profile json",
    )
    args = parser.parse_args()

    profile = load_profile(args.config, args.profile)
    names = merged_benchmarks(profile)

    if not names:
        raise SystemExit(f"profile '{args.profile}' has no benchmarks")

    if args.format == "filter":
        print(benchmark_filter_regex(names))
    elif args.format == "list":
        for name in names:
            print(name)
    else:
        print(json.dumps(profile, indent=2, sort_keys=True))

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
