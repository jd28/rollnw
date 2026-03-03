# Benchmarks

This directory contains the Google Benchmark suite for rollnw and its profile-based CI gating setup.

## Domains

- `cpp`: native C++ engine/rules hot paths.
- `smalls`: Smalls VM/runtime/compiler and Smalls combat policy paths.
- `nwscript`: NWScript lexer/parser/resolver pipeline.
- `parity`: explicit C++ vs Smalls migration pairs.

Benchmark metadata and profile membership are defined in `benchmarks/benchmark_profiles.json`.

## Profiles

- `bench-pr`: fast hard-gate profile on pinned CI machine.
- `bench-nightly`: broader coverage with more advisory checks.

Use the helper to emit a benchmark filter:

```bash
python3 benchmarks/benchmark_profile.py --profile bench-pr --format filter
```

## Compare baseline vs contender

```bash
python3 benchmarks/compare_benchmarks.py \
  baseline.json contender.json \
  --config benchmarks/benchmark_profiles.json \
  --profile bench-pr \
  --warn-threshold 8 \
  --min-baseline-ns 50 \
  --out-json compare.json
```

The comparator supports:

- per-domain/group/benchmark thresholds,
- advisory-only benchmarks,
- migration pair scoreboard (`smalls/cpp` ratios).

## Governance

- New performance-sensitive features should add at least one benchmark and metadata entry.
- C++ -> Smalls migrations should add or update a parity pair in `pairs`.
- Keep noisy benchmarks (currently GC and module load) advisory until variance stabilizes.
