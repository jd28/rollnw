# Reuse packed material surface textures

Status: **historical record**, archived 2026-10-23. This file preserves the
original results, limits and plans; its old status labels and checklists do not
define current work. See the [current renderer status and backlog](../../rendering-performance-overhaul.md).

## Recorded investigation

Status: **implemented, verified and retained in the working tree**, 2026-10-23.
**Tier 1**: a bounded change to
the existing runtime material uploader. No new asset pipeline or GPU protocol.

## Patterns & Conventions Found

- `lib/nw/render/model_asset.cpp:398`: the uploader already reuses color/normal
  sources by source index and upload variant within one asset.
- `model_asset.cpp:481`: surface packing separately decodes metallic/roughness
  and occlusion, allocates a third RGBA image, and uploads for every material.
- `model_texture_cache.hpp:45`: cross-model encoded-source sharing has a
  different lifetime and full-content identity; retain it as-is.
- `gfx/gfx.hpp:161`: cumulative resource counters measure upload bytes,
  submissions and waits. `tests/render_gfx_resources.cpp:73` reads all texture
  mip levels under Vulkan validation. Reuse both, plus existing offscreen corpus
  captures and the vendored Abseil flat hash map. Match local structs, snake_case,
  RAII ownership, explicit failures and clang-format.

## Architecture Decision

Frame: remove repeated source-pair -> packed ORM -> GPU texture work within one
material batch, and avoid allocating an extra output image for packing. Worth
keeping if repeated materials upload one surface per distinct source pair with
exact pixels and a measured material-load improvement. Plan B is to retain only
the independently verified packing simplification if reuse fails correctness.

Real data: DamagedHelmet has one material and five 2048x2048 RGB JPEGs; its
surface combines two distinct source images. MetalRoughSpheres has one material
and two 1024x1024 RGBA PNGs, with metallic/roughness but no occlusion image.
CesiumMan has one 1024x1024 RGB JPEG and no surface image. A controlled variant
will use 16 materials with different factors but shared image indices, plus a
variant using one source for both ORM roles. These variants are capacity tests,
not evidence of production scene distribution.
ASSUMPTION: future authored materials reuse surface images while varying factors
— affects realized savings, not correctness of sharing within this batch.

Platform: current Ryzen/Radeon Linux Vulkan build, one calling thread and
synchronous uploads. Finite host/GPU memory, linear pixel bandwidth and upload
queue submissions constrain this load-time transform. No CPU/GPU topology policy.
Sources and material source indices are stable during a call; material factors
remain independent. Decoded pixels are short-lived and never retained in a cache.

## Component Design / Data Flow

Validated material/source arrays -> lookup by ordered (metallic/roughness source,
occlusion source) indices -> decode unique pair -> pack into an owned decoded
buffer -> existing synchronous upload -> copy the resulting bindless index to
materials. Model ownership stores each created texture once; the transient
lookup expires at return. No draw-time lookup or changed material/GPU layout.

Use a flat map of a 64-bit pair key to {texture index, fallback flag}. Missing
both sources returns the existing neutral fallback without a map entry. The
UINT32_MAX source sentinel remains valid for a missing role; other out-of-range
indices reject before reads. Missing/bad images preserve neutral-channel partial
fallbacks; size mismatch rejects packing. Cache failure outcomes during this
call, as the existing direct-source path does; a later call retries. Diagnostics
count unique attempts, while fallback materials are still counted per material.

If both roles reference one source, decode it once. Otherwise decode each
present source. Reuse the metallic/roughness pixel buffer when valid, retaining
G/B and replacing R/alpha; otherwise reuse the occlusion buffer with neutral
G/B/alpha. Preserve dimensions, row ordering, exact channel bytes and mip rules.

Cost: transient O(unique surface pairs) small metadata, linear lookup per
surface-bearing material, and existing linear image scans. A 2048x2048 RGBA
packing output is 16 MiB; reusing an input removes that allocation, not necessarily
16 MiB from whole-process peak RSS. Decoded source pixels remain the only packing
scratch. Texture handle ownership stays on the model; no new pointer-heavy hot
path. Pixel buffers are contiguous, format decisions are outside the pixel loop,
and per-role presence branches are uniform across an image.

## Implementation Map / Build Sequence

1. Record baseline upload counts/bytes/submissions and material-load times on
   real unique fixtures and the controlled reuse variants before source edits.
2. Modify `lib/nw/render/model_asset.cpp`: local pair lookup, single-source decode
   and in-place packing; no public interface changes.
3. Extend `tests/render_gfx_resources.cpp` and, if needed,
   `tests/render_model_asset.cpp` for exact uploaded texels/mips, pair separation,
   unchanged factors, missing/corrupt/mismatched inputs, ownership and retry.
4. Build, run affected tests, compare offscreen before/after images, then measure
   the final version with no concurrent build/test/capture work.

## Critical Details / Simplification / Done

Do repeated packing/upload once; reuse an existing buffer instead of allocating
another image. Do not add cross-asset surface caching, content hashes, async
uploads, decoded-image retention, a GPU packing pass or an offline compiler.
The existing public material upload is the batch transform; its internal scalar
helpers are used only inside that batch.

