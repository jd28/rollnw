# Rendering performance overhaul

Status: **historical record**, archived 2026-10-23. This file preserves the
original results, limits and plans; its old status labels and checklists do not
define current work. See the [current renderer status and backlog](../../rendering-performance-overhaul.md).

## Recorded investigation

Status: **in progress**. Updated 2026-10-23. This page describes the current
state; experiment chronology and raw evidence are linked from the child issues.
A retained change is present in the working renderer. It is not necessarily a
new commit or a completed acceptance issue.

## Current status

**Latest bounded trial (2026-10-23):** ordered particle batching reduced overview
particle draws 1,548 -> 485 with exact images and 192 passing tests, but saved only
0.138 ms in the median paired host result and improved two of three pairs. It
failed the declared 0.25 ms/all-pairs target and was restored without a longer
cohort. No new optimization is retained from this trial.
[Results and limits](render-particle-draw-batches.md).

| Area | State | What that means |
| --- | --- | --- |
| Renderer CPU performance | **Measured gains retained** | Corrected-reference full-client comparison passes all 40 declared regression guards. City host median 4.248 → 2.304 ms; gate 3.226 → 1.548 ms; outdoor 1.037 → 0.956 ms. |
| Visual correctness | **Known regression repaired and checked** | Independent material coverage restores 20 exact client and two exact glTF images. This is a correctness repair, not an additional speedup. |
| Multithreading | **Partly implemented** | Taskflow workers perform particle simulation and packet preparation. Most model draw preparation/submission remains on the main thread. |
| Large overviews | **Current budget passes** | All twelve production starts meet the 33.333 ms median/p95 requirement. Historical before/after comparison remains incomplete. |
| Editing and lifetimes | **Scoped checks pass** | 222 additional tests and 322 synthetic edit/undo actions pass, including save/reopen, reload and object release. Native pointer validation is separate and unfinished. |
| Presentation pacing | **Unresolved; both FIFO prototypes rejected** | Unbounded FIFO adds ~129 ms median delay. Waiting for each presentation avoids that backlog but delivers only 37.69–58.27 fps. Production pacing is restored. |
| Rare stalls | **Unresolved** | Earlier 51–137 ms host stalls remain in the evidence. The later wait-attribution runs did not reproduce or explain them. |
| Runtime conversion / final-art capacity | **Geometry and surface reuse retained** | Repeated geometry and per-asset packed surface images convert/upload once. The 16-material helmet control falls from 304 to 64 MiB of base texture uploads and 1.623 to 0.189 seconds of material loading. Final-art scene capacity remains unproven. |

**The latest retained change:** material surface textures are packed/uploaded once
per source pair within each asset. The 16-material helmet workload drops from
19 to 4 texture uploads/waits and from 1623.369 to 188.621 ms of material loading.
All 235 affected tests and ten exact captures pass, including GPU mip readbacks
and independent material factors. In-place packing also removes its separate
output image allocation. Runtime conversion remains; this is a load/memory result.
[Full measurements and limits](render-shared-surface-textures.md).

**The preceding retained change:** runtime glTF geometry sharing preserves all
placements while removing repeated payloads and uploads. Sixteen helmets use
12,662,400 → 791,400 GPU geometry bytes and 32 → 2 buffers; sampled CPU import
median is 8.739 → 0.879 ms. All 233 affected tests and 11 exact image comparisons
pass, including two distinct animation poses. CPU/runtime primitive row sizes
remain unchanged. This is a loading/memory result, not an FPS result; unique-input
timings were noisy. [Implementation, full measurements and limits](render-shared-gltf-geometry.md).

