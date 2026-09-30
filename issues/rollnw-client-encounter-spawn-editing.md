# Client Encounter spawn field editing and reordering

Status: inline Single Spawn, read-only CR, and numbered native point markers
with rotation/deletion implemented; reordering remains open. Later sections
supersede the earlier modal and marker designs. Extracted 2026-09-26 from the completed
[object-workbench milestone](closed/rollnw-client-object-workbench-and-property-surfaces.md).

## Existing data and behavior

The Spawns view projects the ordered `EncounterState.creatures` array. Creature
blueprint drops append records; the minus button removes the selected record.
Both use exact, undoable full-array replacement with a maximum of 1,024 entries.
Selection uses an indexed key tied to kernel generation, active object identity,
and mutation epoch, so stale context rejects before mutation. Duplicate entries
and surviving row order are preserved.

The recorded imported sample contains 466 Encounter blueprints: 387 have one
spawn row and the observed maximum is six. The managed-list host renders only
the viewport plus six overscan rows. Standalone previews consume the same array
but display at most 16 valid Creature blueprints; missing resources and preview
truncation produce diagnostics. These are existing bounds, not a reason to
truncate the stored spawn array during editing.

## Selected-row editing and numbered point markers (2026-09-29)

Tier 1. Scope: edit existing Creature spawn records and make area spawn-point
markers visible at ten metres high. Reordering remains a separate follow-up.

### Patterns & Conventions Found

- `tools/client/object_edits.hpp:189` and `object_edits.cpp:3516` already own
  ordered spawn snapshots, validation, atomic replacement and exact undo/redo.
- `tools/client/toolset_backend.cpp:615` validates managed-list selections using
  kernel generation, live object identity and mutation epoch.
- `tools/client/command_view.cpp:295` consumes owning command forms; invalid
  authored fields can redisplay with feedback. Native workbench click capture
  copies values before SDK dispatch; no DOM pointer may survive dispatch.
- `lib/nw/render/viewer/scene_debug.cpp:929` already associates each point's
  geometry/picking range with its zero-based index in `spawn_points`.
- Re-read 466 local imported Encounter blueprints: 6 empty, 387 single-row,
  40 two-row, 24 three-row, 7 four-row, 1 five-row and 1 six-row. Appearance
  values span 0..5574; CR spans 0.125..3814; single-spawn flags are 0 or 1.
  The existing equipped-bow fixture contains duplicate rows with CR 399.
- Follow-up inspection of 609 native area files found 3,512 Encounters and
  4,123 points: 161 have zero points, 2,635 one, 667 two, 43 three, 5 four,
  and 1 five. Point positions/orientations are finite component values;
  editor commands admit points within the area and cap point arrays at 1,024.

### Architecture Decision

Reuse the selected-row command path and the shared command form. Edit ResRef,
CR, appearance and single-spawn, preserving unrelated rows and duplicates.
Keep the existing data contract: nonempty resource names, finite float CR,
signed 32-bit appearance and canonical 0/1 flag; malformed textual values
reject before mutation. Missing Creature resources retain authored references
and the existing preview diagnostics. The modal token binds the row to its
object and mutation epoch, so a changed object/list rejects submission.

Confirmed by the user: the marker number is its one-based position in the ordered
spawn-point array, independently of the Creature list. Larger Encounter changes
are explicitly deferred. The existing picking subindex is the association;
deletion renumbers survivors and undo restores the previous order.
ASSUMPTION: ten metres means ten world units vertically above the saved point —
affects display geometry, never the saved position or navigation admission.

### Component Design / Implementation Map

- Modify `toolset_backend.cpp`: selected-row edit command, owning form values,
  validated submission through existing full-array transaction.
- Modify `object_workbench_view.hpp/.cpp`, `client_application_release.cpp`
  and `ui/panel.rcss`: Edit action captured by native workbench dispatch, with
  its command result forwarded to the application's existing modal owner.
