# Particle System

## Purpose

This document describes the particle system in `lib/nw/render`:

- what the neutral particle model is
- how it is compiled and executed
- how it relates to NWN1 MDL emitters
- where `mudl` fits into the workflow

The core rule is simple:

- NWN emitter nodes are an input dialect
- the runtime architecture is renderer-neutral and source-neutral
- NWN lowers into that runtime through translation, not by defining it

This is intentional. The goal is a particle system that can express NWN1 effects closely enough to author from, while still being clean, cache-friendly, and extensible enough for modern authoring.

## Design Goals

- Keep the public particle model independent of NWN.
- Keep the runtime dense and branch-light.
- Separate authored data from compiled data from live state.
- Support the major NWN particle families without leaking NWN field names into the runtime.
- Make `mudl` useful both as a debugger and as the staging area for a real renderer and future designer.

## Non-Goals

- Bit-for-bit emulation of every obscure NWN renderer quirk.
- Per-particle handles or object identity.
- A generic graph-based VFX system as the starting point.
- Letting importer quirks define the core runtime.

## Layering

The system is split into four layers.

### 1. Authoring IR

The authoring layer is defined in [particle_def.hpp](/home/josh/projects/rollnw/lib/nw/render/particle_def.hpp).

Representative types:

- `ParticleEffectDef`
- `ParticleEmitterDef`
- `ParticleMaterialDef`
- `ParticleEmissionDef`
- `ParticleInitialStateDef`
- `ParticleSpawnOverTimeDef`
- `ParticleOverLifeDef`
- `ParticleTargetingDef`
- `ParticleBeamDef`
- `ParticleRenderDef`

This layer is descriptive. It is readable, serializable, and intentionally source-neutral.

Key ideas:

- emitters describe capabilities, not source-format fields
- materials describe sprite or mesh-backed particles
- targeting and beam behavior are explicit first-class concepts
- attachment semantics are explicit in `ParticleSimulationSpace`

The main enums are:

- `ParticleEmissionMode`
  `continuous`, `single_shot`, `event_burst`, `beam_continuous`
- `ParticleSpawnMetric`
  `per_second`, `per_distance`
- `ParticleSimulationSpace`
  `world`, `local`, `emitter_attached`, `spawn_attached`
- `ParticleRenderMode`
  `billboard`, `billboard_local_z`, `billboard_world_z`, `aligned_world_z`, `velocity_aligned`, `stretched`, `linked_chain`, `beam`, `mesh`
- `ParticleTargetingMode`
  `none`, `point_gravity`, `point_bezier`, `beam_lightning`

This is the contract other sources should target, not the NWN importer surface.

### 2. Compiled Effect

The compile boundary is defined in [particle_compile.hpp](/home/josh/projects/rollnw/lib/nw/render/particle_compile.hpp).

Representative types:

- `CompiledParticleEffect`
- `CompiledParticleEmitter`
- `CompiledParticleMaterial`
- `ParticleCompileResult`

This is the most important performance seam in the system.

The compiler takes the readable authored model and converts it into a smaller set of executable forms:

- classifies emitters into finite kernels
- compiles keyed curves and gradients into runtime-friendly tracks
- derives feature flags for work that can be skipped entirely
- derives effect-level storage flags so unused sidecars do not exist
- computes total capacity budgets

Current kernels:

- `sprite_basic_constant`
- `sprite_basic`
- `sprite_target_gravity`
- `sprite_target_bezier`
- `linked_chain`
- `beam_lightning`
- `mesh_basic`

**`linked_chain` + `point_bezier` targeting note:**
Each particle's position is a kink waypoint in the ribbon. For `point_bezier` targeting the kink starts at the **spawn position** and traverses toward the reference node target at rate `age / transition_factor`, using the per-particle bezier sidecar. The spawn position carries the lateral offset from the emitter's `xsize`/`ysize` region — that off-axis displacement is what creates the visible V-kink shape. The ribbon always spans the full emitter→target distance; `transition_factor` (`combinetime`) controls only how far the kink has traveled along the bolt when the particle dies, not the bolt height. This is distinct from `sprite_target_bezier`, where each particle travels its own independent path from spawn to target without forming a ribbon.

Current compile-time feature flags:

- `CompiledParticleFeature::update_size`
- `CompiledParticleFeature::update_color`
- `CompiledParticleFeature::update_rotation`
- `CompiledParticleFeature::update_frame`

Current compile-time storage flags:

- `CompiledParticleStorage::bezier`
- `CompiledParticleStorage::beams`
- `CompiledParticleStorage::attachment`

These matter because they allow the runtime to do less work and carry less memory when an effect does not need a feature.

