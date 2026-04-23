# NWN Aurora Emitter → rollnw Particle System Map

This document is a companion to [particle_system.md](particle_system.md). It maps every Aurora Emitter field to its rollnw equivalent, states the expected visual outcome for common configurations, and documents known gaps.

**How to use this document:** Given an MDL emitter node, look up each field in the tables below to find the rollnw authoring IR field it maps to, then consult the visual outcomes sections to predict what should appear on screen.

Status legend: ✅ Implemented · ⚠️ Partial (parsed but not fully applied) · ❌ Not in renderer (simulation only or no-op) · 🚫 Deferred

---

## Field Mapping

### Emitter Style

| NWN Field | NWN Values | rollnw Field | rollnw Type | Status |
|---|---|---|---|---|
| `update` | Fountain / Single / Explosion / Lightning | `emission.mode` | `ParticleEmissionMode` | ✅ |
| `render` | normal / linked / billboard_to_local_z / billboard_to_world_z / aligned_to_world_z / aligned_to_particle_dir / motion_blur | `render.mode` | `ParticleRenderMode` | ✅ (see note 1) |
| `blend` | normal / punch-through / lighten | material blend | `ParticleBlendMode` | ✅ |
| `spawntype` | 0 = per_second / 1 = per_distance (trail) | `emission.metric` | `ParticleSpawnMetric` | ✅ |

> **Note 1:** `billboard_to_local_z` falls through to standard camera-billboard behavior. Every other render mode is implemented.

### Emitter Parameters

| NWN Field | NWN Unit | rollnw Field | Notes | Status |
|---|---|---|---|---|
| `xsize`, `ysize` | cm | `region.size` (stored in cm) | Converted ×0.01 to meters at spawn time; 0,0 = point emitter; for linked-chain P2P these also control the initial kink displacement (see cookbook) | ✅ |
| `renderorder` | short int | `render.sort_order` | Higher = drawn on top | ✅ |
| `threshold` | meters | `targeting.kill_radius` | P2P gravity: particle destroyed inside radius | ✅ |
| `combinetime` | seconds | `targeting.transition_factor` | P2P bezier: path traversal time | ✅ |
| `deadspace` | degrees | — | `aligned_to_particle_dir` dead zone; parsed to `EmitterNode`, not lowered to authoring IR | ⚠️ |
| `inherit` | bool | `simulation_space = emitter_attached` | Particles follow emitter translation and direction | ✅ |
| `inherit_local` | bool | `simulation_space = spawn_attached` | Particles stay at world position of their spawn point | ✅ |
| `inheritvel` | bool | `initial.velocity_inheritance` | Fraction of emitter velocity added to particle initial speed | ✅ |
| `inherit_part` | bool | — | No proven reproducible behavior; intentionally deferred | 🚫 |

### Emitter Particles

| NWN Field | NWN Unit | rollnw Field | Notes | Status |
|---|---|---|---|---|
| `colorStart` / `colorEnd` | vec3 [0–1] | `over_life.color` | 3-stage: start / mid / end; mid driven by `ColorMid` + `PercentMid` controllers | ✅ |
| `alphaStart` / `alphaEnd` | float [0–1] | `over_life.alpha` | Multiplied by texture alpha channel if present | ✅ |
| `sizeStart` / `sizeEnd` | float (meters) | `over_life.size_x/y` | `sizeStart_Y` / `sizeEnd_Y` fall back to X value when zero | ✅ |
| `birthrate` | particles/sec | `emission.rate` | Animatable via `BirthRate` controller keys | ✅ |
| `lifeExp` | seconds | `initial.lifetime` | Animatable; particle removed at expiry | ✅ |
| `mass` | float | `initial.mass` | Positive = gravity pulls down; negative = particle rises | ✅ |
| `spread` | degrees [0–360] | `initial.spread_radians` | Converted to radians; 360 = spherical emission | ✅ |
| `particleRot` | rotations/sec | `initial.rotation_rate` | Screen-space for billboard modes | ✅ |
| `velocity` | m/s | `initial.speed` | Base initial speed | ✅ |
| `randvel` | m/s | `initial.speed` (range) | ±range added to base velocity | ✅ |
| `bounce_co` | float | `collision.bounce_coefficient` | Only active when `Bounce` flag is set | ✅ |
| `blurlength` | float | `render.blur_length` | `motion_blur` render mode only; scales streak length relative to particle size | ✅ |
| `loop` | bool | `emission.looping` | `Single` update type: repeat emission | ✅ |
| `bounce` | bool | `collision.bounce` | Simulated against walkmesh; no visual feedback in mudl | ❌ |
| `m_isTinted` | bool | `render.tint_to_scene_ambient` | Tints particle colors to scene ambient light | ✅ |
| `splat` | bool | `collision.splat` | Simulation only; no visual feedback | ❌ |
| `affectedByWind` | bool | `affected_by_wind` | Wind forces applied in simulation; not visualized | ❌ |

