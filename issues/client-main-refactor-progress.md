# Client refactor implementation checkpoints

Scope: Tier 2. The architecture, component map, contracts, and acceptance matrix
in [client-main-refactor.md](client-main-refactor.md) remain the implementation
plan. This log records actual checkpoints; unchecked work is not complete.

## Patterns & Conventions Found

- `tools/client/main.cpp:1941–2072` supplies the production hit/focus policy;
  `tests/rollnw_client_rml_smalls_language_binding.cpp:49` supplies a headless Rml
  renderer and template fixtures. Extract the policy and extend those fixtures.
- `tools/client/main.cpp:8059–8657` supplies metrics formatting and smoothing;
  `viewport_rect.hpp:46` and `lib/nw/render/viewer/session.hpp` supply existing
  timer/statistics records. Keep these authoritative protocols.
- `main.cpp:1326–1478` uses existing recent-project parsing and atomic document
  replacement. `main.cpp:12916–13074` owns CLI exit codes and worker startup.
- Paired files, `nw::toolset`, existing CMake target guards, and clang-format
  remain the style and build boundaries. No feature API takes AppState.

## Architecture Decision

Extract ordinary functions with feature-owned state and specific dependencies,
in the issue's checkpoint order. Preserve event forwarding and callback order
while moving code; consolidate only behind production regressions. Do not add
a services bundle, event bus, or replacement operation implementation.

## Component Design and Implementation Map

The first checkpoints create `client_input.hpp/.cpp` with existing hit/focus
classification, `client_metrics.hpp/.cpp` with HUD state/formatting, and
`client_preferences.hpp/.cpp` and `client_cli.hpp/.cpp` with their actual leaf
functions. `client_runtime.hpp/.cpp` initially owns shared kernel/bootstrap
helpers used by desktop and CLI; later resource extraction extends that owner.
Modify main and existing CMake/tests at each integration boundary. Every new
file contains production implementation, rather than an empty destination.

## Data Flow and Costs

Ordered SDL events → current visible DOM/focus facts → existing native/Rml
dispatch. Classification borrows DOM pointers only for the call; synchronous
callbacks require reacquiring targets. There is one application and displayed
viewport, so resource initialization/HUD configuration are true singletons.

Renderer snapshots and measured times → metrics state → unchanged HUD markup.
Preference JSON → existing dock/recent rows; malformed fields retain current
ignore/clamp policy and I/O errors retain diagnostics. CLI arguments → existing
init/import/blueprint worker operations → unchanged status codes, without SDL
video initialization. Filesystem I/O and renderer/DOM access keep their existing
costs; moving code adds translation units and header maintenance.

Platform: configured Linux desktop SDL3/RmlUi/Vulkan client; UI/kernel/rendering
share the main thread, callbacks are synchronous, GPU resources outlive calls.
Source baseline is HEAD `b84462c02`, with 16,926 lines in main. The original
issue's code anchors still match. No new performance target or speedup claim.

ASSUMPTION: pointer motion is the most frequent event — affects the later
allocation-free routing protocol; rates have not been measured.
ASSUMPTION: adjacent events usually retain their map/gesture — affects branch
expectations only; facts are recaptured after each event.

Valid coordinates/dimensions, resource generations, dense row indices, and
existing operations' rejection policies remain the boundaries. Volatile focus,
keys, pointers, queries, and gestures are read at dispatch/sample time; resource
definitions change at their existing generations. No unrelated state is copied.

## Build Sequence and Definition of Done

1. Characterize production visible hits/focus; build the unchanged dispatch.
2. Extract metrics state, formatter, timing scopes; compare representative HUD
   output and null/delayed counter semantics; build the client.
3. Extract preference and CLI leaves; check round trips/malformed input and
   headless version/help/init/import/worker paths.
4. Continue the issue's C04–C20 gates. Final completion requires its dependency,
   full-test, sanitizer, optional-client, and lifecycle acceptance criteria.

A hidden hit region taking input, changed HUD output/exit status, lost release,
double commit, stale edit, or incorrect teardown falsifies preservation. Plan B
is the issue's smaller mechanical extraction plus characterization, if a narrow
API or ordering-preserving move cannot be established.

## Simplification Pass

Not doing new operation/binding infrastructure removes duplicated policy.
Using existing fixtures avoids another test renderer/control framework.
Metrics state moves once, without copying arrays or introducing a current-counter
representation. DOM pointers are required by Rml's API and remain call-scoped;
they are not persistent routing data. Existing singleton operations keep their
observed shape. Counter-mirror removal and refresh deduplication are later gates.

## Verification Log

C01 input seam: existing hit/focus helpers moved to client_input, with no SDL
switch or forwarding changes. The actual panel.rml fixture checks the same
coordinate and focus with the panel visible, hidden, and shown again. Existing
ordered-release/blur and other overlay characterization remain C01/C10 work.

