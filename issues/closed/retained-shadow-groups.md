# Retained static shadow groups

Status: closed, 2026-10-03. Implementation commit: `3a67dc238`.

Cluster: [Rendering performance overhaul](../rendering-performance-overhaul.md).

Shadow-group reuse and edit invalidation verified; the combined pass has 122
passing tests and 32 clean validation captures. Full gameplay acceptance remains
RP-06.

Follow-up to [retained model poses](retained-model-poses.md).


The pose-only pass has 121 passing tests and reuses all 1,208 static max-city
instances. Its three-run median pre-render cost fell about 1.31 -> 1.03 ms;
whole-frame results remain noisy. Preserve `mudl-after-retained-poses` for the
next isolated comparison.

The earlier zero-loss profile attributes 4.77% of self cycles to shadow batch
construction. `viewer/scene_shadow.cpp:403` validates every candidate's surfaces
and linearly searches compatible model groups separately for directional and
local shadows. Asset identity, surface/material payload and instance eligibility
are stable between edits; camera visibility is volatile and must stay per-frame.

Tier 1 extension: store a flat model-index -> group-index table and group sizes
on `AreaRenderScene`. Rebuild it with the existing exact group builder when the
static/tile generations or available instanced shadow pipelines change. Full
scene rebuild clears it. Per-frame candidates map through these indices in
their existing order; first visible candidate still determines group order and
representative. Dynamic, invalid and newly appended uncached rows use the
existing scalar fallback. Recheck current instance animation policy before reuse.

Modify `viewer/area_render_scene.{hpp,cpp}` for the owner and flat cache,
`viewer/scene_shadow.cpp` for cold compilation and frame-index remapping,
`tests/render_viewer_prepared_draws.cpp` for repeated rendering and tile-edit
invalidation, and `tools/mudl/viewer_runtime.cpp` for a cache build counter.
Retained data has scene-cache lifetime and no model/surface pointers; frame
candidate indices remain transient. The existing shared asset identity checks
stay confined to the cold builder. Pipeline availability is a four-bit key,
so shader reloads cannot preserve a group that loses its required pipeline.

Cost: four bytes per scene model plus four bytes per group (roughly 7 KiB for
this scene); edit-time compilation retains the old grouping cost. The normal
frame traverses candidate indices plus groups, with a small temporary group
remap and existing submission vectors. No new GPU resource, shader layout,
draw order, visibility approximation or cross-frame candidate pointer. This
removes surface validation/group searches from the common frame; actual timing
benefit remains a hypothesis until measured. Verify unchanged commands and
images, cache reuse while the camera changes, and rebuild on actual tile edits.

## Isolated measurements (2026-10-03)

Saved executables: `build/tools/mudl/mudl-before-retained-poses` (starting
point), `mudl-after-retained-poses` (pose cache only), and `mudl` (both caches).
Raw scripts, JSON, logs and analysis are in `/tmp/rollnw-retained-poses`.
Release build, Tracy off, real Radeon 890M Vulkan; 1920x1080 gameplay camera,
60 warmup frames followed by 360 sampled frames, fixed 16 ms simulation step.
Three alternating pairs per scene for each isolated change. Values below are
medians of the three run medians. Different batches experienced different
absolute host timings; compare paired binaries within a batch only.

| Change / max-city phase | Before (ms) | After (ms) |
| --- | ---: | ---: |
| Pose cache: pre-render CPU | 1.313 | 1.033 |
| Pose cache: whole host frame | 7.077 | 6.924 |
| Shadow cache: directional shadow CPU | 2.033 | 1.780 |
| Shadow cache: local shadow CPU | 0.558 | 0.346 |
| Shadow cache: whole host frame | 7.909 | 7.449 |

All 1,208 disabled-animation max-city instances reuse published transforms;
all 472 enabled instances still advance in this workload. Held animation
reuse is covered by tests rather than inferred from these moving frames.
Shadow group compilation runs once across warmup and sampling, shared by the
local and directional passes. Across both isolated comparisons, all 6,480
paired frames have identical non-timing counters (excluding the added reuse
counter). Commands, visible geometry, particles, lighting and runtime-sync
counts agree. CPU phase timers and GPU work overlap; do not add these savings
to host-frame savings.

The shadow-only whole-frame medians for the smaller city were 2.369 -> 2.370 ms,
and outdoor 1.167 -> 1.160 ms: this batch does not establish a useful whole-frame
improvement there despite reduced measured shadow CPU time. Final combined
measurements follow below.

