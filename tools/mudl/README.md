# mudl

`mudl` is a model viewer and headless capture tool for NWN assets plus glTF 2.0 / PBR content, including skinned and animated glTF playback.

It is useful for:
- interactive model inspection
- quick asset stats and hierarchy inspection
- deterministic screenshot capture
- deterministic turntable frame generation for downstream tooling

## Commands

Supported commands:

- `mudl [<resref>] [--module <path>] [--animation <name>] [--dangly-scale <value>] [--dangly-mode legacy|modern]`
- `mudl <path/to/model.gltf|path/to/model.glb>`
- `mudl view [<resref>|path/to/model.gltf|path/to/model.glb|path/to/effect.json>] [--module <path>] [--animation <name>] [--dangly-scale <value>] [--dangly-mode legacy|modern] [--validate]`
- `mudl spell <spells.2da rowid> [--module <path>] [--animation <name>] [--dangly-scale <value>] [--dangly-mode legacy|modern] [--debug] [--validate]`
- `mudl spell-export <spells.2da rowid> <path> [--module <path>]`
- `mudl spell-export-live <spells.2da rowid> <path> [--module <path>]`
- `mudl spell-preview-live <spells.2da rowid> <path> [--module <path>] [--frame <count>] [--view front|top|side] [--metadata]`
- `mudl vfx <spell|resref|VFX_*|effect.json> [--stage proj|cast|conj|impact|duration|cessation] [--module <path>] [--animation <name>] [--dangly-scale <value>] [--dangly-mode legacy|modern] [--debug] [--validate]`
- `mudl stats <resref> [--module <path>]`
- `mudl texture <resref> <path> [--module <path>]`
- `mudl area <resref> [--module <path>] [--debug]`
- `mudl area --dump <module-path> [--output <path>] [--skip-existing] [--limit <n>] [--debug]`
- `mudl area-screenshot <resref> <path> [--module <path>] [--debug]`
- `mudl creature <resref> [--module <path>] [--animation <name>] [--dangly-scale <value>] [--dangly-mode legacy|modern]`
- `mudl item <resref> [--module <path>] [--dangly-scale <value>] [--dangly-mode legacy|modern]`
- `mudl particle-preview <mdl-path|resref> <path/to/out.png> [--time <seconds>] [--view front|top|side] [--animation <name>] [--metadata]`
- `mudl particle-preview-frames <mdl-path|resref> <path/to/out-dir> [--duration <seconds>] [--fps <n>] [--view front|top|side] [--animation <name>] [--metadata]`
- `mudl particle-export <mdl-path|resref> <path/to/effect.json> [--animation <name>] [--module <path>]`
- `mudl corpus <path/to/corpus.json> [--output <dir>] [--module <path>] [--limit <n>] [--filter <tag>] [--ledger <path>]`
- `mudl frames <count> [<resref>] [--module <path>] [--animation <name>] [--dangly-scale <value>] [--dangly-mode legacy|modern] [--validate]`
- `mudl screenshot <resref> <path> [--module <path>] [--animation <name>] [--dangly-scale <value>] [--dangly-mode legacy|modern]`
- `mudl turntable <resref> [frames] [--module <path>] [--output <dir>] [--animation <name>] [--dangly-scale <value>] [--dangly-mode legacy|modern]`
- `mudl nwn-animation-smoke [--module <path>] [--validate]`
- `mudl compute-smoke [--validate]`
- `mudl help`

Common flags:

- `--module <path>`: load a module container before resolving resources
- `--animation <name>`: override the default animation for interactive/headless model previews
- `--dangly-scale <value>`: exaggerate or reduce dangly motion for tuning
- `--dangly-mode legacy|modern`: choose the dangly simulation mode
- `--validate`: enable Vulkan validation layers
- `--debug`: enable the optional debug grid overlay

