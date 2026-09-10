# Blueprint authoring and reference updates

## All authored object types and named creation (2026-09-09)

Tier 2. The fixed input catalog is the nine NWN object blueprint formats:
Creature/UTC, Door/UTD, Encounter/UTE, Item/UTI, Placeable/UTP, Sound/UTS,
Store/UTM, Trigger/UTT, and Waypoint/UTW. Each creation row contains a typed
resource, display name, and the existing type-specific selections. The output is
one native blueprint per row whose name is visible in the project tree and which
can be loaded for Save as New, Update Blueprint, and Update References.

- Real platform and data: the desktop client owns live objects and Smalls
  propsets on the kernel thread. The project already creates directories and
  reads labels for all nine formats, and CAF files already store nine separate
  member arrays. Creature names live in the profile-owned descriptor propset;
  the other eight label readers use `ObjectBase::name`. Creature creation needs
  race/class, Item needs a base-item row, Door uses zero appearance/generic type,
  and the remaining default-zero propsets need no extra selection. Unknown
  resource kinds, blank names, invalid catalog selections, failed allocation,
  and failed profile initialization reject the complete batch.
- Cost: creation and reference updates are cold desktop authoring operations.
  File reads/writes, profile serialization, and object instantiation dominate.
  The type catalog is nine contiguous constant rows; mapping scans at most nine
  rows. No performance result is claimed because this path has no latency
  requirement or dedicated benchmark.
- Batch transform contract: `BlueprintCreationRequest[]` is borrowed for one
  call; `InitializedBlueprints.roots[]` owns equally ordered detached native
  roots until publication or failure. C++ allocates and supplies plain first/last names and
  catalog indices; the profile initializes its propsets in one array call. Live
  replacement consumes ordered owner-slot rows, prepares detached roots, copies
  UUID and spatial placement, revalidates ownership, swaps every applicable CAF
  array, then destroys old roots. Closed CAF replacement preserves the stored
  UUID, location, and scale in complete JSON documents owned by the worker.
- Architecture decision and data flow: one `BlueprintTypeDefinition[9]` table
  drives chooser order, labels, resource/object mappings, default folders, and
  CAF member-array names.
  The chooser carries an explicit action-list bit; confirmation dialogs keep
  their shared horizontal button row. Concrete allocation, typed load, and CAF
  vector replacement remain switches because those operations require distinct
  C++ types. Creation flows from prompt fields to one batch request, profile
  defaults, native identity/name assignment, ordinary blueprint serialization,
  resource refresh, and project-tree adoption.
- Simplification pass: the small lookup-table option removes repeated UI and
  mapping allowlists. No cache is added because nine comparisons occur only on
  user commands. No new type hierarchy, factory ownership layer, alternate
  serializer, or parallel creation path is added. The existing batch API already
  covers multiple creations, while one interactive creation remains count one.
- Build sequence and done criteria: add the catalog and vertical prompt metadata;
  extend allocation/load/snapshot/live-swap cases; pass names through the profile
  boundary; then verify all nine types in one batch through create, publish,
  project-label lookup, reload/snapshot, and live replacement. Verify the six
  newly enabled types through closed CAF replacement; existing coverage retains
  the prior Item path and Creature navigation has separate admission fixtures.
  Build the client and test targets, run the focused blueprint/Rml/
  resource suite, run `git diff --check`, and confirm no `std::map` was added.
  Any type that fails round-trip, loses placement/name, or cannot be replaced
  disproves completion. There are no unresolved design questions in this slice.

## Broad simplification audit (2026-09-09)

Tier 1. The review covers the implemented New Blueprint, Save as New
Blueprint, Update Blueprint, and Update References paths. The required output
is the same NWN-compatible file, live-area, undo, and UI behavior with fewer
duplicate representations and fewer invalid in-process states.

- Real platform and data: the desktop client owns native live area objects on
  the kernel/UI thread, authored project files are native JSON resources, and
  whole-module updates can scan hundreds of area and container documents. A
  reference update reads one frozen source blueprint, a batch of live owner
  slots and/or closed documents, and writes replacement roots while preserving
  each instance's UUID, placement, and container position. New blueprint
  creation normally has one row, but uses the same batch creation path as tests
  and future multi-selection input. Resource identity, directory, race, class,
  and base item are volatile user inputs; the loaded profile tables and object
  layouts are stable for the operation. Invalid resource types, IDs, paths,
  ownership slots, changed source bytes, or malformed documents reject the
  affected operation explicitly.
- Common path and cost: New/Save/Update prepare a small contiguous batch on the
  kernel thread and publish files atomically. Update References scans closed
  documents linearly in a worker process while the UI polls a small progress
  record; an open area is prepared incrementally and published natively on the
  kernel thread. File parsing, object instantiation, and serialization dominate
  the cold user-triggered operation. No performance result is claimed because
  this path has not been benchmarked; the exact follow-up measurement is worker
  wall time, process startup time, peak memory, and UI frame latency for worker
  counts 1, 2, and 4 on a representative module.
- Transform and ownership contract: C++ owns a contiguous batch of
  `{handle,name,last-name,race,class,base-item}` initialization rows and transposes it only at
  the existing Smalls array-call boundary. One resource-type mapping selects
  the native document kind for both discovery and replacement. Typed enums hold
  transient scope and worker phase; strings remain only in the versioned JSON
  request/progress protocol. The request file is the sole source of worker
  project/profile paths. Live replacements remain a separate native transform:
  detached roots are prepared, owner slots are revalidated, roots are swapped,
  and old roots are destroyed. Closed documents remain a JSON transform in the
  isolated worker. The operation directory owns request, manifest, before, and
  after bytes until commit or recovery finishes.
- Simplification pass: removed four parallel C++ initialization arrays, duplicate
  resource-type switches, duplicate blueprint command-registration wrappers,
  arbitrary in-process scope/phase strings, redundant worker environment
  configuration, and redundant Smalls subtype-to-object casts. Caching rule
  choices was rejected because form opening is cold and cache invalidation would
  add state. Parallel workers were rejected pending measurement. Merging the
  six blueprint files was rejected because it would mix file publication,
  native ownership replacement, durable recovery, process lifetime, and UI
  command transforms without removing work. The worker process, durable
  before/after data, separate live/native path, and atomic resource refresh stay
  because each protects an observed ownership or failure boundary.
- Done and disproof: the client and test targets must build, focused blueprint,
  worker, Rml, shared-combobox, and resource tests must pass, formatting and
  `git diff --check` must pass, and no `std::map` may enter these paths. A
  reproducible case where a retained boundary can be removed without losing
  live object ownership, UI responsiveness, or recovery would disprove this
  partition. Desktop pointer/keyboard use and worker-count performance remain
  outside automated verification.

ASSUMPTION: a representative whole-module workload is still the observed
604-area project; this affects only whether worker parallelism later earns its
startup, memory, scheduling, and recovery complexity.

Verified: `rollnw-client` and `rollnw_test` build cleanly. All 38 focused
blueprint, worker, Rml template, shared combobox, and resource tests pass. The
test run compiles and loads the changed NWN1 Smalls modules, including every
direct subtype comparison. `clang-format --dry-run --Werror`, `git diff
--check`, the no-`std::map` scan, and the native-select architecture scan pass.
Desktop interaction and worker-count performance remain unverified.

Final self-check: the common one-row write and single-worker update paths are
straight-line and batch-shaped. Boundary errors reject explicitly, live and
closed data are partitioned before replacement, no pointer-heavy hot loop or
speculative option was added, and the surviving states correspond to distinct
owners or durable phases. The no-cleverness review found no critical or high
severity construct; the remaining medium-complexity state machine and JSON
protocol are isolated at the UI/process and process/disk boundaries.

## Shared command-form comboboxes (2026-09-09)

Tier 1. Command prompts carry a contiguous choice batch of string values and
labels plus one selected value. The current forms use this for Race, Base Class,
Base Item, and the one-or-two-row reference scope selector. The required output
is the app's existing `VirtualComboBox` field and bounded popup for every choice
field; native RmlUi select controls are forbidden in client UI markup.

- Platform/data/cost: the desktop client has one RmlUi command overlay and at
  most one open command-form selector. Opening copies the active field's labels
  once into `VirtualComboBox`, using contiguous choice indices as widget keys;
  popup materialization remains bounded by the shared virtual-list window. No
  wall-clock performance claim is made.
- Transform/errors: retain the prompt's string values as authoritative command
  arguments, map them to temporary contiguous indices while open, and commit the
  selected index back to its exact string. Empty batches, empty or duplicate
  values, missing selected values, and indices outside the batch reject opening
  or selection without changing the prompt.
- Simplification/done: remove the native-select renderer and its private RCSS,
  reuse shared field/option/placement/keyboard behavior, and keep only one popup
  instance. A source-architecture test scans client UI producers and fails if a
  native `<select` tag is added again.

Verified: `rollnw-client` and `rollnw_test` build. All 33 selected blueprint,
client-template, command-form, architecture-guard, and shared `VirtualComboBox`
tests pass. Desktop pointer and keyboard interaction remain unverified outside
the test environment.

Final self-check: the common path copies one batch on open and processes stable
indices; prompt ownership and command arguments remain unchanged. The native
control state and its CSS were removed, invalid boundaries are explicit, and no
second dropdown implementation was introduced.

## Remove persistent blueprint toolbar (2026-09-09)

Tier 1. The supplied desktop capture shows four blueprint command buttons taking
a permanent row above the workspace. The required output is the existing tab bar
followed directly by workspace content; the registered commands and their modal
flows remain available through the command system.

- Data and cost: the input is one static Rml toolbar node with four buttons, one
  main-document click listener used only by those buttons, and toolbar-only CSS.
  Removing them eliminates one persistent layout row and one event route; it adds
  no state or runtime transform.
- Transform and errors: delete the node and its selectors, and stop attaching the
  blueprint listener to the main document. Keep the same listener on the command
  overlay for modal and Update References actions. Command guards and errors are
  unchanged.
- Simplification/done: remove the unused UI path rather than hide it or add a
  preference. The source contains no toolbar node or toolbar CSS, the client and
  tests build, and the blueprint/Rml template suites pass. Desktop visual review
  remains unverified.

Verified: the toolbar node, toolbar selectors, and main-document blueprint click
listener are absent. `rollnw-client` builds cleanly, and all 11 Rml template tests
pass as part of the 32-test focused run described below. The command-overlay
listener remains active for modal and Update References actions.

Final self-check: this deletes one static UI row, its styling, and its event route.
It introduces no state or alternative path; command registration, validation,
modal rendering, and error handling are untouched.

## Consistent object-preview facing (2026-09-09)

Tier 1. The observed input is an object preview that sometimes faces left,
including immediately after New Blueprint. File-loaded preview scenes start with
identity placement, while retained live documents and visual rebuilds use the
same builders as area objects and therefore import spatial heading. The required
output is one neutral standalone-preview pose regardless of which load path
produced the live document. Area placement must remain unchanged.

- Data and cost: a standalone scene is a cold batch of render-model instances
  owned by one preview document. Normalizing their root placement once during
  scene construction is linear in that small model batch and retains no data.
  Area scenes continue to transform their own instance batches from spatial rows.
- Transform and errors: after the live object scene is built, apply the same
  identity root placement used by file previews. Do not write the object's
  spatial component or serialized blueprint. Invalid objects and failed model
  loads keep their existing explicit failure paths.
