# Model view constants experiment — 2026-10-16

Tier 2: change the CPU/shader model submission protocol. **Accepted and retained.**
Evidence: `build/perf-view-constants-2026-10-16`.

## Patterns & Conventions Found

- `lib/nw/render/model_renderer.cpp:336` builds/copies scene and surface constants
  per primitive; the instanced path at line 884 repeats view/light preparation.
  Existing `SceneConstants` is 1,200 bytes and `SurfaceConstants` is 160 bytes.
- `lib/nw/render/model_render_context.hpp:12` is the shared submission context;
  `viewer/session.cpp:1774` submits three color passes with the same view.
  `viewer/scene_shadow.cpp:1284` and `:1566` own each actual shadow view.
- Existing frame uniform allocation, two uniform descriptors (b1/b4), flat
  shader structs, spans, invalid-resource returns, and include expansion suffice.
  No new backend, allocator, descriptor binding, executor, or dependency is needed.
- Follow adjacent snake_case APIs, standard-layout assertions, and clang-format.

## Architecture Decision

Upload shared scene constants once per view; upload model/normal transforms and
material constants together per draw. Reuse b1/b4. Keep the existing 1,200-byte
SceneConstants layout for shared view data, including unused legacy fields: its
fixed per-view cost avoids a second near-identical view layout and changes to
unrelated debug shaders. Model vertex shaders instead read transforms from b4.

The problem is repeated view preparation and upload on the main thread, not draw
count. The previous profile attributes 23.15% inclusive main-thread samples to
model batch submission; it does not isolate uniform preparation. Stop if measured
submission/frame latency does not justify this protocol change; plan B is restore
production source and retain the evidence. No camera restriction is assumed.

Real platform: Linux Release/O3, Ryzen AI 9 HX 370, Radeon 890M UMA, Vulkan 1.3.
Single-thread submission, shared CPU/GPU memory bandwidth, 256-byte uniform offset
alignment constrain this experiment. Existing allocations consume 1,536 aligned
bytes/draw; the proposed 288-byte draw block consumes 512, plus 1,280/view. These
are layout costs, not timing predictions. Descriptor/draw counts stay unchanged.

## Component Design

- **View upload:** consume batches of SceneConstants and produce equal-length
  UniformSpan arrays in the current gfx frame arena. No heap allocation. Invalid
  context or mismatched lengths fail without upload; allocation failure leaves an
  invalid output. Prepared ModelRenderContext carries one non-owning view span.
  Singular preparation is a size-one wrapper, and shadow views use the same path.
- **Draw protocol v1:** ModelDrawConstants = two mat4 (offsets 0/64), followed by
  existing SurfaceConstants (offset 128), total 288 bytes, b4 in both stages.
  b1 retains SceneConstants v1. Both are built/shipped with matching shaders;
  no persisted wire data or runtime version negotiation is introduced.
- **Lifetime:** upload after begin_frame; freeze each view through its draws;
  discard at end_frame. Never retain across frames/devices. Unprepared contexts
  preserve standalone calls by preparing once per supplied model batch. Supplied
  frame spans obey the existing gfx ownership contract; indices remain the model
  and material addressing mechanism. Gfx/backend pointers are true singleton
  service references, and mapped upload pointers are required by the gfx API.
- **Ranges/errors:** existing geometry/material/skin/instance checks remain;
  failed uniform uploads skip draws. Main-view data is prepared only after shadow
  texture assignment. Each cascade/local light receives its own view. No clamping
  or approximation of camera matrices, lighting, or material values is added.

Input shape: current fixed 32/64/96 overview and 32/96 local scenes; previous
opaque draw counts are 2,507/3,287/4,587 and 830/830. Static instanced opaque/cutout
surfaces dominate; scalar, skinned, transparent, water and mesh-particle paths
must preserve behavior. Camera/lighting/shadows change per view/frame; geometry
and materials are mostly stable, transforms vary by draw. Read existing prepared
records, write only frame GPU uniforms. Do not rescan source assets.

ASSUMPTION: these five existing workloads remain representative for this narrow
submission change — affects retention criteria, not a claim about every area.

## Implementation Map

- Modify `model_render_context.hpp`, `model_renderer.hpp/.cpp`: shared view span,
  batch upload and size-one preparation, 288-byte draw record, existing fallbacks.
- Update `render_context.hpp` and `shaders/scene_constants.inc.hlsl` comments to
  distinguish shared scene data from the model draw block.
- Update `model.hpp`'s material-layout comment to point at the shared draw include.
- Create `shaders/model_draw_constants.inc.hlsl`: one b4 layout shared by model
  vertex/pixel shaders; modify the eight model shaders to consume it.