C02: all 152 HUD fields have one ClientMetricsState owner. The formatter reads
two current shell options explicitly; update calls borrow existing renderer/GPU
snapshots. GPU scope labels and call positions are unchanged. Null snapshots
retain previous counters, including delayed GPU results, as before. RmlUi's
EncodeRml was inspected: its four escaped characters match the old helper, so
the extracted formatter uses that existing utility.

C03: preferences borrow only path/docks/recent rows and preserve the original
JSON/error/atomic-replacement policy. CLI and desktop share kernel/package
bootstrap in client_runtime. The information paths retain their pre-logger
ordering. CLI uses SDL subprocess regression fixtures with an invalid video
driver, literal spaces/shell characters, original usage/exit codes, and repeated
initialization. Shared import/blueprint worker fixtures exercise real children.

Normal client/test build passed. Focused regressions: 98/98 across 12 suites
(18.096 s), plus 2/2 permanent CLI regressions (37 ms). Initial execution from
the repository root failed before tests because the fixture requires the
build/tests working directory; subsequent runs use that directory. The new
visibility fixture initially omitted the panel's explicit display property;
fixing fixture setup produced a passing production classification test.

Measured source after the leaves: main is 15,809 lines (baseline 16,926).
All new feature headers/sources are free of AppState and application includes.
Includes for moved JSON parsing/iostream leave main; Rules remains necessary for
its existing appearance code. Latest recorded Release incremental compiles were
22.107 s for main and 4.180 s for metrics, from Ninja's log. These are extraction
cost observations, not a controlled performance comparison or speedup claim.

Combined sanitizer client/test build passed; 46/46 selected metrics/preferences,
Rml-template, preview, import, and blueprint-worker cases passed (125.095 s)
with ASan leak detection disabled and UBSan halt-on-error enabled. The known
baseline VM LeakSanitizer limitation is unchanged; this is not a clean leak test.
After removing unused includes, the normal client/test build passed again.
Renderer-disabled build results are recorded below.

Source comparison against the baseline verified identical bodies for seven
metric transforms/formatter after substituting only owned-state/option arguments
and the equivalent Rml encoding utility. The GPU scope implementation and the
ordered 152-field inventory also compare equal. This supplements the runtime
tests; it is not a performance measurement.

## C04a: command form and progress presentation

Reduced mechanical extraction: prompt/popup/generation/review state and its
render/commit functions move together to command_view. Input/result/loading
integration keeps thin root adapters at existing call positions. The public API
receives CommandViewState, the existing backend, and current loading facts;
there is no transitional root header or complete-root feature dependency.

Actual data is one displayed command overlay, owning prompt fields/choices and
generation-scoped popup state, and existing backend progress/document rows.
The overlay/document is a true singleton. DOM borrows and backend queries remain
synchronous; backend retains operation execution and document ownership. Invalid
field/choice indices, empty/duplicate choice identities, and unmatched current
values keep their original rejection behavior. Generation changes close the
popup before replacing markup. The displayed file window remains 20 documents.
This moves existing O(fields + choices + displayed rows) presentation work and
adds one translation unit; no new queue, copied operation data, or optimization.

Simplification: constrain this checkpoint to presentation to avoid introducing
a broad API for command execution, project loading, and workspace refresh.
Use existing VirtualComboBox and Rml EncodeRml rather than another control or
escaping implementation. Done for this checkpoint means unchanged refresh/commit
behavior with directly callable production functions and no AppState API.

Normal client/test build passed. 26/26 focused tests across five suites passed
(208 ms). New production-template cases verify edited values/focus/DOM retained
on ordinary refresh, choice commits and escaping, invalid-choice rejection,
generation-triggered popup clearing, overlay loading/operation visibility, and
malformed choices retaining the current popup. Main is now 15,436 lines.
Measured from the combined-sanitizer binary's debug types on x86-64 Linux:
sizeof(ClientMetricsState) is 704 bytes and sizeof(CommandViewState) is 432 bytes.
These contain moved state; no duplicate root fields remain for either inventory.

Remaining C04 work: palette querying/focus restoration, result flow and thin
action-listener ownership. These still coordinate the current root's loading,
shell/workspace, and preview state; do not move them wholesale into command_view.
The event switch, early Rml release/blur obligations, slider staging, and native
dialog callback lifetime are unchanged. C04 is not marked complete.

Integration results for C04a are recorded below.

Combined sanitizer client/test build for C04a passed. Source comparison verified
identical bodies for all six extracted form/popup/progress transforms after
substituting only state/dependency arguments and equivalent Rml encoding.
The full Client* sanitizer result is recorded below.

The first full CTest run exposed stale build metadata in the four other tool
binaries, and a blueprint fixture collision across concurrent shards: every
ClientBlueprints case deletes tmp/client_blueprint_authoring. A prepare failed
when its temporary resource directory disappeared. Isolate that fixture by test
name in a separate fix, rebuild the registered tool targets, then rerun the
affected gates. Graphics cases report unavailable headless fixtures; retain
their skips explicitly rather than treating them as renderer validation.

