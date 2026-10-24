# RP-16: share immutable scene particle definitions

Status: implemented; large-view gates pass. Small-view increase not reproduced by the fixed follow-up below; full-client acceptance remains open. Tier 2: bounded ownership rework. Investigation started 2026-10-10; final measurements 2026-10-11.

## Patterns & Conventions Found

- `preview_scene.hpp:369`: RenderModel assets are immutable by convention and shared by scene placements. Asset replacement, not per-frame mutation, is the production edit/reload path.
- `preview_scene.cpp:857`: append visits a model's particle payload array, selects by the actual animation payload contract, copies import data and compiles once per placement today.
- `preview_scene.cpp:893`: full particle rebuild is an explicit refresh boundary. Model-only rebuild selects indexed owners; invalid/duplicate model indices already skip.
- `preview_model_animation.cpp:718`: door leases move particle rows out of the scene and restore them later; borrowed data must survive these moves independently of current scene rows/cache.
- `model_texture_cache.hpp:66` and `preview_render_resources.hpp:70`: existing weak caches avoid keeping unused assets alive. Reuse std::shared_ptr/weak_ptr, absl::flat_hash_map and existing compiler/import/tick code; no new allocator, hash serializer, global cache or resource service.
- Preserve snake_case, explicit flat data records, existing compiler sanitization and clang-format. Tool inspectors read authored/compiled fields; only tests construct them manually.

## Architecture Decision

Frame: remove repeated immutable import/compile payload and compilation for shared placements, without changing any simulation/output. Worth retaining only if measured allocation/definition reduction is real and frame performance passes the below gate. Plan B: retain baseline and record why sharing failed; do not combine another optimization.

Real platform: Linux Release on Ryzen AI 9 HX370 / Radeon 890M, CPU cache working set and serial scene tick matter; main-thread latency is the target, not GPU throughput. Matched tests pin the main thread to fast logical CPU 2 after loading. CPU/GPU overlap.

Data: existing 96×96 city has 5,400 particle systems / 9,504 compiled emitter rows. Full serialized effect data including attachment defaults has 36 distinct definitions / 74 emitter rows. Fixed compiled emitter payload alone is 14,446,080 bytes; distinct payload 112,480 bytes, excluding vector allocations. 456/600 systems are empty in the 32×32 sample. This change shares by actual uploaded asset identity and payload index, not names or serialized equality; distinct assets may intentionally remain separate.

Stable inputs: model payloads (effect, events, animation selector/length) between asset replacement or explicit full rebuild. Volatile: owner transform/visibility, clip selection, emitter clock/seed/bursts, particle arrays and render packets. Read source payload only during construction; tick/render read const definitions and mutate instance state.

ASSUMPTION: existing immutable-by-convention RenderModel contract remains in force; callers editing a published particle payload in place must use full rebuild_particles to refresh it — affects cache invalidation. Add a regression for that explicit boundary. Standalone effects create one owned definition each and are not deduplicated.

Cost: one small shared allocation per live asset payload; one shared pointer per particle row (reference operations only on creation/move/destruction). A setup-only weak cache keyed by asset identity holds an emitter-payload-indexed weak-pointer vector. Cache misses prune expired model entries; full rebuild clears it. No tick lookup or refcount operation. Shared data may reduce cache pressure; speedup is a hypothesis until measured. Maintenance cost: adapt inspectors and add lifecycle regressions. No cross-asset content hashing/cache.

Access pattern: scene rows and per-instance emitter/particle arrays remain linear; each system follows one definition pointer into one of the repeated immutable payloads. Setup cache access is hashed/random, then linear by payload index. The common repeated-placement case reuses a live definition; compile/missing-owner work stays in setup. Tick branches and processing order are unchanged. Cache-miss reduction is a hypothesis, not a measured hardware-counter result.

## Component Design

- SceneParticleDefinition (`preview_scene.hpp`): existing ParticleImportResult + ParticleCompileResult, initialized before publication and held through shared_ptr<const>. Contains existing variable-sized owned vectors; no new serialization protocol. Definition contents are immutable during its lifetime.
- SceneParticleSystem: one strong reference to its definition, existing mutable simulation/owner state. Constructor rejects null definitions, initializes system.effect from the retained compiled effect. Moves retain a stable effect address and need no self-pointer repair; moved-from rows may only be destroyed or assigned.
- SceneParticleDefinitionCache: weak model owner plus weak definitions indexed by source payload index. Cache key is only used for setup lookup. Expired owner clears its slots, preventing pointer-address reuse from selecting old definitions; misses prune dead model entries. Cache never keeps model GPU resources/definitions alive. Full rebuild invalidates all slots. Targeted clip rebuilds reuse still-live definitions.

Pointer justification: tick already consumes a borrowed CompiledParticleEffect pointer. Stable shared definition storage preserves that boundary while leases retain rows across cache/scene changes. A scene-table index alone would require passing the scene through standalone inspectors/tick and pinning/rebinding table generations for leases. The added owning pointer is touched once per system, not per particle; hot emitter/particle traversal remains contiguous/indexed. Existing owner handles remain unchanged.