- Simplification/done: normalize at the existing standalone scene boundary,
  rather than special-casing New Blueprint or changing camera state. A regression
  test loads one real Creature as a retained live document, seeds a non-neutral
  spatial transform, and requires neutral standalone-preview placement.

Verified: the renderer and client build cleanly. The 32-test blueprint, Smalls,
Rml, job, and preview run completed with 31 passes and one renderer integration
test skipped because the local environment has no Vulkan physical device. The
skipped test is compiled and covers the retained-live spatial input. Desktop/GPU
facing remains unverified here.

Final self-check: the common standalone-preview path writes identity placement
once across the scene's existing contiguous model batch. Area construction still
uses spatial rows, and no object component, blueprint byte, camera state, or new
branch is introduced. The fix covers creation, retained documents, and subsequent
live visual rebuilds through their shared scene builder.

## Blueprint modal block flow and actions (2026-09-09)

Tier 1. The supplied 2560x1280 desktop capture is the real input: at a 650px
modal width the title and description share a line, the generated filename and
collision diagnostic share a line, and the Browse/Create/Cancel controls do not
read as buttons. The required output is one vertical form: header, description,
field rows, filename, diagnostic, then actions, with visible enabled, disabled,
focus and primary-action states.

- Data: the modal contains two to four field rows; Creature explicitly contains
  Race and Base Class after Directory. Feedback strings are volatile
  and may wrap; the common collision diagnostic contains both a typed resource
  name and an absolute project path. Actions contain one primary command and one
  cancel command; Browse is a field-row action. Out-of-range width is handled by
  the existing 90% maximum width and vertical overflow.
- Platform/cost: RmlUi performs this cold layout on the desktop UI thread. One
  column flex container and explicit block-width feedback add no persistent data
  and do not alter command processing or validation.
- Transform: emit semantic header/message/feedback/action classes from the one
  shared command-form renderer. Stack direct form children in a column, give
  feedback full-width boxes, and mark the first action primary. Disabled remains
  visibly disabled; it does not use the enabled primary color.
- Simplification: change the shared renderer and stylesheet once. Do not insert
  manual line breaks into data, duplicate a blueprint-only modal, or add layout
  state to C++.
- Done: the exact four-row Creature/collision fixture proves each section begins
  below the preceding section at both 900px and 460px widths. Controls retain
  bounds and hit targets; enabled primary, disabled primary and secondary states
  are visually distinct, while focus uses the established app rule. Build,
  focused Rml tests, clang-format and `git diff --check` must pass. Desktop visual
  review remains a user-run check.

Verified: `rollnw-client` and `rollnw_test` build cleanly. The two focused modal
and command-flow tests passed, followed by 30 Rml template, blueprint command,
blueprint data and blueprint job tests in 15.828 seconds. The modal test uses the
four-row Creature collision form shown in the supplied capture at 900px and
460px widths. It checks vertical section order, bounded fields, visible button
backgrounds and borders, distinct primary/secondary/disabled colors, focus
ownership, hit targets, and the Update References progress layout. The command
test checks that Race and Base Class are both present and populated. The same Rml
renderer now handles field forms and action-only blueprint prompts; non-blueprint
confirmation behavior is unchanged. `clang-format --dry-run --Werror` and `git
diff --check` passed. The earlier unrelated RCSS border parse warning is also
removed. Desktop/GPU visual review was not run; test elapsed time is not a
feature performance measurement.

Final self-check: the common path emits one flat sequence of title, message,
field rows, feedback rows and actions through the existing shared command-form
renderer. Invalid input continues to disable the primary action and display the
diagnostic in the existing error color. The stylesheet reuses the app's existing
panel, input, button, hover/focus, disabled, text and error palette. No new modal
state, renderer context, container, abstraction or per-frame data transform was
added. The simplification pass removed separate blueprint-only layout handling
and manual line breaks; all blueprint prompts and the existing operation overlay
reuse the same block and action styles.

## Smalls-owned New Creature initialization (2026-09-09)

Tier 1 correction. C++ must not name, inspect, or write propsets, and it must
not own NWN initialization policy. The actual creation input is a detached
batch of Creature/Placeable/Item handles plus ordinary numeric selections;
New Creature now also supplies race and class. The required output is a valid
NWN1 blueprint batch whose profile data is initialized by Smalls before the
existing instantiate/save path. Failure rejects and destroys the whole
detached batch.

- Real data: `racialtypes.2da` supplies `Appearance` and
  `ToolsetDefaultClass`; `classes.2da` supplies the six toolset ability scores.
  The current Smalls data specs omit those columns. Fresh Creature propsets
  currently contain six zero abilities and an empty skill array; the latter
  causes `ac_modifier_tumble_dodge` to index outside the array. Race, class,
  appearance, skills and body parts are stable profile data; resref/directory
  remain volatile form inputs. Race/class catalog sizes and actual skill count
  come from the loaded module rather than constants.
- Platform and cost: the desktop UI and Smalls runtime execute on the main
  thread. Creation is normally one row; the existing batch API remains the
  path for larger batches. Initialization performs linear writes over six
  abilities, the loaded skill count, twenty Creature body parts, and at most
  nineteen Item model parts. This cold, user-triggered work adds no area-load
  hook and retains no extra memory after the call.
- Transform/contract: C++ allocates and owns detached handles, passes parallel
  arrays of handles/race IDs/class IDs/base-item IDs to the selected profile,
  and treats false, unequal lengths, invalid IDs, or script failure as a batch
  rejection. NWN1 Smalls validates selections, reads rule rows, initializes
  propsets and native appearance components, then the existing C++ path assigns
  resource identity, instantiates, serializes and publishes. Non-applicable IDs
  are `-1`; Creature requires race/class and Item requires base item.
- UI: New Creature adds Race and Class choices populated from Smalls rule
  catalogs. Human (6) and Fighter (4) are initial selections when present.
  The chosen race provides the default appearance; the chosen class provides
  its toolset ability scores and level-one class row. Required Creature body
  parts start at model 1; the optional belt, both shoulders, and robe override
  start at model 0. Skill ranks start at zero. Other valid zero fields remain
  unchanged until the user authors them.
- Simplification: reuse the existing profile module and existing Creature,
  Placeable, Item setters/native visual functions. Do not add a callback to
  every object allocation, do not infer a new creature from zero-valued fields,
  and do not add C++ propset reflection helpers. The single creation hook is
  batch-shaped because `initialize_blueprints` already accepts batches.
- Done: no direct propset field names or writes remain in blueprint C++; race/class appear in
  the creation form and survive save/reload; abilities match the selected class
  rule row; skill length matches `Skills.count()` with zero ranks; every body
  part matches the NWN appearance default; the real creature-sheet path succeeds;
  invalid selections release the detached batch. Build and focused profile/client
  tests are required.

ASSUMPTION: the NWN class row's `Str/Dex/Con/Int/Wis/Cha` columns are the
toolset defaults for a new level-one creature. This is directly reflected in
the supplied `classes.2da`; a package-driven feat/skill allocation system is
outside the current data and is not added here.

Verified: the client/test build completed without compiler warnings. A focused
78-test profile/client/blueprint-job suite passed in 32.298 seconds, covering
race/class selection, the Human/Fighter initial selection, all twenty body-part
values, skill-array sizing, save/reload, the real creature sheet, a legacy empty
skill array, invalid-selection cleanup, and the Rml form/command path. `git
diff --check` passed. No direct propset field type, name, or write remains in
blueprint C++,
and no `std::map` or `std::set` was added. Desktop interaction remains
unverified; the test elapsed time is not a feature performance measurement.
A final rebuild and 40-test creation/profile/UI subset passed after the
simplification edit that reads the loaded skill count once per Creature.
The latest creation test also verifies required parts at model 1 and belt, both
shoulders, and the robe override at model 0 after save and reload.

Final self-check: the common path is one batch call from C++ into the selected
profile before identity and publication. The boundary contains four flat,
equal-length columns whose ownership ends with the call; invalid values reject
the whole batch. Profile rule data is read once per created row, and no work was
added to ordinary object or area loading. The design adds no client schema
reflection, default lookup table, or per-object callback. The only unresolved
authoring behavior is package-driven feat and skill allocation, for which the
current checkout supplies no creation transform.

## Modal rendering and new-creature race correction (2026-09-09)

Tier 1. The supplied screenshot shows a native preview over the New Blueprint
dialog and borderless form controls. `main.cpp` renders the main Rml context
before the native viewport; only the FPS and command contexts render afterward.
Panel control styling is scoped, so the new controls had only font/width rules.
The user also observed Human appearance with Dwarf race: CreatureStats.race is
zero-initialized (Dwarf=0), while creation selects Human appearance=6.

- Transform: move the two blueprint modal hosts into a separate Rml document in
  the existing command-overlay context; keep native rendering in its established
  order. Route form focus, input, action values and directory-picker results to
  that document. Use the existing panel palette for bounded, visible controls.
- Data/cost: one modal document, usually two fields (Items have a third selector),
  one progress/review panel, and no additional context or render pass. Existing
  overlay size/DPI updates apply. Typical 650px forms must fit smaller windows;
  long directory/choice text stays inside its control. Invalid names continue to
  reject submission. No viewport pause or performance claim is needed.
- New Creature: this earlier fixed Human correction is superseded by the
  Smalls-owned initialization section above. The form now passes explicit race
  and class IDs to the selected profile; loaded and cloned creatures retain their
  authored state.
- Simplification: reuse the command overlay and existing action listener, rather
  than creating another render layer or changing the viewport. Reuse existing
  initialization and save/reload tests for race; add a headless modal layout/input
  check for the screenshot regression.
- Done: blueprint dialogs render after the native viewport, fields/buttons have
  visible bounds and usable hit targets, input reaches the modal, the ordinary
  command palette still works, and new Creature race/appearance survive save and
  reload as Human. Build and focused tests are required; desktop appearance is
  unverified until exercised in the app.

Verified: client/test build passed without compiler warnings; 26 focused tests
passed in 11.143 seconds (`/tmp/rollnw-blueprint-ui-tests.xml`). The new headless
test loads the real modal document and stylesheet, checks title/description
separation, control backgrounds/borders and bounds at 900px and 460px widths,
text entry, cancel/backdrop hit targets, progress geometry and hidden state.
Existing template and blueprint command tests passed; the new-creature save/reload
test checks both Human appearance and race. Render ordering uses the existing
command-context render call after the viewport; no desktop/GPU visual check was
performed. The test output retains a pre-existing RCSS parse warning for the
unrelated quantity-popup border at panel.rcss:2595.

Final self-check: scope/data/cost and error behavior are stated above; the only
new UI owner is a document in the existing command context, borrowed by AppState.
No extra context or renderer pass was added. The two
new modal hosts were removed from the earlier UI pass; the existing listener and
input routes serve the modal actions. Initialization uses the
profile batch hook described above. `git diff --check` passed. Desktop visual review
remains unverified, and no performance claim is made.

## Live-area replacement correction (2026-09-09)

USER DIRECTION: instantiate the replacement, replace the live instance in its
owner, and destroy the old instance. JSON replacement applies to unopened areas.
This supersedes the live-area save/discard and whole-area reload flow below.

