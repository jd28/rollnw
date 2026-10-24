# Shared live model textures

Status: closed, 2026-10-03. Implementation commit: `36536ac92`.

Cluster: [Rendering performance overhaul](../rendering-performance-overhaul.md).

Live texture sharing, ownership and load reduction verified; no steady-state
frame improvement is claimed.

Implementation checkpoint: implemented and verified, 2026-10-02. See final measurements below; diagnostic trace timings in the plan are not used as speedup measurements.

Tier 2: shared texture ownership crosses model/upload/service boundaries.

## Patterns & Conventions Found

- `lib/nw/render/model_asset.cpp:396`: material upload already batches source indices within a model, but each model owns separate textures.
- `lib/nw/render/viewer/preview_render_resources.cpp:68`: weak model lookups share only live assets across scene replacement.
- `lib/nw/render/nwn/render_asset_cache.hpp:26`: the legacy cache destroys its textures on clear. Borrowing these from independently owned modern models would invalidate live models.
- `lib/nw/render/model.hpp:403`: the graphics context must outlive model resources; callers wait for GPU work before destruction.
- Reuse the existing material uploader, decoder, gfx upload/error handling, Abseil flat hash map/hash, and shared/weak ownership conventions. Follow clang-format and existing snake_case APIs.

## Architecture Decision

Frame: remove repeated decode/upload of identical encoded source textures across live models. Plan B: retain the existing uncached uploader if complete load timings regress; do not substitute an unmeasured async queue. This is worth solving before asynchronous uploads: on Ryzen AI 9 HX370 / Radeon 890M, a traced city load decoded 957 sources (836,377,600 RGBA bytes), with 82 unique payload/format/premultiply combinations (63,825,920 bytes). Outdoor: 1,035 sources / 92 unique combinations, 677,414,400 / 50,950,400 bytes. Trace hashes identify candidates; production equality must compare full bytes. Instrumented decode totals were 607 / 534 ms; upload/premultiply/create totals 333 / 350 ms. Logging perturbs these timings.

Data: imported model arrays contain encoded DDS/TGA/PLT source rows and material source indices. Both real 32x32 fixtures (`test`, `fdsaf`) repeat one tile 1,024 times with no objects; varied controls are city 256 tiles and outdoor 512 tiles. Textures dominate reuse across distinct models in the varied areas. Source bytes and upload variants are stable while models live; palette colors/lighting can change without changing these raw payloads. Source indices are validated before access. Missing/invalid payloads retain current fallback/error behavior.

Platform: one render thread, synchronous graphics uploads, integrated GPU/shared system memory. This targets area-load latency and texture storage, not CPU parallel throughput. Dominant removed transform is duplicate encoded bytes -> decoded RGBA -> synchronous GPU upload. New cost is a linear content hash/full equality scan on load plus one retained encoded payload and shared ownership record per live unique texture. Probe unique encoded payloads: city 12,094,144 bytes, outdoor 13,857,970 bytes. No decoded-pixel retention. No per-frame cache lookup.

ASSUMPTION: 32x32 is the supported placement limit — affects benchmark coverage, not an arbitrary texture or crowd budget.
ASSUMPTION: existing render-thread serialization and context-outlives-model contract remain valid — affects reference lifetime and cache access.

## Component Design

- `ModelTextureCache`: one context-specific weak lookup of immutable encoded texture payloads and upload variants. Owned by RenderService; cache deletion/clear does not destroy textures still used by models. Expired rows are pruned at upload batch boundaries. Hash collisions compare bytes and variants and remain distinct. External files bypass sharing because a path alone is not immutable content. ORM packing remains on its existing path (not present in measured repeated work).
- Shared texture record: owns one graphics texture, bindless index, exact encoded bytes, effective decoder type, GPU format and premultiply flag. Models retain shared references. Last owner releases the texture under the existing GPU-idle lifetime contract.
- Existing upload batch: validated source/material indices -> content/variant lookup -> existing decode/upload on miss -> shared reference and bindless material indices. No sharing on failure; retries retain existing behavior.

## Implementation Map

- Add `lib/nw/render/model_texture_cache.hpp/.cpp`: ownership, exact content lookup, expired-row pruning, observable live/source/payload counts.
- Modify model.hpp/model_asset.cpp/model_asset.hpp: shared ownership column, moves/releases, optional upload cache and hit counters; preserve uncached callers.
- Modify render service and viewer resource facade: own/expose cache, connect NWN model upload descriptors and mesh particles; clear lookups safely.
- Modify render CMake and focused model tests; add evidence under this issue.

## Data Flow

