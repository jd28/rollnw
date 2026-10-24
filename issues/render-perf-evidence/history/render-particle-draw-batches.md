# Ordered particle draw batches

Status: **historical record**, archived 2026-10-23. This file preserves the
original results, limits and plans; its old status labels and checklists do not
define current work. See the [current renderer status and backlog](../../rendering-performance-overhaul.md).

## Recorded investigation

Status: rejected after the fixed three-pair screen; candidate archived and source
restored. No confirmation cohort or new optimization retained.

## Result, 2026-10-23

The bounded 64-system implementation reduces overview particle draws from
**1,548 to 485 (68.67%)**, with no shader changes or particle reordering. Uniform
payload allocation falls **4,793,712 -> 4,589,616 bytes/frame**. Six scene-image
pairs at two animation phases are pixel-identical. All **192 selected tests**
pass, including a new exact GPU comparison of combined/separate calls across
texture, alpha/additive/cutout, fog, path, invalid-input and frame-reuse cases.
Existing mesh-particle integration also passes. No skipped tests.

Three alternating pairs per view, 180 warmup + 300 measured fixed-16-ms frames,
complete on the hardware GPU: **18 starts / 5,400 measured frames / 2,700 logical
pairs**. Main CPU2; existing workers CPU2/0/1/3. All logical counters match;
physical draw, binding and allocation reductions are intentional. No failed or
dropped command/resource counters and no Vulkan validation errors/warnings.
These are offscreen full-renderer measurements, not physical presentation timing.

| View | Host median, reference -> candidate (ms) | Median paired host saving (ms) | Faster host pairs | Particle CPU median (ms) | Particle draws |
| --- | ---: | ---: | ---: | ---: | ---: |
| Small local | 1.833 -> 1.674 | 0.033 | 2/3 | 0.114 -> 0.084 | 21 -> 9 |
| Large local | 5.836 -> 5.893 | -0.190 | 1/3 | 0.962 -> 0.955 | 53 -> 13 |
| Large overview | 19.928 -> 19.840 | **0.138** | **2/3** | 4.152 -> 3.911 | 1,548 -> 485 |

Medians of run medians are distinct from medians of paired differences. Overview
paired host savings are +0.272, -1.156 and +0.138 ms. The target requires at least
0.25 ms and all three pairs faster; both fail. All aggregate median/p95 host/GPU
control guards pass. No longer timing run or timing-based retry follows.

Median maximum process RSS is 839,740 -> 840,428 KiB in the overview. This includes
startup and all process allocations; it is not an isolated GPU or instance-vector
measurement. The candidate's instance vector is bounded at 5 MiB, and its input
stack buffer is 1,536 bytes on this platform. Exact retained vector/arena capacities
were not instrumented after rejection. No memory saving is claimed.

Decision: reject the implementation. A 69% particle draw reduction does not
establish a useful frame-latency reduction here. The particle phase is only about
0.24 ms lower in aggregate; do not attribute its remaining cost entirely to any
one kernel. This evidence does not justify a larger draw-submission rewrite on
draw-count reduction alone, nor disprove batching on different workloads/hardware.

Evidence: `build/render-particle-draw-batches-2026-10-23/` contains original and
candidate sources/binaries, `candidate.patch`, the written plan, all commands,
captures, XML tests, per-run JSON/timing/process/affinity data, `screen-results.json`
and `decision.json`. The initial sandbox/software-Vulkan census is separate and
provides counts only. A hardware census independently confirms overview counts.

Final self-check: contracts and pointer exceptions are above/below; the candidate
used the existing instance layout, storage arena and shader paths. Simplification
removed submissions without adding per-particle fields, a second copy buffer,
jobs or a cache. Measured retention criteria failed, so all four implementation
and test files are restored to HEAD. The libraries and test executable were rebuilt;
the shipping executable matches its original SHA-256. All 111 restored particle
and viewer-particle tests pass without skips. No production batch API remains. Presentation,
cross-hardware performance and final-art capacity were not verified by this trial.

Open question: which remaining particle preparation operation has a sufficiently
large measured cost to justify changing its data flow? Do not repeat rejected
axis, atlas, GPU-expansion or submission variants without new evidence.

## Tier 1 framing and data

Determine whether adjacent particle submissions contain enough compatible work
to justify a batch change. The existing large-overview profile spends roughly
4 ms in particle rendering, but that is not all submission cost. Do not repeat
the rejected GPU-expansion, material-copy or billboard-axis trials.

Platform: Linux Release Vulkan on Ryzen AI 9 HX 370 / Radeon 890M. CPU and GPU
share memory bandwidth; work has asymmetric core scheduling and a finite storage
arena. This census chooses no hardware-specific policy.

Input: existing live particle SoA arrays, ordered packets, resolved textures and
draw constants. Historical overview data contains about 50,000 billboards, mostly
ordinary sprites, from 5,400 systems; the local camera sees far fewer. Source
materials are stable while positions, colors, frames and order change each tick.
Output: per-frame counts of emitted packets, adjacent equal-state runs, pipeline
runs, instances and path vertices. No rendering data or ordering is changed.

ASSUMPTION: enough compatible adjacent packets survive existing visibility tests
to remove substantial submission work — affects whether implementation proceeds.
The census tests this assumption directly. Unknown final-art distributions remain
outside the claim; policy must use actual state, never asset names.

## Patterns & Conventions Found