Renderer-disabled gate: build-ci-linux-clang built rollnw_client_core and
rollnw_test successfully with its existing renderer-OFF configuration. All
58 focused ClientAreaTileEdits/ClientPreview/ClientWorkspace cases passed there
(10.255 s). No CMake configuration/benchmark flags were changed.

The first full CTest run exercised 2,080 GoogleTest cases across eight shards;
seven shards passed, one failed on the shared blueprint fixture, and four tool
metadata checks failed. Each ClientBlueprints case now appends its test name to
the temporary project path (commit 12eb7cd4f), following the existing fixtures'
ownership convention. Rebuild registered tool binaries and repeat the parallel
gate to verify that concrete failure. All three required tile/door/workspace GPU
regressions were skipped for unavailable graphics fixtures in this run. They
remain unverified even if the configured CTest gate succeeds after repairs.

Final normal integration: all registered tool/client/test targets rebuilt with
the configured flags, then CTest passed all 15 entries in parallel (163.18 s).
The eight shards ran 2,080 cases: 2,039 passed, 41 graphics cases skipped. The
blueprint case previously failing during parallel prepare now passes alongside
the concurrent shards, and all five tool version/build-info gates pass. The
three required tile/door/workspace GPU cases still skip and remain unverified.
There were no production changes to renderer operations or UI dispatch order.

Dependency and format audit passed for all 12 extracted feature files. Main
remains 15,436 lines versus 16,926 at the baseline; the complete main/root
composition and C04b–C20 remain outstanding. This is a buildable initial
checkpoint, not completion of the full issue. The implementation follows the
issue's scope-reduction rule for command/result/loading coupling: finish those
integration APIs in their own checkpoint rather than exposing the whole root.

Final combined-sanitizer gate: all 315 Client* cases across 45 suites passed
(672.172 s), with ASAN_OPTIONS=detect_leaks=0 and
UBSAN_OPTIONS=halt_on_error=1. This includes the new command generation/popup,
visibility, metrics, preference, and real CLI subprocess regressions and the
existing worker/preview/resource-lifetime coverage. No clean LeakSanitizer or
desktop control-feel claim is made.

Final self-check for the delivered checkpoints passed: one state owner per
extracted feature, narrow APIs, documented singleton/borrow/error contracts,
reuse of existing backend/control/statistics operations, preserved transformation
bodies and integration call order, and no speculative framework or speedup
claim. The complete issue's self-check and C20 acceptance remain open. Commits:
db9e46282 (leaves), 5661d6746 (command presentation), 12eb7cd4f (fixture isolation).

Self-check for these leaves: current source matches the issue's original data
inventory; errors and singleton exceptions are documented; DOM/renderer borrows
are call-scoped; ownership moved without duplicate state; there is no new
general framework or unmeasured performance claim. The full subsystem self-check
remains open until C04–C20 and the remaining C01 integration gates are complete.
Desktop control feel/high-DPI forwarding, acquired-resource startup cleanup,
native callback shutdown lifetime, pending-slider ownership, shared routing/PC
controls, and final full-suite integration are not established by these leaves.
No desktop window is launched.

## C04b plan: palette and command interaction ownership

Frame: finish the remaining command presentation seams without moving project,
workspace, or native-dialog lifecycle into a command feature. Plan B is a thin
root coordinator where an action needs those existing owners. This remains a
Tier 2 extraction; this checkpoint is a bounded interface/state change.

Observed inputs are one palette document, one command overlay, a backend-owned
command list, and ordered synchronous Rml actions. Queries are strings; fields,
choices, and actions are owned vectors. The actual fixtures use zero/multiple
matches and text/choice fields. ASSUMPTION: distributions of query lengths and
match counts are unknown — affects no extraction policy. Stable command specs
are read from the backend; query, focus, form generation, and browse results
change with user actions. Missing DOM elements remain harmless; invalid action
indices, disabled actions, and stale browse generations are rejected.

Transform: query → backend matches → palette markup; visibility → capture/restore
focus IDs; overlay target → a concrete owned command/submit/browse request;
form action → owned argument strings; generation-matching browse result → form;
command result batch → existing shell logs. Native message-box presentation
belongs to command_view, but the root rebuilds CommandContext before each
dispatch and coordinates project/workspace refreshes. DOM pointers are borrowed
only during calls, except the existing context-owned overlay document. One
visible palette/form and one native prompt are true singleton presentations.
Rml traversal requires its pointer API; stored focus identity remains a string.

Cost on the configured Linux SDL3/Rml main thread: the same O(matches) markup,
O(fields) submission, O(ancestor depth) event/focus traversal, and existing DOM
allocation. State moves once into CommandViewState; request strings own their
lifetimes across subsequent DOM changes. No performance requirement is added.
Simplification: reuse backend filtering, existing prompt/action protocols,
Rml encoding, and shell append operations; omit a general callback framework
or dependency bundle. Keep root execution sequencing rather than caching a
potentially stale context.