- Modify `scene_debug.cpp` and document the existing index in
  `preview_scene.hpp`: tall triangular markers using existing triangle batches;
  keep picking and ordinal tied to the same source-array index. Seven-segment
  numbers are baked horizontally at the top for reading from above, using ten
  digit masks and the existing triangle renderer; no fonts, textures or
  per-frame text layout. Numbers align to the area axes independently of point
  orientation. Two crossed triangles keep the marker visible from
  either horizontal axis, and the ground direction arrow remains.
- Extend `tests/rollnw_client_rml_smalls_language_binding.cpp` and
  `tests/render_viewer_area_selection.cpp`; update the client usage guide.
  No new serializer, persistent ID, widget framework or ownership system.

### Data Flow

Managed row selection -> owning form/token -> parse and validate all fields ->
copy the ordered array and replace one row -> existing transaction -> preview,
dirty state and save. Bad/stale submissions leave the complete source unchanged.
Saved point arrays -> linear marker geometry batch + indexed picking ranges;
add/delete/undo rebuild from the authoritative order.

### Build Sequence

1. Add field editing and its native UI dispatch.
2. Add tall indexed markers after resolving the requested number's meaning.
3. Verify commands, UI dispatch, stale/no-op/error cases, exact undo/redo,
   blueprint/CAF persistence, marker height and picking; build client/tests.

### Critical Details

Platform: Linux/Windows desktop, one UI/kernel thread, RmlUi plus the shared
Vulkan renderer. Object data is stable between commands; selection/form values
are volatile. Only the selected Encounter and its document are mutated.
Cold editing costs O(n) copying/validation and two retained arrays per undo
entry (existing bound 1024; common n=1). Marker generation is a linear pass
over points at scene refresh, using indexed contiguous vertex/index buffers.
No per-frame resource lookup or pointer-heavy hot path is added. Runtime timing
impact is unverified; no speedup or latency requirement is asserted.

Simplification: use existing forms, transactions and picking indices; derive
ordinals from array order instead of persisting duplicate identity; keep reorder
out of this slice. The form is genuinely singular because one modal owns input.
Follow neighboring aggregate initialization, explicit rejection and clang-format.
Done requires the build and focused regression checks above, with desktop visual
verification reported separately. A need for stable identity across reordering
would invalidate ordinal-only identity and requires an explicit future contract.

### Verification and self-check

- Release build completed for `rollnw_test`, `rollnw-client` and `mudl`.
- 24 focused tests passed: Encounter serialization; spawn-array and point
  transactions; marker height/picking/renumbering (including two-digit labels);
  existing native workbench click contracts; collection removal; and the new
  selected-row form. The form test covers native Edit dispatch, modal Cancel
  and Apply, unchanged values, malformed fields, resource-length and numeric
  bounds, duplicate preservation, stale object/epoch tokens, one undo record,
  exact undo/redo and blueprint/CAF save/reload.
- Captured and inspected the shared renderer's headless output using an
  isolated imported DockerDemo project with three added points. Markers reach
  ten world units above their saved positions and show 1, 2, 3. The initial
  capture exposed sideways digits; numbering now uses fixed area axes.
- `clang-format --dry-run --Werror` on changed C++ and `git diff --check` pass.
- Simplification/self-check: retained existing forms, undo and indexed picking;
  no new persistent schema, reordering or future Encounter features. Geometry
  stays batched, and point counts outside the picking index range produce a
  diagnostic and omit the complete marker batch. The optional form-output
  pointer is a synchronous cold UI borrow, never retained or used in rendering.
- Verification limits: no interactive desktop pointer/keyboard session or
  Windows run; no before/after performance claim. The headless capture verifies
  the shared geometry presentation, not complete desktop interaction.

## Read-only CR and selected-encounter markers (2026-09-29 follow-up)

Tier 1. This supersedes the editable CR/Appearance fields above. The user wants
CR displayed, Appearance omitted, and points visible only for the selected
Encounter. Scope ends at these editor changes; larger Encounter work is deferred.

### Patterns & Conventions Found

- `toolset_backend.cpp:673` owns the selected-row form and transaction;
  `object_edits.cpp:3467` already derives spawn records from Creature propsets.