```bash
# Interactive viewer
mudl
mudl c_aribeth
mudl view c_aribeth
mudl view ./tests/test_data/user/development/particle_basic.json

# Static glTF / glb preview
mudl ./tests/test_data/renderer/DamagedHelmet/glTF-Binary/DamagedHelmet.glb
mudl ./tests/test_data/renderer/DamagedHelmet/glTF/DamagedHelmet.gltf
mudl ./tests/test_data/renderer/CesiumMan/glTF-Binary/CesiumMan.glb
mudl ./tests/test_data/renderer/MetalRoughSpheres/glTF-Binary/MetalRoughSpheres.glb

# Spell and VFX preview
mudl spell 115
mudl spell-export 115 ./out/spell115.json
mudl spell-export-live 115 ./out/spell115-live.json
mudl spell-preview-live 115 ./out/spell115-preview.png --frame 24 --metadata
mudl vfx VFX_IMP_MIRV
mudl vfx Fireball --stage impact
mudl vfx ./tests/test_data/user/development/particle_basic.json

# Interactive area viewer
mudl area ttr01

# Dump every area in a module to screenshots
mudl area --dump ./mymodule.mod --output ./out/areas --skip-existing --limit 50 --debug

# Headless area screenshot
mudl area-screenshot ttr01 ./out/ttr01.png

# Texture extraction
mudl texture plc_archtarg plc_archtarg_d.png

# Creature and item previews
mudl creature c_aribeth --animation pause1
mudl item nw_it_mpotion001

# Particle debug preview from a local MDL file
mudl particle-preview ./tests/test_data/user/development/vfx_lightning_test.mdl ./out/lightning.png --time 0.25 --metadata

# Particle debug preview from an NWN model resref
mudl particle-preview c_mindflayer ./out/c_mindflayer-particles.png --time 1.0 --view front --animation cspecial

# Particle debug frame sequence
mudl particle-preview-frames it_torch_000 ./out/torch-particles --duration 1.5 --fps 24 --view front --metadata

# Export an NWN particle effect into the native particle JSON format
mudl particle-export it_torch_000 ./out/it_torch_000.json

# Run a checked-in visual corpus and collect per-entry outputs
mudl corpus ./tests/test_data/user/development/spell_corpus.json --output ./tmp/visual-audit/spells

# Run a corpus and update the checked-in visual audit ledger
mudl corpus ./tests/test_data/user/development/creature_corpus.json \
  --output ./tmp/visual-audit/creatures \
  --ledger ./tests/test_data/user/development/visual_audit_ledger.json

# Run the seeded OC Prelude area corpus
mudl corpus ./tests/test_data/user/development/area_corpus.json \
  --module "$NWN_ROOT/data/nwm/Prelude.nwm" \
  --output ./tmp/visual-audit/areas-prelude

# Model stats
mudl stats c_aribeth
mudl stats c_aribeth --module ./mymodule.mod

# Render a fixed number of frames
mudl frames 36 c_aribeth

# Renderer smoke test
mudl compute-smoke --validate

# Single headless screenshot
mudl screenshot c_aribeth ./out/c_aribeth.png

# Headless turntable image sequence
mudl turntable c_aribeth 36 --output ./out/c_aribeth
```

Most commands also accept `--module <path>` to load a module container before resolving resources.
Use this when the model, textures, or override assets live inside a specific `.mod`, module directory, or zip container.

For particle preview commands, `--metadata` writes JSON companion files with emitter/kernel/warning state.
`particle-preview` writes a sibling `.json` next to the PNG. `particle-preview-frames` writes `preview.json`
plus one `.json` per numbered frame.

`mudl view` can also open a native particle asset directly when the input is a JSON file with
`"$type": "ParticleEffect"`. This is the source-neutral authoring path for particle work in `mudl`:

The checked-in particle coverage corpus lives at:
- [tests/test_data/user/development/particle_corpus.json](../../tests/test_data/user/development/particle_corpus.json)

The initial checked-in visual corpora for broader asset review live at:
- [tests/test_data/user/development/spell_corpus.json](../../tests/test_data/user/development/spell_corpus.json)
- [tests/test_data/user/development/creature_corpus.json](../../tests/test_data/user/development/creature_corpus.json)
- [tests/test_data/user/development/item_corpus.json](../../tests/test_data/user/development/item_corpus.json)
- [tests/test_data/user/development/model_corpus.json](../../tests/test_data/user/development/model_corpus.json)
- [tests/test_data/user/development/area_corpus.json](../../tests/test_data/user/development/area_corpus.json)
- [tests/test_data/user/development/visual_audit_ledger.json](../../tests/test_data/user/development/visual_audit_ledger.json)

