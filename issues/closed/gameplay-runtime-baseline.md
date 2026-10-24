# Gameplay runtime baseline

Status: closed, 2026-10-03. Implementation commit: `cc28fc28b`.

Cluster: [Rendering performance overhaul](../rendering-performance-overhaul.md).

Fixed-step and complete host-frame measurement implemented; recorded CLI, area
and component checks passed. Broader workload/budget acceptance is RP-01/RP-06.

Tier 1: repair the existing measurement path, then collect a baseline before
choosing a runtime optimization. No runtime speedup is claimed by this change.

## Patterns & Conventions Found

- `tools/mudl/viewer_runtime.cpp:148` already runs batches of headless frames and
  reserves contiguous samples; reuse it. It currently freezes simulation.
- `lib/nw/render/viewer/session.cpp:1420` samples animation and synchronizes
  instances before the existing render timer starts at line 1549. Preserve that
  timer's meaning and expose the missing preparation interval separately.
- `tools/mudl/mudl_cli.cpp:517` and `tests/mudl_cli.cpp:116` provide the CLI and
  parser test pattern. Use the existing JSON report and Google Benchmark suites.

## Architecture Decision

Input: authored NWN areas, an existing gameplay camera preset, and a batch of
warmup/measured frames. Output: existing scene/counter/GPU data plus fixed-step
metadata and complete host-frame timing, followed by a measured priority list.
Reuse the current harness rather than introduce another profiler.

Observed inputs: ms_4city has 256 tiles, 67 doors and 72 placeables; pvp_area_2
has 512 tiles, 2 creatures and 18 placeables; pl_drowcity_005 has 56 tiles and
one door; pl_icemount_007 has 64 tiles. These are environment workloads, not
crowded combat. Existing particle/navigation/combat benchmarks provide separate
synthetic batch workloads. Their distribution in a shipping game is unknown.

ASSUMPTION: 1920x1080 on this Ryzen AI 9 HX 370 / Radeon 890M Linux Vulkan
machine is a useful initial baseline — affects workload choice, not a shipping
performance promise. Release build with Tracy disabled is the timing reference.
CPU and GPU run asynchronously and share memory; submission time is not GPU
execution time. Measure host latency and GPU pass costs separately.

## Component Design

- CLI: `--step-ms` accepts integer 0..1000; default 0 preserves frozen captures.
  Positive values advance animation/particles each warmup and measured frame.
  Reject missing, malformed, negative and larger values. The camera and time of
  day remain fixed. This is viewer simulation, not the full gameplay loop.
- Frame batch: local contiguous samples own copied stats and host intervals
  until serialization. Time begin-frame (including waits), tick+render, and
  end-frame; sum for host-frame duration. Exclude report serialization and final
  queue drain. No per-frame allocation after reservation.
- Session stats: add preparation duration before the existing render interval;
  keep old `total_render` semantics compatible. Stable geometry/materials and
  volatile animation/particles follow existing ownership and update behavior.

## Implementation Map

- Modify `tools/mudl/{mudl_cli.hpp,mudl_cli.cpp,main.cpp,viewer_runtime.hpp,
  viewer_runtime.cpp,README.md}` for the fixed-step option and host timings.
- Modify `lib/nw/render/viewer/{session.hpp,session.cpp}` for the missing timer.
- Modify `tests/mudl_cli.cpp` for the option contract. Preserve unrelated edits.
- Store compact evidence and results beside this issue; raw reports can live in
  `/tmp` during collection. No module inputs are written.

## Data Flow

CLI validation -> area load/camera setup -> enable playback if step > 0 ->
warmup -> measured frame batch -> JSON -> percentile analysis. Failed load or
begin-frame returns an error; no successful report with partial frame samples.
All frame work stays on the existing calling thread. The session/context are
genuine singletons for this run; models/particles/frames retain batch processing.

## Build Sequence

1. Add the option and additive timings; build mudl, tests and benchmarks.
2. Run parser and relevant viewer checks, then compare frozen/advancing real
   areas. Collect sequentially, without concurrent builds or benchmarks.
3. Run existing combat, navigation and particle batches; report distributions,
   load/cache costs, measured priorities and missing coverage.

## Critical Details

Cost: a few extra clock reads per measured frame, one extra session clock read,
one float per session stats record and three doubles per harness sample; linear
sample writes and a predictable warmup branch. No new resource lookup, global
state, thread, owning pointer or per-model branch. Preserve local naming and
clang-format style. Exact instrumentation overhead is unmeasured.