- Frame/data (Tier 2 continuation): input is the pinned Area's native membership,
  inventory and equipment arrays plus one saved typed blueprint. Output is the
  same Area with matching instance roots replaced; unrelated handles, authored
  edits, placement, UUID and container positions survive. The measured corpus
  includes nested inventory/equipment/store items, so those ownership locations
  are part of this change. Actual current-area match counts are discovered at use.
  Placeables dominate observed placed objects (6,661 of the three supported types;
  only 353 carry nonempty blueprint references). Item ownership includes 322
  inventory, 139 equipment and 2,224 merchant entries. The common placed match
  needs only an indexed area-member swap; item matches use explicit owner slots.
- Platform/cost: the desktop kernel belongs to the UI thread; resource files are
  native JSON; unloaded areas remain worker-owned file batches. Native discovery
  is O(area objects and contained items), loading costs one blueprint graph per
  replacement, and preparation retains only those replacement graphs. Prepare
  incrementally between UI frames; no shared-kernel threads or speedup claim.
  ASSUMPTION: one instance load is a useful progress increment — affects frame
  latency and must be checked in the desktop app with complex blueprints.
- Patterns/conventions: reuse ObjectManager loading, ObjectDocument RAII,
  Inventory grids and native equipment slots, existing equipment property/visual
  callbacks, object mutation epochs, and the existing worker protocol. Membership
  undo deliberately retains detached objects, so it cannot implement destruction.
  Native replacements use `ObjectManager::load<T>` so tag registration is retained;
  its missing-resource check now precedes allocation. Resource validation reads
  saved JSON/GFF, but no live instance is serialized to implement replacement.
- Transform/contract: native discovery -> frozen dependency validation -> detached
  instance preparation -> owner/slot/layout validation -> replace memberships ->
  destroy old roots -> mark area dirty and refresh selection/rendering. Flat rows
  contain owner handles, attachment tags and indices; handles are necessary at the
  existing ObjectManager boundary, never a new pointer-based traversal structure.
  Rows borrow the pinned area for the modal operation; ObjectDocument owns each
  replacement until attachment. Stale owners, unsupported slots, cycles, missing
  resources and invalid inventory dimensions reject before replacement.
- Implementation map: extend blueprint_references.* for native batches;
  object_document.* for ownership release; object_edits.* for mutation publication;
  blueprint_operations.* to exclude the live CAF; backend commands/progress UI
  to prepare/apply live batches and leave area files untouched. Focused tests cover
  native ownership and command integration, alongside existing worker recovery.
- Simplification: no live CAF serialization/patch/reload, no forced save of live
  edits, no clone optimization, and no worker process for Current Area. The live
  area is explicitly excluded from Whole Module's file discovery and recovery.
  The existing structural epoch refreshes a visible area without an additional
  whole-viewport reload; inactive area edits leave the current preview alone.
- Done: old matches destroyed, replacements contain saved values, area/unrelated
  handles and edits retained, placement/UUID/slots retained, area dirty, CAF bytes
  unchanged, cancellation releases detached copies, and progress remains usable.
  Existing handle-based area history must be cleared before old roots are freed;
  durable file restoration covers only files actually changed by the worker.
  Open question: a future live replacement undo can retain instance archives and
  instantiate them on replay; retaining old live roots contradicts this direction.

Verification: client/test build passed without compiler warnings; 35 focused tests
passed in 24.150 seconds (`/tmp/rollnw-blueprint-live-final.xml`). This is test-suite
elapsed time, not an update-performance measurement. New native tests verify all
three supported root types, registered replacement tags, old-root/subtree cleanup,
UUID/placement preservation, inventory/equipment/store slots and metadata,
cancel cleanup, invalid equipment and inventory positions, stale ownership and
missing-resource allocation cleanup. Command integration verifies a dirty live
area survives both scopes with its CAF bytes untouched while a second closed
area updates and restores independently. ObjectSystem, document, authoring and
worker recovery regressions passed. No desktop rendering/latency or Windows
checks were run; no measured performance improvement is claimed.

Final self-check: the native batch contract, ownership lifetimes, observed common
cases, error behavior and cost are stated above. Existing ObjectManager handles
and native container indices avoid a new ownership system. Simplification removed
live JSON patch/reload, forced live saves, a Current Area worker and a redundant
viewport reload. No new profile/Smalls code or `std::map`/`std::set` was introduced.
`git diff --check` passed. Live undo remains the explicitly recorded open question.

Status: implementation in progress. Propagation uses full
blueprint replacement, as clarified by the user during planning.
USER DECISION: retain NWN-compatible blueprint identity for this feature:
the current flat typed resource namespace, with directories organizing storage.
Update Instances requires a responsive progress bar. Independent document
preparation is a candidate for bounded parallel workers; measure before enabling
more than one worker by default.
Tier: 2, spanning project resources, live documents, and multi-file persistence.
Evidence collected from the local checkout and The Awakening project on 2026-09-09.

Previous verification (before the live-area correction): client/test build passed; 26 focused tests passed
in 14.153 seconds (`/tmp/rollnw-blueprint-final.xml`). This is functional test-suite
elapsed time, not a feature benchmark. Coverage includes all four selected Item
model families through save/reload, Creature/Placeable appearance defaults, clone
identity and cleanup, full Creature authored-state preservation, file undo/redo,
changed-file and folder-independent name conflicts, source dirty-state retention,
registry refresh, live document publication, and isolated-worker prepare/commit/
restore across multiple areas. `git diff --check` passed. No `std::map` or
`std::set` was added to the blueprint implementation; no new profile files remain.

Current simplification/self-check: source-based save uses one detached copy and
checks captured facts without making another. Save-only copies do not run runtime
activation; ordinary activation resets Creature current HP. Form data contains no
object JSON snapshot. New-object C++ assigns identity after the selected profile
initializes ruleset data through one flat batch call. The Item-only base-type
choice is supported by the actual read-only workbench boundary. Source copying
uses the existing complete serializer; JSON still serves persisted
project/recovery formats and reference-document traversal. History owns only
paths/bytes. Existing ObjectManager handles are borrowed only in cold command
work; batches own their temporary roots and release them on failure/completion.
Invalid inputs and replay conflicts reject explicitly. No placement cloning
optimization or multiworker scheduler was introduced.

Not verified here: desktop rendering/input latency, Windows filesystem behavior,
full-corpus throughput, and a measured load-versus-clone comparison for repeated
placements. Those release/optimization checks remain open below; the passing
headless tests are not evidence that those checks passed.

## Save as New simplification (2026-09-09)

USER DIRECTION (item follow-up): New Item also offers a base-item type selector.
Inspection confirmed the workbench currently displays Base Item Type read-only
(`toolset/ui.smalls` item details and `nwn1/item.smalls` general rows); no safe
conversion operation exists. The selector reads the existing C++ BaseItem table.
Initialization validates the selected ID and chooses native appearance defaults
by its model family (simple, layered, composite, armor). It adds no conversion
workflow or gameplay-stat initialization. This supersedes the fixed shortsword
and two-field Item form assumptions immediately below.

SUPERSEDED DIRECTION (new-object follow-up): the first implementation put
appearance defaults in C++. The correction at the top of this issue removes that
ruleset ownership from C++. The current form retains the required race/class or
base-item choices as numeric inputs to Smalls; C++ owns allocation, identity and
publication only.

Cloning reuse candidate: several placements of the same blueprint can reuse one
prepared source. Keep batch interfaces; measure three ordinary loads against one
load plus three copies before claiming or introducing a placement optimization.
Current archive-based copying still performs per-instance allocation and wiring.

SUPERSEDED USER DIRECTION: clone the selected object, change its ResRef/UUID, save
the new blueprint, destroy the temporary clone, and refresh the project sidebar.
The later decision makes UUID instance-only: temporary blueprint copies clear it,
and blueprint JSON omits it. Undo deletes the created file and refreshes the tree.
Save as New opens no editor tab.

- Frame/data: the input is any selected authored blueprint object and a validated
  destination; the output is one new file in the flat typed module namespace. The
  UI supplies a batch of one. Existing nested-item reference export rules apply.
- Platform/cost: the owning desktop thread uses the existing full-object archive
  copy path (`object_edits.cpp:897`) and `ObjectDocument` lifetime owner. Copying
  and saving traverse the source payload; temporary memory is the source archive,
  one detached object graph, and saved bytes. No latency target or measured speedup
  is claimed. Resource refresh rebuilds the module's existing directory snapshot.
- Transform: validate destinations -> copy source -> assign native ResRef and clear
  UUID -> serialize blueprint without instance identity -> exclusive save -> release copies -> refresh.
  Prepared batches own copies until saving; history owns only paths and bytes.
  Invalid names, collisions, stale sources, or lossy item exports reject before writing.
- Simplification: revalidate source/destination facts without constructing a second
  copy. The modal form needs a source handle and project generation, not its own
  serialized object snapshot. Undo/redo reuse the existing command history and
  retain the saved bytes, never a live clone or source-object mutation.
- Undo/redo: undo verifies expected bytes before deleting; redo exclusively restores
  the same bytes. Changed files or dirty destination tabs reject without
  changing history. Resource refresh follows either file change; the command UI
  refreshes the project tree. Existing document history stays tab-local.
- Done: verify a real object survives unchanged, the saved blueprint has no UUID,
  no extra live object/tab remains, the resource appears, undo removes it, redo restores
  identical bytes, and collisions/edited-file undo are rejected. An open destination
  tab rejects replay to avoid removing a document while it is being edited. Desktop rendering
  remains a separate manual check.

## Patterns & Conventions Found

### Frame and scope

Support four explicit authoring operations:

| Action | Input | Required output |
| --- | --- | --- |
| New Blueprint... | Object type, new resource reference, destination directory, display name, and required creation choices | Fresh native JSON blueprint initialized by the active profile, published into the project and opened in its editor; no selected source object required |
| Save as New Blueprint... | Selected live object, a new resource reference, and destination directory | New native JSON blueprint, discoverable immediately in the project and resource manager |
| Update Blueprint | Selected live object and its existing typed blueprint reference | Updated project blueprint, or an explicit project override of an inherited blueprint |
| Update Blueprint References... | Saved blueprint and Current Area or Whole Module scope | Reviewed, validated updates to every supported matching instance within the chosen scope |

This supports creating content from scratch, makes object edits reusable, and later
applies blueprint changes to existing content. Ordinary Save on an open blueprint
remains the operation for saving that document. Updating a blueprint does not
implicitly propagate it.

The implemented release covers all nine NWN object blueprint types for creation,
Save as New Blueprint, Update Blueprint, and propagation. Area
duplication, new terrain, runtime actors, binary project mutation, and language or
VM development are outside this plan. Existing profile APIs and serializers remain
dependencies; this work does not move their rules into the client.

The limit is a useful native-project workflow with explicit replacement semantics.
Do not build inheritance, per-field override tracking, automatic propagation,
file watching, or a general transaction framework. Plan B is to ship creation and
update while keeping propagation at read-only discovery until its field policy,
validation, and recovery criteria pass.

### Real platform and observed data

The platform is the Linux/Windows desktop client: one UI/event thread, one active
project and object service, one pinned Area document, retained blueprint documents,
local files, and one displayed renderer scene. Live object/profile operations must
run on their owning process/thread. Background preparation uses isolated kernel
services, never the editor's live object service. Resource lookup is a frozen
registry over containers; the module directory takes a snapshot when constructed. A filesystem write alone
does not publish a new resource.

Read-only census of `/home/josh/projects/the_awakening/shared`:

| Data | Observed volume |
| --- | ---: |
| CAF area documents | 604; 59,443,919 serialized bytes total |
| Largest CAF file | 4,291,754 bytes |
| Top-level objects per area | Median 15; maximum 206 |
| Creature / Placeable / Item blueprints | 815 / 112 / 1,460 |
| Top-level Creature / Placeable / Item instances | 77 / 6,661 / 7 |
| Other top-level placed objects | 1,791 doors; 3,512 encounters; 340 sounds; 198 stores; 550 triggers; 1,331 waypoints |
| Embedded items in area inventories / equipment / stores | 322 / 139 / 2,224 |

For the initial three types, top-level placed objects are predominantly Placeables;
an Item update must also find embedded items, not just the seven ground items.
Among instances whose blueprint exists in the project, 26 of 353 Placeables and
5 of 26 Creatures have tags different from their blueprint. There are 138
Placeables and seven Creatures with nonempty local variables. These are concrete
examples of instance customizations that replacement will overwrite; the review
must describe that outcome rather than silently preserving them.

Every enumerated blueprint filename agrees with its internal resource reference;
observed reference lengths are 4--16 bytes. No initial-type top-level instance has
an empty reference. Elsewhere, 466 Waypoints do. Only 353 of 6,661 Placeables have a
matching project-local blueprint; the remainder are not proven missing, because
the census did not inspect base/hak resources. Resolution must use the effective
typed resource identity and report its source.

Current native blueprint shape is `object`, `components`, and complete persistent
profile sections. Common component shapes are:

- Item: `item_properties`, `item_visuals`, `locals` (all 1,460 sampled project files).
- Creature: `equipment`, `locals` (577 files); other files also carry inventory or
  ability loadout.
- Area: separate typed object arrays plus tiles and area metadata. Placed objects
  carry `components.location`; non-unit authored scale is also instance data.

Blueprint inventory/equipment serialize item resource references. Instance
inventory/equipment serialize embedded Item documents. A blueprint save therefore
does not preserve arbitrary edits inside a referenced item. This must be disclosed
and validated, not mistaken for an exact clone of a live object graph.

For creation from scratch, `ObjectManager::make<T>()` allocates a fresh object and
initializes its registered propsets. Existing blank-object tests exercise Item,
Creature, and Placeable allocation/instantiation and empty inventory ownership.
They do not prove useful authored defaults, valid Creature starting statistics,
or complete Item visual/layout materialization. In particular, `Item::instantiate`
can log a materialization failure and still return inventory-instantiation success.
Creation must check the actual required initialization results, not only that bool.

New Blueprint has no source document or reference to copy. Its stable inputs are
the selected object kind and active profile; its variable inputs are the requested
resource name, display name, and type-specific choices from the current catalogs.
Observed creation frequency and preferred defaults are unknown. Do not derive
defaults from commonly used asset names or assume the first catalog row is valid.

Data changes at user-command/save frequency. Source blueprint contents, active
selection, file bytes, and document state can change between preview and commit;
resource type and document format are stable within a prepared operation. Copy
once per operation and revalidate at commit. Rendering is not an input to copying.

ASSUMPTION: native JSON projects are the writable scope — affects destination
resolution and propagation enumeration; binary resources remain import inputs.

USER DIRECTION: Save as New Blueprint leaves the source object's reference and dirty
state unchanged — affects whether a later Update Blueprint targets the original
resource. Newly placed instances use the newly created blueprint. Relinking the
source is not implicit in a file-creation action.

ASSUMPTION: New Blueprint's Create action persists a validated initial blueprint,
then opens a clean editor tab — affects document lifetime and cancellation. Later
edits use the existing dirty/Save protocol; no unsaved file-less document mode is
added. Cancel before Create writes no file.

USER DIRECTION: propagation replaces the instance from its blueprint and restores
its placement. Tags, local variables, inventory, equipment, and all other
blueprint-authored values are replaced. There is no customization-preservation or
field-merge option in this plan.

ASSUMPTION: rollnw's complete placed transform includes position, orientation,
and scale — affects the small placement record restored after replacement.

ASSUMPTION: this project's distribution is representative enough to choose the
initial traversal — affects measurement fixtures, not hard volume limits. It is
not a measurement of user action frequency or operation latency.

USER REQUIREMENT: Update Instances presents progress throughout discovery,
preparation, saving, and finalization; cancellation/recovery also remains visible.

USER REQUIREMENT: New Blueprint and Save as New Blueprint present a popup with
ResRef and destination directory. Update References asks Current Area or Whole
Module. The user chose NWN-compatible blueprint identity for now: uniqueness is
checked across the module's typed resource namespace, not within each directory.

ASSUMPTION: a modal bulk operation may suspend project authoring while leaving
the event/render loop responsive — affects conflict handling and UI scope. A live
progress bar does not imply concurrent editing of the documents being replaced.

### Reuse and style evidence

- `lib/nw/objects/ObjectManager.hpp:232` and
  `lib/nw/objects/ObjectManager.cpp:416`: fresh allocation plus profile storage
  initialization; reuse this path rather than deserializing a hidden template.
- `tests/kernel_objects.cpp:326`: blank-object inventory/lifetime coverage.
  `lib/nw/objects/Item.cpp:54` and
  `lib/nw/profiles/nwn1/item_materialization.cpp:36`: initialization and explicit
  native layout/visual result paths to validate for new Item blueprints.
- `tools/client/appearance_catalog.hpp:19`: existing typed, validated appearance
  rows; use these and the existing Item base-type catalog for creation choices.
- `tools/client/object_document.cpp:159`: typed blueprint serialization already
  selects `SerializationProfile::blueprint`.
- `lib/nw/serialization/component_propset_json.cpp:54`: common metadata includes
  UUID when present; the profile alone does not establish safe identity copying.
- `lib/nw/serialization/component_propset_json.cpp:161`: shared component export;
  persistent profile sections are serialized by their existing owner.
- `lib/nw/objects/ObjectComponentSystem.cpp:317`: blueprint serialization omits
  instance location and scale.
- `lib/nw/objects/Inventory.cpp:322` and `lib/nw/objects/Equips.cpp:114`: blueprint
  references and embedded instance payloads have different load contracts.
- `tools/client/resource_document.cpp:381`: same-directory temporary write and
  replacement, but the existing save helper requires the target to exist.
- `tools/client/object_document.cpp:74`: project-contained path validation;
  `tools/client/object_document.hpp:42`: independent per-document save results.
- `lib/nw/resources/assets.cpp:81`: use `Resource` identity, not display names;
  `lib/nw/resources/StaticDirectory.hpp:27`: directory enumeration is a snapshot;
  `lib/nw/resources/ResourceManager.hpp:108`: existing registry generation.
- `lib/nw/resources/StaticDirectory.cpp:62` and `tests/resources.cpp:418`:
  ordinary module directories register nested authored JSON under a bare ResRef.
  `lib/nw/resources/assets.cpp:116` and `tests/resources.cpp:403`: path-preserving
  resource keys already exist for package directories; they are a different
  lookup contract, not evidence of directory-local module blueprint lookup.
- `tools/client/project.cpp:1016` and `lib/nw/objects/ObjectManager.hpp:309`:
  project paths currently become bare typed keys; blueprint loading receives a
  ResRef without a referring directory. Folder-local duplicates cannot be resolved
  by adding a save popup alone.
- `tools/client/workspace.hpp:28` and `tools/client/docs/transactions.md`: live
  roots and undo belong to documents. Save All is not a multi-file transaction.
- `tools/client/object_edits.hpp:394`: existing mutation/structure publication;
  `tools/client/main.cpp:937`: consumers track those epochs.
- `tools/client/command_bus.hpp`: one command/result/prompt path shared by UI,
  palette, and terminal. Do not add direct widget writes.
- `tools/client/project_import.hpp:17` and `tools/client/project_import.cpp:21`:
  SDL child-process ownership and nonblocking polling already isolate work that
  uses kernel services. Reuse that lifecycle pattern, not the import command.
- `tools/client/main.cpp:6449` and `tools/client/ui/panel.rcss:958`: existing
  animated import bar supplies presentation styling, but has no determinate count.
- `lib/nw/kernel/Kernel.cpp:282` and `lib/nw/objects/ObjectManager.hpp:232`:
  object allocation/instantiation reaches process-wide mutable services. The
  inspected paths do not establish safe concurrent instance creation in threads.
- `lib/nw/nav/NavWorld.cpp:1119`: independent tile outputs already build with up
  to four threads before serial installation. Reuse this prepare/commit division;
  account for those inner threads when measuring multiple preparation processes.
- `tools/client/CMakeLists.txt:1` and `tests/CMakeLists.txt:181`: explicit source
  lists; the client core is testable without the graphics executable.
- `.clang-format:1`: WebKit-derived format. Match adjacent snake_case operations,
  PascalCase records, `std::span`, `[[nodiscard]]`, RAII ownership, and explicit
  status/diagnostic results. New tests use the existing GoogleTest conventions.

No added third-party dependency is needed. Use the existing JSON implementation,
filesystem utilities, resource identity, and serialization/validation facilities.

## Architecture Decision

### New Blueprint without a source object

Implementation audit: ordinary allocation initializes storage, not an authored
NWN object. C++ therefore allocates a detached batch and passes its handles with
numeric race/class/base-item selections to the selected profile's
`initialize_blueprints` hook. NWN1 Smalls owns all propset and rule-table access:
Creature starts at the selected race/class with its 2DA appearance and toolset
abilities, a level-one class row, zeroed skill ranks, model 1 for required body
parts, and model 0 for the optional belt, shoulders, and robe override;
Placeable and Item use their profile initializers. C++ resumes with identity,
instantiation, serialization and publication only. Cost is one profile call and
one detached graph plus serialized round trip per created blueprint.

Expose New Blueprint from the project resource browser and command palette.
A typed blueprint-category action may preselect its actual resource kind; the
general command asks for it. Selection in an area is not a prerequisite. Initial
kinds are the nine authored NWN object types in the fixed catalog.

Use a small creation dialog: type, ResRef, destination directory, and only the
type-specific choices required to obtain a valid editable blueprint. Item base
type and Creature race/class are concrete numeric inputs. Other starting values
belong to the active profile's authored-default policy. Do not copy zeroed storage
as a purported finished default or depend on a particular existing blueprint.

Add `initialize_blueprints(creation_rows)` to the planned `blueprint_edits` module.
Partition input by supported kind, validate the batch's destination identities and
catalog choices, allocate detached objects through `ObjectManager::make<T>()`,
and pass parallel handle/race/class/base-item columns through the generic optional
profile hook. The active profile validates and initializes the batch. Do not add
client access to propsets or duplicate its rules catalogs. The hook is optional
for profiles that do not implement blueprint creation; attempting creation under
such a profile fails with a concrete diagnostic.

New objects get the requested resource identity and display name through the
appropriate existing native operation.
Default a new object's tag to its requested resource reference; the editor can
change it later. Keep its UUID empty because this is a blueprint, not an instance. Begin
with empty inventory, equipment, local variables, and scripts unless the audited
profile initializer explicitly requires authored values. There is no inherited
instance placement or arbitrary selected object's state.

Validate required catalogs, native components, serialization, and reload into an
independent object before publication. A missing row, unsupported type, incomplete
profile default, allocation/materialization failure, or malformed payload rejects
Create with a concrete diagnostic. Release every detached object allocated by a
failed batch. Do not treat successful allocation or warning-only instantiation as
proof of successful authoring initialization.