- Modify `viewer/session.cpp`, `viewer/scene_shadow.cpp`, and, if needed,
  `viewer/scene_particles.cpp`: prepare view at its actual submission boundary.
- Modify `tests/render_gfx_resources.cpp` and `tests/render_shader_includes.cpp`:
  cover view changes/reuse across frame slots and the shared shader contract.

## Data Flow

RenderContext + assigned shadow textures -> SceneConstants -> one frame view
span -> model batches + transforms/material -> per-draw b4 -> existing draw.
Shadow matrices -> separate SceneConstants -> same view upload -> shadow batches.
Unprepared standalone batches take the same upload path with count one.

## Build Sequence

1. Freeze shipping runtime/shaders; measure all five baseline views before edits.
2. Implement shared view upload and draw protocol together; compile and run
   targeted render tests with Vulkan validation and actual NWN assets.
3. Compare exact deterministic reference/candidate screenshots and logical counts;
   screen three alternating pairs/view, then six 600-frame pairs/view if promising.
4. Simplify/review; retain only if gates pass. Document evidence and open questions.

## Critical Details / Done Criteria

- Latency is the target. Require >=5% median opaque submission reduction at
  overview-96, improvement in >=4/6 paired runs there, and no >5% regression in
  host median/p95 or GPU scene median on any of the five views. Require host
  improvement on overview-96; a memory reduction alone does not pass.
- Match 1920x1080, camera, content, fixed time/step, warmup and CPU/worker affinity.
  No builds, tests, captures or profiles concurrent with timing runs.
- Images must match exactly; draw/instance/index/visibility and simulation counts
  must match per frame; only uniform allocation/byte counters may differ.
  Validation must report zero warnings/errors and resource/drop failures zero.
- Simplification: do shared work once, combine draw data into existing b4, keep
  existing view layout. No cache key, dirty-state machine, persistent material
  table, new command threading, approximation, or backend extension is needed.
- An unprepared single-primitive preview uploads 128 extra bytes (1,488 versus
  1,360); shared views amortize that cost over their draws. Reserved uniform-ring
  capacity remains 32 MiB per frame slot; reduced usage is not a capacity reduction.
- Access is linear writes to a frame ring, predictable view selection then
  straight-line draw copies. Existing random material/primitive reads remain.
  No added synchronization: preparation and submission stay on the owner thread.
- Verify scalar/instanced/skinned/PLT/water/shadow paths and multiple views/frames;
  report any paths not represented by fixtures. Open question: will removing
  repeated view work reduce measured latency enough to keep this change?

## Results

The experiment passes all predeclared gates. Six alternating reference/candidate
pairs per view, 180 warmup + 600 measured frames per run, use identical camera,
resolution, fixed time/step, model/particle content and main/worker affinity.
Values below are medians of the six run medians, in milliseconds. Paired host
percentages are medians of within-pair changes, so they are not the percentage
change between the two aggregate medians.

| View | Opaque CPU before → after | Host frame before → after | Paired host change | GPU scene before → after |
|---|---:|---:|---:|---:|
| Overview 32×32 | 0.546 → 0.470 | 2.117 → 1.982 | −6.10% | 0.920 → 0.888 |
| Overview 64×64 | 1.011 → 0.898 | 8.010 → 7.728 | −2.46% | 3.050 → 2.976 |
| Overview 96×96 | 1.629 → 1.474 | 18.421 → 17.821 | −2.78% | 5.135 → 5.170 |
| Local 32×32 | 0.167 → 0.145 | 1.385 → 1.344 | −2.76% | 1.336 → 1.298 |
| Local 96×96 | 0.209 → 0.185 | 4.620 → 4.625 | +0.85% | 2.149 → 2.142 |

Overview-96 opaque submission improves 9.53% between aggregate medians, and
improves in all six pairs (individual changes −5.87% to −10.26%). Host frame time
also improves in all six pairs; its aggregate p95 falls 19.970 → 19.671 ms.
Every view meets the ≤5% host median/p95 and GPU scene median regression limits.
Local-96 has no demonstrated whole-frame improvement: only two of six host pairs
improve, although its opaque submission improves in all six.

In overview-96, the same 15,592 indexed model draws now require 4,793,712 uniform
upload bytes versus 21,502,336. Total uniform-ring usage falls 24,345,536 →
8,385,728 bytes (65.56%); uniform allocations fall 32,732 → 17,145. Descriptor
allocations, draw calls, instances, indices, visibility and simulation counters
are unchanged. Ring capacity is unchanged. These are per-frame used-byte counts,
not a claim that the reserved GPU allocation or process RSS shrank proportionally.