`mudl corpus --ledger <path>` updates a persistent audit ledger from the current run while preserving
human review fields such as ownership, issue linkage, disposition, and review notes.

Use it as the canonical list of representative NWN particle models, required animations, and behavior classes
when validating importer/runtime/render changes.
NWN MDL import gets you close, and native particle JSON gives you something clean to tune from there.

Example:

```bash
mudl view ./tests/test_data/user/development/particle_basic.json
```

Example turntable output:

![c_aribeth turntable](./readme-assets/c_aribeth-turntable.gif)

## NWN1 Compatibility

Longer-term NWN1 preview/model compatibility notes should live with the reusable runtime under
`lib/nw/render/nwn/docs/`, not under the `mudl` app.

## Output Model

`mudl` stops at deterministic frame generation.

It does not own GIF or video encoding. That belongs in external tools like `ffmpeg` or build scripts that consume the generated PNGs.

Examples:

```bash
# Generate a 36-frame turntable sequence
mudl turntable c_aribeth 36 --output ./out/c_aribeth

# GIF via ffmpeg
ffmpeg -framerate 12 -i ./out/c_aribeth/%04d.png -vf palettegen ./out/c_aribeth-palette.png
ffmpeg -framerate 12 -i ./out/c_aribeth/%04d.png -i ./out/c_aribeth-palette.png -lavfi paletteuse ./out/c_aribeth.gif

# MP4 via ffmpeg
ffmpeg -framerate 12 -i ./out/c_aribeth/%04d.png -pix_fmt yuv420p ./out/c_aribeth.mp4
```

## Current State

- bindless sampled-texture rendering path
- per-draw uniform suballocation
- static and skinned mesh rendering paths
- static glTF 2.0 / PBR render path
- bind-pose skinned glTF render path
- glTF animation clip import and playback
- animated glTF playback now prefers the `Ozz` backend with a custom runtime fallback
- 3-point studio lighting for model and interior previews
- world-space exterior area lighting driven by area weather metadata
- smooth accelerated 60 second exterior day/night preview cycle
- cascaded directional shadows for area lighting
- camera auto-fit to world-space bounds
- interactive orbit/pan/zoom viewer controls
- unified scene pause / stop / reset / single-step controls
- spell and VFX sequence playback, export, and live preview tooling
- typed `progfx` lowering for programmable FX work in `mudl`
- interactive area navigation and lighting time controls
- headless screenshot capture
- headless turntable frame output
- model hierarchy, geometry, and texture stats

## Viewer Controls

### General Viewer

- `Left-drag`: orbit
- `Middle-drag`: pan
- `Mouse wheel`: zoom
- `F`: frame model to bounds
- `W` / `S` or `Up` / `Down`: pitch orbit
- `A` / `D` or `Left` / `Right`: yaw orbit
- `Q` / `E`: zoom out / in
- `P`: pause / resume the whole scene
- `.`: step the whole scene by 33 ms
- `/`: reset the whole scene
- `F5`: reload shaders and rebuild render pipelines
- `Tab`: cycle glTF animation clips
- `[` / `]`: scrub glTF animation time when an animated glTF is loaded
- `Space`: toggle autoplay for the active subsystem
- `J` / `K`: decrease / increase glTF IBL strength
- `N` / `M`: decrease / increase glTF exposure
- `Shift`: faster keyboard step
- `Esc`: exit viewer

### Area Viewer

- `Left-drag`: free-look yaw / pitch
- `Middle-drag`: pan
- `Mouse wheel`: move forward / backward in perspective, zoom in orthographic overview
- `F`: frame area to bounds
- `W` / `S`: move forward / backward
- `A` / `D`: move left / right
- `Left` / `Right`: yaw
- `Up` / `Down`: move up / down
- `Ctrl` + `Up` / `Down`: pitch
- `Q` / `E`: zoom out / in
- `Shift`: faster keyboard step
- `Esc`: exit viewer