- `data_object_editor.smalls:69` projects read-only list cells. The existing
  form supports descriptive text, so no read-only widget extension is needed.
- `scene_debug.cpp:1093` filters contiguous draw ranges; the picker uses their
  indices (`area_render_scene.cpp:1143`). `PreviewScene.active_object` is the
  existing selection authority. Selection changes do not need geometry rebuilds.

### Architecture Decision

Keep the existing stored CR/Appearance fields for compatibility. Display CR in
the list and form description; only ResRef and Single Spawn are inputs. On a
changed ResRef, load that Creature once and reuse the batch record extractor.
Reject missing/invalid replacements; existing unresolved references still allow
flag edits without loading. The earlier observed distributions and 1,024-row
bounds apply. Stable rows/geometry change on commands; selection can change on
each click. ASSUMPTION: replacing a creature should refresh its stored CR and
Appearance from the replacement blueprint — affects reference-change handling.

### Component Design / Implementation Map

- Modify backend, Smalls list, workbench header and CSS for the reduced fields.
  Temporary Creature ownership ends after extraction; failure leaves rows intact.
- Add a selected-object condition to each marker draw range in `preview_scene.hpp`
  and `scene_debug.cpp`. Reuse it for rendering, picking and selection outlines
  in `area_render_scene.cpp` and `session.cpp`; footprints remain selectable.
- Extend existing UI, area selection and headless renderer tests; update README.

### Data Flow

Selected row -> two-field form -> validated reference/flag -> optional Creature
load and batch propset extraction -> existing full-array undo transaction.
Marker ranges + current selected object -> visible index batch and eligible
picking ranges. Invalid triangle ranges are skipped; polygon-only picking keeps
its existing independent contract. A different/no selection hides markers.
All buffers and selection metadata remain owned by the scene.

### Build Sequence

1. Remove derived inputs and verify replacement/flag behavior and round trips.
2. Apply marker visibility to drawing/picking; test switch/clear selection.
3. Build client/tests/mudl, run focused regressions, format and self-check.

### Critical Details

Same desktop/UI-thread/Vulkan platform as above. Editing costs one resource load
only on replacement plus existing O(n) undo copies. Rendering with conditional
ranges costs an O(r) linear scan and O(i) visible-index copy, reserving temporary
capacity up to the full index count. Each draw range adds one 8-byte owner handle.
Geometry is retained. Handles are flat values compared with
the existing selected object, not pointer chasing. Common encounters have one
point; empty and unselected batches draw none. No timing claim or budget.

Simplification: remove two input fields and their parsing; reuse Creature
extraction, current selection, and range filtering. Do not add persistent IDs,
selection caches, form widget types or continuous blueprint synchronization.
The existing one-modal command remains a singleton; geometry and row transforms
remain batches. Plan B if range filtering cannot share the selection contract is
to rebuild debug geometry on selection, at a larger allocation cost.

Done: CR cannot be submitted, Appearance is absent from UI, changing Creature
derives metadata, undo/save preserve exact arrays, and only the selected owner's
markers draw or pick. Headless draw counters and selection regressions must
prove switching and clearing selection; any hidden marker hit fails the design.

### Verification and self-check

- Release build completed for client, `rollnw_test` and `mudl`.
- 46 focused tests passed: 43 renderer, picking, native workbench, Encounter
  transaction/serialization checks, followed by the three affected UI checks
  after fixing the empty fourth-cell protocol value. The three-column list
  still supplies all four string slots required by the native list decoder.
- The real headless renderer's index counters verify no-selection, first-owner,
  second-owner, global Encounter visibility off/on, and cleared-selection draws.
  All 23 area-selection tests pass, including hidden-marker misses, footprint
  selection, marker height, numbering, and unrelated polygon-only picking.
- The form test verifies two editable fields, visible CR text, absent Appearance
  header, rejection of old CR/Appearance submissions, derived replacement data,
  missing replacement rejection, unresolved existing-reference flag edits,
  exact undo/redo, and blueprint/CAF save/reload.
