# Renderer status and next work

Updated 2026-10-23. The broad NWN-overview optimization campaign is ended;
retained code remains the starting point. Historical investigations are
[archived](render-perf-evidence/README.md), including rejected candidates and
incomplete acceptance. Archiving a record does not resolve its outstanding risks.

## Retained results

Measured on Ryzen AI 9 HX 370 / Radeon 890M, Linux Vulkan. These results do not
establish cross-hardware performance or DA:I/Ryse scene capacity.

| Work | Evidence and limits |
| --- | --- |
| Reduced repeated frame work | Corrected-reference full-client host medians: city 4.248 -> 2.304 ms, gate 3.226 -> 1.548 ms, outdoor 1.037 -> 0.956 ms. These are CPU host-work durations, not FPS. [Acceptance record](render-perf-evidence/history/render-perf-gameplay-acceptance.md). |
| Runtime geometry reuse | Repeated glTF placements share decoded/uploaded geometry while retaining independent transforms and materials. [Contract and results](render-perf-evidence/history/render-shared-gltf-geometry.md). |
| Runtime surface-texture reuse | The controlled 16-material helmet workload drops from 19 to 4 uploads, 304 to 64 MiB of base texture uploads, and 1623.369 to 188.621 ms of material loading. This is a load/memory result. [Contract and results](render-perf-evidence/history/render-shared-surface-textures.md). |
| Correctness and measurement | Coverage repair, explicit resource/submission failures, fixed-step offscreen benchmarks, and scoped image/edit/lifetime checks remain. Known verification gaps are listed below. |

The renderer retains static grouping/sort/payload reuse, unchanged-pose and
transform reuse, shared view constants, binding summaries during surface
collection, and Taskflow particle simulation/packet preparation. Most model
preparation/submission remains on the main thread. Individual gains must not be
added together. Original ownership contracts and costs remain in the archive.

GPU particle expansion, indirect model submission, alternative pacing policies,
and rejected preparation/batching trials are absent from production. The last
particle draw-batching trial reduced submissions but failed its frame-time target;
[its rejection](render-perf-evidence/history/render-particle-draw-batches.md) stands.

## Next work

**First finish history/documentation consolidation.** Then make one bounded
model-scaling measurement using existing glTF inputs and the offscreen benchmark.
Vary actual geometry, material and animation work without particles; record
input counts, complete-frame CPU/GPU time, memory and failure counters. Existing
small glTF fixtures are correctness inputs, not representative final-art scenes.
NWN gameplay and whole-area overviews remain regression/stress controls.

The deliverable is a concrete expensive transform and its cost, or an explicit
capacity result if the tested scale exposes no worthwhile bottleneck. Only then
choose one implementation change. Reuse current tools; do not start another
profiling framework or repeat rejected experiments without new evidence.
Set a bounded initial screen and a worthwhile gain before implementation; extend
validation only for a clear candidate. Report timing uncertainty. Historical
unchanged-code runs show that tiny differences can be unresolved; they do not
justify changing old acceptance decisions or relaxing visual correctness.

## Unresolved backlog

These are distinct outcomes, not prerequisites for every future optimization.

| Item | What remains / when to revisit |
| --- | --- |
| Model scaling and final-art capacity | The next bounded measurement above; a representative authored scene is still needed to claim final-art capacity. [Workload requirements](render-perf-workload-contract.md). |
| Presentation pacing and rare stalls | Absolute paced gameplay acceptance remains incomplete. FIFO prototypes were rejected; historical 51-137 ms host stalls remain unexplained. Separate presentation cadence/latency from render throughput. [Record](render-perf-evidence/history/render-perf-frame-pacing.md). |
| Interaction and content acceptance | Native pointer/Wayland input, broader actor behavior, the incomplete large-view historical comparison, and remaining attachment/skin contract questions are not certified by synthetic checks. Existing image/edit/door tests retain their stated scope. [Acceptance](render-perf-evidence/history/render-perf-gameplay-acceptance.md), [architecture questions](render-perf-evidence/history/render-perf-parallel-frame-preparation.md#open-questions-and-required-evidence). |
| Loading and memory | Startup/navigation (~1.6 s in the recorded city case), unique texture upload overlap, cache residency and frame-depth storage are deferred. Revisit against a measured load or memory budget. [Navigation](render-perf-evidence/history/render-perf-navigation-transitions.md), [uploads](gfx-texture-upload-completion.md), [residency](render-asset-cache-residency-budget.md), [depth](render-perf-evidence/history/render-perf-frame-depth.md). |
| Broader hardware | Existing gains and timing decisions are local to this machine/driver. No iGPU-specific policy or universal CPU/GPU placement rule follows. |

## Constraints

Basic NWN1 gameplay requires at least 60 fps with substantial headroom; toolset
overviews target 30 fps. DA:I/Ryse describes the art goal, not a measured workload.
Preserve rendering and behavior, follow AGENTS.md, use plain C-style C++ with
small contiguous working sets and explicit ownership, and choose policy from
actual data rather than asset names. Keep runtime conversion. Offline compilation,
new scheduler machinery and broad CPU/GPU rewrites are not prerequisites.

## History

The pre-consolidation history, including the last rejected particle trial notes,
is retained locally at `backup/main-before-renderer-squash-20261023`.
Historical commit IDs in archived reports refer to that history. The
[evidence index](render-perf-evidence/README.md) separates source-controlled
reports from local build artifacts and verified external archives. No new
performance result follows from consolidating commits or documentation.
