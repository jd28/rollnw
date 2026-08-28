# NWN Zero-Duration Animation Clips

Status: resolved on 2026-08-28 with an explicit static-pose contract.

## Data

Some NWN MDL animations are authored as zero-duration clips. The observed
creature case is `c_mindflayer`:

- `tests/test_data/user/development/c_mindflayer.mdl`: `newanim cgetmidlp`
- `length 0.0`
- many affected node tracks contain a single key at time `0.0`

`c_mindalhoon` inherits this clip through `c_mindflayer`, so the common
RenderModel animation backend sees it while previewing the alhoon creature.

The placed-area door corpus exposed the same data shape in a required hold
state. `dwc_gen_07` has an `opened1` clip with `length 0.0`; its authored pose
keys are all at time `0.0`. The opening transition compiled and played, but the
rejected hold clip made production sampling publish the model's bind pose,
which made the visually open door appear closed again.

The clip name is not invalid by itself. `c_orcus.mdl` also has `cgetmidlp`, but
that source clip has a positive duration (`length 1.000004`) and should be
handled as a normal timed clip.

## Resolution

The common clip remains zero-duration. During cold Ozz backend construction, a
zero-duration clip is classified as a static pose only when every translation,
rotation, and scale key is at exactly time `0.0`. Ozz requires a positive raw
duration, so that internal encoding uses a normalized unit duration. Sampling
at time zero therefore returns the authored constant pose without changing
the source clip's duration or giving it timed playback semantics.

A zero-duration clip containing any nonzero key time is malformed and remains
rejected. The bridge drops only that failed clip and keeps valid clips in the
same backend. The policy depends on the key data, not the animation or model
name.

The focused regression suite verifies both boundaries:

- `DoorZeroDurationHoldSamplesAuthoredPose` verifies that an `opened1` static
  pose builds through Ozz and remains sampled while the door is held open.
- `InvalidRenderModelClipDoesNotDisableValidClip` verifies that a zero-duration
  clip with a later key is rejected without disabling a valid sibling clip.

## Contract

- Assuming all NWN animation names map to positive-duration Ozz clips.
- Exposing the normalized internal Ozz duration as source duration or timed
  playback.
- Treating this as a `cgetmidlp` name-specific problem.