- Inspected the unselected headless area capture: the three numbered markers
  from the earlier capture are hidden. No interactive desktop or Windows run;
  continuous synchronization after separately editing a Creature is outside scope.
- `clang-format --dry-run --Werror` and `git diff --check` pass. Simplification
  retained existing modal, batch record extraction, transactions and scene
  selection; no new persistent schema, cache, widget type or future extension.
  Temporary Creature cleanup is scoped, failures leave arrays unchanged, and
  rendering/picking use the same range predicate. No performance claim made.

## Marker height adjustment and Aurora reference (2026-09-29)

The requested quarter-height adjustment supersedes the earlier ten-metre
presentation: markers now extend 2.5 world units above each saved point. Width,
number readability, order, selection visibility and saved coordinates are
unchanged. This is a mechanical constant adjustment; it adds no states,
allocations or geometry, and uses the same batch generation/picking paths.

Research evidence:

- BioWare's [Aurora Toolset tutorial, page 8](https://neverwintervault.org/sites/neverwintervault.org/files/articles/files/auroratoolsettutorial.pdf)
  describes selecting an Encounter, right-clicking the desired position, and
  choosing **Add Spawn Point**.
- The local decompiled reference
  `/home/josh/Downloads/1.86.8193.34.1 Decompiled Models (One Folder)/spawnpoint.mdl`
  contains a textureless triangular prism: eight source vertices, twelve faces,
  X extent 0.5, Y extent about 0.968246, and Z extent exactly 0..2.5. Its node
  translation does not change Z. These are measured asset dimensions, not a
  claim about the toolset's final display scale.
- The installed `bin/win32/nwtoolset.exe` contains UI controls and setting names
  for showing spawn-point markers, marker height, and marker width. A live
  NWToolset run and its default/user scale settings were not inspected.
- Loading `spawnpoint` through `mudl stats` confirms a single textureless prism
  mesh (22 split render vertices, 36 indices). Its stored model bounding box is
  broader than the decompiled mesh, so it is not evidence of visible height.

Verification: client, test executable and mudl build passed; all four focused
tests passed. Updated Encounter picking
checks exercise the 2.5-unit bounds, hits below the top, misses at the old height,
and numbering across deletion/restoration. The selected-owner headless rendering
check also passed. Interactive desktop visual comparison remains unverified.

## Native spawn model with numbering (2026-09-29)

Tier 1. Replace the generated crossed triangles with the user's requested
`spawnpoint.mdl` geometry. Keep ordered numbering and selected-owner visibility.

### Patterns & Conventions Found

- `scene_debug.cpp:675` batches point geometry, labels and picking ranges.
- `kernel/ModelCache.hpp:22` loads MDL resources; `render/nwn/model_loader.hpp`
  exposes the existing CPU model importer, including authored node transforms.
- The observed model has one static, untextured prism, 22 split vertices and
  36 indices; the source mesh is 0.5 by 0.968246 by 2.5 world units. Its pivot
  includes the authored node translation. The earlier point-count distribution
  applies: one point is most common, observed maximum five, editor cap 1,024.

### Architecture Decision

Load/import the actual resource once per nonempty encounter point batch during
geometry rebuild, flatten its static triangles with authored node transforms,
then rotate/translate them for every saved point. Use the existing debug tint
and triangle renderer. Numbers stay aligned to area axes, centered above the
rotated model bounds, with 0.05 units clearance above the cap to avoid coplanar
faces. ASSUMPTION: stored point orientation is an ordinary Z-axis rotation,
matching existing placed-object transforms — affects model facing.

### Component Design / Implementation Map

- `scene_debug.cpp`: reuse ModelCache and CPU importer; extract finite static
  triangles, release the cache borrow with scope exit, then build marker batches.
  Missing/empty, skinned or invalid geometry logs and omits the marker batch;
  the footprint remains available. No resource pointers survive construction.
- Existing area-selection tests: verify real prism dimensions, rotated faces,
  caps, ordinal labels and hidden-marker misses. Headless renderer test retains
  owner-switch/global-visibility checks. Update README and this issue.
