# Shared Item workbench preview

Tier 1: extend the existing workbench, icon batch, and ViewerSession; no new
rendering subsystem or document format. The Linux desktop client uses SDL3,
RmlUi and nwgfx; UI/kernel mutation is on the main thread, render resources are
GPU-owned, and Area documents own their contained Items.

Frame: inventory/store editing needs visible feedback for the item a player will
see. Keep this bounded to the common Item workbench and existing renderer; a new
scene editor or copied Area is outside scope. If body resources fail, retain the
inventory icon and an explicit mannequin-unavailable message.

## Patterns & Conventions Found

- `tools/client/ui/item_editor.rml:4` is already the common Item editor.
- `tools/client/workspace_view.cpp:325` currently puts Area editing beside the
  world view; `:338` gives blueprint data surfaces the available workspace.
- `tools/client/viewer_viewport.cpp:385` creates ViewerSessions through one
  ViewerDevice. Sessions can borrow live objects and have independent cameras.
- `tools/ui/smalls_item_icons.cpp:158` composes batches of materialized icon
  layers; its cache evicts images absent from the submitted batch.
- `tools/client/object_document.hpp:18` supplies movable RAII ownership for a
  temporary object tree. Item commands already target the original document.
- Profile appearance rules live in SmallS (`nwn1.creature`, `nwn1.item`);
  C++ captures ownership and presentation facts. Match this boundary and format.

## Architecture Decision

Use the same two-column Item workbench at every entry point: its own preview
column plus the existing editor. Every item gets an inventory icon. Armor gets
an independent mannequin session, initially based on an available humanoid
creature ancestor, otherwise human, with Male/Female controls in both cases.
The world session remains retained while hidden. Back restores its exact camera
and selection; item commands/history/save keep their existing destination.

Inputs: one active Item handle, optional Creature ancestor, mutation/resource
generations and gender selection (-1 initial/source, 0 male, 1 female). Item
appearance is a profile-defined batch (armor has 19 parts); creature appearance
has 20 body parts and four colors. Base-item model type, not resource names,
decides whether armor needs a mannequin. Typical numeric fixture values include
human appearance 6 and gender 0/1. Usage distributions are not measured.
ASSUMPTION: one item is edited at a time — affects retaining one preview only.
ASSUMPTION: preserving a humanoid owner's body is useful — affects the initial
mannequin; non-humanoid owners fall back to the human body.

Stable input is the Item/ancestor identity; item edits/undo and gender switches
invalidate the cold preview snapshot. The frame path compares generations and
handles. Missing objects/resources or invalid gender fail with a visible status;
never mutate the source creature to obtain a preview.

## Component Design

- Item preview state (`item_preview.hpp/.cpp`): owns a temporary Creature and
  cloned armor through ObjectDocument, plus an independent icon cache. Reads
  borrowed source handles only during refresh; no original equipment ownership
  transfers. One displayed preview is a true UI singleton. Its icon transform
  reuses the plural API with a batch of size one.
- SmallS preview policy (`toolset/item_preview.smalls`): copies only body
  appearance fields; validates gender and chooses the human fallback. No
  commands, persistence, placed location, stats copy, or world selection changes.
- Existing viewer: a second session shares its device/resources and borrows the
  mannequin. Render/drag/zoom affect that session exclusively; clear it before
  releasing device resources. Borrowed scene teardown does not read dead roots.
- Workbench UI: native presentation hydrates icon/status and gender controls;
  capture/apply clicks validate original Item identity. Viewport hit tests are
  limited to the preview rectangle. Other Item fields remain unchanged.

## Implementation Map

Create `tools/client/item_preview.hpp/.cpp`, `tools/ui/scripts/toolset/item_preview.smalls`.
Modify workbench state/view, Item RML/RCSS, workspace layout, application frame,
pointer/release handling, renderer wrappers and generated-texture lookup,
ClientViewerViewport, client/test CMake inputs, README and focused tests.

## Data Flow

Active Item + ancestor + generations -> validate/profile classification ->
one icon batch and (armor only) owned mannequin/armor copy -> separate viewer
session -> Item preview rectangle. UI gender choice invalidates the preview;
actual edit commands mutate the original Item and refresh both icon and mannequin.
Closing/navigating away drops preview state. World scene and history are retained.

## Build Sequence

1. Add snapshot/policy and focused ownership/gender/icon tests.
2. Add the shared layout, separate render session, and pointer routing.
3. Build client/tests; verify UI, instance navigation/history and renderer behavior.

## Critical Details