## Batch Transform Contract / Data Flow

Input: a live shared RenderModel, owner handle/index, selected animation; iterate its payload array (zero or more). Selection retains existing named replacement/default behavior; empty emitter payloads skip. Output: ordered SceneParticleSystem rows, each with a valid const definition and independent state arrays. Owner is scene/lease; definition lifetime is the union of rows holding it. Scalar scene add is existing setup wrapper over this payload batch.

Source payload index -> weak cache slot -> reuse live definition or import/compile once -> initialize mutable instance -> bind current attachments -> existing tick/render. Compiler retains existing 16-bit emitter/material bounds, particle caps, non-finite sanitization and warnings. Invalid owner/attachment behavior is unchanged. Empty effects are valid; null definition construction fails explicitly. Full rebuild drops rows/cache, then reconstructs; leases independently retain their old snapshots. Model replacement uses a new asset identity. No new protocol range or frame-time error branch.

## Implementation Map

- Modify preview_scene.hpp/.cpp: shared definition/cache records, constructor, batched construction/rebuild, read sites. Remove bespoke self-repair moves.
- Modify scene_particles.cpp, preview_load_report.cpp and mudl app_runtime.hpp/imgui_runtime.cpp/particle_tools.cpp: const definition reads only.
- Modify viewer particle, animation, mudl tests: construct immutable data before publication; add sharing/independence, move/lease survival, full refresh, asset replacement, expiration and standalone tests.
- Diagnostics stay temporary and run after timed frames; record distinct definition addresses and retained payload sizes, core/emitter state and RSS. Keep production probe-free.

## Build Sequence

1. Freeze baseline and source; current preflight. Implement ownership change alone and build tests/mudl.
2. Run particle/import/animation/inspector regressions plus area edit/replacement suites. Verify source identity (same asset shares; same name/different asset does not), clocks/seed isolation, move/erase and old-definition lifetime.
3. Early paired benchmark to reject clearly ineffective changes. Acceptance: 3 starts small local, 6 starts each large local/complete64 overview, 180 warmup + 600 samples, alternating order; no simultaneous builds. Record load time and actual definition memory separately.
4. Exact captures all views; core/emitter state comparisons local views. Archive binaries/patch/commands/resources/raw measurements and final self-check.

## Critical Details / Simplification / Done

Simplification asks once/fewer: compile/copy once per live source payload and eliminate move-time self-pointer repair. No approximate curves, sparse ticking, new threading, simulation density, camera, or LOD policy. Do not deduplicate arbitrary equal-looking assets or change core particle storage.

Done requires materially fewer retained definitions/compiled bytes, all targeted regressions and exact state/captures/counts, zero new validation/submission failures, large-local host median improvement at least 2%, and no >2% overview median or >5% large-view p95 regression across prescribed matched starts. All runs retained. A failed gate means reject/revisit the ownership implementation without declaring a hardware ceiling. Frame pacing/full-client gameplay, arbitrary module distribution and total GPU-memory acceptance are outside this experiment; report them unverified. Concurrency unchanged: cache construction on existing scene thread; definitions read-only afterward.

## Measured result and final self-check

Retain the implementation for its measured large-area benefit. Six alternating
starts per large view and three small-view starts, 180 warmup + 600 samples each,
main thread pinned after loading as planned. Medians of per-start statistics;
all 18,000 frames retained. [Raw evidence](render-perf-evidence/README.md).

| View | Baseline host median / p95 ms | Shared host median / p95 ms | Scene tick median before → after ms |
| --- | ---: | ---: | ---: |
| 32×32 local | 1.527 / 1.828 | 1.540 / 2.278 | 0.273 → 0.266 |
| 96×96 local | 8.177 / 10.463 | 7.443 / 8.948 | 5.492 → 4.911 |
| Complete 64×64 overview | 13.153 / 14.992 | 12.673 / 14.764 | 2.506 → 2.176 |

Large-local median/p95 improve 9.0%/14.5%; overview improves 3.7%/1.5%.
The prescribed large-view gates pass. Large-local median improves in all six
pairs, overview in five; p95 improves in five/four pairs respectively. Small-view
median worsens 0.9%, p95 24.6% (+0.450 ms), with two of three pairs worse.
No samples were excluded or reruns added to rescue that result. Do not claim
an across-workload win. GPU scene medians: 1.483 → 1.498 / 2.421 → 2.391 /
3.012 → 3.009 ms; CPU/GPU overlap, so these are not paced frame/FPS results.

