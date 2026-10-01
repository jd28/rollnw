# Shared Item workbench preview

Status: closed on 2026-09-30. Implementation and recorded regression/rendering
evidence satisfy the shared Item preview work; the user requested closure.

Standalone and contained Items share the workbench preview, with an isolated
armor mannequin and standalone non-armor models. Appearance edits and undo
refresh the preview while preserving source ownership and the Area scene.
The initial preview verification recorded 105 passing checks; the final
non-armor extension recorded 31 passing focused tests, with no skips. Rendering
captures were inspected; intermediate fixes retain their own evidence below.

Closure preserves the verification limits: full native desktop pointer use and
subjective layout acceptance were not verified by the automated/headless checks;
the non-armor missing-model diagnostic was reviewed, not fault-injected. No new
runtime or performance verification is claimed. The original plan and subsequent
implementation checkpoints below are retained as history; later checkpoints
supersede the initial armor-only preview scope.

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

## Global item colors (Tier 1, 2026-09-30)

Patterns & Conventions Found: `nwn1/item.smalls:1122` already produces six color
rows for part -1, and `:1290` validates/edits global colors with the existing
opaque undo-key protocol. `get_item_editor_color_rows` omits those rows, so its
availability validator rejects global edits. `item_editor_data_model.cpp:155`
already titles/selects global palettes; the main RML has no entry point. Reuse
these bounded snapshots, batch edits, preview notifications and palette button.

Architecture Decision: expose the existing six global defaults for layered and
armor items, with one Global colors palette entry above Models. Parts retain
their overrides and may explicitly Inherit as before. No new storage, commands,
serializer, preview path or bulk overwrite action. Plan B is to retain the
existing editor while correcting any failed inheritance/undo boundary.
ASSUMPTION: global means the stored item-wide defaults, not overwriting all part
overrides — affects editing semantics; this matches the existing data contract.

Component Design: main-thread Linux SDL/RmlUi client reads a stable Item handle
and a change-triggered snapshot of at most 19 parts and 120 color rows (six
globals plus 114 overrides); only mutations/selection replace that snapshot.
Part -1 is global, parts 0..18 are local, channels 0..5, palette values 0..175;
255 means inherit only for local parts. Invalid values, unsupported item types,
duplicates or stale targets reject through existing validation. Actual fixtures
cover armor, layered, composite and non-colorable models; usage frequency is
unmeasured. Common edits choose one channel; the existing batch path handles it
as a batch of one. The displayed Item remains the existing UI singleton.

Implementation Map: extend `lib/nw/smalls/scripts/nwn1/item.smalls`,
`tools/client/item_editor_data_model.cpp`, `ui/item_editor.rml`, client
README, and the existing SmallS, RmlUi and item-preview renderer regressions.
Data Flow: item color arrays -> global plus part rows -> first valid global
channel -> existing selector -> validated batch/undo -> icon/mannequin/placed
appearance refresh. Caller-owned object lifetime and indexed color keys remain.

Build Sequence: add regressions for global availability, inheritance/override
preservation, validation, undo/redo and serialization; expose rows and the UI
entry; build client/tests and run focused UI/editor/preview checks. Cost is six
additional cold snapshot rows, one scalar selection and one button; no added
frame transform or pointer-heavy hot path. No performance improvement claimed.

Critical Details: done means all six global channels are reachable, inherited
parts update while explicit overrides remain, global Inherit is unavailable,
undo/redo and save roundtrip preserve both arrays, and existing part controls
and preview refresh still work. Any global edit overwriting a part override or
stale UI binding fails acceptance. Simplification reuses the existing selector
and mutation protocol, avoiding six extra main-view swatches and a second color
editor. Report desktop interaction separately from automated UI/GPU checks.

