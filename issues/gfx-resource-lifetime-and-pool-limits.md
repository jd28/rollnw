# Gfx Resource Destroy Lifetime And Pool Limits

## Problem

`nw::gfx` destroys GPU resources immediately with no in-flight guard, and
the handle pools have unchecked capacity/generation arithmetic. Both are
implicit out-of-range/lifetime policies; AGENTS.md requires them to be
explicit. Found in the 2026-07 gfx/render audit; verified in source.

## Findings

### Immediate destroy with no in-flight guard

- `vulkan_context.cpp` `destroy_buffer`/`destroy_shader`/`destroy_pipeline`/
  `destroy_texture` call `vmaDestroyBuffer`/`vkDestroyPipeline`/etc.
  immediately. No deferred-destruction queue exists anywhere in
  `lib/nw/gfx`. With `MAX_FRAMES_IN_FLIGHT == 2`, destroying a resource
  referenced by a still-executing command buffer is a GPU-side
  use-after-free. Safe today only because in-tree callers `wait_idle()`
  first — nothing in `gfx.hpp` documents or enforces that contract.
- `lib/nw/render/shadow_renderer.cpp:36-42` and
  `local_shadow_renderer.cpp:36-42`: `ensure_resources` destroys the old
  depth textures/render targets on resolution change with no `wait_idle`.
  The sibling `FogRenderer::ensure_resources`
  (`fog_renderer.cpp:103-111`) does this correctly. Dormant today because
  shadow resolution is env-var-fixed per session, but the class contract is
  unsafe and becomes live the moment resolution is runtime-adjustable.

### Handle pool arithmetic

- `lib/nw/gfx/gfx.hpp` `Pool::insert`: `static_cast<uint16_t>(
  storage_.size())` with no capacity check. Past 65535 entries a new handle
  silently aliases an existing slot, with only the generation check between
  that and wrong-resource access.
- `Pool::destroy` increments a `uint16_t` generation; on wrap to 0 the next
  `insert` returns a handle that reports `!valid()` — the slot is live but
  unreachable and un-freeable (silent leak under create/destroy churn such
  as shader hot-reload loops).

### Related surface issues

- Frames-in-flight (2) is hardcoded independently in
  `forward_plus_renderer.hpp:122`, `model_gpu_backend.hpp:143-145`, and
  `particle_renderer.hpp:52` as `std::array<FrameStorageArena, 2>`,
  selected with `min(frame_index, size()-1)`. `gfx.hpp` does not expose the
  constant; changing the backend count would silently alias two live frame
  slots onto one arena.
- Bindless table exhaustion (`kBindlessTextureCapacity = 4096`) leaves
  `bindless_slot == 0` (the invalid sentinel) with no diagnostic — textures
  silently render as "no texture".
- `RenderTargetDesc::color[4]` and attachment `mip_level`/`layer` are
  declared but never read by the backend (only `color[0]`, always mip/layer
  0) — silent no-op for callers, and speculative generality per AGENTS.md.

## Fix

- Decide and write down the destroy contract: either (a) document
  "resource must not be in flight; caller waits or tracks frame
  completion" in `gfx.hpp`, or (b) add a small deferred-destruction ring
  (retire into a per-frame-slot list, free after that slot's fence
  signals), mirroring the existing ring-buffer reset pattern.
- Add `wait_idle` before destroy in both shadow `ensure_resources`
  (matching `FogRenderer`), or unify the three renderers' resize paths in
  one helper so the divergence cannot recur.
- `Pool::insert`: fail loudly (`GFX_CHECK`) or return an invalid handle at
  65535 entries. `Pool::destroy`: skip generation value 0 on wrap.
- Export a `constexpr` frames-in-flight count from `gfx.hpp` and size the
  three arena arrays from it.
- Log/assert on bindless-table exhaustion.
- Shrink or implement `RenderTargetDesc` MRT/mip/layer fields; reject
  non-default values at `create_render_target` until implemented.

## Open Questions

- Deferred destruction ring vs documented wait contract: the ring costs a
  per-slot retire list and one sweep per `begin_frame`; the contract costs
  nothing but leaves the hazard one careless call site away as more
  subsystems consume `nw::gfx`. Decide before the toolset adds
  runtime-adjustable quality settings.
