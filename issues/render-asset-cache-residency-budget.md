# Render Asset Cache Residency Budget

## Problem

`RenderAssetCache` now has an explicit owner, cache/resource generations, safe
whole-cache invalidation, and epoch-aware retained consumers. The remaining
question is whether a long-lived tool session needs bounded residency between
explicit clears. There is no product memory budget or representative long
browsing trace yet, so selecting LRU, per-scene ownership, or another eviction
machine would be guesswork.

## Observed Data (2026-07-14)

Platform: Linux headless Vulkan on the development workstation. These values
are cache-owned decoded/upload payload counters, not backend allocator or exact
VRAM residency.

`mudl area-benchmark ms_4city` after one warmup and two measured frames:

- 85 owned textures, 65,217,536 texture upload bytes (62.20 MiB)
- 2 decoded source images, 61,440 pixel bytes
- 79 texture-analysis rows

One-process `mudl area-sweep` over `ms_4city`, `pl_drowcity_005`, and
`pl_icemount_007`, one warmup and one measured frame per area, retained the
service cache across cases:

| Area | Owned textures | Texture bytes | Source-image bytes |
| --- | ---: | ---: | ---: |
| `ms_4city` | 85 | 65,217,536 | 61,440 |
| `pl_drowcity_005` | 118 | 93,304,832 | 179,200 |
| `pl_icemount_007` | 137 | 106,232,832 | 179,200 |

All three cases rendered and produced zero Vulkan validation warnings or
errors. The final payload was 101.31 MiB of texture uploads. This proves growth
across area browsing; it does not establish that the growth exceeds an
acceptable tool budget.

Separate `mudl screenshot` preview sessions recorded:

- `pl_wh_ajouga.utc.json`: 1 owned texture / 16,384 upload bytes and one
  16,384-byte source image. The creature body used the separate RenderModel
  path, so this snapshot mostly covers the particle/legacy bridge.
- `rlgs_chest_003.utp.json`: 3 owned textures / 1,572,864 upload bytes.

Both preview screenshots were visually inspected and contained the expected
rendered object.

## Data Needed

Record a single long-lived Arclight session with:

1. At least 50 mixed creature/placeable previews and 10 area switches from a
   real authoring workflow.
2. The existing cache payload counters after every load.
3. Backend allocator used/committed bytes if the product budget is stated in
   VRAM rather than decoded/upload payload bytes.
4. A product budget and acceptable reload-latency target for the actual editor
   workstation class.

## Decision Gate

Do not add eviction until the observed working set exceeds the stated budget.
If it does, partition the trace by scene transition and measure reuse distance;
choose the smallest policy that removes enough residency while meeting the
reload target. Out-of-budget behavior must be explicit: evict a measured
partition or fail the load, never silently exceed a hard budget.

Done means the trace, budget, backend counter interpretation, selected policy,
and before/after residency and reload measurements are recorded here.