### 3. Runtime Instance

The live runtime state is defined in [particle_system.hpp](/home/josh/projects/rollnw/lib/nw/render/particle_system.hpp).

Representative types:

- `ParticleSystemInstance`
- `ParticleEmitterState`
- `ParticleStorage`
- `ParticleCoreStorage`
- `ParticleBezierStorage`
- `ParticleBeamStorage`
- `ParticleAttachmentStorage`

The hot path uses dense storage. Particles are anonymous rows in contiguous arrays. They do not have stable public identities.

Core streams include:

- `position`
- `velocity`
- `age`
- `lifetime`
- `mass`
- `size_x`
- `size_y`
- `rotation`
- `rotation_rate`
- `frame`
- `frame_offset`
- `color_rgba8`
- `normalized_age`
- `emitter_id`
- `material_id`

Sidecars exist only when the compiled effect says they are needed:

- bezier data
- beam data
- attachment data

This is why the system is not just "SoA everywhere." It is intentionally partitioned so uncommon kernels do not widen the default hot path.

### 4. Render Packets

Render output is defined in [particle_render.hpp](/home/josh/projects/rollnw/lib/nw/render/particle_render.hpp).

Representative types:

- `ParticleRenderPacket`
- `ParticleRenderPacketList`
- `ParticleRenderPath`

Simulation does not render directly out of the storage layout. Instead it builds packets with semantic information:

- material id
- blend mode
- sprite-sheet metadata
- ordering flags
- path payloads for bezier and beam-like emitters
- packet-level uniformity hints such as `uniform_color`

This allows the renderer and debugger to consume particle intent without reverse-engineering raw storage.

## Runtime Flow

The normal flow is:

1. Author or import a `ParticleEffectDef`.
2. Compile it with `compile_particle_effect(...)`.
3. Create a `ParticleSystemInstance` with `create_particle_system(...)`.
4. Advance it with `tick_particle_system(...)`.
5. Produce render packets with `build_particle_render_packets(...)`.

At tick time the runtime does, broadly:

1. update emitter state
2. accumulate spawn counts
3. spawn into dense arrays
4. run the kernel-specific update path
5. remove dead particles with dense compaction / swap behavior
6. rebuild render packets on the cold side

Important runtime properties:

- no per-particle heap allocation
- no per-particle handles
- dense live arrays
- work skipping driven by compile-time features
- storage narrowing driven by compile-time storage flags

## Curves And Time Domains

The system distinguishes three kinds of authored time:

- emitter-time
  for example `rate_over_time`
- spawn-time sampled values
  for example animated size/color/alpha at the moment of spawn
- over-life values
  for example alpha fade, size growth, and color shift over normalized particle lifetime

This matters for NWN because MDL emitters use all three patterns:

- keyed birthrate over animation time
- animated spawn values
- start/mid/end over-life behavior

The neutral model expresses these as:

- `ParticleEmissionDef::rate_over_time`
- `ParticleSpawnOverTimeDef`
- `ParticleOverLifeDef`

The compiler then turns those into keyed tracks and feature flags that the runtime can evaluate cheaply.

## Attachment Semantics

NWN has several finicky inheritance flags. In the neutral system these become explicit simulation-space or spawn-velocity behavior.

Current simulation spaces:

- `world`
  particles simulate freely in world space
- `local`
  particles remain relative to the emitter transform
- `emitter_attached`
  particles move with emitter translation
- `spawn_attached`
  particles remain anchored to their spawn origin

Initial emitter motion can also be folded into particle velocity through `velocity_inheritance`.

This is a clearer and more portable model than preserving NWN’s raw flag vocabulary.

## NWN Mapping

The NWN lowering layer lives outside the renderer in the model code:

- [mdl_particle_import.hpp](/home/josh/projects/rollnw/lib/nw/model/mdl_particle_import.hpp)
- [mdl_particle_import.cpp](/home/josh/projects/rollnw/lib/nw/model/mdl_particle_import.cpp)

It translates NWN MDL emitters into `ParticleEffectDef`.

Important mappings:

- `update`
  lowers to `ParticleEmissionMode`
- `spawntype`
  lowers to `ParticleSpawnMetric`
- `xsize` / `ysize`
  lower to `ParticleSpawnRegion`
- `inherit`, `inherit_local`, `inheritvel`
  lower to simulation-space and velocity-inheritance semantics
- `render`
  lowers to `ParticleRenderMode`
- `texture`, `chunkName`, `blend`, `xgrid`, `ygrid`, `fps`, `random`
  lower to `ParticleMaterialDef` and `ParticleSpriteSheet`