Done: main owns no palette query/matches/focus state or overlay classification;
form submission and generation-scoped browse updates use production feature
functions. Headless real-template tests cover focus restoration, repeated open,
hidden/missing restore targets, query refresh, cancellation/disabled submission,
and stale browse results. Build client/tests and run the relevant existing
command/Rml suites. A changed action order or stale argument/focus result would
disprove the extraction. Native dialog appearance and desktop feel remain
unverified without a desktop fixture.

### C04b result

Normal client/test build passed; all 43 affected cases across seven command,
terminal, shell, combobox, managed-list and Rml suites passed (167 ms). The same
43 passed the combined ASan/UBSan build (607 ms), with leak detection disabled
for the documented baseline leak. Five new cases exercise production palette,
overlay, submission, browse-generation and logging functions. The initial
cancellation fixture was corrected to match actual forms (Cancel follows the
primary action). Bounds checks reject malformed one-field browse forms and
native prompt button counts exceeding int, without changing valid inputs.

Palette state, markup, focus capture/restoration, local overlay handling, owned
form submission, prompt presentation and browse-result mutation now live in
command_view. Root adapters coordinate fresh command contexts, native-dialog
lifetime and project/workspace refresh. Source and formatting audits passed;
main is 15,192 lines. C04 is complete at this seam; routing adapters are still
scheduled for C10. Native prompt appearance and desktop control feel were not
verified. Self-check: owned arguments/IDs outlive DOM changes, invalid/stale
inputs reject explicitly, logs use a batch path, presentations are documented
singletons, and no generic callbacks or performance claims were introduced.

## C05 plan: tile state and coordination

Frame: move the observed tile editor, not tile-fitting algorithms or the shared
workspace/input authority. First establish the owned state and palette/pure
gesture transforms; then move renderer coordination with explicit current area,
viewport, and eligibility facts. Cross-feature cancellation stays in the root.
The configured platform and callback lifetime constraints above still apply.

Observed data: palette rows/matches/textures, one selection, one active stroke,
row-major visited/preview masks, cell/corner index vectors, and retained preview
rows. Actual constants are 58 px rows and three overscan rows; the existing
ttr01 fixture starts with three folder matches. A brush is a selected action
row; selection with Shift also works with no brush. Group orientation has four
values. Cells/corners are uint32 indices; dimensions are positive int32 values
whose products and +1 corner lattice must fit the supported index range.
Invalid/stale grids cancel or reject with the existing feedback, and allocation
failures retain the existing diagnostics. ASSUMPTION: user event rates and
typical area dimensions are unmeasured — affects no extraction policy.

Transform: live area → existing palette build/filter → virtual visible rows;
current pointer/modifier + brush → existing selection/stroke operations →
preview rows; validated stroke → existing backend edit batch. Palette specs are
stable until the resource generation changes; hover/selection/stroke buffers
change during ordered input. The feature owns all these vectors and cached
presentation fields; the renderer and Rml document are borrowed. Current input
authority and stale-viewport facts are recaptured by the root. One displayed
tile editor/stroke is a true singleton; edit builders retain their batch paths.

Cost: unchanged O(visible rows) markup/thumbnail requests, O(area cells/corners)
stroke masks and reserved buffers, O(visited targets) preview/edit batches on
the main thread. Access is indexed/linear; kernel and renderer handles remain
required by their existing resource protocols. Simplification: retain existing
tile/palette/virtual-list functions and seed policy, omit another brush model,
general event callback interface, and operation implementation. Separate pure
and DOM presentation from renderer integration so headless tests can exercise
production functions without constructing a renderer.

Done: tile state and transforms accept no AppState, the root retains only
cross-feature/input acquisition adapters, and dispatch precedence is preserved.
Check real tileset palette virtualization/filtering plus existing tile/group,
height, undo/redo, and pointer policy tests. Add tests for the extracted transient
state where fixtures permit. Renderer preview movement remains subject to the
configured GPU regression fixture; report any skip and desktop-feel gap.

### C05 result

Tile state, palette/selection markup, brush/rotation/stroke builders and height
preview coalescing now live in area_tile_editor.hpp/.cpp. Renderer coordination
lives in area_tile_editor_runtime.cpp implementing the same feature header;
the headless test target compiles the presentation/pure source. This separation
keeps a renderer out of DOM/state fixtures without adding a test-only library,
interface or compiler/linker option. Private renderer helpers are not exported.
Root adapters still acquire SDL/Rml facts, synchronize structural rebuilds and
coordinate editor/workbench cancellation; native routing remains scheduled C10.

Normal and combined ASan/UBSan client/test builds passed. All 77 affected cases
across nine tile/UI/workspace/drag suites passed normally (146 ms) and with both
sanitizers (672 ms, baseline leak detection disabled). Four added cases cover
real ttr01 virtual rows/filtering, brush selection and four stationary rotations,
overlapping-corner preview coalescing, and original half-open viewport edges.
Malformed selection grids now hide selection info instead of dividing by zero.
Rml encoding of three string-view labels requires an input string copy; other
markup and tile/renderer operations keep their existing paths. Context snapshots
move to root adapters, so a failed gesture may construct its tab-ID string
earlier than before. No speedup or unchanged measured performance is claimed.

