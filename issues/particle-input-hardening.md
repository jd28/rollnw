# Particle Authoring Input Hardening

## Problem

Particle JSON hardening is inconsistent: emission rate is sanitized and
emitter/material counts are capped, but several other authored numeric
fields flow to the runtime unclamped. One of them is a load-time DoS. Found
in the 2026-07 gfx/render audit; verified in source.

## Findings

- `lib/nw/render/particle_json.cpp:501`: `max_particles` (uint32) is read
  with `j.value(...)` and never range-checked; nlohmann converts a stored
  `-1` to 4294967295 unchecked. `compile_particle_effect`
  (`particle_compile.cpp:308-312,349-351`) sums it uncapped, and
  `create_particle_system` (`particle_system.cpp:1574-1576`) reserves ~20
  parallel per-particle arrays with it — a multi-hundred-GB allocation
  attempt from loading one bad asset.
- `spread_radians` (and `gravity`, `drag`, `region.size`, mass,
  rotation_rate, ...) have no `isfinite` guard, unlike
  `sanitize_emission_rate` (`particle_system.cpp:73-77`). A JSON literal
  like `1e40` parses to `+inf`; `random_range_f32(0, inf)` yields NaN
  whenever the RNG produces t == 0, poisoning that particle's
  direction/velocity/position for its lifetime.
- `sort_order` (`int32_t`) is packed with `<< 24` into an 8-bit key field
  (`particle_system.cpp:1402-1416`); values outside [0,255] (or negative)
  silently alias unrelated orderings. No clamp, no documented range.
- Sprite-sheet `columns * rows` (both uint16) is narrowed to `uint16_t` at
  `particle_compile.cpp:176-177,300` and `particle_system.cpp:115`; a
  legitimate 256x256 atlas truncates to 0 frames and skips the frame-range
  clamp. `particle_render.cpp:75` already widens correctly — bug is only in
  those three sites.

## Fix

One sweep applying the two existing guard patterns to every JSON-sourced
numeric field at parse or compile time:

- Clamp `max_particles` (per emitter and the summed total) to an explicit,
  documented cap, with a compile warning — same shape as
  `kMaxCompiledEmitterCount` / `kMaxCompiledMaterialCount`.
- Reject or sanitize non-finite floats at load, mirroring
  `sanitize_emission_rate`.
- Clamp `sort_order` to [0,255] with a warning, or widen the key.
- Widen sheet dimension math to `uint32_t` before multiplying.

## Related Cleanup

`particle_system.cpp:272-1120` contains an entire unused duplicate of the
particle update pipeline: the `uint16_t emitter_id` overload family
(`particle_anchor_position`, `advance_dynamic_particle`, etc.) has zero
callers repo-wide; only the `(CompiledParticleEmitter&,
ParticleEmitterState&)` overloads run. Delete the dead copies — they are a
divergence landmine for any future fix in the live ones.

## Verification

`tests/render_particles.cpp` has kernel coverage but nothing for hostile
inputs. Add cases: negative/huge `max_particles`, out-of-range
`spread_radians`, `sort_order` outside [0,255], large sheets. Consider a
small JSON fuzz corpus alongside the existing parser fuzzers.