Feed the resulting snapshot into the same exclusive-create publication used by
Save as New Blueprint. Do not add a second serializer, filesystem layout, name
validator, registry refresh, or save command. After successful file publication
and resource refresh, open the blueprint tab and transfer the validated detached
root to its `ObjectDocument`. This reuses document ownership and the focused editor.
If opening the tab or preview fails after saving, keep the created resource and
report that distinct outcome; do not report creation failure or delete the file.

Canceling the dialog releases temporary preparation data and writes nothing.
Validation/write failure keeps the entered choices available for correction;
resource-publication failure follows the existing saved-but-not-published retry
contract. New Blueprint never silently changes into Update on a name collision.

### One explicit copy policy

For propagation, construct a fresh detached instance from the saved blueprint and
its resolved dependency snapshots, restore the existing placement/attachment
record, validate it, and substitute its serialized instance payload at the same
document location. This replaces the whole authored object; it is not a field
patch or an override merge. The old instance stays intact until staging/commit.

Use the existing native object protocol and profile instantiation path. Do not
manually copy selected profile fields, infer editability from reflection, or
duplicate profile schemas in C++. Full replacement avoids per-field preservation
machinery and makes all blueprint-authored values authoritative.

Historical context: BioWare's [Module Construction Tutorial, page 24](https://neverwintervault.org/sites/neverwintervault.org/files/project/1463/files/auroratoolsettutorial.pdf#page=25)
presents deleting/repainting a Creature and using Update Instances as alternatives
after changing its blueprint. Page 17 documents module-wide updates. This supports
the replacement model; the tutorial does not establish the exact orientation or
other field-preservation implementation. The user-directed contract above is the
design authority here, not a claim of byte-for-byte NWToolset compatibility.

| Data | Instance to blueprint | Blueprint to matching instance |
| --- | --- | --- |
| Typed resource identity | New requested reference, or the exact update destination | Retain matching reference and type |
| Live handle, UUID, ownership, membership/order | Never serialize instance identity; blueprint JSON and temporary blueprint copies have no UUID | Fresh live object; keep destination top-level persistent UUID and parent membership position |
| Position, orientation, scale | Omit through blueprint profile | Preserve exactly |
| Tag, local variables, name, description, comments, palette, authored appearance/statistics/scripts | Export using existing owning serializers | Replace from blueprint |
| Inventory/equipment/store composition | Export validated resource references and slot/layout data | Instantiate fresh contents from blueprint references; old contents and customizations are replaced |
| Item properties/visuals, ability loadout | Export through existing native components | Replace from blueprint |
| Placement of an Item inside an unaffected owner | Not part of the Item blueprint itself | Preserve enclosing inventory coordinates/equipment slot/store category and membership metadata; validate fit/legality |
| Geometry and other spatial authoring absent from the blueprint | Respect the existing blueprint profile | Audit as placement data before enabling Trigger/Encounter and other additional types; no arbitrary gameplay-field preservation |
| Transient/runtime/renderer state | Exclude | Exclude |

Retaining a placed destination root UUID is an identity-bookkeeping choice, not
retention of old authored state. New descendants follow the existing new-instance
identity policy and never inherit unrelated old child UUIDs. Blueprint documents
omit UUIDs at their serialization boundary.

A new appearance may invalidate creature clearance at its unchanged position; an
Item update may change equipment legality or inventory footprint in an unaffected
parent. Reuse the current profile/admission/layout validators and reject invalid
replacement with a concrete diagnostic. Do not silently move the replacement or
drop failing blueprint children. Removing old customized inventory is the intended
replacement behavior; missing newly required inventory is a validation failure.

Before publishing an instance as a blueprint, resolve nested item references and
compare their exportable payloads with the effective saved item blueprints. Reject
empty/unresolved references and edited nested payloads that would be discarded by
reference serialization; identify the item that must be saved/updated first. Cache
these dependency reads once per operation. Do not auto-create nested blueprints.

### Identity and destination rules

**User decision: retain NWN-compatible blueprint identity for now.** Use the
existing flat `(ResourceType, Resref)` namespace for module blueprints. Directories
organize storage; they do not participate in that key. Moving a blueprint between
valid organizational directories does not change its identity or stored references.
Different resource types may share a ResRef. Layered module/base/hak resources may
deliberately share the same typed key through established override precedence;
uniqueness does not mean across every installed resource container.

Observed ordinary-module lookup discards directories. Thus two Creature files
`town/guard.utc.json` and `dungeon/guard.utc.json` currently compete for the same
`(utc, guard)` key. The core supports longer ResRefs and path-preserving package
keys already, so retaining this namespace does not claim complete NWN format
compatibility. Native resource limits and binary export constraints are separate
questions; do not quietly change either through this dialog.

Directory-local blueprint identities are outside this feature. Retaining the
existing namespace avoids reference migration and changes across indexing,
placement, nested references, and propagation matching. Do not add reference
aliases or enable duplicate same-type project ResRefs in separate directories.

Match `(ResourceType, Resref)` exactly within the active project's resource view.
Type comes from the actual typed record/resource protocol, never a tag, model name,
arbitrary string field, or filename substring. Existing `object.resref` is the
instance's blueprint reference; no separate GUID/provenance database is required.
An imported filename/internal-reference mismatch must reject updating until
resolved; do not silently choose either identity.

Validate a new name before constructing/interning a `Resref`: nonempty, valid
single resource name, no separators/traversal/control characters, within both the
resource and destination-filesystem byte bounds. Normalize case using the existing
resource contract. The core maximum is 4,096 bytes, not an assumed NWN 16-byte
limit; native project rules must explicitly reject names the filesystem cannot
store. Audit platform reserved names and trailing-dot/space behavior on Windows.

New Blueprint and Save as New Blueprint reject collisions with any effective
resource of the same type/reference, including inherited resources and case aliases. Update Blueprint
replaces the unique project-owned native resource, or explicitly creates its
project override when the effective source is inherited. Show that destination in
the action prompt. Never write into installed content, haks, or a second project.
Implementation audit: `ResourceManager::update_container_search` puts haks and
configured override/custom containers above module files. Preserve that order.
An inherited source can be updated via a module override only when the module
would win; otherwise reject with the higher-priority source diagnostic. Do not
save an ineffective copy and report that the referenced blueprint was updated.
Ambiguous duplicate project resources, binary conflicts, and outside-project
symlink targets reject. Missing update identity/destination directs the user to
Save as New Blueprint; it is not an accidental create operation.

Both New and Save as New use the same destination popup and validator:

- **ResRef:** an editable resource name without path or extension. Display name
  and tag remain separate values. Type determines the `.utc.json`, `.utp.json`,
  or `.uti.json` suffix; show the resulting destination filename.
- **Directory:** choose a writable directory inside the active native module's
  loaded project resource root. Default New to the selected valid resource folder
  or the existing type-specific blueprint folder. Default Save as New to the
  source blueprint's project folder when applicable, otherwise that type folder.
  An instance's area folder is not its blueprint destination.
- Resolve and validate directory containment, symlinks, permissions and filesystem
  name limits before preparation, and revalidate before publication. Reject an
  outside-project or unindexed destination; do not save an undiscoverable resource.
- Report file and effective typed-resource collisions inline, including the
  existing resource's location. Under the chosen identity policy, choosing
  another folder does not resolve a same-type ResRef collision. Create stays
  disabled until the entered destination is valid; publication checks it again.

The popup/path-validation work reuses project paths and the shared publisher.
Its cost is bounded interactive validation plus the existing resource-view lookup;
it requires no new reference schema or project-wide content migration.

New writes need a create-without-replacement primitive. Extend the existing
resource-file code to stage complete bytes and publish exclusively; an `exists`
check followed by ordinary overwriting rename is insufficient. Preserve the
current replace-existing contract for ordinary Save. Fail explicitly on unsupported
filesystem semantics. Do not reuse `move_file_safely`, which copies over its target.

### Live documents and publication

The modal Save as New form captures the source handle and project generation;
submission copies the current object. Update review captures the source payload
and destination facts; confirmation revalidates those facts without creating another copy. An open dirty
destination rejects and identifies the conflicting tab; Save/Discard/Cancel uses
the existing document protocol. When the selected source is that same blueprint
document, Update is ordinary Save. Disable these authoring operations during the
transient F9 play preview; its detached actor is not an authored document source.

Prepare replacement document data before overwriting an open clean destination.
After a successful replacement, refresh that document at a safe event boundary,
release its old history before its old graph, invalidate dependent selections,
and rebuild its presentation. Keep unrelated documents/history/cameras intact.
Do not leave a clean open tab displaying the old bytes or retaining undo actions
against destroyed objects. The source area stays dirty if it was already dirty.

Publish successful file writes through one module-resource refresh per batch.
Extend `ResourceManager` to rebuild the module directory and registry without
restarting services or unloading live documents/haks. Prepare replacement registry
storage before invalidating old container keys, preserve priority, and advance the
existing generation only on successful publication. Rebuild the project catalog
and affected preview metadata from that generation; do not reload the whole kernel.

File persistence and registry publication are distinct outcomes. If the file was
committed but refresh fails, report "saved; resource refresh failed", preserve a
retryable publication request, and avoid stale placement/preview use of affected
resources until it succeeds. Never report that nothing was written or invite a
blind retry of exclusive creation.

### Current-area and whole-module propagation

The command is Update Blueprint References. Before discovery, show the saved
source blueprint and two explicit scope choices:

| Choice | Candidate documents and matching instances |
| --- | --- |
| Current Area | The pinned native Area's top-level objects and nested inventory/equipment/store instances; no file writes |
| Whole Module | The same native live batch plus supported project-owned authored documents, excluding the live CAF from file writes |

ASSUMPTION: Current Area defaults when a pinned authored area exists, including
when the active tab is a blueprint — affects dialog selection. Show its area name
and path. With no current authored area, disable that choice with an explanation
and offer Whole Module. Never silently broaden a Current Area request.

Capture the scope and exact area document identity when starting the operation.
Changing tabs cannot retarget it. Current Area enumerates its native ownership
arrays. Dependency resolution still uses the
same effective module resources. Other areas and standalone blueprint documents
are outside its write set. Whole Module follows the full protocol traversal below;
installed resources, haks, runtime actors and other projects are never targets.

Current Area instantiates replacements, restores placement and UUID, swaps native
owner slots, and destroys the old objects. It keeps unrelated unsaved edits and
becomes dirty. Whole Module excludes that CAF from disk staging and restoration.
The area's old handle-based history is cleared before destruction; live-area undo
is an open follow-up. File batches retain their explicit Restore operation.

Use a saved blueprint snapshot as source. A dirty blueprint must be saved or
discarded before discovery. Enumerate native documents within the chosen scope
and explicit object-bearing locations: typed CAF arrays, nested inventories, equipment, and
store inventories. Walk those protocols with an explicit stack, retaining paths by
index/offset. String item references already resolve to the changed blueprint and
need no file edit; report them separately from materialized instances. Do not scan
script strings or arbitrary JSON properties for matching text.

For Item propagation, embedded items inside project-owned blueprint documents are
also part of the format audit; current blueprint inventories normally contain only
reference strings. Do not rewrite unrelated blueprints that simply reference the
same Item. Unsupported formats, unknown object-bearing schemas, missing references,
or malformed candidates are reported explicitly. A scan with incomplete coverage
must not offer a successful complete-scope commit.

After strict preparation, produce a viewport-virtualized review containing source,
scope, document counts, matching instance counts, retained placement, replaced contents,
and conflicts. State that
instance edits, tags, variables, inventory, and equipment are overwritten. A zero-
change result is a no-op. Phase 1 does not build a persistent reverse index: scan
the chosen scope once per requested operation and group affected rows by document.