- `tests/test_data/user/development/spawnpoint.mdl`: native ASCII fixture from
  the inspected decompiled model. The dedicated-server test resources omit
  this toolset mesh; ordinary runtime loading still uses the game's resource.

### Data Flow

Resource -> CPU model primitives -> contiguous model-space triangle positions ->
saved position/orientation array -> tinted world triangles plus number strokes ->
existing scene-owned draw and picking ranges. Saved data is read-only. Resource
data and geometry are stable between rebuilds; active selection changes per click.
Malformed index triples, non-finite transformed vertices and unsupported skinned
data reject the complete marker model. Empty point batches do no resource work.

### Build Sequence

1. Replace procedural geometry using the actual MDL and existing importer.
2. Update shape/facing/picking regressions and inspect a selected-marker capture.
3. Build client/tests/mudl, run focused checks, format, and self-check.

### Critical Details

Desktop CPU/Vulkan platform, existing UI/kernel thread. Model import and temporary
triangle storage are cold O(v+i); point construction is O(points * triangles +
label strokes). Native prism body contributes 12 triangles per point. Frame-time
visibility filtering is unchanged. No performance target or measured speed claim.
Existing model pointers are scoped cold importer inputs; hot geometry uses linear
arrays and indices. Simplification removes the ground arrow/cross and crossed
triangle generator; native shape supplies facing. No new render instances,
animation state, persistent IDs, resource cache or settings. If CPU import fails,
diagnostics plus the selectable encounter footprint are the explicit fallback.
Done requires source-sized prisms, visible non-coplanar labels, correct rotated
picking and unchanged owner selection. Any label/geometry divergence fails it.

### Verification and self-check

- Release client, test executable and mudl build passed. Five focused tests pass:
  point add/move/delete/replay, encounter footprints, separate marker picking,
  numbering/native geometry/facing, and selected-owner headless rendering.
- The shape check verifies the native mesh's actual extents and node offset,
  twelve body triangles, all four quarter-turn orientations, label clearance,
  two-digit numbering, and renumbering after deletion/restoration.
- Captured and inspected `build/tests/tmp/encounter-native-markers.png`: two
  selected-owner prisms at different angles with white 1/2 labels above the caps.
  The capture now uses the headless target's actual 256-by-256 viewport. The
  renderer test passed again after that capture-only correction.
- Initial tests without the resource exercised the logged missing-model path:
  footprints remained, and point markers were omitted. Adding the native test
  fixture lets dedicated-server resource tests verify the real model geometry.
- Simplification/self-check: removed the old arrow/cross/triangle generator;
  kept native geometry import, existing labels and batch selection metadata.
  Empty batches avoid loading, scoped model references are released, invalid
  triangle data rejects, and the complete vertex/index budget is checked before
  writing markers. No new cache, renderer instance, saved schema or per-frame
  resource lookup. `clang-format --dry-run --Werror` and `git diff --check` pass.
- No runtime timing claim; no interactive desktop or Windows verification.

## Inline Single Spawn and marker controls (Tier 1)

### Patterns & Conventions Found

- `area_object_editor.cpp:123` applies routed wheel commands; `editor_input.cpp:158`
  resolves flat input batches. Ctrl+wheel already means 15-degree rotation.
- `area_object_editor_runtime.cpp:148` deletes a selected point using the existing
  whole-array transaction. `object_edits.cpp:4035` validates finite data, area bounds,
  stale snapshots and the 1,024-point cap, and publishes debug-geometry changes.
- `rml_managed_list.cpp:280` renders bounded, escaped cells; it currently has no
  checkbox presentation. `object_workbench_view.cpp:2198` captures boolean clicks
  as values before SDK release. Row keys include object identity and mutation epoch.

### Architecture Decision