Renderer-disabled core/test build passed with the existing flags. Full normal
CTest passed all 15 entries (159.01 s). The three requested tile/door/workspace
GPU cases still skip for unavailable graphics fixtures and remain unverified;
no desktop window/control-feel check was performed. Formatting/source/ownership
audits and checkpoint self-check passed: one feature state owner, no AppState
API, existing batched operations, explicit errors/lifetimes, preserved capture,
preview/commit/cancel order, and no speculative event/transaction framework.

## C06 plan: area object interaction ownership

Frame: extract existing object drag/placement/region coordination and door/nav
helpers. Keep workbench activation and structural rebuild cancellation in the
root; reuse object edits, placement validation, region geometry and door hooks.
The root supplies the current tab/selection and fresh CommandContext rather
than lending AppState. This is the next bounded Tier 2 checkpoint.

Observed inputs: one drag with before/preview spatial rows and saved viewport;
one placement resource/temporary live object, phase and region point vector;
finite projected positions, selected encounter spawn indices, native edit keys.
The current threshold is 5 px and preview opacity 0.45. Position bounds are
inclusive [0, width*10] and [0, height*10]; regions require the existing validated
path/closing geometry. Saved viewport/area identity and active object must still
match at commit. Bad rays, stale indices/owners and failed snapping reject or
cancel with existing diagnostics; rollback destroys temporary roots and rebuilds
retained renderer rows before ownership changes. ASSUMPTION: typical drag rates
and region point counts are unmeasured — affects no extraction policy.

Transform: pointer + current viewport → existing ray/nav projection → preview
spatial/region rows; valid completed gesture → existing backend edits. The
feature owns gesture rows, region vectors and door/nav snapshots. Kernel and
renderer borrows are synchronous; their ObjectHandles identify externally
owned resources, so indices cannot replace that protocol in this extraction.
One displayed gesture is a true singleton; placement and geometry operations
retain their plural batch paths. Spatial reads are lookup followed by contiguous
row/vector traversal; validity branches remain outside the common valid update.

Cost: unchanged O(region points) validation/copying and O(spawn points) edits;
door/nav snapshot costs stay in the existing cached operations. Main-thread
renderer updates and rollback behavior remain unchanged. Simplification: omit
another navigation/door policy, generic transaction/event framework, and root
dependency bundle. Keep owned temporary-resource lifetime coupled to commit and
cancel, and extract existing finite/bounds helpers for headless checks.
The nearby float-text helper has only a workbench widget consumer; retain it
for C13 rather than creating an unrelated area-editor dependency.

Done: gesture state/coordination accept no AppState, active workbench publication
and cross-feature cancellation stay explicit root adapters, and existing drag,
region, placement, door, undo/redo tests pass. Audit source operation order and
add real-area boundary checks. GPU live door geometry and desktop control feel
remain unverified when their graphics fixture is unavailable.

### C06 result

Area drag/placement state and finite/bounds/resource/door-hook helpers now live
in area_object_editor.hpp/.cpp; renderer/nav/region/gesture coordination and edit
key actions implement that feature header in area_object_editor_runtime.cpp.
Root adapters retain workbench publication, current context acquisition,
viewport-kind/input gating and structural cross-editor cancellation. The source
press index is a specific scalar borrow, cleared at the original threshold step;
it is not a root dependency bundle. Unknown edit keys return no action.

Normal and combined-sanitizer client/test builds passed. All 93 affected cases
across eight area/navigation/region/door/object/workspace/Rml suites passed
normally (30.216 s) and under ASan+UBSan (160.685 s, baseline leak detection
disabled). The added real-area fixture covers inclusive bounds, nonfinite
positions, malformed dimensions and stale handles. Six build identity/version
gates passed (1.56 s). The normal build's first probes exposed missing direct
protocol includes; these are explicit now, without broadening target flags.
Two existing float-to-double promotion warnings in preview sampling remain.

Ownership/source/format audit and checkpoint self-check passed: preview and
rollback ordering remains in the moved bodies, root publication happens after
successful drag setup, batch operation implementations are reused, and no
generic transaction/navigation model was added. Observed native transform
handlers and renderer preview calls do not dispatch Rml callbacks or change
workbench identity between root fact capture and execution. Root snapshots may
copy context/tab IDs on a rejected gesture earlier than before; that cost is
unmeasured. Region/spawn vector allocation errors still propagate before command
commit. GPU geometry and desktop feel remain unverified for the recorded fixture
gap. Main is 13,257 lines; C07–C20 and C01/C10 integration remain open.

## C07 plan: project resource drops