Implementation/verification: the profile snapshot now includes six global rows
before its part rows (12 total for the layered fixture, 120 for armor); the
edit validator accepts that complete batch. The shared Appearance view has one
Global colors palette button above Models. Existing CSS, selector, palette
assets, opaque undo keys, serializers and preview notifications were reused;
no CSS or renderer implementation changes were needed. The serialized armor
fixture has defaults `[7, 34, 7, 4, 20, 16]` and all six torso colors set to 255
(inherit), confirming the existing contract on real input.

The new regression failed before exposing the global rows: global edit
preparation returned no batch. After correction, all 63 focused Item,
ClientSmallsItemEditor, ClientRmlTemplates and item-preview renderer tests pass,
with no skips. Checks cover all six global UI channels, hidden global Inherit,
invalid values/channel rejection, the complete 120-row protocol, unchanged
explicit overrides, updated inherited values, undo/redo and JSON roundtrip.
The mannequin/icon and equipped creature material checks exercise both global
and per-part changes and inverse edits while retaining Area camera/selection.
UI row shrinking/clearing produces no captured data-binding warnings.

Final self-check: client/tests build, changed C++ formatting and diff checks
pass. The change adds six cold rows, one scalar and one main-view button; it
uses the existing batch transaction and documented active-Item singleton.
No new per-frame work, ownership model, persistence format, speculative option,
or unmeasured speed claim was introduced. Verification used automated RmlUi
layout/events and Linux offscreen Vulkan with desktop assets; interactive
desktop pointer use, subjective appearance and Windows remain unverified.

## Consistent color actions (Tier 1, 2026-09-30)

Patterns & Conventions Found: the user's desktop capture shows the global
palette isolated in a tall row above an empty Models strip; the user also
reports misaligned Inherit and requires consistent buttons. `panel.rcss:2153`
already styles neutral text actions in the Variables toolbar, and `:2721`
defines the shared appearance selector header. Item-specific global wrappers
and Inherit styling duplicate those presentation rules.

Architecture Decision / Component Design: put a labeled Global colors action
in the Models header, use the existing shared selector header for item color
and property selectors, and share the existing neutral text-button rules with
Global colors and Inherit. One current Item, six channels and up to 19 model
rows remain unchanged; availability still follows the existing validated data.
Linux RmlUi layout, font metrics and viewport width constrain the result.

Implementation Map / Data Flow: change item RML/RCSS and the common panel RCSS;
the existing scalar availability and commands feed standard header/button
markup. Extend the existing layout checks for button alignment and capture the
real RmlUi renderer for visual inspection. No object mutation or persistence
changes. Error/hidden states continue through data-if and existing validation.

Build Sequence / Critical Details: replace duplicated presentation, verify
Global colors and Inherit have the same dimensions and centered labels, check
header right-edge alignment at narrow/wide widths, and exercise existing click
paths. Cost is ordinary layout of the same two buttons; removing the global
wrapper row and private styles removes work. No runtime performance claim.
Done requires rendered inspection as well as functional checks; a floating
control, clipped label or shifted Inherit fails acceptance. Plan B is to retain
the shared styling and revisit placement from the user's next visual feedback.

## Correct paired variations and quick reset (Tier 1, 2026-09-30)

Patterns & Conventions Found: `nwn1/creature.smalls:858` transforms up to 19
armor variations into visual rows. Its inheritance branch changes the source
part and requests the opposite mesh; `native/core_creature.cpp:344` implements
that request, and `profiles/nwn1/body_part_catalog.cpp:116` also falls back to
opposite-side resources. Existing integration fixtures contain both left/right
thigh and shoulder meshes. `toolset_backend.cpp:2999` already provides validated,
undoable model-part edits. No geometry transform or new storage is needed.

Architecture Decision / Component Design: inherit only the variation number;
resolve the destination part's mesh and retain its part/color identity. Keep
same-side phenotype fallback, with unavailable models remaining absent. The
shoulder None policy is awaiting the user's clarification and is independent
of this correction. Add an adjacent reset action for colorable part rows,
setting that row's variation to 1 through the existing edit batch and refresh.
This is a single active-editor command over a batch of one, not a new mutation
protocol. Composite model/variation rows do not expose per-part color actions.

