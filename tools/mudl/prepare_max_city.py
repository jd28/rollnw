#!/usr/bin/env python3
"""Reproduce the fixed 32x32 rendering workload from the authored 16x16 city."""

import argparse
import copy
import hashlib
import json
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path)
    parser.add_argument("user", type=Path, help="NWN user data supplying hak/tlk/override")
    parser.add_argument("output", type=Path, help="New, isolated benchmark user directory")
    args = parser.parse_args()

    source = args.module / "shared/areas/ms_4city.caf.json"
    source_bytes = source.read_bytes()
    source_hash = hashlib.sha256(source_bytes).hexdigest()
    if source_hash != "c3ae513fbc8bd2577d8f80fa010a6fa25927b11f4c7cdb16bcf6a3811ee65102":
        parser.error("ms_4city differs from the recorded baseline; establish a new baseline explicitly")
    base = json.loads(source_bytes)
    if (base["width"], base["height"], len(base["tiles"])) != (16, 16, 256):
        parser.error("expected a 16x16 source city with 256 tiles")
    if not args.user.is_dir():
        parser.error("NWN user data directory is missing")
    if args.output.exists():
        parser.error("output already exists; choose a new directory")

    area = copy.deepcopy(base)
    area["width"] = area["height"] = 32
    area["object"]["resref"] = area["object"]["tag"] = "max_city"
    area["name"] = {"strref": 4294967295, "strings": [
        {"lang": 0, "string": "32x32 benchmark: four city copies"}
    ]}
    area["tiles"] = [copy.deepcopy(base["tiles"][(y % 16) * 16 + x % 16])
                     for y in range(32) for x in range(32)]
    for key in ("creatures", "doors", "encounters", "items", "placeables",
                "sounds", "stores", "triggers", "waypoints"):
        area[key] = []
        for oy in (0, 160):
            for ox in (0, 160):
                for original in base[key]:
                    row = copy.deepcopy(original)
                    position = row["components"]["location"]["position"]
                    position[0] += ox
                    position[1] += oy
                    area[key].append(row)

    payload = (json.dumps(area, indent=2) + "\n").encode()
    fixture_hash = hashlib.sha256(payload).hexdigest()
    if fixture_hash != "5fe65099bc4cb24fc2b027d5810dd7c0eb22b8839c942939ec929ff8f3c7afdc":
        parser.error("generated fixture differs from the recorded baseline")
    (args.output / "development").mkdir(parents=True)
    for kind in ("hak", "tlk", "override"):
        target = (args.user / kind).resolve()
        if target.is_dir():
            (args.output / kind).symlink_to(target, target_is_directory=True)
        else:
            print(f"Missing optional user container: {target}")
    (args.output / "development/max_city.caf.json").write_bytes(payload)
    (args.output / "fixture.json").write_text(json.dumps({
        "source": str(source.resolve()), "source_sha256": source_hash,
        "fixture_sha256": fixture_hash, "user": str(args.user.resolve()),
    }, indent=2) + "\n")
    print(f"Prepared {args.output}: {fixture_hash}")


if __name__ == "__main__":
    main()
