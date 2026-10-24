# Shared geometry in runtime glTF import

Status: **historical record**, archived 2026-10-23. This file preserves the
original results, limits and plans; its old status labels and checklists do not
define current work. See the [current renderer status and backlog](../../rendering-performance-overhaul.md).

## Recorded investigation

Status: **implemented, verified and retained in the working tree** (2026-10-23).

Tier 1: bounded extension of the existing asset/upload contract, not a new
asset pipeline. Runtime conversion remains the entry point.

## Patterns & Conventions Found

- `lib/nw/render/model_asset.hpp:57`: CPU primitive rows currently own vectors;
  `model_asset.cpp:1086` validates the whole asset before allocating buffers.
- `lib/nw/render/model.hpp:346`: GPU primitive rows also carry placement state;
  `model_asset.cpp:733` releases resources through the model owner.
- `lib/nw/render/gltf/import_gltf.cpp:1016`: selected nodes expand each referenced
  mesh primitive independently, including repeated accessor decoding and uploads.
- Reuse existing vectors, indexed rows, `absl::flat_hash_map`, buffer upload,
  model resource lifetime, glTF diagnostics, offscreen tests and corpus captures.
  Match existing snake_case, flat structs, explicit checks and clang-format.

## Architecture Decision

Frame: remove repeated immutable geometry decoding/storage/upload while retaining
every placement. The inspected shared-mesh control contains 64 distinct nodes,
one mesh primitive, 24 vertices and 36 indices. Unique-mesh controls and the
existing CesiumMan, DamagedHelmet and MetalRoughSpheres assets provide controls.
ASSUMPTION: final content will reuse meshes within assets — affects eventual
benefit, not correctness. These controls do not establish final-art frame rates.

The platform is the current Ryzen/Radeon Linux Vulkan system. Finite host/GPU
memory, memcpy/upload bandwidth and finite buffer-handle capacity constrain this
load-time transform. Immutable geometry is stable after import; transforms,
materials, skin bindings and node identities remain separate per placement.
This is a memory/allocation requirement, not a frame-latency requirement.

Use a backward primitive index for shared geometry. An owning row retains its
existing payload; a referencing row has empty payload vectors and points directly
to an earlier owner in the same asset. No chains. GPU rows retain direct buffer
handles for existing draw loops, with an ownership bit for destruction. The
runtime needs no geometry-owner lookup.
No new per-frame lookup, worker scheduling, hardware-specific path or shader.

Cost: one CPU owner index and one GPU ownership bit, both using existing row
padding on this build, plus transient import lookup storage.
Linear vertex/index work is paid once per unique source primitive/skin binding;
static bounds still scan positions per placement to preserve exact tight bounds.
Skinned bounds may be reused only for the same source primitive and skin.
Lookup keys are source primitive indices and skin indices, never resource names.
Unique geometry incurs lookup/metadata overhead; measure it alongside reuse.

## Component Design

- CPU contract: `model_asset.hpp/.cpp`; index is UINT32_MAX for owned geometry,
  otherwise strictly earlier than the referencing row, targeting an owner with
  matching vertex format. Empty, forward, cyclic/chained, mixed-payload or
  mismatched-format references reject the asset before upload. The asset owns
  vectors; references expire with it. Existing local count methods describe
  resident payload; consumers needing placed geometry resolve the owner index.
- Import: `gltf/import_gltf.cpp`; transient indexed lookup by source primitive
  and skin binding. Decode first occurrence, reuse subsequent successful rows;
  preserve exact placement bounds, transforms and fallback skin decisions.
  Reject/drop unsupported or nonfinite data through existing diagnostics.
- Upload/lifetime: `model_asset.cpp`, `model.hpp`; upload owners once, copy only
  geometry handles/counts to references, apply placement metadata independently.
  Model destruction and partial failure destroy each owned buffer once and clear
  all references. Context and in-flight lifetime requirements remain unchanged.
- Reporting: `viewer/preview_load_report.cpp`; preserve placed vertex/index
  counts rather than accidentally reporting empty reference payloads.

## Implementation Map / Data Flow