Data Flow / Cost: byte variations (0..255, with 0/255 inheritance currently)
plus stable, module-loaded catalog data -> resolved variation -> same-side
resource/anchor and destination color -> existing visual row batch. Desktop
Linux CPU runs these bounded transforms on edits, not every rendered frame.
Actual inheritance usage distribution is unmeasured; explicit variations are
the straight-line case. Invalid part/variation requests reject using current
validation; missing resources produce no visual row. No allocations or lookup
layers are added; opposite-side lookup work and the prefer-mirror flag are
removed. No speed claim or new synchronization.

Implementation Map / Build Sequence: add side-specific inheritance regression
in `tests/smalls_engine_integration.cpp`, then correct the profile, native
binding and catalog; add RML reset action, data-model event and backend command;
extend existing UI event/layout and editor undo tests. Capture the shared
buttons with the native renderer. Existing arrays own snapshots and visual
rows; transient references retain existing lifetimes.

Critical Details / Done: both inheritance directions use the correct mesh,
anchor and per-part colors; explicit variations still work; reset selects 1,
preserves other parts/colors and supports undo. Shoulder None follows the
confirmed policy. Simplification removes opposite mesh selection entirely and
reuses the established edit/preview path. No name-based resource exceptions,
geometry reflection, new persistence fields or generic reset framework.

Policy confirmed by user: item None stays absent, including either shoulder;
mirroring belongs to creature body-part variations. Remove item-side inheritance
entirely (the item selector offers None and explicit variations, not Mirror).
Keep creature value 255 as opposite-variation inheritance, resolving its own
side's mesh. Item 0/255 supplies no armor override; ordinary limbs retain their
underlying creature body, while optional shoulders with no base remain absent.

Implementation / verification: the item inheritance branch and opposite-mesh
native flag are removed. Catalog fallback now stays on the destination side,
including phenotype fallback. Creature body value 255 still borrows the paired
variation. Regression covers left-only, right-only, both and neither shoulders,
correct part colors, body inheritance in both directions, and a catalog input
where only the opposite-side model exists. The shoulder regression reproduced
the unwanted extra piece before the fix (`/tmp/item-mirror-before.log`).

Global colors is now a labeled action at the right of Models. It and Inherit
use the shared neutral button and centered selector header; row palettes and
adjacent × resets share that button style. Reset uses the existing validated
model edit and undo stack. UI dispatch, real command execution from None to 1,
undo/redo, repeated-reset no-op, unchanged colors/other parts, invalid IDs and
stale Item rejection pass. No separate reset mutation protocol was added.

Both client and tests build. All 74 focused tests pass with no skips: 73 in
`/tmp/item-appearance-tests.xml` plus the registered-editor backend integration
in `/tmp/item-reset-backend-tests.xml`. Native Vulkan/RmlUi captures at editor
widths 560 and 1000 were inspected for both headers and row controls; automated
checks assert common button sizes, right-edge alignment and centered labels.
Captures are `build/tests/tmp/item-appearance-actions-{560,1000}.png` and
`build/tests/tmp/item-inherit-action-{560,1000}.png`. These show the real editor
pane and fonts; mannequin composition remains covered by separate renderer
regressions. The existing global/local color, serialization, undo and equipped
preview tests also pass. No Vulkan validation warnings/errors in the UI capture.

Final self-check: scope stayed within the existing editor, profile and catalog;
formatting and diff checks pass. The simplification pass removed item mirror
state, opposite-side lookup branches and duplicate button/header styles.
Existing batch edits and the active-editor singleton remain. No new hot-path
pointer layout, persistence fields, generic reset framework or performance
claim. Interactive desktop pointer use and Windows were not verified.

## Shared header icons and robe reset (Tier 1, 2026-09-30)