Keep the existing arrays, undo commands, managed lists and input adapters. Replace
the spawn Edit modal with one inline boolean column. Render that declared column
as checkboxes; all other columns remain escaped text. Use one encounter color for
both footprint and prism bodies; white numbers remain legible. Route Ctrl+wheel
only when an encounter point is selected. Preserve its ordinal across same-sized
geometry rebuilds; point insertion/deletion clears point selection to the footprint.

### Component Design / Implementation Map

- `scene_debug.cpp`: share the existing footprint RGBA with the marker bodies.
- `editor_input.*`, `area_object_editor.*`, `client_application_pointer.cpp`:
  carry the selected subindex, add a radians delta to that point, submit the existing
  array edit. Missing/stale indices reject without transforming the encounter.
- `session.cpp`, `client_application_workspace.cpp`: preserve point selection on
  rotation refresh, without overriding explicit footprint selection.
- `rml_managed_list.*`: one optional declared checkbox column (0..3, otherwise none);
  cells must be `0` or `1`, otherwise remain text. No new list storage or script ABI.
- Workbench, backend, script and CSS: remove modal/button/plumbing, capture row key,
  index and old/new boolean, validate and apply a Single Spawn transaction.
- Existing input, workbench, persistence, selection and renderer tests; README.

### Data Flow

Stable model/outline color -> existing scene-owned vertex arrays. Selected point
index + finite wheel amount -> before/after point arrays -> validation/undo -> live
rebuild. Live creature rows -> at most 1,024 managed rows -> visible checkbox cells
-> owned click values -> current-key/index/boolean validation -> array transaction.
Indices are zero-based internally; saved numbering stays one-based. No creature
reference, CR or appearance edit is exposed by the inline boolean control.

### Build Sequence

1. Apply shared color, selected-point wheel routing and selection preservation.
2. Replace the modal with inline checkbox rendering and the existing command flow.
3. Verify routing, repeat rotation, deletion, stale clicks, undo/redo and save/reload;
   build client/tests/mudl and inspect the headless marker capture.

### Critical Details

Desktop CPU/Vulkan and one client UI/kernel thread; rendering uses indexed arrays,
while cold SDK DOM traversal retains scoped borrows. Point positions and radians
are finite, positions remain in area bounds, and arrays are capped at 1,024. The
observed point distribution above is dominated by one point; the real Encounter
fixture supplies creature rows, including duplicates. ASSUMPTION: checkbox clicks
and wheel steps are infrequent user gestures — affects retaining ordinary per-edit
undo records. No measured performance requirement: edits retain O(n) array copies
and undo memory, rendering adds O(visible rows) checkbox markup. Refresh already
rebuilds the live area; selection preservation scans its old/new debug-range arrays
linearly and adds no additional rebuild. No timing claim. Snapshot allocation
failures propagate on the UI thread before submission, as with existing gestures.

Simplification: remove the modal, prompt propagation and replacement-creature load;
reuse existing boolean click and array edit machinery. No future encounter schema,
point IDs or generic cell-editor framework. Done means matching marker/line RGBA,
inline Single Spawn with read-only CR, Ctrl+wheel changing only the selected point
through repeat edits, and Delete removing only that point with undo. Wrong-owner
edits, lost point selection on rotation, or field/order drift fail these criteria.

### Verification and final self-check

- Release client, mudl and test executable build passed. All 23 focused tests
  passed (none skipped): input routing, native workbench wheel commands, managed
  lists, inline Single Spawn, stale clicks, collection removal, encounter array
  edits, marker picking, and headless rendering/rebuild selection.
- Inline checkbox testing covers mixed checked/unchecked rows, actual RML layout,
  clicking a different row from the selection, duplicate/stale clicks, invalid
  indices/booleans, object switching, one undo per change, and blueprint/CAF
  save/reload. CR, appearance, references and order are compared as full records.
  Missing creature resources do not prevent changing the boolean.
- Rotation testing covers repeated 15-degree steps on only the selected point,
  unchanged encounter orientation, invalid indices and overflow rejection,
  undo/redo, deletion/replay, and serialized point orientation. Headless picking
  verifies selection survives two orientation rebuilds and returns to the
  footprint after a point count change. Existing Delete-key routing was inspected;
  the deletion transaction and replay were exercised without a desktop key event.