Selected glTF nodes -> indexed primitive/skin lookup -> owned geometry plus
placement rows -> batch validation -> unique GPU uploads plus placement rows ->
unchanged drawing -> owner-only release. Failures drop invalid imported rows or
reject the asset; partial GPU allocations are released.

Modify the files above and `tests/render_model_asset.cpp`,
`tests/render_gltf_import.cpp`. One-off measurement code and artifacts live under
ignored `build/render-shared-gltf-geometry-2026-10-23/`.

## Build Sequence / Critical Details

1. Record baseline payload bytes, distinct GPU buffers, logical geometry/bounds
   and bounded CPU import timings with existing fixtures before source changes.
2. Extend validation/ownership, then importer reuse and report resolution.
3. Test invalid references, placement preservation, skin/static variants,
   buffer contents, move/release lifetime; run affected existing suites.
4. Compare offscreen baseline/candidate images for reused static/skinned and
   unique geometry. Keep only with exact pixels and deterministic payload gains.

Simplification pass: do repeated decoding and upload only once; retain current
draw path and per-placement tight bounds. No content hashing, cross-asset cache,
offline compiler, GPU instancing, SIMD or threading is needed to remove this work.
Cold lookup is indexed; vertex/index passes are linear with predictable format
branches outside element loops. Existing gfx handles remain necessary at the
backend boundary and do not add a new pointer-heavy hot path.

Done: the 64-placement shared control has one CPU geometry payload and two GPU
buffers, all 64 placements survive, logical geometry/bounds and captured pixels
match baseline, affected tests pass. Unique controls retain their geometry.
Reject on ownership/validation/visual failures. If exact bounds cannot be kept
without a larger rewrite, limit reuse to uploads and report the narrower result.
Do not claim FPS gains from allocation or import measurements.

## Measured result (final ownership layout)

The working implementation decodes and uploads each repeated source primitive /
skin binding once. All placements survive. CPU and runtime row sizes remain
184 and 192 bytes respectively on this build; the new index/ownership bit use
existing padding. Runtime draws retain their existing direct buffer handles.

The fixtures below are controlled reuse workloads: the existing 64-node shared
mesh control, and generated 16-placement variants of the repository's
DamagedHelmet and CesiumMan glTF assets. They are not production scene samples.
The shared-skin variant references one skeleton, so its placements overlap;
independent skin bindings are covered separately in the importer tests.

| Workload | CPU vertex/index payload bytes, before → after | GPU vertex/index payload bytes, before → after | GPU buffers, before → after | Warm import median ms, before → after |
| --- | ---: | ---: | ---: | ---: |
| shared | 82,944 → 1,296 | 78,336 → 1,224 | 128 → 2 | 0.138 → 0.033 |
| unique | 82,944 → 82,944 | 78,336 → 78,336 | 128 → 128 | 0.337 → 0.220 |
| shared-helmet | 14,145,792 → 884,112 | 12,662,400 → 791,400 | 32 → 2 | 8.739 → 0.879 |
| shared-skin | 3,829,632 → 239,352 | 3,381,120 → 211,320 | 32 → 2 | 5.564 → 0.451 |

The 64-placement reuse case removes 98.4375% of geometry payload bytes and buffer
allocations. Each 16-placement reuse case removes 93.75%. Byte totals describe
logical vertex/index payloads and requested upload bytes, not total process RSS,
texture memory, driver allocation granularity or peak import scratch memory.
All seven probe cases retain exact logical vertex/index bytes, transforms,
bounds, material/skin/node values and primitive counts (digest comparison).

Timing method: Release build, current Ryzen/Radeon Linux system, four alternating
process starts in A/B/B/A order; each case performs five warmups and 30 timed CPU
imports per start. The table reports the median of the two process medians.
Parsing, buffer reads and CPU conversion are timed; destruction, GPU upload,
texture conversion and rendered frames are excluded. No build, tests or capture
process from this task ran concurrently with the measurement cohort.

Reuse timings were well separated: helmet baseline medians 8.466–9.013 ms versus
0.877–0.881 ms; skin baseline 5.527–5.601 ms versus 0.450–0.452 ms. Other samples
were noisy. The table below preserves unique-asset controls rather than implying
that all imports became faster:

| Unique control | Baseline process medians, ms | Candidate process medians, ms |
| --- | ---: | ---: |
| unique.gltf | 0.457, 0.216 | 0.220, 0.219 |
| MetalRoughSpheres.gltf | 13.570, 12.945 | 18.188, 13.832 |
| DamagedHelmet.gltf | 0.904, 0.397 | 0.627, 0.626 |
| CesiumMan.gltf | 0.891, 0.448 | 0.734, 0.749 |

Unique payload/buffer counts are unchanged. Unique-input latency is not proven
non-regressing by this small noisy sample. The deterministic memory/allocation
requirement is the retention criterion; this is not a universal import-speed
claim, an FPS improvement, or proof of final-art scene capacity.

## Verification and evidence

Evidence root: `build/render-shared-gltf-geometry-2026-10-23/` (ignored local
artifacts, not a new shipped asset build system).

- `original/`: snapshots of touched files before this change, including earlier
  unrelated worktree changes. No prior optimization was reverted.
- `probe.cpp`, `build_probe.py`, `run_probe.py`, `probe-runs.json`,
  `measurements.json`: reproducible bounded CPU import/payload/buffer probe and
  final raw measurements. `sizes.json` records both row sizes.
- `corpus.json`, baseline and final captures: shared/unique geometry, helmet,
  skin, spheres, distinct materials and transparency. Baseline captures passed
  all nine entries; pixel comparisons are checked separately from exit status.
- `capture_final.py`, stepped model-benchmark captures: shared skinned geometry
  at two different animation poses, with exact logical draw counts and Vulkan
  validation checked. These runs are correctness captures, not timing evidence.
- The corpus `frame` field is only applied to spell previews. Three attempted
  model-pose captures repeated the same image and are excluded from distinct
  pose coverage; the stepped model-benchmark captures replace them.
- An initial corpus launch lacked its shader path and returned unsupported
  entries; it is excluded. An initial test launch used the wrong working
  directory and failed before any tests ran; the corrected run uses `build/tests`
  and the repository's NWN environment.

Final Release build of `rollnw_test` and `mudl` passed. All **233 tests** in
seven affected suites passed with **zero skips**. All **11 image comparisons**
are pixel-identical (nine corpus cases and two demonstrably different animation
poses). Stepped captures report zero Vulkan validation errors/warnings and exact
logical draw counts. `check_final.py` / `final-checks.json` verify those claims,
all seven logical payload digests and unchanged record sizes. `git diff --check`
passes. Existing compiler warnings remain; there were no build errors.

The done criteria are satisfied. Retain this change for the measured geometry
memory/allocation benefit and reuse-workload conversion savings. No FPS claim,
unique-asset timing acceptance, cross-GPU performance claim, allocation-failure
injection or production final-art capacity claim is made.

## Simplification and self-check

The simplification pass removed duplicate decode/normal/tangent work, duplicate
payload allocation, duplicate index validation on references, duplicate uploads,
and the former one-call callback/wrapper around node import. It kept per-placement
static bounds scans because transforming an AABB would change tight bounds.
The final pass removed the unused GPU owner index in favor of one ownership bit.
No approximation, worker pool, per-frame lookup, content-name policy, persistent
asset bookkeeping, new dependency, SIMD dispatch or hardware-specific tuning
was introduced.

Batch boundaries remain selected-node import, asset validation, upload and model
release. The scalar geometry-index accessor resolves one row inside those batch
loops; it does not duplicate a separate singular conversion path. Inputs are
borrowed for the duration of each call; assets/models retain explicit ownership.
Invalid references reject before buffer creation; partial upload failure uses
the same owner-only release path. Allocation-failure injection was not performed.
Import repair diagnostics now count decoded source/skin payloads; reused copies
do not repeat the same accessor repair diagnostics.

The real input, costs, stable/volatile fields, common unique-owner and repeated
reference cases, ranges, fallback and done criteria are stated above. Public
payload readers used by reporting resolve references; the NWN-only spawn-marker
reader still receives the NWN adapter's exclusively owning primitive rows.
There is no new pointer-heavy hot path. No unresolved architecture choice is
needed to retain this scoped change. Cross-asset sharing, sharing between different
skin bindings and final-art capacity remain outside this result.