Patterns & Conventions Found: `panel.rcss:1928` provides the standard 28dp
square action style and `:3971` the centered close glyph. The header currently
uses a text action while part rows use palette/× controls. `nwn1/item.smalls:97`
and `tools/client/item_editor.cpp:134` define/read the existing cold part-row
protocol. `object_edits.cpp:4164` already prepares validated, sorted batches
and `:5458` commits them as one undo action.

Architecture Decision / Component Design: reuse the square button and close
glyph styles for header and row palette/× controls. Header palette opens global
colors. Header × resets all editable variations in one batch, while row × uses
the same path with one row. User explicitly requires robe -> None (0); the
profile supplies reset_value per row (1 for other parts). Composite rows retain
their separate model/variation editor and reject this reset operation.
ASSUMPTION: the header × means reset all variations, matching the per-row × —
affects the new header command; communicated to the user before implementation.

Data Flow / Cost: one active Item's at most 19 rows (byte values 0..255, fixture
cloth028 has robe 0 and varied limb values) -> profile reset values -> sorted
part/value arrays -> existing atomic edit/undo -> refresh. The CPU cost is a
cold bounded sort and one edit batch per click on the Linux SDL/RmlUi client;
no per-frame transform, new ownership or synchronization. Usage distribution is
unmeasured. Existing scalar row edits are the common path; all-row reset is the
same batch transform. Unknown/stale parts, split rows and invalid values reject;
already-reset values yield a no-op and no extra undo entry. Colors are untouched.

Implementation Map / Build Sequence: update part-row metadata and reader,
RML/RCSS, shared data-model callbacks and backend reset handler; extend the
existing UI/backend regressions and native renderer capture. Verify individual
robe reset, whole-armor reset/undo/redo/no-op, stale events and header/row column
alignment at 560 and 1000 widths. Simplification removes the one-off compact
text-button sizing and shares the reset batch rather than dispatching 19 edits.

Critical Details / Done: matching header/row icon styles and X columns, palette
still edits global colors, robe clears to None in both reset paths, other
variations become 1, and one undo restores every original variation. Keep
profile policy out of CSS/C++ part-name tests. No unrelated button redesign;
plan B for layout regressions is to adjust shared spacing while preserving the
existing glyphs and command path. No performance claim.

Implementation / verification: header palette and × now share the standard
square action style with their row counterparts; × uses the existing close
glyph's size/weight/centering. Native captures at widths 560 and 1000 show the
same palette and reset columns, and automated geometry checks pass. The robe
row tooltip says Clear variation (None). The profile publishes reset_value=0
for robe and 1 for other parts; both reset commands consume that metadata.

The reset implementation prepares one sorted batch and removes unchanged
patches. The integration test caught that global-scope editor commands do not
record undo; the two reset commands now use the existing hidden workspace
command registration, so the command bus records exactly one undo action.
Individual robe reset, complete armor reset, undo/redo, unchanged colors,
one mutation notification, repeated-reset no-op and stale-target rejection
all pass. Global-palette and both reset UI events remain reachable.

Final self-check: client/tests build and format/diff checks pass. All 43 focused
checks passed: 42 SmallS/editor-template/native-render checks in
`/tmp/item-header-icons-tests.xml`, then the corrected backend integration in
`/tmp/item-header-reset-backend-tests.xml`. The first XML retains the original
undo-registration failure for diagnosis. Captures remain at
`build/tests/tmp/item-appearance-actions-{560,1000}.png`. Simplification reused
existing button/glyph rules and the batch edit/undo protocol, with no new
persistence format, per-frame work, ownership model or performance claim.
Desktop pointer interaction and Windows remain unverified.

## Standalone non-armor preview (Tier 1, 2026-09-30)

Patterns & Conventions Found: `item_preview.cpp:78` stops after icon generation
for non-armor items; item RCSS and pointer handling also gate the 3D viewport on
armor. `viewer_viewport.cpp:314` already maintains a separate, revision-driven
preview session. `preview_scene.cpp:3341` and `:4733` resolve standalone Item
models through profile visual rows and borrow the live object without owning
it. Real fixtures include the composite shortsword `nw_wswss001`, composite
scimitar `wduersc004`, and simple `pl_aleu_shuriken`; existing armor tests cover
separate world/preview sessions.
ASSUMPTION: use each item's existing standalone model resolution, including its
profile default model when no specific model exists — affects fallback display.