- Every native prism body vertex matches the footprint RGBA. Inspected the renewed
  `build/tests/tmp/encounter-native-markers.png`: magenta prisms and white numbers.
- Simplification removed modal fields, prompt forwarding and unused includes;
  no new saved schema, point identity table, script ABI or speculative editor was
  introduced. Existing full-array transactions remain the batch path; individual
  wheel/click commands are the established singleton input transactions.
- Finite/range/stale checks and cold allocation behavior are explicit. Indexed
  geometry stays contiguous; DOM/engine borrows are scoped to synchronous UI calls.
  No measured performance claim. Formatting and `git diff --check` pass.
- No interactive desktop or Windows verification was performed.

## Preserve live particles during point edits (Tier 1)

### Patterns & Conventions Found

- `client_application_workspace.cpp:147` routes every debug-geometry mutation to
  `rebuild_live_viewer_area`. Spawn-point rotation/movement/add/delete and Sound
  radius edits publish this mutation kind (`object_edits.cpp:3965,4092`).
- `session.cpp:733` builds a new scene and calls `bootstrap_scene_playback`, which
  recreates/primes particles. Ordinary object rotation already uses the in-place
  spatial batch (`preview_scene.cpp:1416`). The reset is a lifetime bug.
- `scene_debug.cpp:996,1035` already builds encounter/sound overlays. Geometry is
  seven scene-owned vectors, with contiguous owner vertex/dot spans and indexed
  draw/picking ranges. `preview_scene.cpp:4940` already appends/remaps these vectors.
- Existing live visual refresh supports model replacement, not editor overlays;
  debug draw/picking consumes the vectors directly, so model/area records need no
  rebuild. C++ naming, span batches, result diagnostics and formatting follow these
  functions and the existing headless viewer tests.

### Architecture Decision

Stage overlays only for the changed object batch; compact retained overlay rows
and remap their indices, append the staged replacements, then swap only those
seven vectors into the live scene. Preserve active point selection by owner/index
when its point count is unchanged. Count changes return selection to the footprint.
No scene/model/light/particle replacement. Invalid or failed refresh leaves the
current overlay intact and reports failure; no full-area-rebuild fallback.

### Component Design

- Overlay refresh: `scene_debug.*`. Borrow current scene and live handles for the
  call; staging owns arrays until atomic publication. Accept only existing area
  encounters and sounds (the actual debug-mutation producers), reject duplicate,
  stale/wrong-area/unsupported inputs and malformed spans/indices. Reuse existing
  appenders and sound visual policy. Retain all unaffected overlay data.
- Selection adapter: `session.*`. Add a plural debug refresh path; the existing
  one-object visual refresh wrapper dispatches to it for encounters/sounds. Restore
  current debug selection after array remapping without touching camera/playback.
- Client mutation routing: call existing visual-refresh adapter instead of area
  rebuild for debug changes. Save/undo commands and schema remain unchanged.

### Implementation Map

- Move existing `append_debug_geometry` to `scene_debug.*` for reuse by both paths.
- Add staged batch refresh and scoped diagnostics there; extend `session.*`.
- Update `client_application_workspace.cpp` and the existing renderer regression;
  add focused CPU overlay replacement/validation coverage and this issue note.

### Data Flow

Live finite spawn positions/radians or sound visual radius + edited handles ->
replacement overlay arrays -> retained row compaction/index remap -> append ->
swap overlay vectors -> restore selection. Most requests contain one edited
encounter with one point (observed counts above); point arrays are capped at 1,024
by editor validation. Geometry is stable between gestures; particle/animation
state changes every tick and must be untouched by refresh. ASSUMPTION: the reported
rotation is a selected spawn marker — affects the identified input route; ordinary
object spatial rotation will also be checked for scene retention.

### Build Sequence

1. Implement staged overlay replacement and route point/Sound edits through it.
2. Verify rotation, movement, insertion/deletion, undo/redo and picking while real
   emitter/particle state remains unchanged; reject malformed batches atomically.
