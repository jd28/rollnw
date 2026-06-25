# Render Asset Cache Lifecycle

## Problem

`RenderAssetCache` now has an explicit invalidation path and mudl clears it at
scene-load boundaries, but it is still a whole-cache policy. Long-lived tools
need a measured asset lifetime policy for sessions that intentionally keep
multiple scenes or previews resident.

## Current Contract

- Owner: `nw::render::nwn::RenderAssetCache`.
- Inputs: resource names, PLT palettes, decoded source images, particle mesh
  model resrefs, and GPU fallback texture handles.
- Outputs: cache-owned GPU texture handles, decoded source image pointers, and
  cache-owned particle mesh model instances.
- Observability: `RenderAssetCache::stats()` reports entry counts,
  `texture_upload_bytes`, `source_image_pixel_bytes`, and
  `particle_mesh_payload_bytes`. These are cache-owner payload counters, not
  backend allocator residency. `mudl area-benchmark` and `mudl area-sweep`
  include these counters as `asset_cache` JSON snapshots for collecting
  representative session data. `model_loader_resource_cache_stats()` reports
  the static MTR material and texture-analysis cache entry counts, and
  `RenderService::stats()` exposes them as
  `nwn_model_loader_resource_cache`; mudl area reports include both snapshots.
- Invalidation: `RenderAssetCache::clear()` destroys cache-owned textures except
  the caller-owned fallback texture, clears decoded image and particle mesh
  caches, and clears NWN model-loader resource analysis caches.
- Mudl scene loads release the old scene before invalidating caches, then load
  the next scene into a fresh cache.

## Open Questions

- What memory budget should bound cache residency? This needs real cache
  counter samples from representative tool sessions and backend GPU allocation
  counters if exact VRAM residency is required.
- Should eviction be LRU by lookup, scene-generation based, or explicit
  per-scene ownership? Pick after measuring scene switching and area browsing
  workloads.
- `ViewerSession` loads through the service-owned cache, and `ViewerDevice` can
  create more than one live session. Do not clear the global cache from an
  individual session load/destructor until the owner is either per-session or
  the API explicitly declares exclusive use of the render service cache.
- Should `ResourceManager` expose a registry/resource generation so caches can
  automatically invalidate on `unfreeze()`, `unload_module()`, or container
  mutation? Without that protocol, automatic reload-awareness would be guesswork.

## Verification Needed

- Record `asset_cache` snapshots from representative `mudl area-benchmark` and
  `mudl area-sweep` runs, then repeat while browsing creature/placeable
  previews.
- Add backend allocator counters if an eviction policy must target exact GPU
  residency rather than uploaded texture payload bytes.
- Confirm scene reload output parity after cache invalidation on a machine with
  Vulkan screenshot support.
