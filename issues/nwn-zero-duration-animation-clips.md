# NWN Zero-Duration Animation Clips

## Data

Some NWN MDL animations are authored as zero-duration clips. The observed case
is `c_mindflayer`:

- `tests/test_data/user/development/c_mindflayer.mdl`: `newanim cgetmidlp`
- `length 0.0`
- many affected node tracks contain a single key at time `0.0`

`c_mindalhoon` inherits this clip through `c_mindflayer`, so the common
RenderModel animation backend sees it while previewing the alhoon creature.

The clip name is not invalid by itself. `c_orcus.mdl` also has `cgetmidlp`, but
that source clip has a positive duration (`length 1.000004`) and should be
handled as a normal timed clip.

## Current Behavior

The Ozz backend rejects zero-duration clips. `ozz::animation::offline`
`RawAnimation::Validate()` requires `duration > 0.0f`, so `AnimationBuilder`
returns null for `c_mindflayer`'s `cgetmidlp`.

The current bridge policy drops only the failed Ozz clip, logs the clip name and
index, and keeps the backend for the remaining valid clips. Legacy/reference
sampling remains available as the debug fallback path for clips that cannot be
compiled into Ozz.

## Direction

Zero-duration NWN clips need an explicit common-runtime policy instead of being
silently treated as ordinary animation clips.

Possible policies:

- `drop_and_flag`: keep current behavior for production Ozz playback and report
  the missing clip through diagnostics.
- `pose_clip`: classify zero-duration clips as static pose data and store them
  in a separate pose table, not as Ozz animations.
- `epsilon_clip`: convert to a tiny positive duration only if the runtime
  semantics are proven equivalent for NWN playback.
- `reference_only`: keep these clips on the legacy/reference sampler until the
  modern animation contract has explicit pose-clip support.

Do not invent a positive duration during import without deciding which semantic
contract the runtime is supposed to preserve.

## Open Questions

- Which NWN animation states use zero-duration clips as required gameplay or
  visual pose data versus unused placeholders?
- Should common `ModelInstanceAnimationState` address pose clips separately
  from timed clips?
- How should UI clip selection expose a dropped Ozz clip that still has
  reference-sampler data?

## Do Not Carry Forward

- Assuming all NWN animation names map to positive-duration Ozz clips.
- Silently stretching zero-duration source clips into timed clips.
- Treating this as a `cgetmidlp` name-specific problem.