Simplification pass: do not replace Tracy or add a profiling framework; retain
existing counters and GPU timers, collect once per frame, serialize after the
batch, and use existing benchmarks. No camera-path or crowd-generation feature
until evidence calls for it. No approximation of simulation or rendering.

Done: valid frozen and advancing reports with finite nonnegative timings,
complete frame counts, CLI boundary checks, and baseline results that distinguish
host and GPU time. Reject conclusions if workload counters are invalid or runs
are unstable. Missing end-to-end crowds, walking and frame pacing must be stated;
the headless run cannot prove shipping-game performance. Plan B is to retain
only trustworthy component results and describe the missing measurement.

## Results — 2026-10-01

Release, Tracy OFF, Ryzen AI 9 HX 370 / Radeon 890M, RADV STRIX1, Mesa
26.2.2-arch1.1. Device identity was captured through the existing resource trace;
tracing was disabled for timed runs. The initial sandbox pilot used software
rendering because `/dev/dri` was hidden; it is excluded. Hardware runs used the
approved batch outside the sandbox. Base commit: `f51aedc47117`, with the working
tree palette fixes and this instrumentation. No runtime optimization was made.

Each area: 1920x1080, gameplay camera, 30 warmup + 240 measured frames, three
fresh-process repetitions each of frozen and 16 ms advancing playback. Order
was reversed on repetition two. Builds and benchmarks did not overlap. The OS
file/shader caches were not flushed: "fresh process" is not cold disk I/O.

Values below are the median of the three runs' nearest-rank percentiles, in ms.
Host timing includes begin-frame waits and submission; it excludes final GPU
drain, sample storage and serialization. These are headless throughput/host
frame measurements, not displayed frame pacing or input-to-photon latency.

| Area | Models / particle systems | Frozen p50 | Advancing p50 | Advancing p95 | Advancing p99 |
|---|---:|---:|---:|---:|---:|
| ms_4city | 420 / 150 | 2.748 | 2.881 | 3.942 | 4.966 |
| pvp_area_2 | 578 / 16 | 2.262 | 2.271 | 3.866 | 4.041 |
| pl_drowcity_005 | 58 / 8 | 1.869 | 1.840 | 2.507 | 2.893 |
| pl_icemount_007 | 66 / 0 | 1.560 | 1.557 | 1.937 | 2.050 |

The city advancing p99 varies from 4.663 to 6.115 ms across runs; do not treat
small frozen/advancing differences as a speedup or a stable effect size. The
added pre-render timer exposes roughly a quarter millisecond of city preparation
that the old `total_render` field omitted. Full results are in
`/tmp/rollnw-gameplay-baseline/summary.json`; raw host frame times
are in the `run-*.json` files in that temporary directory.

Existing Google Benchmark cases ran with `--benchmark_min_time=0.05s` and three
repetitions. These are median **wall time per benchmark iteration**, not frame
percentiles. Dedicated-server test resources and the existing fixtures were
used, independently of the authored-area renderer runs.

| Component workload | Median wall time |
|---|---:|
| One scripted attack, three creature fixtures | 0.0488–0.0577 ms |
| 128 direct / obstructed / door path queries | 0.330 / 0.435 / 0.332 ms |
| 128 navigation locomotion updates | 0.0060 ms |
| One door navigation rebuild, four affected tiles | 1.388 ms |
| Fresh navigation build, 64 / 1,024 synthetic tiles | 36.4 / 585.5 ms |
| 10,240 particles across five mixed emitters: tick | 0.0658 ms |
| Same mixed particle fixture: render packet construction | 0.0874 ms |

All 45 benchmark cases completed without benchmark errors. Retained raw output:
`/tmp/rollnw-gameplay-baseline/components.json`. Particle fixture
copies are outside the timer. Single-emitter cases requesting 5,120 and 10,240
particles hit `kMaxCompiledParticlesPerEmitter = 4096`, but their old counters
report requested counts; exclude those throughput/count claims. Mixed-emitter
cases split the total across five emitters and stay within that limit. The
preview fixed-tick cases use idle input, so they do not establish walking cost.

One long-lived viewer visited city, outdoor, cavern, mountain, city, outdoor.
Loads took 1,029 / 1,048 / 237 / 86 / 945 / 932 ms. The revisits still rebuild
the scene; retaining the service alone does not make these transitions cheap.
The advancing city runs also recorded 966 cumulative upload submissions/waits
and about 837 MB uploaded, including initialization. This is a candidate for
load-phase investigation, not proof that all loading time is GPU upload.
See `/tmp/rollnw-gameplay-baseline/area-transitions.json`.