Require affected open documents to be clean before taking final disk snapshots;
offer the existing save workflow and re-scan afterward. At commit, reject changes
to the source, project/resource generation, candidate file set, target bytes, or
affected open-document state since review. Do not merge a stale preview.

Process one document at a time per isolated preparation worker and stage validated
before/after bytes on disk. A document is the indivisible scheduling unit: two
workers never edit separate instances inside the same output document.
Capture the target's placement/attachment record, instantiate the complete fresh
blueprint graph detached, restore placement, validate, then serialize that graph
with the instance profile into the document's replacement slot. Never deserialize
over the old live instance. Reuse `load_area_object_blueprints`' detached ownership
pattern, but add snapshot-based construction so review and commit consume exactly
the same source/dependencies rather than re-demanding mutable resources.

The current area loader can warn and omit a failed child; strict validation must
prove all newly required blueprint children loaded, all unaffected document
children remain intact, and only intentional replacement removes old descendants.
Do not compare the old and new child counts for equality: changing blueprint
inventory is expected to change that count. Validate dependency cycles/missing
resources before recursive instantiation and stage each resolved blueprint once
per operation; each worker owns its cache of decoded dependencies. All
allocation/load failures release the detached prefix.

For nested matches, an outermost selected replacement owns its subtree; do not
also apply stale paths to old descendants that it replaces. Count those covered
descendants separately in the preview. Do not recursively run the update again on
freshly created descendants; they already come from the frozen dependency set.
This gives one deterministic pass even when parent and child reference the same
Item blueprint. Cyclic source dependency graphs reject rather than recurse forever.

All affected files in the chosen scope must pass preflight and staging before the
first authored file changes. Stage an operation-specific recovery manifest and originals under
`.rollnw/operations/blueprints/<operation-id>/`. The manifest is a versioned local
protocol containing only project-relative paths, expected before/after content
identities, source identity, scope and captured area path when applicable, and
commit state. Reject any target outside that frozen scope. It contains no live
handles or undo closures. Full before/after bytes are stored once per affected file, not per match.

Commit by atomic replacement per file. This is recoverable multi-file work, not a
claim of filesystem-wide atomicity. On failure, stop and restore the committed
prefix only while current bytes still match this operation's output. If recovery
cannot finish, retain the journal, identify each outstanding file, and block new
writes to affected documents until resolved. Never overwrite intervening edits.
On restart, reconcile each file against before/after identities; an unexpected
third value is a conflict requiring explicit resolution. Cancellation before
commit changes no authored file; cancellation during commit follows recovery.

After success, refresh resources once, reload affected clean live documents at the
safe boundary, clear only their invalidated histories, and publish visual and
navigation invalidation once per affected displayed area. Replaced root UUIDs,
parent ordering, and spatial data remain unchanged; descendants are newly created
from the blueprint. Process-local handles are
reacquired after document reload. Preserve camera and restore selection where a
validated identity/path allows it. If reload fails after disk commit, report that
distinctly and prevent use of stale live data.

Project propagation uses its recovery record for explicit Restore, not the current
tab's Ctrl+Z stack. Restore revalidates current after-bytes and has the same recovery
rules. Retain completed records until explicitly discarded; do not invent an
automatic retention budget. Ordinary blueprint Save/Update retain existing save
semantics and do not imply filesystem undo.

### Background execution and parallel preparation

Use one UI-owned `BlueprintUpdateJob` following the existing SDL process pattern.
An internal client CLI mode runs preparation without creating a renderer/window;
it initializes its own selected profile, resource view, and object service. The UI
captures source/project state, launches the job, polls status, shows review, and
reconciles its own live documents. It never waits synchronously for a process or
runs the full project scan/instantiation loop inside an input callback.

The initial implementation runs one preparation process. That is enough to keep
the editor's event loop available while expensive parsing, object construction,
validation, and staging run. It is not a throughput improvement claim. Do not
make the shared kernel thread-safe just to parallelize this operation.

Implemented lifecycle: preparation exits when review is ready. Commit and restore
each start a separate process; commit boots its own kernel, while restore only
replays staged bytes. This reduces persistent IPC/lifecycle states at the cost of
another kernel startup for commit. Multiworker preparation and process reuse
remain unimplemented; no throughput improvement is claimed.

After the one-worker baseline, measure two and four isolated workers on the same
frozen inputs. If beneficial within the measured memory budget, distribute whole
affected documents across a bounded group. Start processes once per operation and
give each many documents; do not spawn one process per area or item. Initially
partition documents by measured file sizes/counts; add dynamic work distribution
only if traces show material imbalance. Keep the worker count an internal
measurement-controlled policy, not a new user setting.
Current Area has one candidate document and uses one preparation worker; extra
workers cannot partition that indivisible document and add no assigned work.

Every worker gets a versioned job manifest with explicit document indices, private
stage paths, frozen scope and optional area path, selected profile/resource
configuration, and captured blueprint and dependency bytes. Workers use the same
immutable source/dependency set. Bootstrap
must verify it can resolve that configuration; no worker may silently select a
different package or resource precedence. Interned resource values are constructed
inside each worker; handles, TypeIDs, container pointers, numeric service generations,
and UI objects never cross the process boundary. Project-owned before-bytes and
external dependency identities are revalidated before commit.

Preparation workers own disjoint output/status files under the operation directory
and cannot write authored project paths. An assigned document produces exactly one indexed
result; aggregate by input index so output/review order does not depend on worker
completion order. Reject duplicate, missing, stale-operation, malformed, or
outside-assignment outputs. A failed worker stops preparation; stop/reap the
others and retain useful diagnostics. Completed temporary files are not a partial
successful update.

Use a phase barrier: aggregate scanning counts only against the frozen scanning
assignment, and preparation counts only against its affected-document assignment.
Advance a phase after every worker's result is accepted; do not add counts from
different stages or grow a known total while displaying its percentage.

One commit worker owns the recovery journal and performs final replacements in
document order after the UI authorizes the reviewed, revalidated batch. It may be
an existing worker switching phase; no concurrent commit writers or parallel
rollback. The editor polls it just as it polls preparation, so slow writes and
recovery do not block progress rendering. Live registry publication and document
adoption remain editor-thread operations after disk work; workers never touch
the editor's kernel.

Snapshot through commit is one modal authored operation. Disable conflicting
project writes, project replacement, and play-preview entry while it runs. Keep
event processing, progress rendering, and Cancel available. External file changes
still require the existing expected-content checks. Before commit, cancellation
stops preparation and discards staged outputs without changing authored files.
During commit, send a cooperative cancellation request and show Restoring until
the ordered rollback finishes. Never implement normal commit cancellation as a
process kill. Unexpected worker exit invokes journal reconciliation; application
quit waits asynchronously for cancellation/recovery or explicitly reports an
unresolved operation on the next launch.

### Progress bar contract

The progress surface is required for the first propagation release, including the
one-worker implementation. Show a stage label, progress bar, completed/total units
when known, current document, affected-instance count, and Cancel. Keep progress
separate from the potentially large, viewport-virtualized review/error list.
Keep the scope visible: Current Area with its name, or Whole Module. All document
and instance counters refer to that scope; Current Area's document total is one.

| Stage | Bar and meaningful unit |
| --- | --- |
| Starting / discovering files / resolving dependencies | Indeterminate; show stage and counts discovered so far |
| Scanning references | Documents examined / frozen candidate-document count |
| Preparing replacements | Documents fully validated and staged / affected-document count; also show instance count |
| Review | Awaiting the user's Apply or Cancel; no running animation or implied save |
| Saving | Authored documents atomically replaced / changed-document count |
| Finalizing | Indeterminate named step for resource publication and live-document refresh |
| Restoring | Documents reconciled/restored / rollback-document count |

Percentages describe stage work units, not elapsed time. A large area can take
longer than a small one. Do not invent an ETA or combine stages with guessed
weights. Saving at 100% is followed by Finalizing; show Complete only after
publication and live-document reconciliation succeed. An empty scan is "No
matching instances"; unchanged matches are "Already up to date", without division
by zero. Failure/cancel is a distinct terminal state, never a successful full bar.

Add a versioned flat progress record containing operation ID, worker index,
sequence, stage, total-known flag, completed/total units, matched/changed-instance
counts, current document index, and status/diagnostic index. Counters are checked
64-bit values; invalid enum/index/range, counter regression within a stage, or
completed greater than a known total is a protocol error. Stale-operation or older
sequence records are ignored. One operation's terminal state cannot be replaced
by late progress from a worker.

Use the operation's existing file protocol: each worker atomically replaces its
own small progress snapshot; the UI reads only complete records with nonblocking
process polling. Initialize the status location before launch. Publish stage
transitions promptly and coalesce intermediate updates to at most 10 Hz per
worker. Aggregate disjoint counts in the UI at at most 10 Hz; render the existing
indeterminate animation normally between updates. Do not create a DOM row/event
for every instance or parse free-form logs as progress. Missing early status means
Starting; a malformed complete snapshot or process exit without a terminal result
is failure, not a percentage estimate. Full diagnostics remain in bounded review
rows and the operation log.

The UI must not call a blocking process wait/join. Measure main-thread source
capture, status reads, final publication and document refresh separately: moving
preparation off-thread does not prove those steps are responsive. Split any
measured blocking finalization work into safe event-loop steps before release;
the progress surface must remain active through finalization and recovery.

## Component Design

| Component and files | Responsibility and interface shape | Ownership and errors |
| --- | --- | --- |
| `tools/client/blueprint_edits.hpp/.cpp` (new) | `initialize_blueprints(creation_rows)`, `prepare_blueprint_writes(requests)` and `publish_blueprint_writes(prepared)`; typed fresh initialization and common source/destination publication | Operation owns detached new roots and serialized snapshots/results; borrows existing live identities only while capturing/revalidating; failed initialization releases its prefix; successful new roots may transfer to workspace documents |
| Existing `object_document.*`, `resource_document.*`, `project.*` | Reuse typed export, project paths, native resource layout, and file staging; add exclusive create and expected-content replacement | RAII temporary files and validated paths; never silently replace on create; keep Save's existing missing-file behavior |
| Existing `ResourceManager.*` | `refresh_module_resources()` rebuilds the one active module resource view and publishes generation | True singleton: one shared module registry serves many writes; registry owns containers/keys; failure retains old valid registry |
| Existing `workspace.*`, `toolset_backend.*`, `main.cpp` | Route commands/prompts, protect dirty documents, replace clean documents safely, invalidate presentation | Existing workspace owns roots/history; one active modal review is a UI singleton; all target changes remain batched |
| `tools/client/blueprint_references.hpp/.cpp` (new, later phase) | `collect_blueprint_references`, `prepare_blueprint_updates`; typed traversal, detached replacement batches, placement restoration and dependency validation | Owns flat rows, path/text buffers, source/dependency snapshots and one working document; errors identify document and object path; incomplete scans cannot commit |
| `tools/client/blueprint_operations.hpp/.cpp` (new, later phase) | `stage_blueprint_updates`, `commit_blueprint_updates`, `restore_blueprint_updates`; bounded processing and local recovery record | Owns stage directory, before/after bytes and journal; no service pointers in persisted state; failures stop/reconcile explicitly |
| `tools/client/blueprint_update_job.hpp/.cpp` (new, propagation phase) | SDL process tracking, assigned document partitions, cooperative cancel, progress polling/aggregation and internal CLI dispatch | UI owns trackers; worker processes own their kernel state; no blocking UI wait, shared live data, or concurrent authored-file writers |