3. Build client/tests/mudl, run focused checks, simplify and self-check.

### Critical Details

Desktop CPU/Vulkan, existing single UI/kernel thread. Work is O(overlay vertices +
indices + ranges + dots + replacement geometry), with temporary remap arrays and
replacement buffers. Batch membership checks cost O(edited objects * owner ranges);
the common one-object gesture follows the same path. No runtime timing claim or
performance target; this is simulation continuity. No GPU resource destruction is
needed for CPU overlay replacement. Linear indexed arrays remain the render data;
scoped cold model/engine borrows are necessary for existing resource APIs.

Simplification removes the whole-area build and playback bootstrap from overlay
edits. No per-point render instances, incremental allocator, cache, saved IDs or
new mutation protocol. Done requires unchanged scene/model/particle identities,
unchanged particle ages/emitter times during edits, advancing simulation afterward,
correct updated picking/numbering and point selection, and exact undo/redo. A reset,
wrong point hit, stale selection, or loss of unrelated overlay data fails it.

### Verification and final self-check

- Release client, mudl and test executable build passed without new warnings.
  All 31 focused tests passed, none skipped: complete area selection coverage,
  overlay batch replacement, headless particle continuity, placement/transient
  visuals, weather refresh, spawn-point commands and ordinary wheel commands.
- The headless area test adds native `plc_cndl02` (placeables row 384), advances
  its emitters for one second, and requires live particles. Across repeated point
  rotation, movement, undo/redo, deletion and insertion it checks the same scene,
  model handles, area record cache and particle storage, with identical ages,
  positions, animation times, emitter times, random seeds and spawn accumulators.
  The next tick advances emitters and the scene renders successfully. Ordinary
  candle rotation through the spatial batch also preserves those particle values.
- CPU tests verify removal/insertion, a mixed Encounter/Sound refresh batch,
  retained unrelated vertices/dots/picking, valid remapped indices, and atomic
  rejection of duplicates, stale/unsupported/wrong-area handles, malformed draw
  ranges and non-finite internal data. The public component setter already rejects
  NaN; the corruption test now verifies that boundary before injecting an invalid
  internal row to exercise refresh rejection.
- Simplification/self-check: removed the full-area build/playback bootstrap and
  its point-selection preservation workaround from this edit path. Reused marker
  builders, the existing append/remap helper, current result type and client visual
  adapter. One local compaction loop serves three contiguous arrays; remaining
  passes remap their explicit cross-indices. No new persistent cache, IDs, schema,
  GPU lifetime operations or fallback rebuild. Allocation/capacity failures leave
  live overlays untouched. Batch processing and synchronous borrowed inputs remain
  explicit. No timing improvement is claimed; simulation continuity is verified.
- `clang-format --dry-run --Werror` and `git diff --check` passed. No interactive
  desktop or Windows verification was performed.

## Remaining work

- Add explicit spawn reordering with defined insertion and no-op behavior,
  preserving all fields and duplicate rows.
- Retain the shared managed-list selection feedback, stale-context rejection,
  and ordinary preview/document refresh.

The current commands already pay O(n) copy/validation and retain before/after
arrays for undo. Measure any additional cost introduced by the chosen editor;
no timing or speedup is claimed here. This issue does not need a new collection
framework, serializer, preview ownership model, or unbounded row materialization.

## Completion criteria

- Record field contracts and explicit rejection of invalid values, stale
  selections, unsupported objects, and oversized arrays before mutation.
- Verify field changes and first/middle/last row moves against real Encounter
  fixtures, including duplicate entries, empty/single-row arrays, no-op moves,
  and stale selections after mutations or object changes.
- Preserve every unrelated field and exact array order through undo/redo;
  successful changes publish one transaction and dirty-state update.
- Verify UI dispatch, selected-row feedback, and bounded row materialization;
  keep the bounded preview consistent with the edited source array.
- Save/reload standalone blueprints and containing CAF Area documents and
  verify exact persisted fields and order. Record desktop verification limits.