For these fixtures, all indexed draws are models. Every compared frame satisfies
the exact layout prediction with D model draws and V = 1 + shadow cascades + local
shadow views: allocation reduction D−V; upload-byte reduction 1,072D−1,200V;
aligned ring reduction 1,024D−1,280V. The particle pass's ring-demand delta grows
by 128 bytes because it includes alignment after the preceding 288-byte model
draw block; particle allocations and upload bytes themselves are unchanged.

### Measurement controls and exclusions

The three-pair screen passed correctness and showed a promising timing direction,
but process records showed KDE's file indexer active. Final measurement suspended
that indexer and restored its prior running state in a finally block. The first
32×32 reference run still overlapped 1.54 CPU seconds of indexer drain after the
DBus state reported Suspended; its paired candidate had no such activity. That
entire pair was excluded from timing aggregation before final analysis and
replaced with round 6 after observing two seconds of actual CPU quiescence.
Both original runs remain archived and included in counter validation. No other
timing pairs were excluded. No indexer CPU use was recorded in the remaining
final runs or replacement pair. The indexer was resumed afterward.

No builds, tests, captures or profiles overlapped timing runs. The baseline was
frozen and measured before production edits. Fixture/harness regular files were
copied and hash-compared with the baseline inputs; external game assets remain
linked. Per-run commands, schedules, affinity, process CPU/RSS/background data,
raw frame samples and summaries are retained.

### Correctness and final self-check

- All 124 targeted tests passed (`RenderGfxResources`, `RenderPreparedModelDraws`,
  `RenderViewerPreparedDraws`, `ShaderIncludes`), with zero Vulkan VUID messages.
  The expanded culling test was rerun successfully after adding direct transient
  roots used by mesh particles, alongside scalar/grouped/instanced rendering.
- The added view regression renders two different views in one frame, changes
  lighting across both frame slots, checks independent offsets and rejected
  output lengths, and compares shared versus standalone view uploads exactly.
- All five deterministic 1920×1080 screenshot pairs match exactly (RGBA), with
  zero validation warnings/errors. Logical counters match across 4,500 screen
  frame pairs, 18,600 final/exported frame pairs (including the replaced pair),
  and five capture pairs. All uniform-byte changes satisfy the formulas above.
- Static, skinned, mirrored, water, shadow and preview paths pass the existing
  tests/captures. The city workloads contain zero mesh-particle packets; the
  transient-root model submission path is covered by the expanded pixel test,
  not by a dedicated full mesh-particle effect capture. No particle simulation,
  animation policy or camera limit was changed. No other GPU/backend was measured.
- Simplification retained two uniform bindings and the existing frame allocator;
  it removed repeated view preparation/copies and one uniform allocation per
  model draw, amortized against view uploads. No cache state, material table,
  worker pool, persistent cross-frame view or new dependency was introduced.
- Flat draw layout and offsets are asserted. View uploads are a batch transform;
  size-one wrappers use it. Existing public model batch paths share the same
  primitive emission code. Ownership, frame lifetime, invalid lengths/context,
  allocation failure and existing geometry-range rejection remain explicit.
  Backend singleton and mapped-upload pointers are justified above; model and
  material addressing remains index-based.
- Formatting and `git diff --check` pass. Final rebuilt mudl is byte-identical
  to the measured candidate. No source edits changing runtime behavior were made
  after freezing it; the later changes were a material-layout comment and tests.

Reference SHA256: `c1f4ec968eb8608e1287326cff21b334cb55cc7322d66e2e1a60e2369de020ac`.
Retained candidate SHA256: `a1f4ce8455346fa2f30cf6e9dfb3f319aff25b0ede12c8e9b0083579aa384d92`.

The open question for this experiment is resolved: the lifetime split is worth
retaining on the measured platform. The complete 96×96 overview still costs
about 17.8 ms per host frame; this result does not establish a CPU ceiling or
justify a camera restriction.

Evidence archive:
`/home/josh/projects/rollnw-evidence/2026-10-16/view-constants.tar.gz`, SHA256
`d68d240d0284bd95191f18b39615b8935952a9a34addfba3649d930d07f269e9`.
All 1,035 payload files were verified by streaming the archive and checking their
manifest hashes (1,476,221,424 payload bytes; 203,824,841 compressed bytes).
Includes both runtimes and shader trees, complete trial patch/source snapshot,
pre-implementation plan, report, fixed fixtures/harness, raw frame data, captures,
test/build logs, exclusion rationale, indexer restoration records and self-check.