Contract version: in-process source API (no persisted/wire format). Input: source indices are invalid sentinels or less than source count; material binding rows are absent or match the material count. Upload formats are RGBA8/RGBA8Srgb. The existing image decoder retains its dimension/pixel limits. Validated material/source arrays are borrowed for the duration of `upload_model_asset_material_textures`; sources carry owned immutable byte vectors, effective decoder type, RGBA8 or RGBA8Srgb format, and boolean premultiplication. Output: model-owned material rows with bindless indices and references to successfully uploaded texture records. Each texture belongs to exactly one graphics context; mismatched cache contexts are rejected before GPU work. Missing sources/failed decode/upload use supplied fallbacks and increment existing counters. Empty/external sources bypass the lookup. The batch API is unchanged; the cache is a true singleton lookup per context, not a separate singular texture transform.

Pointer justification: shared/weak references are used only during asset loading/unloading to express lifetimes spanning independently destroyed models and scenes. Integer indices alone cannot retain those owners without a second manually reference-counted registry. Frame rendering still reads contiguous material rows and integer bindless indices, with no shared-pointer traversal.

## Build Sequence

1. Remove temporary probe; implement exact sharing and ownership, then service integration.
2. Test variant/content/context separation, cache clearing, moves, last-owner release, corrupt-source fallbacks, and existing model/import tests.
3. Matched hardware A/B with tracing off: city, outdoor and both real 32x32 fixtures; three alternating pairs each. Capture representative city/outdoor images with validation; test repeated scene replacement. Record load time, uploaded payload/count and frame time.

## Critical Details

Simplification: do duplicate work zero times after the first live upload; reuse existing decode/upload and fallbacks. No asynchronous queue, worker pool, persistent disk cache, eviction budget or speculative surface/file sharing. Full bytes preserve exact correctness rather than approximate hash identity. Pruning affects lookup metadata only.

Done: varied-area upload counts approach observed unique combinations, total loads improve in matched runs, small/max repeated-tile cases do not materially regress, lifetimes/variants/fallbacks pass tests and validation. A load regression, stale texture after changed content, or premature texture destruction falsifies the approach. Frame/render equivalence must be measured; existing capture variability is documented separately. Outstanding question: a populated 32x32 gameplay fixture and creature/effect budget remain unspecified; existing max fixtures measure placement repetition only.

## Results — 2026-10-02

Release / Tracy OFF on the Radeon 890M, 1920x1080 gameplay camera, 16 ms simulation steps, 30 warmup + 240 measured frames per run. Three fresh-process pairs per area, order reversed in repetition two; no concurrent builds/benchmarks. These are area load times, excluding process/renderer/module startup. OS caches were not flushed. Timed runs have tracing and validation off.

| Area | Tiles | Load before → after (ms) | Reduction | Texture uploads | Base-level upload MB before → after |
|---|---:|---:|---:|---:|---:|
| ms_4city | 256 | 1054.600 → 215.993 | 79.5% | 957 → 82 | 836.378 → 63.826 |
| pvp_area_2 | 512 | 932.086 → 221.309 | 76.3% | 1035 → 92 | 677.414 → 50.950 |
| pl_drowcity_005 | 56 | 218.822 → 80.979 | 63.0% | 209 → 32 | 174.946 → 29.002 |
| pl_icemount_007 | 64 | 93.848 → 30.721 | 67.3% | 99 → 18 | 66.718 → 11.618 |
| test | 1024 | 2.433 → 2.299 | 5.5% | 1 → 1 | 0.000 → 0.000 |
| fdsaf | 1024 | 2.446 → 2.470 | -1.0% | 1 → 1 | 0.004 → 0.004 |
| max_city | 1024 | 1292.579 → 371.803 | 71.2% | 957 → 82 | 836.378 → 63.826 |

Texture payload is upload accounting, not a direct allocator/VRAM measurement; generated mips are excluded. The cache additionally retains 12.09 MB / 13.86 MB encoded bytes for city/outdoor exact equality. GPU textures and encoded payloads follow live model ownership; there is no cache residency extension or decoded pixel cache. All varied-area after-load times were below every corresponding before-load time. Repeated-tile loads remain roughly 2.3–2.5 ms; `fdsaf` median increased 0.025 ms with overlapping ranges.

### Populated 32x32 coverage

`max_city` is explicitly synthetic: four translated copies of the real 16x16 city, generated only under `/tmp`, using the original module and hak/override/tlk resources. It has 1,024 tiles, 268 doors, 288 authored placeables (276 rendered), 72 waypoints, 1,680 model records and 600 particle systems. Its unique assets are unchanged from the original city. This measures placement scaling with fixed asset diversity, not a worst-case unique-asset/crowd budget. The initial fixture omitted hak resources and is excluded.

The corrected max fixture loads in 371.803 ms median (332.651–408.307), versus 1,292.579 ms (1,236.807–1,320.002). Both original/max city share exactly 82 live texture variants / 63.83 MB base-level pixels after this change. No original module file was changed.