- `birthrate`, `lifeExp`, `velocity`, `randvel`, `spread`, `mass`
  lower to neutral initial/emission values
- `p2p`, `p2p_sel`, tangents, gravity, drag, threshold
  lower to `ParticleTargetingDef`
- lightning fields
  lower to `ParticleBeamDef`

The importer also emits warnings for unsupported or lossy cases rather than leaking NWN-specific terms into the runtime.

### Deferred NWN Flags

Some NWN emitter flags are intentionally deferred rather than guessed.

- `inherit_part`
  This flag appears in shipped content and is tracked by the corpus, but current validation has not yet produced a clear, reproducible runtime-visible behavior that justifies a neutral lowering. Candidate assets such as `c_mindflayer` and `vim_ruin` are useful for investigation, but not yet enough to prove a specific implementation. The current policy is:
  - keep it importer-visible
  - emit a warning
  - do not invent runtime semantics until a real asset demonstrates a clear authored effect

- scene-coupled behavior beyond the current simulation context
  The runtime now has explicit seams for external forces, force events, and collision queries. Additional scene-dependent NWN behavior should be added through those neutral seams, not by passing engine/world state directly through the hot path.

That is the intended ownership boundary:

- `lib/nw/model` understands NWN emitters
- `lib/nw/render` understands particles

## What "Close To NWN" Means

The goal is not exact engine archaeology. The goal is:

- close enough that imported NWN effects land near the authored intent
- predictable enough that an author can finish the last bit in the neutral system

That is why some behavior is normalized rather than reproduced as a direct engine quirk.

The system already covers the major NWN families:

- ordinary billboard sprites
- sprite-sheet animation
- continuous and burst emission
- per-distance trails
- chunk/mesh particles
- linked chains
- gravity targeting
- bezier targeting
- lightning / beam-like emitters
- keyed birthrate and spawn animation
- three-stage over-life behavior

## JSON And Native Assets

The source-neutral serialization layer is defined in:

- [particle_json.hpp](/home/josh/projects/rollnw/lib/nw/render/particle_json.hpp)
- [particle_json.cpp](/home/josh/projects/rollnw/lib/nw/render/particle_json.cpp)

This allows `ParticleEffectDef` to be loaded and saved independently of NWN.

This is important for the broader plan:

- NWN import can generate a close starting point
- authors can save and edit neutral particle assets directly
- future particle design tools can target the same schema

In `mudl`, native particle JSON can already be loaded directly through:

```bash
mudl view ./tests/test_data/user/development/particle_basic.json
```

That is the beginning of the native authoring path.

## `mudl`'s Role

`mudl` is not the particle runtime. It is the staging area around it.

Today it already serves three useful roles:

- importer validation
- runtime/debug visualization
- renderer staging for real NWN particle presentation

It can:

- preview imported NWN particle effects
- preview native particle JSON assets
- show frame sequences and metadata for particle debugging
- exercise the real render path against actual NWN models

This is deliberate. The same neutral particle runtime should be usable by:

- NWN import
- `mudl`
- future authoring tools
- non-NWN formats

## Performance Strategy

The main performance principle is:

- spend complexity in the compile step
- keep the runtime tight

Concretely, the current system does that by:

- compiling emitters into finite kernels
- deriving per-emitter feature flags
- deriving per-effect storage flags
- skipping unnecessary per-particle updates
- using dense storage and anonymous particle rows
- keeping render packet generation on the cold side

The current implementation already avoids a large class of waste:

- constant emitters do not update size/color/rotation/frame every tick
- effects that do not use bezier/beam/attachment data do not allocate those sidecars
- packet consumers can use uniformity hints instead of re-deriving constant values per particle

This is also where future SIMD work should attach. If `xsimd` is introduced for particle updates, it should be applied to already-specialized kernels and narrow streams, not to a bloated universal update loop.

## Current State

Architecturally, the system has crossed the risky part.

What exists today:

- neutral particle defs
- compiled effect layer
- dense runtime state
- semantic render packets
- NWN importer
- particle JSON serialization
- `mudl` preview/debug integration
- `mudl` real render integration for particle effects

What is still expected to evolve:

- more compile-time specialization
- narrower runtime streams where profitable
- higher-fidelity real rendering for the remaining tricky NWN effects
- the authoring/designer layer on top of the neutral particle defs

## Decision Rule

When choosing where logic belongs, the rule is:

- if it requires knowing NWN, it belongs in the importer
- if it is about generic particle behavior, it belongs in the renderer particle system
- if it is about inspection, staging, or authoring UX, it belongs around the runtime, not inside it

That rule is what keeps the system clean while still letting NWN be a first-class source.