The new files organize three actual transforms that existing helpers do not own:
source-to-blueprint publication, typed project reference discovery, and recoverable
multi-file application, plus the required background job/progress lifecycle. Keep
serialization, registry implementation, and ordinary
save in their existing modules. No generic editor framework or second object model.

### Batch transform contract

Version 1 of the in-process authoring protocol uses flat rows plus owned payload
and path buffers:

- Request rows: explicit `create`, `save_as`, or `update` tag, destination `Resource`,
  validated project-relative destination-directory span,
  and a tagged input payload. New rows reference a creation-choice row; save-as and
  update rows reference a source document/handle and captured snapshot. Partition
  by case before processing; do not require a dummy source handle for New Blueprint.
- Creation-choice rows: supported object kind, display-name text span, and the
  actual typed initial catalog selections required by that kind. The operation
  owns the text/choice buffers until initialization ends; stale catalog generations,
  invalid IDs and unavailable required defaults reject the complete input batch.
- Reference rows: document index, supported object type, typed reference, and a
  span into an indexed path-segment buffer. Path segments identify array/slot
  locations from the known document protocol, not arbitrary JSON pointers from UI.
- Propagation requests: source typed key and captured snapshot, checked
  `current_area` or `whole_module` scope, and a document index for Current Area.
  Reject unknown scope values, missing/stale area identity, or out-of-scope
  assignments/results before staging. Candidate document rows and scope remain
  owned by the operation until completion or recovery disposal.
- Placement rows: parallel target indices plus existing spatial state or a tagged
  owner attachment record (inventory position/equipment slot/store membership).
  Preserve the root UUID separately from source blueprint data. The record contains
  only the placement/identity information actually needed by that target kind.
- Document rows: canonical project-relative path span, expected source-content
  identity, and range of affected reference rows.
- Result rows: input index, status, changed-instance count, and diagnostic span.
- Serialized payload buffers and paths belong to the prepared operation through
  publication/restore. Results remain valid until that operation is released.

Use checked 32-bit row indices and 64-bit byte offsets/counts. Invalid counts,
overflow, duplicate destinations/paths, stale handles, incompatible types, or
out-of-buffer spans reject before writes. Empty batches are a no-op. No silent
truncation or partial successful discovery. Every UI singular command invokes the
same path with one request; it does not have an independent mutation algorithm.

Hot traversal is linear over flat rows and explicit document arrays, partitioned
by document and type. Expected branches are stable within a partition; malformed
data leaves the common path. Locality assumes an ordinary desktop CPU with caches
and sequential filesystem reads; no SIMD, GPU work, or hardware-specific tuning.

Existing generational handles are justified for cold access to ObjectManager-owned
live graphs and stale-source validation. Existing container pointers remain owned
by ResourceManager. Neither belongs in reference traversal or the recovery format;
replacing those established lifetime protocols is outside this task.

## Implementation Map

Create in the indicated phase:

- `tools/client/blueprint_edits.hpp/.cpp`: fresh initialization, prepare/publish API
  and copy policy.
- `tests/rollnw_client_blueprints.cpp`: from-scratch creation, save-as/update and
  publication integration, including profile defaults and fresh-object cleanup.
- `tools/client/blueprint_references.hpp/.cpp`: reference review/preflight.
- `tests/rollnw_client_blueprint_references.cpp`: traversal, complete replacement,
  placement restoration, and nested-match partitioning.
- `tools/client/blueprint_operations.hpp/.cpp`: staging/commit/recovery.
- `tests/rollnw_client_blueprint_operations.cpp`: failure/restart/restore evidence.
- `tools/client/blueprint_update_job.hpp/.cpp`: isolated process lifecycle and
  progress aggregation, using SDL like the existing import tracker.
- `tests/rollnw_client_blueprint_jobs.cpp`: job/progress protocol checks and
  subprocess integration where the built client executable is available.

Modify:

- `tools/client/blueprint_commands.cpp` (new): keep blueprint command/form handling
  beside the backend instead of adding another large block to its existing command
  registry. It uses the same CommandBus and owned CommandPrompt data. Extend that
  prompt with text/select/directory fields for the requested creation popup; the
  desktop renders it modally while continuing normal event processing.

- `tools/client/object_document.hpp/.cpp`: expose/reuse typed export and safe
  document replacement preparation without duplicating type dispatch.
- `tools/client/resource_document.hpp/.cpp`: explicit create/replace file modes,
  exclusive publication, expected-content checks, and narrow staging helpers.
- `tools/client/project.hpp/.cpp`: expose native destination calculation and
  project resource enumeration/identity validation already used by import/tree.
- `tools/client/object_edits.hpp/.cpp`: reuse/extend detached blueprint batch
  construction and existing admission checks to consume captured source data.
- `tools/client/blueprint_edits.cpp`: keep missing authoring initialization private
  to the client and reuse the existing profile APIs. No new files or APIs under
  `profiles/nwn1`. No generic factory or parallel rules catalog.
- `lib/nw/resources/ResourceManager.hpp/.cpp`: module-only refresh with safe key
  lifetime and one successful generation publication.
- `tools/client/workspace.hpp/.cpp`: explicit replacement of clean target documents
  with history-before-root release; no cross-document Ctrl+Z facade.
- `tools/client/toolset_backend.hpp/.cpp`: command registration, source resolution,
  pending prepared operation, conflict/result routing, and safe publication retry.
- `tools/client/main.cpp`, `tools/client/ui/panel.rml`,
  `tools/client/ui/panel.rcss`: selected-object/blueprint actions, naming prompt,
  shared ResRef/directory popup, New Blueprint type/creation choices and catalog
  validation, Current Area/Whole Module scope prompt,
  internal worker CLI entry before renderer startup, nonblocking job polling,
  review/progress/recovery UI and document-refresh integration. Reuse existing
  RmlUi/native command plumbing; no new scripting feature or UI generation DSL.
- `tools/client/CMakeLists.txt`, `tests/CMakeLists.txt`: explicit source registration.
- `tests/rollnw_client_documents.cpp`, `tests/rollnw_client_commands.cpp`,
  `tests/resources.cpp`: extend existing document, command and registry coverage.
- `tools/client/README.md`, `tools/client/docs/transactions.md`: user actions,
  replacement/placement behavior, overrides, recovery and undo boundaries.

Additional source changes are permitted only for a concrete missing validator or
authoring initializer identified in preflight; record its ownership and cost here
before implementation.
Do not broaden this into a serializer or profile-ownership rewrite.

## Data Flow

```text
project browser / Home / palette: New Blueprint
  -> type + ResRef + directory + required profile choices
  -> initialize detached object batch -> validate serialized reload
  -> shared exclusive-create publisher -> resource registry refresh
  -> existing blueprint document/editor -> ordinary edit / Save

selection / blueprint tab
  -> native command -> capture typed source payload
  -> validate copy policy, dependencies and destination
  -> ResRef + directory popup / review existing update destination
  -> revalidate -> exclusive create or expected-content replace
  -> publish resource registry -> reconcile open documents -> refresh presentation

saved blueprint + project
  -> choose Current Area or Whole Module -> freeze scope and source
  -> capture job inputs -> launch isolated preparation worker(s)
  -> enumerate scoped typed documents -> collect matches -> stage replacements
  -> UI polls scoped progress -> review scope / affected documents / conflicts
  -> revalidate source, dependencies, targets -> persisted recovery manifest
  -> one commit worker performs ordered per-file replacements
      -> failure/cancel: guarded restore or explicit unresolved recovery
      -> success: editor resource publication -> document reload/invalidation
```

## Build Sequence

1. **Establish copy and identity policy.**
   - [x] Settle blueprint identity compatibility: the user chose the existing
     flat typed namespace with organizational folders. Directory-local blueprint
     identities are outside this feature.
   - [ ] Audit fresh Creature, Placeable and Item initialization: required catalog
     inputs, profile-owned defaults, native component materialization, serialization,
     and preview/reload. Record exact client initializer ownership and values.
   - [ ] Turn the observations above into focused fixtures for all three initial
     types, inherited sources, differing tags/locals, and embedded items.
   - [ ] Encode full replacement plus placement restoration, and verify nested-item
     export rejection for the reverse instance-to-blueprint direction.
   - [ ] Verify name, UUID, ownership, and dependent-layout/admission contracts.
   - [ ] Measure current resource-registry rebuild cost and source export cost;
     record the actual test machine and corpus, without extrapolating a speedup.

2. **Ship New Blueprint, Save as New Blueprint, and Update Blueprint.**
   - [ ] Add the shared ResRef/directory popup, filename preview, inline collision
     diagnostics and indexed project-directory validation to both create actions.
   - [ ] Add the source-free creation dialog and typed `initialize_blueprints`
     batch; use existing constructors/profile helpers and catalog rows.
   - [ ] Add the shared prepare/publish batch and exclusive file creation.
   - [ ] Add module-resource refresh, safe publication retry, and open-document
     reconciliation; inherited updates explicitly create a project override.
   - [ ] Wire UI, palette, and terminal to the same handlers.
   - [ ] Prove the edit -> publish -> place several -> save -> restart workflow.
   - [ ] Prove New Blueprint with no selected object -> Create -> focused editor
     -> edit/Save -> place -> restart/reopen, for each of the three initial types.

3. **Build propagation preview and strict preflight.**
   - [ ] Add Current Area/Whole Module selection, capture scope in the job, and
     enforce its document boundary during discovery, review, commit and recovery.
   - [ ] Add one isolated preparation worker using the existing SDL process
     lifecycle, with a versioned job/result/progress protocol and cooperative Cancel.
   - [ ] Enumerate all object-bearing protocols for the selected supported type,
     including nested items and reference-only uses.
   - [ ] Produce bounded review rows and strict coverage/conflict diagnostics.
   - [ ] Ship the stage-aware progress bar with counts and indeterminate states;
     keep UI event processing active even with one worker or a slow document.
   - [ ] Build fresh replacement instances from frozen source/dependency data;
     restore placement and replace complete serialized instance payloads.
   - [ ] Validate no unintentionally lost children, invalid equipment/layout, or
     newly invalid creature placement. Identical outputs leave the write batch.
   - [ ] Measure scan/validation latency and peak working memory on the census
     corpus; release review-only functionality independently if useful.
   - [ ] Compare one, two and four preparation workers on the same inputs, including
     startup, profile boot, navigation's inner threads, aggregate memory, and I/O.
     Enable a larger group only if the measured gain justifies those costs.

4. **Ship both propagation scopes and recovery.**
   - [ ] Stage all outputs/originals and the versioned recovery record before commit.
   - [ ] Run ordered commit/recovery in one worker and poll it without blocking UI.
   - [ ] Implement guarded replacement, rollback, interrupted-operation discovery,
     explicit Restore, and conflict-safe record disposal.
   - [ ] Show Saving, Finalizing and Restoring distinctly; Complete requires disk,
     resource-publication, and live-document success.
   - [ ] Reconcile affected open documents and renderer/navigation caches once.
   - [ ] Inject failures before/after every replacement and journal boundary.
   - [ ] Complete fresh-process desktop and Windows filesystem checks.