Costs: one extra session/camera plus one temporary creature/armor tree; textures
and device are shared. Cold refresh copies O(item serialized data + body fields)
and resolves its model rows. Frames draw one mannequin, independent of Area size.
No measured performance requirement; do not claim a speedup. No new pointer-heavy
hot loop; existing renderer consumes its existing contiguous model/draw batches.

Simplification: reuse existing fields, icon composition, renderer, document RAII,
orbit camera and commands. Do not clone the Area, copy the creature inventory,
build a second item editor, add alternate item documents, or cache many mannequins.
Do not make every non-armor item render a 3D model.

Done: identical preview layout for standalone and contained Items; changed parts/
colors and undo update the icon/mannequin; male/female both render when assets are
available; source creature appearance/equipment/location and Area camera/selection
remain unchanged by preview controls; no preview objects enter saved documents.
Test malformed/stale input, source isolation, cleanup, UI hit targets, navigation,
undo and resource failures. Report interactive/GPU checks separately.

## Implementation and verification

- Shared Item template now includes the icon and armor-only mannequin; Variables
  is the final Item tab. Ordinary items use a narrow icon column.
- Placed humanoid appearance is copied into the private mannequin. Store and
  standalone armor use a human body. Male/Female selection, orbit and zoom target
  only the separate session. The original Area session remains retained.
- Simplification pass reused the icon batch and ObjectDocument ownership and
  removed duplicate session cleanup after `clear()`. No new rendering subsystem,
  alternate save format, or per-frame serialization was introduced.
- Client and test executables build. Ownership tests cover independent armor
  copies, source serialization remaining identical, gender validation, unchanged
  mutation epoch, human fallback, stale identities and preview cleanup.
- Desktop-asset graphics acceptance renders both genders with the existing
  Adept's Tunic fixture. The generated PNGs were visually inspected. Color edits
  and inverse edits refresh the icon and mannequin; the retained Area scene,
  camera and selected object remain unchanged.
- RmlUi checks cover both layout contexts, visible gender hit targets and final
  Variables-tab position. Navigation/history/save regressions use real placed
  inventory, equipment, nested-container and Store instances.
- Final run: 104 focused client/layout/editor/Area tests passed, plus the desktop
  graphics acceptance test (105 total, zero skips/failures). Changed C++ passes
  clang-format verification; `git diff --check` passes.

The preview owns one temporary creature/armor tree and a second session sharing
the renderer device. No performance claim is made or benchmark required. Full
interactive desktop pointer use and subjective layout acceptance remain for
manual review; automated RmlUi layout and offscreen GPU rendering are separate
checks. Missing complete desktop body assets explicitly skip only the graphics
acceptance test; core ownership and UI tests still run.

Final self-check: the stable-frame path only compares identities/generations;
cold work uses the existing icon batch and a documented singleton mannequin.
Invalid identities clear the preview, invalid gender is rejected and missing
resources produce a diagnostic. Temporary ownership, save isolation and source
immutability were verified. No speculative extension points or new pointer-heavy
hot paths were added. The remaining manual check is explicitly stated above.

## Placed armor refresh correction (Tier 1)

Patterns & Conventions Found: `client_application_workspace.cpp:162` resolves an
equipped Item to its placed Creature but only rematerializes rows for the naked
Appearance view. `Creature::instantiate` is idempotent; renderer refresh therefore
reads the old ObjectVisualState. `nwn1.item.update_visual_for_slot` clears the chest
slot even though `nwn1.creature` owns its body rows. Callback registration order
can consequently remove armor rows after the body rebuild.

Architecture Decision: rematerialize the affected creature through existing
`update_appearance_preview_rows` before rebuilding GPU rows, retaining its current
equipment-visibility policy. Keep chest-body ownership exclusively in the creature
script. Limit this correction to visual synchronization; no Area reload or new
equipment tracking. Plan B on materialization failure is to keep the existing
rendered model and report the failed refresh.

Component Design / Data: input is one published UI mutation and its existing
Area ownership rows, plus live equipment/body data (18 slots, 20 body parts,
19 armor parts, 6 armor color channels). Output is refreshed creature visual rows
then replacement models in the retained scene. Common unchanged frames do no new
work; edits and equip/unequip are cold paths. Usage distribution is unmeasured.
ASSUMPTION: the reported disappearing body is the chest rows being removed —
affects the callback regression; verify it before accepting the fix. Stale handles
and failed profile updates return false; never replace models from failed rows.

Implementation Map / Data Flow: edit -> existing owner resolution -> profile
body/equipment materialization -> existing batch model replacement. Modify
`client_application_workspace.cpp`, `nwn1/item.smalls`, and focused client/renderer
tests. Existing singleton mutation observation is retained; model replacement
uses the existing plural API. No new pointer-heavy hot path.