### Texture / Chunk

| NWN Field | NWN Type | rollnw Field | Notes | Status |
|---|---|---|---|---|
| `texture` | string (≤16 chars) | `materials[].texture` | Set to `NULL` when using `chunkName` | ✅ |
| `twosidedtex` | bool | `materials[].double_sided` | Parsed; GPU pipeline does not yet use it | ⚠️ |
| `xgrid` / `ygrid` | int | `materials[].sheet.columns/rows` | Sprite atlas grid dimensions | ✅ |
| `fps` | float | `materials[].sheet.frames_per_second` | | ✅ |
| `frameStart` / `frameEnd` | int (0-based) | `materials[].sheet.frame_begin/end` | A 4×4 texture has frames 0–15 | ✅ |
| `random` | bool | `materials[].sheet.random_start` | Randomizes starting frame per particle | ✅ |
| `chunkName` | string (≤16 chars) | `materials[].mesh` | Overrides texture; selects `mesh_basic` kernel | ✅ |

### Emitter Advanced

| NWN Field | NWN Type | rollnw Field | Notes | Status |
|---|---|---|---|---|
| `lightningDelay` | float | `beam.update_interval` | How often the beam shape is re-randomized | ✅ |
| `lightningRadius` | float | `beam.jitter_radius` | Peak lateral displacement per subdivision | ✅ |
| `subdivision` | int | `beam.subdivisions` | Number of jitter segments along the beam | ✅ |
| `lightningScale` | float | `beam.noise_scale` | Maximum fluctuation amplitude per subdivision | ✅ |
| `blastRadius` | float | `force_event.radius` | Wind burst: affected radius | ✅ |
| `blastLength` | float | `force_event.length` | Wind burst: duration | ✅ |
| `opacity` | float [0–1] | `render.opacity_scale` | Motion blur opacity multiplier | ✅ |
| `p2p` | bool | `targeting.mode != none` | Requires a `node reference` child; will warn if absent | ✅ |
| `p2p_sel` | 0=gravity / 1=bezier | `targeting.mode` | `point_gravity` or `point_bezier` | ✅ |
| `p2p_bezier2` | float | `targeting.source_tangent` | SRC tangent: arc length for first half of path | ✅ |
| `p2p_bezier3` | float | `targeting.target_tangent` | TRG tangent: arc length for second half of path | ✅ |
| `grav` | float | `targeting.gravity` | Acceleration (m/s²) toward reference node | ✅ |
| `drag` | float | `targeting.drag` | Slingshot coefficient: particles overshoot then return | ✅ |

---

## Expected Visual Outcomes

### Update × Render Matrix

What you should see on screen for each combination:

| Update | Render | Expected Screen Result |
|---|---|---|
| Fountain | normal | Steady stream of camera-facing quads from the spawn region |
| Fountain | linked | Continuous ribbon connecting living particles; quads stretch where particles are sparse |
| Fountain | billboard_to_world_z | Quads lie flat, face upward (parallel to ground); good for pools, auras |
| Fountain | aligned_to_world_z | Quads face sideways (perpendicular to ground); good for rings, halos |
| Fountain | aligned_to_particle_dir | Quads rotate to face their emit direction; `deadspace` would cull camera-facing ones (not yet applied) |
| Fountain | motion_blur | Quads stretched along velocity; `blurlength` scales the particle's longitudinal size rather than adding world-space length from speed |
| Single | normal | One particle emitted once; loops if `loop = 1` |
| Explosion | normal | No particles until a `detonate` animation event fires; then `birthrate` particles burst simultaneously |
| Lightning | (beam internally) | Jittered polyline from emitter to reference node; re-randomized at each `lightningDelay` interval |

### Blend Mode

| NWN `blend` | rollnw `ParticleBlendMode` | GPU Pipeline | Visual Character |
|---|---|---|---|
| `normal` | `alpha` | `particle_transparent_pipeline_` | Standard alpha compositing; suitable for textured particles with an alpha channel (smoke, chunks) |
| `punch-through` | `cutout` | `particle_cutout_pipeline_` | Hard-edge cutout: pixels below 0.5 alpha are discarded; no semi-transparency |
| `lighten` | `additive` | `particle_additive_pipeline_` | Additive blend; black pixels are transparent; creates glow/magic feel; white adds brightness |

