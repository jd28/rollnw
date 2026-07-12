# Visual Asset Protocol

This document describes how gameplay/script decisions become renderer-visible
model, light, and icon resource references.

The goal is to keep the renderer source-neutral. Smalls and rules modules decide
which semantic assets an object needs. C++ owns object-component storage,
resource loading, validation, and renderer submission. Renderer passes consume
uploaded renderer data, not NWN table policy or model-name rules.

## Data Flow

```text
propsets, load_config data, equipment, appearance state
  -> Smalls resolver functions
     nwn1.placeables.update_visual
     nwn1.doors.update_visual
     nwn1.creature.update_visual
     nwn1.item.update_visual_for_slot
  -> core.visual native row functions
     clear_visual, clear_visual_slot
     add_visual_model, add_visual_model_row, add_visual_part
     add_visual_light
  -> C++ object components
     ObjectVisualState
     ObjectVisualModel[]
     ObjectVisualLight[]
  -> viewer/runtime assembly
     load model ResRefs at the renderer boundary
     apply PLT/material/animation/socket compatibility data
     create model instances and light lists
  -> renderer frame protocol
     RenderModel, ModelInstance
     PreparedModelDraw, PreparedModelSurfaceDraw
     light packets and renderer passes
```

The common case is a batch of rows for one changed object. A single static
object has one root model row. A weapon or shield has one or more item model
rows. A humanoid creature has body/loadout rows, plus equipment rows owned by
slot. Area and viewer code later collect these component rows into renderer
instances and flat draw streams.

## Equipment Boundary

Equipping is an engine transaction. C++ owns the mutation of equipment slots,
inventory ownership, object/component versions, and effect ownership. Smalls
does not replace that transaction.

Visual resolution happens after the equip state changes. The input is the object
and its current equipped item handles. Smalls reads the relevant creature/item
propsets and `load_config` data, then emits the rows the renderer needs:

- creature body/loadout rows for the owner object
- equipped item rows partitioned by slot
- optional attachment rows such as wings, tails, shields, cloaks, and weapons
- later row protocols for animation slate changes or other render-facing state

The renderer does not inspect equipment slots, base-item IDs, or NWN tables. It
loads and submits the `ObjectVisualModel` rows already attached to the object.
If an equipped item changes the visible model, icon, attachment, or animation
state, that change must be expressed as explicit row data before renderer
assembly begins.

Rows are mode-filterable data. Default rows render in both game and toolset
contexts. Producers can mark a row hidden in game mode or hidden in toolset mode
when editor helpers, placement previews, or gameplay-only visuals need different
visibility. The renderer consumes those flags as data; it does not infer the
mode from model names or source tables.

## Row Contract

Smalls writes visual rows through `core.visual`. The native functions are generic
over `object`; they do not know whether the object is a door, item, creature, or
placeable.

`ObjectVisualModel` is the C++ storage row:

- `model`: semantic model `ResRef`
- `attach_to`: socket or target node name, if the row is attached
- `attach_from`: source socket/node, if needed
- `kind`: root model, item model, creature model part, or creature attachment
- `slot`: equipment/loadout slot, or `-1` for no slot
- `part`: source item/body part vocabulary
- `source_part`: authored part number before mapping
- `model_part`: resolved dynamic model part number
- `flags`: row behavior such as `requires_wearer`, `hidden_in_game`, or
  `hidden_in_toolset`

`ObjectVisualLight` is the C++ light row:

- `light_color`: semantic palette/index value
- `light_offset`: local-space offset

Out-of-range behavior is explicit at the producer boundary. Invalid appearance
rows fail the resolver. Invalid attachment indices return an empty row. Empty
model `ResRef`s are rejected by the row consumer. Missing resources are counted
or warned at the loading boundary, not inferred by renderer passes.

## Models

Smalls communicates semantic model references as `ResRef`, not as concrete
filenames and not as `Resource`.

That distinction is intentional:

- Smalls decides which model identity should exist for the object.
- C++ decides which concrete resource type and upload path satisfy that identity.
- Renderer code sees uploaded model data and draw records.

For NWN compatibility, `nwn1.*` modules may still construct legacy model names
from table data. That is compatibility policy and should stay in `nwn1.*`, not
inside `nw::render` and not in low-level object components. A future RPG or
non-NWN rules module can emit different `ResRef`s or different row kinds without
changing renderer passes.

## Icons

Icons are not model rows and are not part of the model renderer protocol.

Item icon resolution returns icon `ResRef`s for UI/tooling consumers. The C++
side can ask Smalls for an item icon when a UI or preview tool needs one, then
the texture/image loading boundary decides whether the concrete source is TGA,
DDS, KTX, PLT, or another supported image format. The renderer should not care
which authored image format produced the icon.

Model rows may carry an icon hint for tooling, but renderer model loading does
not depend on that hint.

## Ownership

Smalls owns policy:

- resolving base-item, appearance, placeable, door, and creature state
- lowering current equipment into visual rows
- mapping equipment changes to visual rows
- deciding when a row requires wearer context
- emitting root, item, body/loadout, attachment, and light rows

C++ object components own storage:

- mutating equipment and inventory state before visual rebuilds
- `ObjectComponentSystem` stores visual rows by object handle
- rows are cleared and rebuilt on object load or on relevant state changes
- rows are contiguous per object and cheap to scan for viewer/runtime assembly

`nw::render` owns renderer meaning:

- validating and loading model assets
- applying material, PLT, animation, socket, and compatibility sidecar data
- creating `RenderModel` and `ModelInstance` data
- collecting `PreparedModelDraw` and `PreparedModelSurfaceDraw` arrays
- submitting renderer passes through `nw::gfx`

## Boundary Rules

- Do not resolve model names in renderer passes.
- Do not infer rules from model, texture, or node names.
- Do not make C++ read propsets for visual policy, except for narrow temporary
  migration bridges.
- Do not return `Resource` from visual/loadout resolvers. Emit semantic
  `ResRef`s and let C++ pick the concrete load path.
- Do not add per-frame visual resolution. Resolve on object load or state change,
  then iterate stored rows.
- Keep NWN oddities in `nwn1.*` lowering code until they can be replaced by
  source-neutral authored data.

The renderer boundary is explicit data, not a name convention.