**The latest retained frame-time optimization:** collecting surface-binding statistics
during construction saves a median paired **0.436 ms / 2.15%** of large-overview
host time, with 201 passing tests and 27 exact images. This is a serial,
asset-independent reduction in work. [Result and scope](render-perf-parallel-frame-preparation.md#phase-1-extract-one-simple-serial-batch-transform).

**The latest ordered-surface trial was rejected and restored:** constructing
surface payloads directly in pass order removed their permutation, but the
**0.754 ms median paired overview saving remains inconclusive** (upper one-sided
95% ratio 1.015091). Paced-city outer GPU median rose 2.26% under the declared
rejection rule. All 48 starts completed, with 202 passing tests and 27 exact
images after correcting replay input/camera-observation gaps. The original
reference-image divergence remains unexplained and preserved. No confirmation
cohort ran. The preceding draw-refresh and direct-emission trials were also
rejected; the retained binding-summary improvement remains intact.
[Decision and next step](render-perf-parallel-frame-preparation.md#phase-1-extract-one-simple-serial-batch-transform).

**The pacing investigation retained no new production optimization.**
Its useful result is a calibrated measurement of presentation queue delay.
The 16-image request was already known in RP-11; it is not a new discovery.
Earlier 16 → 4 Mailbox tests saved memory but regressed some timings, so simply
repeating that change is not an established fix.

**The unchanged-code calibration confirms limited timing resolution.** All 24
starts completed, but eight resolution checks and five directional checks failed
despite identical executables. Overview median paired difference was 0.174 ms;
paced-city outer GPU median differed by 2.28%. Observer CPU stayed at 0.484–0.721%.
This is no new renderer gain or proof of the source of interference. No more
general calibration cycle is planned. [Evidence](render-perf-parallel-frame-preparation.md#measurement-resolution-2026-10-22).

**The fused-preparation trial found a CPU gain but failed full acceptance.**
Constructing ranges and surfaces per instance saves **0.740 ms / 4.10%** in the
large overview, with all six paired host medians lower. All 202 tests and 27 exact
images pass. Paced-city outer GPU p95 rises **5.15%** (median paired increase
0.141 ms), so the candidate is **rejected and restored**. No confirmation cohort
ran. All 48 planned timing starts are preserved, including five completed starts
before a post-run observer-check repair and server interruption. Original source,
shipping runtimes and tests are hash-verified; the prior binding-summary gain
remains. No experiment is running. [Result and limits](render-perf-parallel-frame-preparation.md#fused-preparation-trial-2026-10-22).

## Measured performance and scope

Ryzen AI 9 HX 370 / Radeon 890M, 1080p full client, Release/Tracy off.
The small-view common-reference comparison contains 160 starts and 192,000
measured frames, with exact logical work. These are host-work medians, not FPS
or physical display latency. Individual optimization gains must not be added.

These measurements cover one integrated AMD GPU and its driver/compositor.
They are not cross-vendor or discrete-GPU performance validation. Local trial
rejection means no retention from that experiment, not rejection of an approach
for all hardware. Presentation results also depend on Wayland/compositor behavior.

| Uncapped workload | Reference → current host median (ms) |
| --- | ---: |
| Gate route | 3.226 → 1.548 |
| City route | 4.248 → 2.304 |
| Outdoor route | 1.037 → 0.956 |
| 32 overview | 6.281 → 3.215 |
| 32 orbit | 6.148 → 3.102 |

[Full comparison and limits](render-perf-gameplay-acceptance.md#2026-10-21-completed-small-view-common-reference-comparison).
Current 64/96 stress overviews have uncapped host medians 9.425/21.078 ms,
with 6,720/15,120 models and 2,400/5,400 particle systems. These are whole-area
stress views, not ordinary gameplay. Their current production budget passes;
the historical paired comparison stopped after a reference exit without terminal
rows. [Large-view results](render-perf-gameplay-acceptance.md#2026-10-22-large-view-correctness-and-production-budgets).

## Work present in the renderer

- Shared immutable particle definitions and [Taskflow particle batches](render-perf-particle-execution.md), including the explicitly accepted small-view tail tradeoff.
- Static grouping/sort/payload reuse and [exact draw-transform reuse](render-perf-draw-transforms.md).
- [Shared model view constants](render-perf-view-constants.md).
- [Surface-binding summary during collection](render-perf-parallel-frame-preparation.md#phase-1-extract-one-simple-serial-batch-transform), removing the normal-path validation scan while retaining explicit validation.
- [Material coverage repair](render-perf-backface-culling.md), corrected shadows,
  explicit capacity-failure reporting and 32 MiB per-frame uniform capacity.

Individual evidence, memory costs and outstanding acceptance limits remain in
those issues. GPU billboard expansion, indirect model submission, skin lookup,
nanosecond sleep, simple FIFO, in-place draw-refresh, direct-emission, ordered-surface
and fused-preparation trials are **not retained**.
They must not be reported as current capabilities or gains.

## Active work and remaining acceptance

Presentation trial: **one-pending-presentation wait rejected**
([results](render-perf-frame-pacing.md#2026-10-22-one-pending-presentation-wait--rejected)).
All twelve starts and 14,400 calibrated frames finish, with sixteen exact images
and 7,200 timed logical pairs. Every candidate run is below 60 fps; only 16/48
guards pass. Production was restored after that trial. No experiment is currently running.

Phase 0 attribution is complete after the user's architecture review.
[RP-18: runtime conversion and parallel frame preparation](render-perf-parallel-frame-preparation.md)
is the detailed Tier 2 plan: reuse current in-memory assets, build on existing
SoA columns, make hot working sets explicit and expand independent CPU preparation.
Its findings select surface preparation (4.00–4.36 ms in the large overview) for
serial simplification before threading; animation collection/sampling is
0.55–0.58 ms. The first serial change is now retained: produce binding statistics
while collecting surfaces. The six-pair screen and extended paced-city control
pass; 68 timed starts / 81,600 measured frames complete. Small-view speedups are
inconclusive, and other confirmation cases have only two pairs. This does not
establish cross-hardware gains or final-art capacity. Subsequent in-place
draw-refresh, direct-emission, ordered-surface and fused-preparation screens each
complete 48 starts / 57,600 frames and fail their retention gates; all candidates
are restored. The fused candidate establishes a 0.740 ms overview gain but fails
the paced GPU-p95 control, leaving that cost unexplained.
The later CPU profiles identify temporary draw construction and surface
conversion/ordering costs. Eliminating full-surface permutation has now been
tested without an established retained frame gain. Revisit the critical path
before another layout or worker change, accounting for dispatch/gather/merge
cost and actual modern-asset requirements.
All temporary replay hooks are removed; production hashes are verified and no
experiment remains running.

RP-12 depth storage remains unfinished. Its reference diagnostic runtime is
preserved; all five original source files and all 192 production client runtime
files now match their entry hashes. No depth-ownership candidate was implemented.
Memory savings/timing remain unmeasured.

1. **Presentation backlog and pacing (RP-08/RP-11).** Reconcile the existing
   image-count measurements with the new latency evidence. A further candidate
   must explicitly bound outstanding presentations and measure cadence, latency,
   CPU/GPU work per second, acquire waits and memory. The simple FIFO screen
   stopped after three of twelve starts because city returned one unavailable
   pixel timestamp; one complete gate pair already fails all four latency guards.
   No timing retry or complete-cohort claim.
2. **Rare-stall attribution (RP-08).** Preserve the historical stalls; ordinary
   millisecond-limiter rounding explains some pacing misses, not those stalls.
3. **Remaining acceptance (RP-06 and children).** Native pointer/Wayland and broader
   actor activity; incomplete large historical comparison; final reconciliation
   of provisional batching/pose changes with the completed scoped checks.
   Door close/reopen and edit/undo/reload checks are already complete within
   their documented synthetic-input scope.
4. **Startup/transitions (RP-15).** City startup around 1.6 s remains a separate
   measured issue. Do not mix startup cost with steady gameplay frame cost.

GPU work, SIMD, more worker jobs and an offline compiler are architectural tools,
not required checkboxes. Select the next change from observed work and account
for synchronization, bandwidth, memory and maintenance cost. No new architecture
has been approved merely because another engine uses it.

## Requirements

Basic NWN1 gameplay must reach at least 60 fps with substantial headroom toward
the DA:I/Ryse art target; whole-area toolset overviews target 30 fps. Earlier
4/2 ms overview aspirations are not mandatory gates. No new camera/content limit
has been selected. Preserve visuals and behavior, plain C-style C++ and clarity.
Use NWN and modern glTF inputs through common runtime data; never choose policy
from asset names. Keep runtime conversion now. An offline compiler remains a
possible later direction and is not a prerequisite for threading or optimization.
Follow AGENTS.md with small hot working sets, linear indexed access and measured
cache behavior; avoid new bookkeeping or abstractions without demonstrated need.

The renderer must not become optimized for one iGPU. Prefer portable reductions
in work/data and resource lifetimes. Distinguish those from hardware-dependent
CPU/GPU placement, job granularity and presentation policy; report the latter as
locally measured until broader hardware evidence exists. Do not introduce
GPU-model checks or promote local thresholds as universal policy.

## Architectural direction

Use established rendering practices while keeping the implementation plain
C-style C++ and driven by this renderer's data. CryEngine and Frostbite are
references for engineering decisions; their implementation scale is not a
requirement for this project. These are directions to evaluate, not delivered
features or unmeasured speedup claims.

| Principle | Application here | Cost to account for on supported CPU/GPU platforms |
| --- | --- | --- |
| Convert and retain stable data at runtime | Source importers produce the existing common in-memory geometry/material/animation data. Reuse conversion and invariant metadata within current ownership/generations. | Load-time CPU work, retained memory and invalidation. Offline file/version/database bookkeeping is deferred. |
| Process independent batches through jobs | Reuse the existing executor; extend beyond particles only where dependencies and granularity justify it. | CPU scheduling/synchronization, temporary storage and shared memory bandwidth. |
| Declare pass dependencies and resource lifetimes | Make reads/writes and lifetime boundaries explicit; introduce only the scheduling machinery current passes need. | CPU graph/barrier processing and maintenance, balanced against removable work and resource reuse. |
| Bound presentation backlog | Treat display cadence, rendering throughput and latency as separate measurements. | Waiting can reduce GPU occupancy; additional buffered frames cost memory and latency. |

Primary references: [CryEngine resource compiler](https://www.cryengine.com/docs/static/engines/cryengine-5/categories/23756816/pages/44959586),
[CryEngine job profiling](https://www.cryengine.com/docs/static/engines/cryengine-3/categories/1114113/pages/1048775),
[Frostbite FrameGraph talk, 2017](https://www.gdcvault.com/play/1024612/FrameGraph-),
[Vulkan swapchain guidance](https://docs.vulkan.org/samples/latest/samples/performance/swapchain_images/README.html).
The later Frostbite talk is an architectural reference, not a claim about the
specific implementation shipped in Dragon Age: Inquisition.

## Issue map

| Issue | Status |
| --- | --- |
| [RP-01: Workloads/budgets](../../render-perf-workload-contract.md) | Open |
| [RP-02: Attribution](render-perf-submission-profile.md) | Open |
| [RP-03: Static groups](render-perf-static-main-batches.md) | Implemented; final acceptance open |
| [RP-04: Uniforms/descriptors](render-perf-uniform-descriptors.md) | Open |
| [RP-05: Submission](render-perf-draw-submission.md) | Open |
| [RP-06: Acceptance](render-perf-gameplay-acceptance.md) | Open |
| [RP-07: Shadow ranges](../../closed/render-perf-shadow-surface-ranges.md) | Closed |
| [RP-08: Frame tails](render-perf-frame-pacing.md) | Open |
| [RP-09: Shader failures](../../closed/render-perf-shader-failures.md) | Closed |
| [RP-10: Constant poses](render-perf-constant-poses.md) | Open |
| [RP-11: Swapchain count](render-perf-swapchain-images.md) | Open; optional |
| [RP-12: Depth targets](render-perf-frame-depth.md) | Open; optional |
| [RP-13: Target initialization](../../closed/render-perf-benchmark-clear.md) | Closed |
| [RP-14: Particle culling](../../closed/render-perf-particle-culling.md) | Closed |
| [RP-15: Navigation transitions](render-perf-navigation-transitions.md) | Open |
| [RP-16: Shared particle definitions](render-perf-particle-definitions.md) | Implemented; small-view increase not reproduced, full-client acceptance open |
| [RP-17: Backface culling](render-perf-backface-culling.md) | Coverage regression repaired; historical NWN culling gains superseded, full-client performance acceptance open |
| [RP-18: Parallel frame preparation](render-perf-parallel-frame-preparation.md) | Phase 0 complete; surface preparation selected; serial simplification before threading; runtime conversion retained |


Close the cycle when scoped acceptance passes and required work is resolved.
Optional experiments need not all be implemented. Preserve rejected results and
record deferrals explicitly. [Evidence index](../README.md).