## Verification and simplification

- Release build passes. All 122 tests in `RenderModelInstance`,
  `PreviewModelAnimation`, `RenderViewerPreparedDraws`, `RenderViewerParticles`
  and `RenderViewerLighting` pass without skips on the desktop GPU. The new
  shadow test explicitly gives the small fixture tiles caster geometry;
  their default simplified test assets do not cast shadows.
- Forced and retained sampling match through time, clip, looping, enable,
  root and binding-index changes, independent shared-asset instances, failures,
  fallback policy changes, static publication and door-preview restoration.
- The shadow test checks a real draw, retained grouping after repeated frames
  and camera movement, one rebuild after a tile height edit, then reuse again.
- Simplification pass: retain existing pose outputs, reuse the original cold
  shadow classifier and existing edit generations. No duplicate poses, new
  GPU resources, dirty-queue framework, per-frame surface hashes or animation
  approximation. First encountered candidate still determines batch order.
- Boundary audit: null/failed animation inputs retain existing failure
  behavior and are retried; explicit output/backend/asset mutations invalidate
  pose reuse. Invalid/uncached/animated shadow rows take scalar submission.
  Dynamic and preview record changes leave the cached static prefix intact;
  full or tile-record rebuilds invalidate through existing generations.
- Persistent shadow data is flat indices and counts. Existing model/instance
  pointers are borrowed for a sampling batch or cold compilation, never
  retained in either new cache. Cache maintenance is on the render thread.

## Combined result and limits

Five alternating pairs per scene, with the same settings above, compare the
starting binary against both retained caches. Each table entry is the median
of five per-run whole-host-frame medians:

| Scene | Before (ms) | After (ms) | Change |
| --- | ---: | ---: | ---: |
| Repeated city, 32x32, 1,680 models | 7.485 | 6.714 | -10.3% |
| Original city, 16x16 | 2.179 | 2.010 | -7.7% |
| Outdoor | 1.042 | 1.026 | -1.5%, noisy |

Every paired city and max-city run improved. Outdoor had two slower pairs;
no reliable whole-frame outdoor gain is claimed. On max-city the combined
pre-render median was 1.367 -> 1.053 ms, directional shadow CPU 1.802 -> 1.574 ms,
and local shadow CPU 0.497 -> 0.308 ms. These nested timers are not additive to
host or GPU time. The extra 5,400 paired frames have identical existing
non-timing counters, bringing the moving-frame parity check to 11,880 pairs.
All shadow caches compiled once per scene across warmup and sampling.

Image/validation checks used fixed area time 11.25, step 0, three warmup and
three sampled frames, at 1920x1080. Captures cover all three scenes with normal
and radius-two visibility. All 32 validation runs report zero errors and zero
warnings. The 48 paired frozen frames match all existing render counters;
only the intentionally avoided animation runtime-sync work differs.
Outdoor and radius-two captures are pixel-identical. Unrestricted city images
are not bit-identical: original city pairs differ at 16/46 pixels and repeated
max-city pairs at 56–145 pixels out of 2,073,600. Six captures per max-city
binary found 6–129 differing pixels between baseline repeats and 36–127 between
optimized repeats. Every cross-version differing pixel also differs within
repeated captures of one binary; there are zero pixel locations unique to the
cross-version differences. This supports existing repeat variability, not a
claim of exact city image parity. Details: `city-repeat-analysis.json`,
`capture-parity.json`, and `analysis.json` beside the raw captures.

Final executable is preserved as `mudl-after-retained-runtime`; SHA-256 hashes
are in `binary-hashes.txt`. Formatting and `git diff --check` pass. Final
self-check: framing, input distribution, limits, failure behavior, cache
ownership/invalidation, batch paths, borrowed-pointer justification and
simplification are documented above; reference tests and paired measurements
meet the stated criteria. No speculative extension points or new GPU state.

The measured max-city workload still issues 9,989 draws, 9,989 descriptor
allocations and 19,806 uniform allocations writing 13,384,144 bytes per frame.
This pass removes repeated CPU preparation; it does not reduce those counts.
A 6.7 ms renderer is not the requested negligible area cost. Further work
needs to address submission and uniform/descriptor traffic, with a fresh
measurement and design before changing buffer or shader protocols. This is a
fixed-camera rendering workload with animation and particles, not a complete
gameplay frame containing AI, pathfinding, combat, streaming or crowds.
