# Render Transparency Regression

## Problem

`mudl` previews show alpha/transparency failures in at least two render paths.
The failure should be diagnosed from the material/texture/pipeline data, not
from model names.

## Data Observed

- Screenshot `Screenshot_20260708_080858.png`: `mudl` viewport `1280 x 720`,
  static RenderModel path, debug panel reports `Models: legacy 0 static 1` and
  `Particles: 2`. The preview shows a large mostly opaque pale rectangular
  sheet where particle or billboard transparency is expected.
- Screenshot `Screenshot_20260708_080951.png`: same viewer settings, debug
  panel reports `Models: legacy 0 static 1` and `Particles: 1`. A plant/foliage
  surface shows a black opaque polygon around alpha-textured leaves instead of
  transparent background.

## Common Cases To Check

- Particle billboard/path draw constants and blend state.
- RenderModel material lowering for alpha-tested and transparent NWN textures.
- Texture upload/source-image alpha preservation.
- Static PBR shader sampling and alpha discard/blend policy.
- Pipeline key selection for opaque, cutout, transparent, and additive-like
  surfaces.

## Out-Of-Range/Error Policy

- If a texture has no alpha channel, render it as opaque and report that in the
  load report when transparency was requested by material state.
- If material alpha mode and texture alpha disagree, report the exact surface,
  source material, texture, alpha mode, and selected pipeline.
- Do not silently fall back to opaque for surfaces marked cutout/transparent.

## Done Criteria

- Reproduce the two screenshots with named assets and a `mudl screenshot` or
  equivalent command that can be rerun.
- Add load-report diagnostics that show, per affected surface/particle draw:
  source texture alpha presence, material alpha mode, selected render queue,
  selected pipeline blend/depth-write policy, and discard threshold.
- Fix the data path that chooses the wrong alpha behavior.
- Add at least one regression test or screenshot comparison for a cutout foliage
  surface and one particle/billboard transparency case.