The 96×96 inventory retains 5,400 independent systems but only 36 shared
definitions / 74 compiled emitter rows, versus 9,504 copied rows. Fixed compiled
emitter payload falls 14,446,080 → 112,480 bytes (99.2%); scene rows shrink
1,336 → 1,120 bytes. Definition metadata is 232 bytes × 36 plus shared ownership
and cache overhead; nested allocations are not included in that payload count.
One production capture per variant measures peak process RSS 853,332 → 817,416
KiB (35.1 MiB less), including initialization/graphics allocations, not isolated
retained particle memory. Median area load time across the six starts falls
1.852 → 1.687 s; loading runs before main-thread pinning.

388 tests pass without skips: simulation/import, viewer/animation/inspector,
GPU-backed object/tile edits, prepared draws and visibility. Five new tests cover
sharing with independent state, asset/payload identity, full-rebuild refresh,
lease lifetime/address reuse, and standalone move/release. Three production
captures and two inventory captures match RGBA exactly; both local particle
inventories match core hashes and reported emitter state. Every timed logical
count/command field matches, with zero reported validation/submission failures.
Production binary matches the frozen candidate and contains no inventory probe.

Self-check: data/assumptions/cost and batch ownership contract are above; reuse
and once/fewer simplification removed repeated compilation/copies and custom
move-time pointer repair. Existing sanitization/selection remains, null input
fails explicitly, no tick cache/refcount operations or speculative extension
points were added. Build and diff checks pass. Concurrency is unchanged.

The original small-view p95 increase prompted the follow-up below; general tail
causation and full acceptance remain open in
[RP-08](render-perf-frame-pacing.md) and [RP-06](render-perf-gameplay-acceptance.md).
The measured large-view improvement does not establish its precise cache cause,
a CPU ceiling, or the need for a camera restriction. The estimated 2–3 ms local /
4–6 ms overview CPU targets remain unmet. Paced/full-client gameplay, arbitrary
modules and total GPU-memory acceptance remain unverified.

## 2026-10-11 small-view follow-up

No additional production change. A separate, fixed six-pair comparison of the
same frozen binaries uses 180 warmup + 1,800 frames per start, CPU 2 after load,
alternating order. All 21,600 frames retained. The prior three-pair result above
is unchanged; longer simulation duration makes this a separate experiment.

| 32×32 local | Baseline | Shared definitions |
| --- | ---: | ---: |
| Host median / p95 ms | 1.507 / 2.049 | 1.510 / 2.018 |
| Scene tick median / p95 ms | 0.271 / 0.521 | 0.262 / 0.486 |

Host median is +0.2%, p95 -1.5%; only 2/6 paired p95s worsen, while tick median
improves in all six. The predeclared repeatable-regression test (>5% aggregate
p95 increase and at least 5/6 worse pairs) is not met. This does not explain or
erase the original increase; it supplies no repeatable regression to fix.

Temporary diagnostics hold the current executable/1,120-byte row layout fixed,
retain both shared and independently copied immutable definitions, and rebind
only definition/effect pointers between blocks. Mutable state remains untouched.
Four starts with balanced initial order each contain 12 alternating 660-frame
blocks. Keep all 31,680 rows; the first 60 frames of each block were designated
settling in advance, leaving 28,800 statistical samples. Thread CPU clocks and
context-switch counts separate execution from wall time. Source and production
binary are restored exactly afterward; no diagnostic or scheduling policy ships.

Medians across per-run/per-mode block statistics, private → shared:
host 1.540/2.477 → 1.551/2.434 ms median/p95; thread CPU
0.987/1.965 → 0.969/1.726 ms; tick median 0.287 → 0.276 ms.
Host p95 improves in 2/4 starts and worsens in 2/4; thread CPU p95 improves in all
four. There is no consistent same-process tail penalty from shared definitions.

Typical begin-frame wall medians are 0.550–0.593 ms, but thread CPU is only
0.004–0.005 ms. That path starts with the frame-slot fence wait; the measurement
establishes mostly off-CPU time, not that every microsecond is GPU waiting.
The slowest 5% cohort has update/render CPU medians 1.691–1.893 ms versus
0.924–0.989 ms across all frames. Both waiting and on-CPU work contribute.
Involuntary switches occur in 30–50% of tail frames versus 9–25% overall, a
correlation rather than proof of the cause. CPU 2 is observed at both boundaries
of every measured diagnostic frame. Frequency, cache and sibling contention
causes are not isolated.

All corresponding logical counts/commands match, with zero reported
validation/submission failures. All four final captures and particle inventories
match exactly. Each clock/resource stamp costs 193–195 ns in the setup microcheck
(four per frame); this is not a measurement of total observer overhead. Both
definition sets remain resident and the old 1,336-byte row layout is not recreated.
Instrumented timings therefore serve attribution, not production acceptance.
The earlier 388 passing regressions were not rerun: production source/executable
hashes match exactly, and the clean restoration build/diff checks pass.

Retain sharing; no new runtime fix is justified by these results. General CPU
tail variability and paced/presented gameplay remain open. To attribute the
remaining execution-time variation, correlate per-frame cycles/instructions and
frequency/sibling activity; coarse core affinity alone does not establish cause.
[Raw evidence and fixed plan](render-perf-evidence/README.md).