## Priority and cost

Follow-up: the first experiment below is now implemented and measured in
[forward-plus-zero-lights.md](forward-plus-zero-lights.md). The baseline data
here remains the pre-filter reference.

1. **Small next experiment: exclude non-contributing lights before Forward+
   clustering.** The city sends 446 lights to Forward+, while only five of its
   550 scene lights exceed the existing colored-light threshold (1e-4). CPU
   cluster bounds/depth filtering reject bad radius/intensity but accept black
   RGB (`forward_plus_renderer.cpp:193,287`). GPU overflow policy also uses the
   unfiltered candidate count. Check actual RGB values, preserve stable scene
   indices/shadow slots, filter exact zero contribution in the existing batch,
   and compare day/night images and frame timings. Cost: a per-light predicate
   in existing CPU traversal and regression coverage; no new cache needed.
   GPU/CPU savings remain **unverified** until that experiment.
2. **Keep construction off the active frame budget.** Area loads near a second
   and fresh navigation construction up to 585 ms dwarf these steady frames.
   Investigate existing upload batching and prebuilt navigation inputs first.
   Cost: load/pipeline work, retained or serialized data and invalidation rules;
   a larger change than filtering lights. No saving or schedule estimate is
   established by this baseline.
3. **Measure crowds before changing combat scheduling or animation ownership.**
   These authored areas have only two creatures; component timings cannot prove
   combined frame cost. Cost: a representative crowd/camera workload and a
   timeline capture. The 0.066 ms mixed particle tick is not evidence that an
   animation/particle subsystem rewrite would be a good first investment.

Tracy already has CPU scopes in resource, scripting, particle and graphics work.
Use it for nested timing and thread/wait investigation once a phase is selected;
retain these Tracy-OFF runs for comparisons. Allocation and GPU timeline hooks
are not currently integrated. No Tracy configuration change was needed here.

## Verification and limits

- Built mudl, rollnw-client (dependency), rollnw_benchmark and rollnw_test.
- Passed 17 CLI checks and four existing viewer animation/particle/area checks.
  The viewer checks ran on the sandbox's software Vulkan implementation.
- Collected 24 complete hardware runs (5,760 measured frames), 45 component
  cases and six successful area transitions. Timings were finite/nonnegative;
  measured area frames reported no dropped draws or failed animation samples.
- Performance captures did not enable Vulkan validation; zero validation
  counters must not be read as a validation-layer pass.
- The authored city rejects model `plc_s03`; the outdoor area rejects `plc_d07`
  during import. Baselines describe the resulting loaded scenes, not complete
  asset fidelity. Empty optional resource-container warnings also occur.
- Camera movement, end-to-end combat crowds, display pacing, allocations and
  storage-cold loads remain unmeasured. No gameplay speedup is claimed.
- Simplification/self-check: reused the batch runner, stats and benchmark suites;
  no new profiler, cache, thread or pointer-heavy hot path. New step boundaries
  reject invalid input explicitly; frozen behavior is preserved. Tests and
  actual report counts meet the measurement criteria, with the limits above.

Reproduce an advancing area run (replace the area; use `--step-ms 0` for control):

```sh
./build/tools/mudl/mudl area-benchmark ms_4city \
  --module /home/josh/projects/the_awakening --camera gameplay \
  --step-ms 16 --frames 240 --warmup 30 --width 1920 --height 1080 \
  --json /tmp/city-playing.json
./build/benchmarks/rollnw_benchmark \
  '--benchmark_filter=^(BM_particles_|BM_recast_|BM_nav_locomotion_batch|BM_preview_|BM_creature_attack_direct_nwn1_combat_module_context)' \
  --benchmark_min_time=0.05s --benchmark_repetitions=3 \
  --benchmark_out=/tmp/gameplay-components.json --benchmark_out_format=json
```

Use real GPU access for renderer comparisons. Full temporary reports, logs,
device trace and the batch runner are under `/tmp/rollnw-gameplay-baseline`.

Follow-up: [shared live model textures](model-texture-sharing.md) removes repeated
decode/upload across distinct model assets. Its matched loads, frame controls,
32x32 coverage and ownership checks supersede the loading-cost baseline here.