Done: the 16-material helmet variant creates one surface texture, all material
factors survive, distinct pairs stay distinct, failures preserve explicit
fallback semantics, all uploaded mip bytes and rendered captures match, affected
tests pass, and material-load timings show a clear reuse-workload improvement.
Unique controls retain their texture count and output. Report noisy timing
limits; no FPS, cross-hardware or final-art capacity claim follows from this work.
One-off probes and evidence live under ignored
`build/render-shared-surface-textures-2026-10-23/`.

## Results

The production change is confined to `lib/nw/render/model_asset.cpp`; two GPU
readback/regression tests were added in `tests/render_gfx_resources.cpp`. Public
interfaces, material layout, shaders and runtime frame preparation are unchanged.
The preceding shared-geometry optimization is retained.

Material upload timings below include source file reads, image decode, packing,
texture creation, upload and synchronous completion. They exclude glTF parsing,
geometry conversion/upload, renderer startup, model destruction and rendered
frames. The reuse cases are controlled material batches derived from existing
assets: 16 material rows use the same source indices with different material
factors. The packed-ORM variant points both roles at the same image index.

Release build on the current Ryzen/Radeon Linux machine; validation off for
measurements, OS caches warm. Four process starts in A/B/B/A order, with two
warmups and three timed material uploads per case/start. The table reports the
median of the two process medians. No build, tests or capture work from this task
ran concurrently. These are sampled load timings, not frame-rate measurements.

| Case | Materials | Material-load median ms, before → after | Base upload MiB, before → after | Texture uploads / waits, before → after |
| --- | ---: | ---: | ---: | ---: |
| DamagedHelmet | 1 | 192.549 → 187.089 | 64 → 64 | 4 → 4 |
| DamagedHelmet | 16 | 1623.369 → 188.621 | 304 → 64 | 19 → 4 |
| MetalRoughSpheres | 1 | 8.852 → 8.864 | 8 → 8 | 2 → 2 |
| MetalRoughSpheres | 16 | 80.623 → 8.300 | 68 → 8 | 17 → 2 |
| MetalRoughSpheres (same-image ORM) | 1 | 10.442 → 9.747 | 8 → 8 | 2 → 2 |
| MetalRoughSpheres (same-image ORM) | 16 | 100.634 → 9.984 | 68 → 8 | 17 → 2 |
| CesiumMan | 1 | 12.075 → 11.844 | 4 → 4 | 1 → 1 |

The 16-material helmet case removes 240 MiB (78.95%) of base-level texture
uploads and 15 uploads/waits; its sampled material-load median falls 88.38%.
Its two baseline process medians were 1631.281 / 1615.456 ms; candidate medians
were 185.086 / 192.156 ms. The ordinary 16-material spheres case falls from
78.455 / 82.791 ms to 8.287 / 8.314 ms. Reuse gains are well separated in this
sample. Single-material controls retain their counts and have small/overlapping
timing differences; no general single-material speedup is claimed.

Upload byte totals exclude generated mip levels, driver allocation granularity,
other scene resources and process RSS. The model owns each uploaded surface only
once. In-place packing also removes the separate width × height × 4 output
allocation (16 MiB for the helmet's 2048x2048 surface); that is an algorithmic
scratch reduction, not a measured whole-process peak-RSS reduction.

## Verification

- Release build of `rollnw_test` and `mudl` passed; `git diff --check` passed.
- **235 tests across seven affected suites passed, zero skips.** New GPU tests
  compare every mip of five packing combinations against explicitly authored
  expected texels, including reversed source pairs and same-image ORM.
- Tests cover shared upload counts/bytes/waits, independent material factors,
  missing roles, corrupt sources, size mismatches, material-local fallback state,
  retries in later calls, unmodified source bytes and texture release after moves.
  The existing invalid-index/context and upload-failure tests also pass.
- **Ten before/after captures are pixel-identical**, including unique/repeated
  geometry, PBR materials, transparency, a skinned model and the 16-material
  helmet scene with visibly different roughness/metallic factors.
- New GPU readback tests run with Vulkan validation and report zero errors and
  warnings. Correctness captures use validation; timing probes do not.

Evidence under `build/render-shared-surface-textures-2026-10-23/`:
`probe.cpp`, `build_probe.py`, `run_probe.py`, `probe-runs.json`,
`measurements.json`, `tests.xml`, `image-checks.json`, `corpus.json`,
`baseline-captures/`, `candidate-captures/`, `change.patch`, and `hashes.json`.
The snapshots and isolated patch preserve earlier worktree edits.

## Final simplification and self-check

The plan's input, cost, ownership, fallback and done criteria are satisfied.
Sharing uses two validated indices, not source names, approximate content hashes,
material factors or hardware identifiers. Missing-both-role materials bypass the
lookup and keep the neutral fallback. Each populated pair is attempted once per
call; diagnostic counts describe unique attempts and fallback counts describe
individual materials. A later call starts with an empty lookup and can retry.

The simplification pass removed repeated packing/upload, a duplicate decode for
same-image ORM, the extra full-size packing output allocation and copying the
unchanged channels into it. It retained linear exact byte transforms and existing
model ownership. Metadata is small and local to the material batch; decoded
pixels do not outlive upload. There is no new pointer-heavy hot path, worker
system, GPU packing shader, global cache, public option or offline asset build
step. No unresolved architecture choice is needed to retain this scoped result.

GPU allocation-failure injection specifically for surface reuse, cold-disk load
latency, full-scene FPS, other GPUs and final-art scene capacity were not measured.
Those limits do not change the verified reuse-workload loading/memory result.