Frame: move the current cross-panel resource drag and its temporary-item lifetime;
reuse existing item/equipment/store/spawn/sound edit operations. Extract the
existing workbench surface enum and three predicates into its real owner as a
required leaf, avoiding another enum/policy copy. Full workbench presentation
still belongs to C13. Plan B is a thin root adapter supplying current view facts.

Observed data: one drag resource/path, tab/owner identity, temporary item handle,
optional spawn/sound edit snapshots, integer footprint, and tagged drop target.
The actual target cases are inventory, equipment (18 slots), five store
categories, encounter spawns (limit 1024) and sound resources. Item/spawn sources
require authored JSON; sound sources are wav resources. Inventory rows/pages and
capacity come from the real owner. Invalid footprints, cells, slots, source
files and full lists reject; materialization failure suppresses retries until a
new drag. Motion below 5 px remains a click. ASSUMPTION: source/target case
frequencies and resource sizes are unmeasured — affects no extraction choice.

Transform: resource + current workbench facts → armed drag; intentional motion
→ existing source materialization → current DOM target/operation validation →
target visuals; release → backend batch edit or destruction of the temporary
item. Current workbench/tab/owner match facts are a call-scoped flat record, not
cached root authority. The drag owns its path, target and snapshots. Rml/kernel
borrows last one call; DOM traversal requires the existing pointer API and stored
identities remain IDs/indices/handles from existing resource protocols. One
displayed drag is a true singleton; backend item/spawn/resource edits stay batch.

Cost: existing one-time source I/O/materialization, O(ancestor depth) hit
classification and O(inventory footprint) validation on the main thread. Context
capture adds bounded field reads and owns the string returned by
WorkspaceState::active_tab_id(); it cannot borrow that temporary. This adds an
ID copy on calls whose old path rejected before reading the tab. Simplify:
reuse one workbench enum, strict numeric parsing, existing edit builders, and
Rml target traversal; omit a generic drop bus or transaction interface. Preserve
DOM clearing before temporary-root destruction and command/cleanup order.

Done: project drag state/operations accept no AppState; root supplies only actual
current facts, source press state and command context. Headless production tests
cover jitter, context switches, invalid source/targets and temporary-item
cancel/commit lifetime alongside existing inventory/equipment/store/spawn/sound
operation regressions. Root event precedence and managed-list reorder isolation
remain intact. Desktop gesture feel remains unverified without its fixture.

## C07 result and checkpoint self-check

The resource drag owns source materialization, target classification/visuals and
temporary-item cleanup. Main retains five narrow adapters and captures actual
workbench facts. The workbench surface enum and its three real predicates now
live in object_workbench; full presentation remains C13. No new backend
operations or AppState consumers were introduced. The context owns its tab ID
because the workspace getter returns by value, preventing a dangling borrow.

Normal executable/test builds passed, and all 89 resource drag, object edit,
Rml template, managed-list and workspace cases passed (29,057 ms). Three added
production coordinator tests use a real loaded Creature and an authored JSON
Item: sub-threshold jitter, context loss/cancel destruction, cached malformed
sources, and a real panel inventory drop whose committed item survives undo and
redo. Managed-list reorder remains on its separate existing path. The combined
sanitizer build passed with only the two baseline float-promotion warnings;
the same 89-case sanitizer run subsequently passed (145,024 ms). Leak checking
retains the previously documented baseline exclusion. Test command-host borrows
are unbound before their fixture owners disappear.

The simplification/self-check found no new registry or transaction abstraction;
one UI gesture is documented as a singleton, edits retain existing batch paths,
invalid source/target policies and clear-before-destroy order are explicit.
Formatting and diff whitespace checks passed. Desktop gesture feel and graphics
remain unverified. Main is 12,582 lines; C08–C20 remain outstanding.

## C08 plan: shared PC device translation

Frame: extract the observed F9 bindings and device/pick acquisition without
changing detached-session ownership. One pure map serves every PC consumer;
role is absent from its input. Plan B is the existing root capture/dispatch
adapter until C10 owns ordered UI arbitration.

Observed input: ten physical keyboard keys (W/S/Q/E/A/D and arrows), four stick
axes, two trigger axes, two shoulder buttons, connected-device state, pending
pointer/cancel edges, pixel look deltas, wheel delta and accumulated elapsed
time. SDL signed axes normalize with the existing asymmetric 32768/32767
divisors. Movement clamps to [-1,1]; combined look/zoom are deliberately
unclamped. Stick deadzone remains the existing radial 0.20 transform. Current
production acquisition is one active displayed PC per frame; the pure map
accepts borrowed equal-size contiguous input/output batches. Pointer rays and
dense door hits use the existing renderer and preview navigation APIs.
ASSUMPTION: device combinations/event rates are unmeasured — affects no binding
or optimization choice. Desktop SDL/main-thread, one viewport, and 60 Hz preview
ticks with at most six catch-up rows constrain the implementation.

