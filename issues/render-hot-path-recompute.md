# Render Hot-Path Recompute And Allocation

## Problem

Several per-frame paths recompute or allocate data that is invariant across
frames (or within a frame) — the AGENTS.md "can we do this only once /
fewer times" questions not yet applied. All are verified code facts; the
performance deltas are unverified hypotheses per AGENTS.md, each with a
stated measurement.

## Findings

- `lib/nw/render/particle_system.cpp:1337-1354` (`ensure_beam_particle`):
  every `beam_continuous` emitter, every tick, linear-scans all live
  particles in the effect to answer "does this emitter have a particle" —
  `live_particles_per_emitter[emitter_id]` already answers it O(1).
  Fix: use the counter. Measure: effect mixing one beam emitter with
  high-count sprite emitters; per-tick time before/after.
- `lib/nw/render/model_draw.hpp:1010-1090`
  (`collect_prepared_render_model_skin_tables`, `find_range` lambda):
  linear scan of `out.ranges` from index 0 per skinned surface; matches
  cluster at the tail, so the pass is effectively quadratic in
  (instance, skin) pairs per frame.
  Fix: per-frame hash map or search backward from the tail.
  Measure: 500-2000 skinned instances; collection time and comparisons.
- `lib/nw/render/model_instance_animation.cpp:139-157`
  (`build_node_to_joint_map`): fresh `std::vector<int32_t>` per animated
  instance per frame, derived only from the immutable skeleton.
  Fix: cache per skeleton (alongside `Skeleton::eval_order`).
  Measure: N instances sharing one skeleton; allocation count, p95/p99.
- `lib/nw/render/model_renderer.cpp:241-281` (`fill_render_model_bones`):
  `glm::inverse(node.world_transform)` per skinned-primitive draw, per
  frame, in both color and shadow passes — static bind-pose data owned by
  the immutable RenderModel.
  Fix: precompute at asset upload (`Primitive::inverse_mesh_transform`).
  Measure: CPU time for N skinned surface submissions.
- `lib/nw/render/model_renderer.cpp:658-670`
  (`render_prepared_render_model_surfaces`): allocates a fresh packet list
  per call (per model run, per pass, per frame) instead of taking
  caller-owned scratch like its sibling collect function.
  Fix: scratch-buffer parameter. Measure: allocation count per frame.
- `lib/nw/render/viewer/preview_scene.cpp:1997-2008, 2165-2229`
  (`sync_model_instance_runtime_state`): full linear scan of
  `scene.model_attachments` per model, and `ViewerSession::tick`
  (`session.cpp:522-529`) calls `scene->update()` unconditionally every
  tick for every scene kind — O(models x attachments) per frame, quadratic
  in creature population for area previews (humanoid assembly is still on
  this path per `preview_scene.hpp:52-57`).
  Fix: store the attachment binding index alongside the model at
  `add()`/`add_attached()` time. Measure: synthetic area, 50 creatures x
  15 parts, `update()` time before/after.
- `lib/nw/render/particle_renderer.cpp:145-175,398-400`
  (`is_white_alpha_mask_texture`): re-samples up to 256 texels of the same
  unchanging texture every render call per additive packet.
  Fix: cache classification per texture. Bounded cost — lowest priority.
- `lib/nw/render/viewer/scene_shadow.cpp:938-949` (non-area path):
  candidate vectors rebuilt per frame where the area path reuses
  persistent generation-marked buffers. Bounded (single-model previews);
  fix by hoisting to session-owned scratch if it ever shows up in a
  profile.

## Not Doing (yet)

Per-instance `AnimationBackend` means ozz skeleton/clip build is duplicated
per instance rather than shared per RenderModel. Currently 1:1
model:instance in the only caller, so no cost is paid today — revisit when
the multi-instance runtime path lands (see modern-runtime-sockets.md).