**Practical heuristic:** If the texture has a black background and uses lightness to define shape (fire, sparks, magic), use `lighten`. If the texture has an explicit alpha channel (smoke puff, debris), use `normal`. For leaves or hard-edged decals, use `punch-through`.

### P2P Reference Node Direction Convention

P2P reference node **offsets are inverted** relative to intuition. The engine pulls particles *toward* the reference position but defines "toward" backward:

- A reference at offset `(0 0 0.63)` above the emitter pulls particles **downward** (toward –Z in world space).
- A reference at offset `(0 0 -0.63)` pulls particles **upward**.

When debugging P2P effects, always verify the observed direction of particle flow against the sign of the reference node's offset. This is a NWN engine convention, not a rollnw quirk.

---

## Common Pattern Cookbook

For each of MerricksDad's canonical emitter patterns, the key fields and expected screen result:

### Basic Puff (e.g., vim_magblue — magic missile impact)

```
update    Explosion
render    Normal
blend     Lighten
texture   fxpa_flare    (single frame: xgrid 1, ygrid 1)
xsize 0   ysize 0       (point emitter)
colorStart 1 1 1 → colorEnd 0.36 0.76 1.0
alphaStart 1 → alphaEnd 0
sizeStart 0.5 → sizeEnd 0.07
birthrate 70   lifeExp 1
```

**Expected:** On `detonate` event — camera-facing colored flash that shrinks and fades over 1 second. Additive blending means the center appears brightest; edges fall off. Nothing visible between detonate events.

### Animated Puff / Flame (e.g., vim_exp2flame)

```
update    Fountain
render    Normal
blend     Lighten
texture   fxpa_firebb   (xgrid 4, ygrid 4, frameStart 0, frameEnd 15, fps 16)
xsize 60  ysize 50
inherit 1
birthrate 30   lifeExp 1
mass (negative or zero)
```

**Expected:** Continuous animated quads rising from a 60×50 cm base; each quad cycles through the 4×4 sprite sheet over its lifetime. `inherit 1` makes the flames follow the emitter when it moves. Particles always face camera.

### Chunks (e.g., vim_destruct — gore/debris)

```
update    Fountain
render    Normal
blend     Normal
texture   NULL
chunkName Red_M_Giblet
spread    6.28          (360°, all directions)
velocity  5   randvel 1
particleRot 1.5
birthrate (driven by animation keys, bursts near 100 then drops to 0)
lifeExp   4
```

**Expected:** 3D mesh instances launched in every direction from a point. Each chunk has a random-ish trajectory and tumbles (particleRot). Normal blend because they are opaque lit geometry.

### Trail of Smoke (e.g., it_torch_000)

```
update    Fountain
render    Normal
blend     Normal
texture   fxpa_smoke01  (xgrid 4, ygrid 4, random 1, fps 30)
mass      -0.2          (rises)
velocity  0   randVel 0.2
sizeStart 0 → sizeEnd 2
alphaStart X → alphaEnd 0
```

**Expected:** Expanding, fading puffs that rise from the emitter. `random 1` means each puff shows a different noise frame. `mass < 0` gives upward buoyancy; `randVel` adds subtle variance. Puffs diffuse visually as they scale up and fade out. `twosidedtex` is parsed but not yet GPU-applied — if the puff rotates edge-on it may vanish (currently not a practical issue since these are camera-facing).

### Detonation Burst

```
update    Explosion
event     <time> detonate    (in the animation sequence)
```

**Expected:** All `Explosion`-mode emitters in the model fire simultaneously when the `detonate` animation event fires. Using multiple `detonate` entries in one sequence triggers multiple bursts. Set `birthrate = 0` on specific emitters to suppress them on a given detonate pass.

### Lightning (P2P Beam)

```
update    Lightning
lightningDelay    (re-randomize interval)
lightningRadius   (lateral jitter amplitude)
subdivision       (number of segments)
lightningScale    (per-segment max offset)
node reference <name>    (child of the emitter node)
```

**Expected:** Animated jittered polyline from the emitter to the reference child node. Shape re-randomizes at each `lightningDelay` interval. More subdivisions = more jagged. Larger `lightningRadius` = more chaotic spread. End-fade applied via sine function.

### P2P Gravity Vortex (e.g., vff_comvortex — Implosion)

```
update    Fountain
render    Normal
blend     Normal
p2p 1     p2p_sel 0     (gravity type)
xsize 1000  ysize 1000  (wide spawn plane)
chunkName Fx_Rockchunksml
grav      2             (pull acceleration)
drag      1             (slingshot past then return)
threshold 1.5           (destroy within 1.5m of reference)
spread    0.175         (≈10°, mostly perpendicular to plane)
node reference <name>   (target singularity)
```