Architecture Decision / Component Design: select the existing armor mannequin
or the live non-armor Item as the preview object, using a computed accessor on
the singleton preview state. Reuse the existing viewer session, viewport,
rotate/zoom, revision refresh and icon cache; no new renderer or object copies.
Show the 3D pane for both; gender controls remain armor-only. Missing model
resources show a fixed diagnostic and retain the inventory icon.

Data Flow / Cost: one live Item handle and stable profile/model resources ->
existing visual rows (one simple/layered model or up to three composite parts)
-> existing preview GPU model batch. Updates follow Item/resource revisions;
unchanged frames reuse that scene and render it. Ordinary non-armor edits avoid
mannequin cloning. Linux SDL/Vulkan/RmlUi and a separate world camera constrain
ownership and layout. Frequency/type distribution is unmeasured; no performance
claim. Cost is one preview scene's model/material GPU resources and draw work,
using the same limits and lifecycle as the existing armor preview.

Implementation Map / Build Sequence: update item preview accessor/hydration,
frame and pointer routing, renderer parameter names, viewport subject selection,
and item RCSS. Extend existing ownership/UI tests; render real non-armor
fixtures and an edited variation through the existing viewer tests. Update the
README. Invalid/stale handles clear the preview, unavailable resources report
failure, and unsupported gender changes reject through current policy.

Critical Details / Done: non-armor 3D geometry plus icon are visible, gender
buttons hidden, drag/zoom enabled, edits refresh and retain the preview camera,
source Item/owner and world scene/camera remain intact, and switching back to
armor still works. Simplification reuses the already-supported live-Item load
path and computes the preview subject instead of storing another state. The
preview is a true singleton for the active editor. Plan B for unavailable model
resources is the existing icon plus diagnostic, not invented geometry.

Visual verification refinement: generated scimitar fixtures contain only a
triangle, so acceptance captures use the installed shuriken and shortsword
models without development model overrides. Those real models start edge-on
under the general object's front view and are small under its one-unit minimum
camera distance. Use the existing orbit-camera API with a three-quarter view
and radius derived from the current scene bounds, respecting the camera's
existing 0.25-unit minimum. This adds one constant-time framing branch on load
for non-armor items; same-item revisions retain the user's camera as before.

Verification / final self-check:
- `rollnw-client` and `rollnw_test` builds pass; all 31 focused inventory-preview,
  RmlUi template, armor rendering, and standalone-item rendering tests pass
  (`/tmp/item-model-preview-tests.xml`). No skips.
- Installed simple shuriken and composite shortsword geometry renders with a
  nonempty inventory icon; a part edit and inverse reload the composite model.
  Clearing/switching previews preserves the source Item, and independent world
  scene, selection, and camera stay intact. Armor mannequin ownership/gender
  tests still pass. RmlUi verifies visible non-armor geometry viewport and hidden
  gender controls, then armor control visibility.
- Inspected `build/tests/tmp/item-preview-pl_aleu_shuriken.png` and
  `build/tests/tmp/item-preview-nw_wswss001.png`: the oblique view exposes the
  model faces and the bounds-based distance frames small items visibly.
- Pointer routing and same-item camera retention reviewed in the client path;
  native desktop mouse interaction was not exercised. Rendering/UI checks ran
  headlessly. Missing-model diagnostics were reviewed, not fault-injected.
- Simplification removed the armor-only gate and reused the existing borrowed
  object, icon batch, renderer, camera API, and singleton lifecycle. No new scene
  ownership state, caches, configuration, hot loops, or performance claims.
  Invalid handles clear; empty model scenes fail explicitly. C++ formatting and
  `git diff --check` pass.