- `lib/nw/render/particle_renderer.cpp:317`: packet traversal, existing shader
  constants, private reusable vectors and frame storage arena. Reuse these.
- `lib/nw/render/viewer/scene_particles.cpp:212`: mesh-first ordering per system;
  any later batching must flush before intervening mesh draws.
- `lib/nw/render/render_context.hpp:491`: explicit uniform and instance layouts.
- Existing `area-benchmark` fixed-step replay and JSON/image checks supply inputs
  and completeness checks. No new benchmark command or public option is needed.

## Architecture Decision / Component Design

First observe submission boundaries in a temporary diagnostic build. A flat
counter state on the existing render-owner thread compares resolved draw state;
linear traversal adds no particle copies, jobs, shader work or retained cache.
State resets each frame. Existing invalid-packet/texture/pipeline handling stays
unchanged; only valid submissions contribute. Pointer access stays in the existing
borrowed system/provider interfaces; the probe introduces no ownership graph.

## Implementation Map / Data Flow

Archive the original renderer source and executable under
`build/render-particle-draw-batches-2026-10-23/`. Temporarily instrument
`particle_renderer.cpp`: valid draw state -> run comparisons -> one count row per
frame. Preserve and restore the exact original source after the census.

## Build Sequence / Critical Details

1. Count three fixed workloads: small local, large local, large overview; include
   path and mesh counters. Preserve every completed result.
2. Stop before a production rewrite if same-state batching removes less than 25%
   of large-overview submissions. A different shader/data boundary needs its own
   explicit cost and contract before implementation.
3. If viable, state the measured counts, design one bounded candidate and require
   exact images, complete draws and a material measured end-to-end gain.

Cost of this diagnostic is integer comparisons/counters per emitted packet and
one output line per frame. Its timings cannot establish a speedup. Simplification:
reuse existing resolved state and replay, avoid another general profiler, and
reject unsupported machinery before building it. No new scheduler, global
reordering, approximations or content compiler.

Done for the census: actual counts replace the assumption, the original source
is restored, and a concrete implementation or stop decision is recorded here.

## Census result and implementation decision

Eight fixed-step samples after 180 warmup frames give 21 -> 8 compatible runs
in the small local view, 53 -> 8 in the large local view, and 1,548 -> 469 in the
large overview. No mesh or path draws occur in these sampled views. The renderer
source is restored after the census. The sandbox hides `/dev/dri`; these initial
runs use software Vulkan and establish counts only, never hardware timings.

Proceed with a bounded **Tier 2** change to the particle submission boundary.
Keep particle arithmetic, sorting within packets, shaders, textures and scene
submission order identical. Accumulate adjacent billboards with equal pipeline,
texture and alpha-gating state, then upload and draw the contiguous instance run.
Flush before a different state, path geometry, mesh draw or end of input batch.
No global texture sort is permitted: alpha blending remains order-dependent.

### Batch contract and costs

Input is a span of flat borrowed rows: system pointer plus material span. The
scene owner supplies at most 64 rows at once in a 1,536-byte stack buffer; empty
or null systems do no work. This buffer limits producer scratch without a new
allocation or persistent queue. Pointers are necessary because existing systems
own separate SoA vectors; copying their data or adding a parallel identity table
would add work. They remain stable for the synchronous render call.

Output is the existing 80-byte billboard instance layout in unchanged order,
followed by existing storage uploads and direct draws. The renderer owns reusable
instance storage; only the render owner writes it. Existing per-frame arenas own
GPU spans until their frame fence permits reuse. Uniform state is constant for
each run; view/fog state is constant across a call. Path draws retain their current
vertex path. Invalid inputs/resources retain existing rejection behavior; prevent
oversized packets from exceeding the existing 65,536-particle compiled-effect
limit. Flush at that same limit so one run requires at most 5 MiB of instance
storage. Preserve allocation-failure reporting.

Cost: one run-state comparison per packet, up to 1.5 KiB stack input rows, larger
retained instance-vector capacity and fewer aligned storage/uniform allocations.
No added per-particle fields, copies, shader fetches, tasks or synchronization.
The 64-system call boundary can split compatible runs; this deliberately bounds
the input working set. Measure actual emitted draws and capacity, not just the
unbounded census opportunity. This is a latency experiment on the current machine;
cross-hardware speed remains unverified.

### Implementation map and verification

- `particle_renderer.hpp/.cpp`: replace the singular entry point with
  `render_systems`; reuse existing packet arithmetic and upload/submit helpers.
- `viewer/scene_particles.cpp`: gather borrowed rows on the stack, drain before
  mesh submissions and at capacity/end. Preserve current mesh-first system order.
- `tests/render_gfx_resources.cpp`: compare exact GPU images and draw counts for
  combined versus separate calls, including blend/texture changes, paths, empty
  input and repeated frame reuse.

Simplification: one existing instance vector holds each run; eliminate repeated
uploads/uniform allocations/draws, without a second staging vector, shader rewrite,
global sorting or persistent invalidation state.

First run exact synthetic/corpus images and three alternating hardware pairs for
small local, large local and large overview. Stop if the overview saves less than
0.25 ms median host work, or controls regress more than 5% in median/p95 or GPU
time. Only a successful screen warrants six-pair confirmation. Require matching
logical counts, explicitly account for changed physical draw/allocation counts,
and report retained memory. Offscreen results do not certify presentation pacing
or final-art capacity. Do not rerun a failed screen to obtain acceptance.