**Expected:** Chunks spawn across the wide plane, drift toward the reference node with increasing speed, overshoot slightly (`drag`), curve back, and are destroyed when they enter the `threshold` radius. The reference node's position is inverted — if offset is `(0 0 0.63)`, particles converge downward.

### Linked Chain / Lightning Curtain (e.g., T_Door21 ground_lightning, vps_tentacle)

```
render    Linked
p2p 1     p2p_sel 1     (Bezier)
combinetime               controls kink traversal speed (see below)
lifeExp                   particle lifetime; kink travels (lifeExp/combinetime) fraction of path
xsize / ysize             spawn region; lateral spread provides initial kink displacement
sizeStart / sizeEnd       ribbon width over particle lifetime
```

**Rendering model:** The ribbon is drawn **source (emitter position, fixed) → particle (animated kink) → target (reference node, fixed)**. Both endpoints are anchors. The kink is a per-particle waypoint that starts at the particle's **spawn position** and traverses toward the target at rate `path_t = age / combinetime`. Because `xsize`/`ysize` give each spawn a lateral offset within the emitter's local XY plane, the kink starts off the emitter→target axis and converges toward the target as the particle ages. This off-axis starting position is the source of the visible V-kink and zigzag character. When the particle dies, a new one spawns at a fresh random position, instantly resetting the kink shape.

**`xsize` and `ysize` on linked-chain emitters** define the spawn region that provides the initial kink displacement — they are not the ribbon width (that is `sizeStart`). Large values give extreme initial kinks; zero gives a point spawn at the emitter with no off-axis bend. The emitter's local orientation determines which world axis the spread falls on.

**`combinetime` effect on kink convergence:**
- Small `combinetime` relative to `lifeExp` (e.g., `combinetime 0.11`, `lifeExp 0.1`): kink reaches 91% of path before death → spends most of its life near the target end, nearly converged. Bolt starts wide/kinked and snaps toward straight quickly.
- Large `combinetime` relative to `lifeExp` (e.g., `combinetime 1.0`, `lifeExp 0.1`): kink only travels 10% of path before death → stays close to its spawn position the whole time.
- In both cases the bolt spans **full height end-to-end** (source → kink → target). `combinetime` does not affect bolt height, only how far the kink has converged when the particle dies.

**Expected:** Full-height ribbon spanning from emitter to reference node. The kink shape changes each spawn due to random lateral spawn offset. `random 1` on the sprite sheet gives each new particle a different texture frame → flickering lightning character. Additive blend (`Lighten`) makes it glow. Bolt reshapes every `lifeExp` seconds.

### Wind Burst (e.g., vff_pulswind)

```
update    Explosion
texture   NULL
blastRadius 15
blastLength 1.2
```

**Expected:** Invisible emitter — no particles drawn. On `detonate`, pushes a force event outward in `blastRadius`. Affects particles with `affectedByWind = 1` and animesh dangly objects. No visual representation in mudl; only observable through secondary effects on other particles.

### Ground Aura / Flat Ring

```
render    billboard_to_world_z    (quads lie flat, face up)
   or
render    aligned_to_world_z      (quads face sideways, ring-like)
spread    180–360                 (wide horizontal emission)
mass      0                       (no gravity drift)
velocity  (low, or 0 for static)
```

**Expected:** `billboard_to_world_z` — quads flat on ground plane, always face camera-up; circular disc appearance when `spread = 360`. `aligned_to_world_z` — quads perpendicular to ground; suitable for vertical ring/halo effects.

---

## Known Gaps Summary

| Gap | Affected Fields | Impact |
|---|---|---|
| `billboard_to_local_z` falls through to camera-billboard | `render billboard_to_local_z` | Particles that should stay face-forward relative to emitter instead always face camera; most noticeable when emitter tilts |
| `twosidedtex` not applied to GPU pipeline | `twosidedtex 1` | Particles may disappear when camera views them edge-on; only matters for non-billboard modes |
| `deadspace` not lowered to authoring IR | `deadspace` on `aligned_to_particle_dir` | Camera-facing particles in the dead zone are not culled; slightly more particles visible than NWN would show |
| `bounce`, `splat`, `affectedByWind` simulation-only | corresponding flags | Effects exist in simulation but mudl provides no visual confirmation |
| `inherit_part` deferred | `inherit_part` flag | No implementation; a warning is emitted; NWN behavior not yet established |