### Area Lighting Controls

These controls apply to exterior area previews with `day_night_cycle` enabled.

- `Space`: toggle day/night autoplay
- `[` / `]`: scrub backward / forward through time of day
- `\`: reset the cycle to the area's authored starting phase
- `G`: toggle authored area fog on / off
- `H`: toggle shadow cascade debug view

### Spell / VFX Controls

- `Space`: toggle sequence autoplay
- `P`: pause / resume the whole scene
- `.`: step the scene by 33 ms
- `/`: reset the scene to time 0
- Debug panel support includes:
  `Sequence time` scrubbing, active-step inspection, per-step source / target metadata, and emitter isolation controls such as `Only ring` / `Hide ring`

## Lighting Notes

- Exterior areas use a stable world-space outdoor lighting rig instead of camera-relative studio lighting.
- Interior areas keep the viewer's studio-style lighting.
- Exterior areas with `day_night_cycle` enabled run through a full 60 second preview loop.
- Outdoor lighting uses authored `.are` sun/moon ambient and diffuse colors when present, with viewer fallbacks for missing or unusable values.
- The static glTF / PBR path now uses split-sum IBL with:
  - HDR environment input
  - diffuse irradiance
  - GGX-prefiltered specular environment mips
  - BRDF LUT integration
- glTF standalone previews expose viewer dials for this path:
  - `J` / `K`: decrease / increase glTF IBL strength
  - `N` / `M`: decrease / increase glTF exposure
- The current glTF environment is still a viewer-side validation setup, not authored per-asset lighting.

## glTF Notes

- `mudl` currently supports static `.gltf` / `.glb`, skinned glTF rendering, and imported glTF animation playback.
- The v1 static glTF path flattens node transforms into per-primitive world transforms.
- The current skinned glTF path retains the minimum node/skin data needed for bind-pose skinning and animation playback.
- The current animated glTF path runs through the renderer-owned animation runtime and prefers the `Ozz` backend for sampling.
- Current material support is focused on:
  - base color
  - normal
  - metallic-roughness
  - emissive
  - occlusion merged into the runtime ORM/surface texture at import time
- Current non-goals for the glTF path:
  - morph targets
  - exact Khronos Sample Viewer parity

## Regression Set

Current graphics regression models:

- `c_aribeth`: humanoid character baseline
- `c_drgshad`: skinned creature with wing skinning
- `plc_archtarg`: placeable with emitters
- `ttd01_a04_03`: tile with static meshes, lights, and an aabb node
- `DamagedHelmet.glb`: static glTF / PBR baseline
- `CesiumMan.glb`: skinned glTF bind-pose baseline
- `MetalRoughSpheres.glb`: glTF metallic/roughness material baseline

Suggested checks:

```bash
mudl stats c_aribeth
mudl stats c_drgshad
mudl stats plc_archtarg
mudl stats ttd01_a04_03

mudl screenshot c_drgshad ./out/c_drgshad.png
mudl screenshot plc_archtarg ./out/plc_archtarg.png
mudl screenshot ttd01_a04_03 ./out/ttd01_a04_03.png
mudl ./tests/test_data/renderer/DamagedHelmet/glTF-Binary/DamagedHelmet.glb
mudl ./tests/test_data/renderer/CesiumMan/glTF-Binary/CesiumMan.glb
mudl ./tests/test_data/renderer/MetalRoughSpheres/glTF-Binary/MetalRoughSpheres.glb
```

## Build

Requires:
- `ROLLNW_BUILD_RENDERER=ON`
- DXC runtime binaries via GitHub releases on Linux/Windows, or a system install on macOS

```bash
cmake --preset linux-arclight-dev
cmake --build --preset linux-arclight-dev --target mudl
```

## Design Notes

- `mudl` is a consumer of `nw::gfx`, not a rendering framework
- headless capture is built in because it is part of rendering validation
- encoding stays outside the tool
- NWN assets still come first, but glTF / PBR is now the primary modern material path for renderer validation
