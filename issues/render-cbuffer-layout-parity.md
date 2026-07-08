# CPU/GPU Cbuffer Layout Parity

## Problem

C++ structs mirrored into HLSL cbuffers are kept in sync by hand-padding
convention only. The structs that carry `static_assert`s are correct; one of
the two that don't has a real layout bug shifting every particle fog uniform
by 4 bytes. Found in the 2026-07 gfx/render audit; verified by walking both
layouts field by field.

## Findings

- `lib/nw/render/render_context.hpp:483-497` (`ParticleDrawConstants`):
  `alpha_intensity` (float) ends at byte 156; `glm::vec4 fog_color` follows
  at 156 on the C++ side (plain 4-byte-aligned glm types). HLSL cbuffer
  packing cannot let a `float4` straddle a 16-byte register, so the shaders
  (`render_particle_billboard.vs/ps.hlsl`, `render_particle_path.vs.hlsl`)
  place `fog_color` at 160. The struct is memcpy'd verbatim
  (`particle_renderer.cpp:733`), so `fog_color`, `fog_range`, `fog_amount`,
  and `fog_enabled` are all read 4 bytes off for every particle draw.
- `FogCompositeConstants` (`render_context.hpp:473-481`) happens to be
  correctly padded but has no assert either.
- `SceneConstants` and `ForwardPlusCullParams` show the working pattern:
  explicit pad fields plus `static_assert` on size/offsets.

## Fix

- Insert one explicit pad float between `alpha_intensity` and `fog_color`
  in `ParticleDrawConstants`; add `static_assert(sizeof(...) == 192)`.
- Add the missing size assert to `FogCompositeConstants`.
- Adopt the policy: every struct memcpy'd into a cbuffer carries a
  `static_assert` on its size (and offsets where a mismatch is plausible),
  placed next to the struct. A tiny macro or a comment convention is
  enough; the point is that a new cbuffer mirror cannot land without one.

## Verification

Fog on particle draws is the visible symptom: before/after screenshot of a
fogged particle effect (mudl), plus the existing render tests. The asserts
are the regression guard.