5. **Extend supported blueprint kinds only after their policy audit.**
   - [ ] Audit only the additional placement/attachment data required for Door,
     Waypoint, Trigger, Encounter, Sound and Store; blueprint fields still replace.
   - [ ] Add explicit per-kind placement/dependency tests before enabling each
     kind. Until then, the action reports that kind as unsupported.

## Critical Details

### Cost and scheduling

These are complexity estimates, not measured latency or engineering-time results.
The first three actions are medium implementation scope because file creation,
valid authored defaults, and registry/document consistency are missing. Creation
from scratch adds a small type-specific input surface and initializer validation;
it reuses the planned publication pipeline. Propagation is larger because its
correctness boundary crosses files and process restarts.

Let B be blueprint serialized bytes, R visible resources, S scanned project bytes,
M matching materialized instances, and C total bytes of changed documents:

- The chosen flat identity retains current lookup and reference formats.
  A directory picker adds path validation and UI state; no identity migration or
  additional lookup protocol is required.
- S is scope-specific: one CAF for Current Area, all candidate authored documents
  for Whole Module. Current Area uses the same batch/recovery implementation with
  count one; it pays worker startup and staging overhead but no whole-module scan.
- Publication costs source/dependency export plus O(B) file work and the existing
  full registry rebuild over R. Measure that rebuild before adding incremental
  publication machinery. The batch shares one dependency cache and one refresh.
- New Blueprint replaces source capture with detached allocation, profile
  initialization, and serialized reload validation. Cost scales with the created
  payload and its required components/catalogs, not project area count. Measure
  create-to-editor latency and temporary live-object count; do not launch a worker
  group or project scan for this small interactive operation without evidence.
- Discovery is O(S + visited object records), with contiguous result storage O(M).
  Ordinary hash/identity checks add linear byte reads; they are command-time work.
- Replacement constructs fresh graphs, including referenced inventories/equipment,
  then writes one working document per worker at a time. Cost includes all
  constructed nodes and existing profile initialization. With W workers, retained
  memory includes W kernel/resource/profile states, W decoded dependency caches,
  up to W working/validation documents, and the UI's flat review rows. Decoded
  memory is not equal to the 59 MB serialized corpus size and must be measured.
- Isolation adds process startup, profile/resource bootstrap, manifest/status I/O
  and result validation. Multiple workers add scheduling overhead and possible
  disk/cache contention; their improvement in elapsed time is unverified. Compare
  W = 1, 2, 4, including total child-process memory and existing inner nav threads.
  Retain W = 1 if extra workers do not produce a useful measured gain.
- Recovery adds approximately before + after changed-file payloads on disk
  (about 2C when sizes are similar), a manifest, and temporary replacement space.
  Disk exhaustion rejects staging or triggers explicit commit recovery.
- New steady-state frame work is limited to existing invalidation checks; do not
  continuously scan project files or maintain a reverse-reference index.

Progress and asynchronous execution are required independently of any throughput
optimization. Bulk parsing, instantiation, validation and file work run outside
the editor process; its own live-object changes remain on the editor thread.
Report source-capture/finalization latency and UI event-loop gaps as well as worker
time. Never hide a blocking main-thread loop behind an animated-bar design.
The modal authoring pause removes competing editor writes; external-state checks
and cooperative cancellation remain required.

Report scan/stage/commit/publication durations, maximum document-step duration,
changed/scanned document and instance counts, peak memory, and recovery bytes.
Record process count, startup/boot cost, work imbalance, status publication rate,
maximum UI event-loop gap, and Linux/Windows builds used. No performance improvement
is claimed by
this plan; acceptable interaction latency and a representative larger project
remain measurements to establish during implementation.

### Simplification pass

- **Do not do it:** no automatic propagation, live inheritance, arbitrary field
  editor, customization merge, new blueprint format, or project-wide load of live
  object graphs. Full replacement removes per-field preservation decisions.
- **Creation:** New Blueprint reuses the same exclusive-create publisher as
  Save as New Blueprint and the existing blueprint editor. Persisting a validated
  initial resource removes a separate file-less document/save lifecycle.
- **Identity and scope:** share one ResRef/directory validator. Retain the chosen
  flat namespace without adding reference aliases or migration work.
  Current Area constrains enumeration to one document and reuses the batch path,
  removing a full-module scan and a second update/undo implementation.
- **Do once/fewer times:** snapshot the source and each dependency once; scan once
  per operation; write each affected document once; publish resources once.
- **Approximation:** inapplicable to authored state and identity; copy exactly or
  reject. Unchanged files do no write work.
- **Small table:** use the finite supported resource/type and placement/attachment
  protocol, not resource-name special cases or per-field reflection guesses.
- **Large table:** no persistent reference index without measured repeated-scan cost.
- **Small buffer:** one working document per isolated worker plus staged files
  bounds memory by the chosen worker count; UI rows remain viewport-sized and
  progress is a small coalesced snapshot. One background worker meets the isolation
  requirement; more workers require measured benefit.
- **Constrain further:** native JSON project, explicit action, clean affected disk
  snapshots, three initial object types, no runtime mutation or implicit relinking.

### Verification and done criteria

- New Blueprint works without an active area, blueprint tab, or selected object.
  Each supported kind produces a valid serialized/reloadable native blueprint,
  opens the correct editor, and can be placed using the normal placement path.
- Fresh initialization uses the active profile/catalogs and explicit validated
  choices; unavailable defaults/rows fail visibly. Verify required Item native
  components even when `instantiate()` would otherwise return true after a warning.
- New creation has independent identity and no selected-object state. Cancel,
  invalid choices and write failure leak no live root and publish no partial file;
  a post-save UI/publication failure accurately reports the saved resource.
- Creation rejects collisions, traversal, case aliases, overlong/platform-invalid
  names, stale source, and a target created between preflight and publication.
- Both creation actions show ResRef, directory and resulting filename. Under the
  chosen flat policy, same-type names collide even in separate folders;
  different types remain distinct. Moving between valid organizational folders
  does not change the resource key. Check outside-root/unindexed paths, inherited
  collisions and case aliases without changing established override precedence.
- Update resolves by type/reference, handles inherited overrides, preserves the
  destination identity, and does not write installed or binary resources.
- A successful new blueprint can be demanded/placed in the same process without
  project reload. Source object/reference, source document dirtiness, existing
  placed instances, and unrelated documents retain their state.
- Open clean blueprint destinations refresh; dirty destinations reject without
  losing edits; ordinary Save keeps its established missing-file semantics.
- Nested item reference export never silently discards customized or missing
  item payloads. Serialization/validation failures preserve authored input bytes.
- Discovery covers top-level and nested instances, distinguishes reference-only
  uses, rejects incomplete scans, and matches only exact typed blueprint references.
- With matching instances in two areas, Current Area discovers/writes only the
  captured area and its nested instances; Whole Module finds both, including the
  unopened area. Verify byte-identical out-of-scope files, disabled Current Area
  without an area, dirty-area handling, scoped counts and manifest enforcement.
  Tab changes and stale results must never retarget or broaden a prepared request.
- Propagation replaces differing tags, local variables, appearance/statistics,
  inventory and equipment from the blueprint. Root placement/UUID and unaffected
  parent attachment records survive; replaced descendants use fresh identities.
  Validate dependent layout/navigation and write each changed file once. Compare
  semantic replacement output before creating incidental new child identities so
  an already-current instance is a no-op, not an identity-only rewrite.
- Nested parent/child matches are applied once through the outermost replacement;
  missing/cyclic dependency graphs reject and detached allocations are cleaned up.
- A changed source/target/file set, dirty document, malformed child, or unknown
  schema invalidates preflight; no authored file changes before all staging passes.
- Failure injection proves pre-commit rejection, mid-commit guarded restoration,
  restart reconciliation, conflict handling, and explicit Restore. No third-party
  edit is overwritten during recovery. Distinguish write success from refresh failure.
- One-worker and parallel preparation produce equivalent replacement payloads
  (respecting fresh-identity policy), identical target sets and stable review order.
  Workers never share mutable kernel objects or write the same staged/authored file.
- Worker startup/exit failures, missing/duplicate results, stale progress, invalid
  counters and late completion after cancellation are handled explicitly. A crash
  during commit enters recovery; normal Cancel does not kill the commit worker.
- UI checks cover unknown totals, zero matches, already-current instances,
  out-of-order worker completions, a slow document, Saving at 100% while finalization
  is pending, and Restoring after cancellation/failure. Counters are stage-correct;
  progress updates are coalesced and Complete never precedes publication/reload.
- Instrumented desktop runs show the progress surface repainting and Cancel/input
  events serviced during the expensive stages, with no blocking process wait on
  the event thread. Record finalization gaps and fix any observed freeze before
  shipping; worker isolation alone is not proof of responsiveness.
- UI smoke: edit -> create blueprint -> place multiple -> save/reopen; edit ->
  update blueprint -> choose scope -> review references -> apply -> restart and inspect both
  open and previously unopened documents. Exercise inherited source and nested item.
- UI smoke also starts from Home with no selection: New Blueprint -> choose type
  and required values -> Create -> edit/Save -> place and reopen after restart.
- Use targeted client-core/resource tests, relevant object round trips, GPU-enabled
  preview/invalidation checks, and Linux/Windows file-operation coverage. Measure
  the real corpus in a scratch copy; do not mutate the author's project for tests.

Evidence against this approach includes placement data not represented by the
restore protocol, widespread legitimate instances that fail dependency validation,
unacceptable full-registry refresh cost, or a single document exceeding the
interactive work budget. Record those observations here;
adjust only the affected phase. Do not silently widen copying or weaken recovery.

### Open design questions (local issue record)

1. Additional object kinds: identify the exact placement data absent from each
   blueprint (especially Trigger/Encounter polygons and spawn positions). Gameplay
   fields and customizations still replace; do not add preservation exceptions by
   field-name inference. Document any unavoidable spatial exception before enabling
   the kind, with an actual fixture.
2. Establish the actual filesystem durability guarantees required on supported
   platforms (process interruption versus machine/power loss). Recovery tests must
   match the guarantee; flush/rename alone must not be described as power-loss-safe.
3. Establish measured latency/memory acceptance on the target workstation class.
   The census records data sizes, not operation timings. Choose worker count from
   one/two/four-worker wall time, aggregate memory, and UI responsiveness evidence;
   default to one until measured. Do not infer the benefit from core count alone.
4. New Blueprint authored defaults: audit the exact required starting values for
   Creature, Placeable and Item in the active profile. Blank-object storage and
   allocation tests are insufficient evidence of valid authoring defaults. Record
   the existing initializer/mutation APIs to reuse and any missing typed owner
   operation before coding that kind. Required user choices must be explicit;
   do not invent IDs or fall back to copying a specially named blueprint.

### Plan self-check and verification status

Framing, observed inputs/distributions, assumptions, platform, cost, copy/error
contracts, batch/lifetime/process boundaries, progress/cancellation behavior,
save-directory validation, area/module scope, the accepted blueprint identity
decision, reuse, simplification, phased file map, and
done criteria are documented. Remaining decisions are listed in this local issue.
No speculative index, general transaction framework, or unmeasured speedup is
proposed. The new recovery protocol is justified only by the requested multi-file
operation and is deferred until that phase.

Verified for this plan: source/call-site inspection, native JSON samples, and a
read-only file/object census; document structure, whitespace, and source-reference
existence checks. Not verified: proposed operation behavior, runtime
performance, new filesystem primitives, recovery, or desktop interactions. Those
are implementation acceptance checks, not claims about current functionality.