### Steady-state frame controls

Host frame medians (median of per-run nearest-rank percentiles), including begin-frame waits, simulation/update/render and submission; final drain and JSON are excluded. GPU timings are asynchronous and must not be added to these.

| Area | Host p50 before → after (ms) | Host p95 | Host p99 |
|---|---:|---:|---:|
| ms_4city | 2.362 → 2.284 | 3.062 → 2.814 | 4.385 → 3.838 |
| pvp_area_2 | 1.146 → 1.137 | 1.522 → 1.446 | 1.984 → 1.923 |
| pl_drowcity_005 | 0.724 → 0.693 | 0.978 → 1.007 | 1.473 → 1.442 |
| pl_icemount_007 | 0.436 → 0.415 | 0.522 → 0.490 | 1.169 → 1.060 |
| test | 0.204 → 0.200 | 0.256 → 0.246 | 0.353 → 0.286 |
| fdsaf | 0.739 → 0.732 | 1.055 → 1.036 | 1.464 → 1.481 |
| max_city | 7.865 → 8.073 | 10.109 → 10.768 | 13.090 → 11.878 |

No frame-rate improvement is claimed. The max fixture's p50 is 7.865 → 8.073 ms, with per-run medians 7.845/8.658/7.865 before and 8.073/9.260/7.814 after: ranges overlap, so a small frame-time effect remains unresolved. In the after build its CPU total-render p50 is 5.683 ms, pre-render 1.359 ms, tick 0.959 ms; shadow submission is 1.853 ms inside total-render. Opaque GPU p50 is 1.951 ms. CPU scene updates and draw/shadow preparation/submission remain material; this is not yet a negligible populated 32x32 frame. No combat, pathfinding, moving camera or NPC crowd is included.

## Verification and final self-check

- Built `mudl`, `rollnw_test`, and dependency `rollnw-client`; `git diff --check` passes.
- 152 distinct tests passed across RenderModelAsset, RenderModelLoader, RenderAssetCache and RenderViewerPreparedDraws. Initial dedicated-data run: 147 passes, four desktop-asset skips and one missing-desktop-model failure. Those five all passed on rerun with `NWN_ROOT` pointing at the full install. The three new tests exercise ownership across clear/move/last release, full-byte/type/format/alpha separation, expired rows, failed-source retry, context rejection and raw PLT sharing.
- 42 timed hardware runs / 10,080 measured frames; no dropped draws or failed animation samples in the reports. All seven transition cases per build passed with zero validation warnings/errors (14 cases). Post-change city revisits were 208.5/204.3 ms versus 1,104.6/1,131.7 ms; outdoor revisit 221.1 versus 1,069.8 ms. Transition timings include validation and are separate from the performance table.
- Eight fixed-time captures with validation, all zero warnings/errors. Outdoor is pixel-identical across both before/after pairs and repeat controls. City pairs differ on 45–50 / 2,073,600 pixels, max channel delta 31/255; repeat captures of unchanged builds differ on 46–49 pixels at the same maximum. Every changed before/after pixel lies within the union of repeat variability. City exact image equality remains unverified; no differences outside that variability were found. Viewed the resulting city/outdoor images.
- Existing asset failures remain: city `plc_s03` and outdoor `plc_d07` imports are rejected. The synthetic city inherits the former. Measurements describe the loaded scenes, not repaired asset fidelity.
- Simplification: reused the decoder, upload batch, fallbacks and render-service lifetime. No new threads, asynchronous uploads, file cache, persistent retention or eviction policy. Local per-source bookkeeping uses two allocations instead of six and distinguishes all four format/alpha variants; the variant test also covers the pre-existing linear-albedo/sRGB-emissive alias case. No performance attribution is made to that small correctness change.
- Contracts/ranges, singleton lookup exception and load-only shared-pointer justification are recorded above. Common repeated sources avoid decode/upload; cold failures preserve fallbacks. Full-byte equality handles hash collisions without false reuse. The cache is not consulted during drawing.
- Done criteria met for load improvement, expected unique upload counts, repeated-tile controls and ownership/render validation. Remaining questions are the small max-scene frame-time effect, CPU frame cost, full game startup/nav/crowd budgets and existing city capture variability; no claim that 32x32 gameplay is now negligible.

Evidence: `/tmp/rollnw-texture-sharing/results.json` records the measured
runs. Reproduction scripts, reports, trace logs and captures remain in that
temporary directory, not tracked issue-local artifacts. The preserved baseline
is `build/tools/mudl/mudl-before-texture-sharing`, with the earlier palette,
benchmark and zero-light changes already present. Storage-cold loading and
actual allocator-resident memory were not measured.