Transform: SDL values → flat PcDeviceSample with separate keyboard/controller/
pointer eligibility → translate_pc_input_samples → existing PreviewInputSample
→ unchanged fixed-tick builder. Invalid ranges/nonfinite values or mismatched
spans clear every output and reject the batch. Missing controller contributes
zero. Claimed sources are suppressed separately; claimed pending pointer deltas
are discarded rather than replayed. Negative frame-time contribution remains
zero; no emitted tick retains eligible pending edges/deltas. Current visible
modal/text ownership is recaptured after event dispatch, with captured look
gestures retaining pointer ownership. C10 will centralize the ownership facts.

Cost: bounded physical key/axis reads and O(N) contiguous translation with O(1)
scratch, no allocations in the pure path. SDL owns the one open controller;
its adapter closes it before SDL shutdown. Renderer/DOM borrows stay outside
the flat batch protocol. Existing viewport-ray/hit APIs require their current
renderer borrow; no pointer graph or actor state enters the translator.

Simplify: reuse preview_radial_deadzone, pointer setters and fixed-step batches;
move the fixed key meaning into one map and omit role branches, a device registry,
binding configuration or gameplay message bus. Done: actual production sampling
uses the plural translator at count one; tests assert baseline combined fields,
all binding signs, deadzone/axis endpoints, separate source suppression,
invalid-batch clearing, latest pointer actions and zero/multiple-tick edges.
SDL device I/O and graphics picking require their desktop fixture and remain
an explicit verification gap when that fixture is absent.

## C08 result and checkpoint self-check

pc_input now owns the one fixed physical-key/controller interpretation and
pointer-action map; its headers/sources contain no SDL, Rml, backend, role or
detached-session ownership. runtime_input acquires the physical device values,
owns the controller through the existing SDL API and resets/consumes pending
edges. Ray/door acquisition remains a separate renderer adapter using the same
header, following the tile/object renderer-source arrangement. Door interaction
still avoids navigation projection; the preview consumer supplies world facts
to the same pure pointer map. The root owns RuntimeInputState separately from
PlayPreviewState. F9 stop resets pending data and preserves the connection.

As required by the issue, production held sampling now uses current source
eligibility after UI callbacks/layout. Visible command UI and existing blocking
load/publication/native-dialog gates suppress held sources. Text focus suppresses
keyboard; captured UI gestures suppress pointer, with captured PC look retaining
ownership. Home import configuration/background importing remain nonmodal, as
the observed UI allows continued work. C10 still owns centralizing and testing
the full ordered arbitration boundary. Claimed pointer/controller edges are
discarded instead of replayed. This eligibility behavior is an explicit issue
requirement, not a claim that the baseline already performed it.

Normal and combined-sanitizer executable/test builds passed without new warnings.
All 52 PC/runtime input, preview and Rml template cases passed (8,185 ms normal,
41,161 ms sanitized). Seven PC cases were rerun in both configurations after
preserving the original shoulder/trigger floating-point grouping; all passed.
The renderer-disabled core/test build and nine PC/fixed-sample cases passed.
Seven pure and three acquisition regressions cover key signs, combined axes,
deadzone endpoints, missing device, source suppression, invalid-batch clearing,
latest door/navigation intent, elapsed time, retained zero-tick edges and
single-use catch-up edges. Inactive pointer payload fields intentionally retain
the existing unspecified values; only their flags govern consumption.

Observed x86-64 debug type sizes are PcDeviceSample 136 bytes and RuntimeInputState
104 bytes. The translator uses caller buffers and no allocation; source reads
and validation add bounded work and no speedup is claimed. Actual controller I/O,
raw SDL axis endpoints and graphics picking remain unverified without their
device/graphics fixture. No registry, role branch or new gameplay transport was
added; singleton acquisition is documented and translation/pointer edits have
plural contracts. Formatting/diff checks passed. C09–C20 remain outstanding.

## C09 plan: detached preview lifecycle and presentation

Frame: move the F9-owned actor picker/drop, detached session, fixed-step buffers,
renderer attachment and overlay. Keep project/workspace eligibility and
cross-feature structural rebuilds in small root adapters. Plan B: adapters
retain shell refresh/layout/focus orchestration where that presentation has not
yet acquired its C11/C12 owner.

Observed input: one active area/tab/generation, authored Creature resource,
navigation spawn ray and camera yaw, one preview session, one pending device
accumulator, six fixed input rows and one spatial/locomotion output row. The
overlay has picker, pending placement, failed placement, active and navigation
debug presentations. Generation/tab loss stops the detached preview; malformed
actors prompt the picker, unavailable area/rebuild rejects, startup errors retain
pending placement, attachment errors stop and restore. These are desktop
main-thread singleton lifetime operations, not many-actor simulation transforms.
Simulation/navigation and fixed tick batches stay in preview_session.
ASSUMPTION: lifecycle transition frequencies are unmeasured — affects no policy.

Transform: current area/project gate → existing preview setting/resource check
→ picker or owned pending actor/area identity → navigation start → renderer
attachment → shared PC sample → unchanged fixed-tick simulation/output batches
→ renderer updates; stop removes visuals before destroying the detached session,
then resets pending device/tick state and restores shell layout/cursor. Rml
overlay borrows the current document/viewport only for one presentation call.
Root supplies specific current facts and reacts to explicit result tags; no
AppState or replacement service bundle crosses the boundary.