Build Sequence: reproduce stale armor rows and both callback orders; patch the
two ownership gaps; verify color/model edits, undo/redo and unequip/re-equip with
real humanoid armor, including immediate body presence and retained Area state.

Critical Details: Linux main-thread SmallS and GPU renderer are the platform.
Additional work is one body/equipment row rebuild per affected visual mutation
(bounded by the profile parts and equipment slots), with no new persistent state.
Simplification removes the redundant chest-slot clear, reuses existing refresh
policy, and avoids polling/reloading the Area. Done requires actual armor row and
rendered-material changes, not merely new model handles or a changed icon.

Reproduction: the new placed-armor test observed unchanged cloth color 7 after an
edit to 14 and unchanged torso model 39 after an edit to 1. A creature-then-item
equip callback order reduced body rows from 18 to 1. Both checks failed before
the correction and pass afterward. This resolves the callback-order assumption;
the user's precise interactive sequence has not been replayed in a desktop UI.

Correction: all affected creature visual mutations rematerialize body/equipment
rows before model replacement, retaining the naked Appearance-view policy when
active. Failed materialization skips replacement and reports the existing refresh
error. The generic Item slot updater leaves chest rows to the creature body pass
and rejects invalid creature identities. No new state or ownership transfers.

Verification: desktop-asset regression checks inspect the placed creature's PLT
material and torso row after color/model edits and inverse edits, exercise three
unequip/re-equip cycles, render each refresh, and verify retained scene/camera/
selection. Both equip-callback orders preserve the full body. The original
male/female mannequin isolation test also passes. Client/test builds and C++
format checks pass. No performance claim; the added work stays on visual edits.

Final correction self-check: 58 equipment/editor/Area regressions and both desktop
graphics tests pass (60 distinct tests, no skips). The callback regression also
passed in the desktop run. The existing batch model path and singleton mutation
observer remain; invalid-input behavior and ownership are unchanged except for
explicitly rejecting stale chest-update targets. No new cache, general-purpose
abstraction, hot path or persistent state was added. The stated row/material and
body-presence criteria are verified; full interactive desktop replay remains
unverified.

## Single palette entry per Item part (Tier 1)

Patterns & Conventions Found: the common Item RML creates a button for each of
up to six valid color channels per part. `ItemEditorDataModel::build_main` copies
those rows while `build_color` already supplies the complete channel selector.
The client uses CSS-drawn button icons. RmlUi caches binding update depth when a
view is created; bindings on generated `data-for` roots can run before their
parent loop removes obsolete rows (`DataView.cpp`, `DataViewDefault.cpp`).

Architecture Decision / Component Design: keep one palette entry for each
colorable part and retain all channels in the existing color selector. Select
the first valid channel as its initial value; no valid channel means no button.
Move row-dependent root bindings into children of the structural loop so array
shrink/clear cannot read old row indices. No RmlUi fork changes or warning filters.

Implementation Map / Data Flow: bounded Item parts/colors -> first valid channel
per part -> one CSS palette button -> existing `open_color(part, channel)` command
-> existing selector. Modify Item data model, RML/RCSS and the existing RmlUi
event/focus regression. Main-thread Linux client, no asset loader changes or new
state. Up to 19 parts and six channels each; distribution is not measured.
ASSUMPTION: one palette entry per colorable part is intended — affects placement.

Build Sequence / Critical Details: adapt the existing UI regression to count one
entry, exercise channel selection and applying colors, and capture bounds warnings
across color mode, shrinking arrays and clearing the active Item. Then update
markup/data and build client/tests. Cost is one button and one channel index per
colorable part instead of six swatch buttons/copies; no measured performance claim.
Simplification removes the nested main-view color list and reuses the selector.
Done is one usable palette icon per part, all color channels still editable, and
no out-of-bounds binding warnings during these transitions. Invalid channels keep
the current explicit error behavior. Existing singleton displayed-editor model
and batch row transforms remain. Plan B is retaining valid rows until teardown if
the markup ordering correction is insufficient.

Verification/self-check: the appearance regression first failed on the previous
multiple-swatch entry count. After the change, it verifies one palette entry,
opening the correct part/channel, switching channels, applying a color, retained
model-field focus, and transitions from 19 parts to one to no active Item.
17 focused template/Item editor tests pass, with no captured array-bounds or
missing-data-variable warnings. The user's exact warning sequence was not
reproduced in the test; the root-binding ordering ambiguity is removed by the
markup change. Client/tests build, C++ formatting and diff checks pass. No changes
to authored data, save/undo behavior or renderer were needed. Icon appearance
still awaits the user's desktop review.