Cost: existing project-setting I/O and navigation/session construction on start;
bounded per-frame sampling plus up to six existing simulation ticks. Overlay
formatting retains the existing per-frame allocations; no performance change
is claimed. Renderer borrows are required by attachment/update APIs and do not
enter the simulation's indexed/span protocols. Simplify: reuse the existing
session and device owners, keep shell orchestration in its actual root adapters,
omit a generic session framework or new simulation buffer representation.

Done: feature state and lifecycle/render bodies take no AppState; existing
detached session/fixed-tick tests pass and headless production overlay tests
cover hidden/no-viewport, picker, failed placement escaping, active/debug states,
and the facing-camera yaw inputs. Graphics attachment failures and desktop
restoration remain unverified unless the existing graphics fixture can run.

## C09 result and checkpoint self-check

PlayPreviewState and the picker/restore, actor-setting resolution, spawn yaw,
overlay, arming, start/stop, pointer/navigation consumer, debug toggle and
fixed-step/render update bodies now live in play_preview_view/play_preview_runtime.
They accept owned state and specific existing dependencies, never AppState.
The shared overlay document loader moved to its actual HUD owner, client_metrics;
its markup/style is unchanged. Root adapters retain workspace/project/structural
eligibility and shell refresh/layout/focus/cursor orchestration. Stop still
removes visuals before session destruction; start failure retains placement,
attachment failure stops, and eligible zero-tick input remains pending.

Normal and combined-sanitizer executable/test builds passed without warnings.
All 55 preview-view, PC/runtime-input, detached-preview and Rml-template cases
passed (8,267 ms normal, 44,622 ms sanitized). Three new production tests cover
camera-facing yaw and actor resource policy, repeated picker/query restoration,
and the real shared overlay's hidden/current viewport, picker, escaped failed
placement, active and navigation-debug states. Existing preview tests include
rejected/retried starts, replacement doors, fixed batches, stop restoration and
100 lifecycle repetitions with stable engine counts. The six existing CLI,
preference and HUD-history cases also passed in both configurations. Leak
checking retains the documented baseline exclusion.

Simplification removed the overlay's dependency on the whole workspace viewport
request: it needs only an optional rectangle. Device connection ownership remains
separate; no session framework, copied simulation representation or catch-all
service bundle was introduced. Renderer borrows and true singleton lifecycle
exceptions are documented, and existing simulation/edit batches stay plural.
Formatting/diff checks passed. Graphics attachment failure/desktop restoration
remain unverified without the graphics/device fixture. Main is 12,138 lines;
C10–C20 remain outstanding.

## C10a plan: ordered forwarding seam

Frame: first replace the ambiguous dispatched_to_rml flag with explicit native
handling and forwarding state; move the existing coordinate, blur and Rml
forwarding adapters. Full native dispatch cannot acquire narrow feature APIs
until loading/shell/browser/workbench extraction is complete, so C10 is split:
C10a provides the actual ordering seam, C11–C16 establish owners, and C10b finishes
the pure route/map integration and native handlers. This avoids exporting AppState
or a replacement service bundle simply to relocate the large switch.

Observed data: one borrowed SDL event, current toolset/command context, native
handling disposition and at most one Rml forwarding obligation. Eight existing
forwarding sites include operation/form gates, visible palette dispatch, early
workspace mouse-up release and default dispatch. The SDL backend returns whether
Rml propagation continues; that boolean is not native handling. Mouse-down blurs
the previous variable input before reacquiring the next target. Root coordinates
currently pass through unchanged; SDK motion divides by window pixel density.
ASSUMPTION: pointer/event and DOM sizes are unmeasured — affects no optimization.

Transform: current finite event → existing native dispatch/blur → explicit
before/after-native forwarding phase → production RmlSDL adapter once → existing
post-forward slider/output obligations. Invalid pointer coordinates are rejected
before hit queries; invalid release explicitly cancels captured state. Callback
borrows last only one call and no old DOM target is reused by the blur adapter.
The forwarding singleton is one ordered event; its state is a flat value record.
Costs are bounded state checks plus existing SDK/DOM work on the desktop main
thread; preserving the pass-through/density paths adds no coordinate policy.

Simplify: use one forwarding function and preserve native-versus-SDK dispositions,
remove the local early-release duplicate flag, and reuse the real SDK backend.
Do not build a generic callback/handler hierarchy. Done: every existing SDK site
uses the same forwarding obligation; headless real production forwarding tests
verify one release, propagation semantics, native suppression and blur-driven
DOM replacement. A valid dummy SDL window is required for motion/density tests;
if unsupported, report the fixture gap rather than using nullptr as a substitute.
Action identity revalidation, full pure routes and map/owner consolidation remain
C10b gates and are not claimed complete by this seam.
