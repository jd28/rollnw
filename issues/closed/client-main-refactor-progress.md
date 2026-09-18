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
currently pass through unchanged; the inspected SDK multiplies motion by window
pixel density. Non-unit-density agreement remains a later integration check.
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

## C10a result and checkpoint self-check

Every production Rml forwarding site now uses one per-event disposition with
separate native handling, recipient and before/after-native phase. The adapter
records forwarding before invoking callbacks, preserves the SDK propagation
result, and rejects a second forwarding or an already consumed event. Coordinate
and variable-blur adapters moved without changing valid-coordinate policy.
Invalid numeric pointer events cancel root gestures, pending pointer input and
Rml capture before any hit query or unsafe integer conversion.

Normal and combined-sanitizer builds passed. All nine production forwarding,
validation, runtime-input and visible/focused classification cases passed: 29 ms
normal and 142 ms sanitized. Four tests used actual hidden SDL dummy windows;
none skipped. They cover stopped propagation independent of native handling,
early release before a DOM replacement, fresh targets after blur callbacks and
cancellation of a malformed release without a delayed click. Characterization
showed Rml emits a removal blur while replacing an already blurring focused
input; the fixture records that sequence and performs one replacement. Native
edit exactly-once behavior is still the C13 gate.

Simplification removed the ambiguous dispatched flag and redundant local release
flag, retaining one SDK call site. No handler hierarchy, service bundle or
performance claim was added. Raw numeric error policy and callback lifetimes are
explicit; non-unit density, native action identity revalidation, pure routing
and owner dispatch remain C10b integration gates. Formatting/diff checks passed.
Main is 12,126 lines. C10b and C11–C20 remain outstanding.

## C11 plan: loading state and presentation

Frame: give native browse results, the one subprocess import tracker, pending
project load and synchronous progress callback their actual loading owner. Root
retains command prompting, recent-project persistence and workspace refresh until
their C12 owners exist. Limit: no worker redesign or recursive event dispatch;
plan B is narrow root adapters for those cross-feature effects.

Observed inputs: one SDL user-event payload with owned path/error/cancellation,
one active native dialog command/default location, one import job with source
module/parent destination and module-generation snapshot, and one load request
with path, command source, initial-presentation flag and close-import flag. Most
frames have no completion/load; the common path returns immediately. Empty paths
are rejected, concurrent dialog/import starts are ignored as today, failed import
keeps the current runtime, and dirty/generation-changed/preview-active work blocks
auto-opening the imported project. A load waits for its first overlay presentation
before the synchronous backend call. Stage changes present immediately; repeated
stages present at most every 32 ms. These stable policies are copied unchanged.
ASSUMPTION: completion rates and stage durations are unmeasured — affects no policy.

Transform: native callback copies selection into one queued owned payload →
loading consumes/deletes payload exactly once → import selection or typed root
command request. Home target → loading-owned action/start → subprocess completion
→ status and auto-open request under current-work facts. Presented load request →
backend batch/module loading → synchronous stage callback → current overlay/render
→ command result; root resolves prompts before clearing the load and refreshing
workspace. Each protocol states ownership; DOM/window/renderer borrows last only
the synchronous call. Dialog/import/load are actual singletons in one desktop
application; underlying import/module transforms remain existing resource batches.

Cost on the desktop main thread: unchanged per-completion string/markup allocation,
subprocess import I/O, synchronous backend load and existing progress renders.
Specific renderer/context borrows are necessary for presenting during a blocking
load, not a new pointer-based simulation path. Simplify: one loading state, one
owned native-result consumption point and specific current-work facts; reuse
ProjectImportJob and backend/module progress APIs, omit a generic job/dialog bus.

Done: loading bodies/state take no AppState, callback pumping does not poll or
redispatch application events, and normal/sanitized native-result, progress markup,
import, browse-generation and synchronous project-load tests pass. Desktop dialogs,
graphics progress frames and late-callback shutdown remain explicit fixture/C19
gaps until their lifecycle integration is verified.

## C11 result and checkpoint self-check

LoadingViewState now owns native dialog command/location/payload routing, import
selection/job/generation/status and the pending project request. Native initiation,
selection processing, home controls/actions, import polling/completion policy,
overlay and synchronous progress presentation moved to loading_view/loading_runtime
without AppState. The root retains command prompt resolution, recent persistence
and workspace/shell effects. It still waits for the initial overlay presentation,
resolves prompts before clearing the load, and polls jobs before SDL dispatch.

Normal and combined-sanitizer builds passed. All 13 loading, real subprocess
import, browse-generation, synchronous kernel-progress and project replacement
cases passed: 4,825 ms normal and 26,311 ms sanitized, no skips. New production
cases verify callback-owned selection copying, one payload deletion, unrelated
and null event behavior, cancellation, stale directory generations, empty/duplicate
loads and dirty/generation/preview/dialog/busy-load completion gates. The actual
command_modals.rml overlay keeps its path/element while updating stage and clears
on completion; home controls escape inputs and reflect active-dialog state.
Dummy SDL video confines message-box calls to its unavailable presentation path.

Simplification removed the progress callback's AppState borrow and repeated
command-overlay visibility synchronization during an unchanged active blocking
load. Existing backend/import batches and singleton ownership are documented;
no job framework or performance claim was added. Source inspection confirms
SDL_PumpEvents without SDL_PollEvent/recursive application dispatch in progress.
Desktop dialogs, rendered progress frames and queued/late callback shutdown are
not verified here; the last remains C19. Formatting/diff checks passed. Main is
11,746 lines. C10b and C12–C20 remain outstanding.

## C12a plan: browser and workspace presentation

Frame: move the browser's project/recent/home-area rows, queries, hover/selection
and virtual render caches; extend workspace_view with the existing tab strips,
locked-prefix reorder geometry, home header and viewport request. Root keeps
content/workbench hydration until C13–C17, and shell layout until C12b. Plan B
is those small composition adapters, not a public AppState API.

Observed inputs: backend project tree and filtered area rows, bounded recent
history, contiguous workspace tabs/subtabs and one current DOM viewport. Tree
rows use 26 px and eight overscan rows; home cards use 190 px/two-row overscan
and one to four columns. Repeated unchanged windows skip markup replacement.
Filter forces expanded tree rows, module generation resets home query/window,
missing documents/elements return without work, tab scroll clamps to content,
locked leading tabs constrain reorder, and viewport rectangles clamp to frame
bounds and reject dimensions under eight pixels. Native numeric pointer rejection
remains the input boundary; existing authored resource validation stays backend.
ASSUMPTION: project/area/tab counts and UI frequency are unmeasured — affects no optimization.

Transform: backend tree → existing visible flat rows/indices → virtual window
→ escaped markup and browser highlight state; current tab batch → tab markup
→ existing DOM synchronization/reorder target; current home facts → header and
browser/load controls; current viewport DOM/frame bounds → owned resource/rect
request. Feature state owns vectors/strings/controllers; DOM, backend and tab
borrows last one synchronous call. These are one displayed browser/home/strip
and viewport; row/tab transforms remain plural and index based. DOM pointer
traversal is required by Rml's existing tree/hit APIs, not a new simulation path.

Cost: unchanged tree flatten/filter traversal on refresh, visible-window string
allocation/render and linear tab synchronization on the desktop main thread.
Simplify: retain existing render caches and tab-strip helper shared by its two
actual strips, pass current home/preview facts instead of their full owners,
and omit a browser/document framework. No performance change is claimed.

Done: moved state/bodies use narrow APIs without AppState; normal/sanitized tree
filter/window/highlight, tab close/reorder/locked-prefix, home and viewport layout
checks pass. Full native routing and shared content refresh remain C10b/C17 gates.

## C12a result and checkpoint self-check

BrowserViewState now owns recent/project/home-area rows, queries, selection,
virtual controllers and rendered windows. WorkspaceViewState owns tab scroll and
captured tab drag. Browser tree/filter/highlight/hit and home-area presentation,
workspace tab/subtab/icons/DOM synchronization/reorder geometry, home header and
owned viewport request moved into browser_view and the existing workspace_view.
Root retains shell layout and content/workbench composition; no AppState or
catch-all bundle crosses the new APIs. Home composition reuses its module-open
fact instead of reading it twice.

Normal and combined-sanitizer builds passed without warnings. All 35 browser,
workspace, actual project-tree and Rml-template cases passed: 2,653 ms normal and
12,510 ms sanitized, no skips. Four new headless production cases cover real
DockerDemo tree filtering, forced expansion and actor-picker title, unchanged
home/tree windows, generation/query reset, empty areas, frame-clamped/minimum
viewport geometry, locked-prefix reorder/close synchronization and hidden hits.
The new span-loaded font fixture initially freed its borrowed bytes after setup;
the sanitizer exposed the SDK read and the final fixture retains them through
Rml shutdown. That corrected fixture passed the same full focused matrix.

The fourth case characterizes an existing selection defect: replacing rows keeps
the selected index but the equality guard skips painting its new DOM highlight.
It is documented in issues/closed/client-browser-selection-rebuild.md and intentionally
preserved in this move. A failing desired-behavior regression and separate fix
must follow before the shell extraction. Simplification retains existing caches
and shared strip helper, removes root-state borrows and avoids a browser/document
framework. Row/tab paths remain contiguous/index based, UI singleton/DOM borrow
exceptions are documented, and no performance claim was made. Formatting/diff
checks passed. Main is 10,700 lines. C10b, C12b and C13–C20 remain outstanding.

## Browser selection repair plan

Tier 1: input is the retained selected row index and a newly replaced visible DOM
row batch; output must include the matching selected CSS class. Existing equality
suppression mistakes unchanged data for unchanged presentation. The production
characterization confirms the defect; the desired regression changes that final
assertion to require the highlight. Remove the selected-index guard, keeping the
already cached virtual-window early return and existing hidden/missing/range
behavior. Cost is traversal of visible DOM rows on explicit selection presentation
on the desktop main thread; no speedup is claimed. This removes a branch/invalid
presentation state without adding a force parameter or another cache. Done:
the desired regression fails first, then passes with focused normal/sanitized
browser/workspace/template checks; no feature extraction is mixed into this fix.

## Browser selection repair result and self-check

The desired production regression failed on the missing selected class before
implementation. Removing the equality guard now paints the current DOM row batch
while retaining the selected index. Unchanged virtual windows still skip work.
All 35 focused browser/workspace/project-tree/template cases passed again, with
no skips: 2,479 ms normal and 13,079 ms combined sanitizer; both builds passed.
The local defect issue is marked repaired. This adds no state, force option,
framework or unmeasured performance claim and keeps explicit missing-document
and index presentation behavior. Formatting/diff checks passed; shell extraction
may now proceed independently of this small behavior repair.

## C12b plan: shell presentation and preferences history

Frame: move dock layout/resize, output selection/filter/scroll, terminal rendering
and completion to shell_view; relocate recent-history persistence into the
existing preferences module. Keep command dispatch and active-content composition
in root until their owner/routing checkpoints. Limit: preserve valid-input UI
behavior and the existing shell controller; plan B is narrow root adapters for
preference writes and cross-feature visibility.

Observed inputs: one desktop's DockLayout and output/terminal row batches, one
selection byte range, filter text and deferred scroll action, one captured dock
resize, current preview layout flags and one UI preference file/history. Default
window dimensions are 1280×720 when querying fails, reserve is 96 px, sizes clamp
to actual pane/frame constraints. New output preserves selection offsets only
when old text remains a prefix; changed filters clear selection. UTF-8 hit mapping
returns byte offsets, missing rows return no hit, hidden output defers scrolling,
and one grave-toggle TEXT_INPUT is suppressed as before. Logs, filters, selection
and capture are volatile; pane constraints/DOM IDs and preference schema are stable.
ASSUMPTION: output lengths/change rates and actual display density are unmeasured
— affects no optimization or input policy.

Transform: shell rows/filter → visible flattened text/byte offsets → escaped
markup/highlight → existing deferred follow-tail observation after layout;
current window/pane/preview facts → layout; primary resize → owned scalar drag
state → clamped pane dimensions → release/persist. Terminal input/cursor → existing
backend completion → text/cursor/candidate rows. Recent path → canonicalized
bounded preference history → existing JSON write. State owns text/scalars/path;
DOM/context/window/backend borrows are synchronous. One displayed dock/output/
terminal and preference file are true singletons; row/history transforms stay
plural and contiguous. Pointer traversal/string-width calls use Rml's actual APIs.

Cost: unchanged output/terminal traversal and string-width/markup work, bounded
dock math and filesystem canonicalization/history write on the main thread.
Simplify: reuse ShellController, existing completion/preferences, and current
preview flags instead of session ownership; leave the current duplicated terminal
refresh in the mechanical move for the later shared-refresh pass. No generic
widget/persistence framework or performance claim. Done: normal/sanitized shell,
layout, output selection/scroll, UTF-8 completion and preference cases pass with
no AppState in moved bodies. Native dispatch remains C10b; startup cleanup C19.

## C12b result and checkpoint self-check

ShellViewState now owns dock resize scalars, selection text/byte offsets,
deferred scroll, terminal-toggle suppression and the preferences path. Dock,
output and terminal presentation/completion moved into shell_view with explicit
controller/document/window/backend borrows and current preview layout facts.
Root persists history on the same resize release edge. Recent-project history
canonicalization/deduplication/bounding moved to client_preferences unchanged.
No AppState crosses these APIs and the existing ShellController remains owner
of shell rows and policy. Singleton exceptions and row/metadata ranges are
documented; DOM pointers are required by the SDK's hit/font APIs.

Normal and combined-sanitizer builds passed. All 19 cases in eight suites passed,
108 ms normal and 430 ms sanitized, no skips. Six new production cases cover pane
clamps and captured resize scalars, preview chrome restoration, append-preserved
selection/filter reset/selection clamping, hidden deferred scrolling, real UTF-8
glyph hit byte offsets and malformed row metadata, completion preserving Unicode
arguments/rejecting selected text, one-shot grave suppression, and canonicalized
bounded history persisted/read back. Font bytes remain live through SDK shutdown
and aliases are registered before loading the document. Tests acquire no desktop
capture; real display scaling/capture remain integration gates. Leak checking
remains excluded for the recorded VM baseline.

Simplification retains controller/completion/persistence implementations, uses
current preview facts, and removes root feature bodies without a framework or
performance claim. The intentionally duplicated terminal refresh remains for
C17. Formatting/diff checks passed. Main is 10,129 lines. C10b, resource/dialog
content composition and C13–C20 remain outstanding.

## C13 plan: base workbench presentation

Frame: move details/variables snapshots, virtual windows, tooltip/combobox,
active-tab identity and pending change/blur state to the workbench owner. Keep
the three existing pure workbench policies in rollnw_client_core; their SDL/Rml
presentation pair uses the executable/test Rml guard. Root retains cross-feature
activation orchestration until C17. No engine/rule implementation is replaced.

Observed data: one active tab/object and surface, details/variable row batches
produced by existing SmallS/snapshot functions, two cached visible virtual ranges,
sound-position options 0–2, slider projection 0–10/storage 0–127, UTF-8 variable
text and pending numeric prefixes. Invalid/missing row, non-ready snapshot or
wrong active tab rejects controls; hidden/inactive surfaces skip presentation.
Rows and selected object change on mutation/activation; metadata, row heights
(30/34 px), overscan (8) and DOM IDs are stable. Existing listener suppresses its
own synchronous Enter-triggered blur and coalesces sound changes until release.
The pending slider currently has no owner; characterize that defect in this move,
then require a failing identity regression and separate fix before continuing.
ASSUMPTION: row volumes/typing rates are unmeasured — affects no cache/optimization.

Transform: current workspace/base state → active identity predicates; existing
runtime/identity → owned snapshots → visible row batch/markup; change/blur event
and freshly supplied command context → existing backend command/result batch.
Document/context borrows last one synchronous call; no AppState crosses the
feature boundary. Active workbench/tooltip/edit gesture are true singletons;
rows and command results use existing plural paths. Rml DOM pointers and validated
engine ObjectHandles retain their SDK/engine identity contracts at cold boundaries.

Cost: unchanged SmallS snapshot generation, visible-window strings and DOM/font
traversal on the main thread; backend still owns edits/undo. Simplify: reuse
existing adapters/lists and pass current context instead of preview/root owners;
leave child clearing/hydration orchestration for its extraction checkpoints.
Done: narrow feature APIs compile and normal/sanitized details/variables/window,
Enter/blur, numeric-prefix and pending-slider characterization checks pass. No
performance claim or generic workbench/widget dispatch framework is introduced.

## C13 result and checkpoint self-check

ObjectWorkbenchViewState now owns details/variables snapshots, virtual lists,
popup/tooltip, active-tab/surface/strip state, blur suppression and the pending
sound gesture. object_workbench_view contains the real row adapters, snapshot
and window/popup presentation and change handling. The three existing pure
workbench policies remain in rollnw_client_core. Root's listener only supplies
current command context and feature dependencies; cross-feature child clearing
and activation remain root orchestration for C14–C17. No AppState enters the
feature API or implementation.

Both builds passed without warnings. The initial focused matrix passed all 48
cases in four suites (15,142 ms normal; 83,074 ms combined sanitizer). After adding
the actual UTF-8 variable-control/window case, all five new workbench cases passed
(2,682 ms normal; 14,889 ms sanitized). These checks cover existing property-tree/
variable edits/templates plus cached/stale details and variables windows,
Enter-triggered reentrant blur committing exactly once, numeric prefix restoration
and the pending sound defect. UTF-8 form values are checked on the actual controls,
not against the SDK serializer's choice of attribute escaping.

The baseline characterization confirms a first sound's pending slider changes a
replacement sound and records undo in its replacement tab. This move preserves
that behavior; issues/closed/client-sound-slider-owner.md records the concrete regression
and required separate repair. The slider's unchecked numeric conversion is part
of that repair, not a verified input boundary in this move. Other row/tab/range
checks, singleton/DOM/engine identity exceptions and lifetime contracts are
documented. Simplification removes root bodies and reuses existing snapshot/list/
backend edit paths without speculative parameters or performance claims.
Formatting/diff checks passed. Main is 9,281 lines. C10b, shared resource/dialog
composition and C14–C20 remain outstanding; repair the sound gesture next.

## Sound gesture repair plan

Tier 1: input is a sequence of sound-volume changes and the original object,
tab/module identity and row/current value; output is at most one existing edit
for that same still-current target. The production characterization demonstrates
cross-object/cross-tab corruption. Capture identity in pending state, cancel it
when clearing/replacing the workbench, and revalidate active object/tab/module,
ready row/editor/current value before backend dispatch. Reject invalid slider
floats before rounding and discard pending state so they cannot replay an edge.
Keep common valid drags coalesced into one edit and existing undo record.

Cost: one owned tab string plus handle/generation in this single cold gesture;
bounded validation at staging/commit on the main thread. No throughput claim.
Simplify: use existing handle, module generation, UI selection host and backend
validation; no transaction queue, new identity registry or alternative edit path.
Done: the desired replacement-object regression fails before implementation,
then normal/sanitized replacement/tab, valid coalescing/undo, stale row, clearing
and non-finite/out-of-range value checks pass with existing workbench cases.

## Sound gesture repair result and self-check

The desired production regression failed first: the replacement sound's volume
became 89 instead of 0 and its tab gained an undo entry. Pending edits now own
object/tab/module identity. Staging/commit validate current selection and context,
ready row/editor/current value; clearing/replacement cancels pending state.
Non-finite, out-of-range and fractional slider values discard pending state
before unsafe conversion or command dispatch. Valid changes still coalesce into
one existing backend edit; undo restores the exact previous volume.

Both builds passed without warnings. All 11 focused cases passed, 5,568 ms normal
and 31,583 ms sanitized, no skips. Added coverage includes multiple changes/one
commit, actual undo, stale snapshot row, backend selection changing before snapshot
refresh, same-object tab changes, clearing, NaN/infinity/extreme/range/fractional
values and replacement objects. The local defect issue is marked repaired.
Module-generation validation is present/reviewed; actual reload during a live
desktop slider gesture and pointer capture were not exercised. Simplification
retains existing identities/backend batches, moves pending ownership on commit,
and introduces no queue/registry/alternative edit path or performance claim.
Formatting/diff checks passed; creature presentation extraction can proceed.

## C14 plan: creature classes, feats and spells

Frame: move the creature presentation snapshots, queries/filters, virtual windows,
class/spell markup and selector state to creature_workbench_view. Existing SmallS
providers and backend commands retain rules/edits. Root hydrates the overall
workbench until inventory/appearance owners are extracted; no shared controller
or alternative creature editor is introduced.

Observed inputs: SmallS-owned class, feat and spell batches copied into snapshots;
feat rule IDs/assigned flags, spell IDs/levels/known-or-uses, class/metamagic choices,
UTF-8 text slices, current query, level -1 (All) or 0–9 and one displayed popup.
Rows use stable dense match indices; invalid handles/provider data retain their
existing diagnostics. Invalid level/class/metamagic choice rejects the selection;
missing/stale active tab returns waiting/unavailable. List height is 30 px with
8 overscan rows. Snapshots change on activation/mutation/filter choice; queries,
scroll and popup are volatile. Existing provider fixtures supply actual creatures;
total row distributions and UI event rates remain unmeasured.
ASSUMPTION: no performance requirement beyond preserved behavior — no optimization.

Transform: existing runtime/object → owned class/feat/spell snapshots; query/level
→ dense spell match indices; current workbench target facts → identity checks;
visible ranges/choices → escaped markup and shared combobox updates. A small
in-process target protocol carries object, surface, matching-active-tab and area
header facts rather than the base workbench's row arrays/root state. It contains
no owning resource or retained DOM pointer and is captured anew for each call.
One displayed workbench/popup are singleton exceptions; provider/list/filter
paths operate on existing contiguous batches. SDK DOM borrows are synchronous.

Cost: unchanged provider work/filter traversal and visible-window strings/DOM
on the main thread, plus bounded scalar target scratch. Simplify: use existing
providers/controls and cached windows; no registry, rule ownership, callbacks or
new generality. Done: no AppState in the feature, normal/sanitized real creature
providers, classes/filters, cached windows, stale-tab and SmallS/Rml suites pass.

## C14 result and self-check

Creature classes, feat/spell snapshots, queries/match indices, filter popup and
visible-window caches now belong to creature_workbench_view. Target facts are
copied per synchronous call through the small in-process ObjectWorkbenchTarget
protocol; no AppState or DOM/resource ownership crosses the interface. Existing
SmallS providers/backend edits retain rule ownership. Root retains the overall
multi-surface hydration/activation sequencing until C17.

The initial 42-case matrix passed (11,012 ms normal, 64,435 ms sanitized). Extending
the popup fixture exposed an independent vendored RmlUi scrollbar destruction
use-after-free. A plain-document regression failed under ASan before the separate
repair; its contract/result are in client-rml-scrollbar-lifetime.md. Afterward,
the final 51-case matrix passed (11,176 ms normal, 62,408 ms sanitized, no skips),
including actual wizard classes/feats/spells, valid/invalid level/class filters,
stale tabs, cached row/popup windows, expression bindings, templates and eight
managed-list cases. Both client/test builds passed. A full SDK rebuild emitted
nine warnings from unchanged color-parser code; moved client code added none.

Simplification reuses providers, dense indices and shared cached controls; no
new rule model, registry or callback framework. Existing diagnostics/range
rejection and singleton/borrow contracts are documented. No performance result
is claimed. Formatting/diff and feature dependency checks passed. Main is 8,647
lines. Live desktop filter feel and unavailable graphics remain unverified;
C10b, resource/dialog composition and C15–C20 remain outstanding.

## C15 plan: inventory, equipment and icon ownership

Frame/data: move the one displayed Creature/Item/Placeable inventory's validated
snapshot, selected dense source index/page, render cache and generated item icons
together. Existing SmallS provider owns grid/footprint validation; item/backend
operations remain authoritative. Actual rows contain uint8 page/row/column/size,
18 fixed equipment entries for creatures, UTF-8 text slices and generated image
sources. Repository DockerDemo/pl_agent/item fixtures provide real rows/icons;
row distributions and live event rates are unmeasured. Common ready inventories
render the current page's rows and fixed equipment; invalid provider data renders
its diagnostic and stale tabs render waiting.

Contracts: provider/object/cache → owned rows/text/images; current target facts
and page/selection → escaped grid/equipment markup; markup → current SDK DOM.
Text slices last until rebuild. Renderer borrows the stable texture collection
until unbound before owner destruction. Rebuild clamps bad pages to 0 and drops
bad selection to -1; provider rejects invalid footprints/handles. Clearing rows
retains the existing icon cache. Resource generation continues invalidating icons
through the provider. One displayed page/selection are singleton exceptions;
providers/images/rows remain batches. DOM borrows last one synchronous call.

Platform/cost: same main-thread SDL/Rml/Vulkan client, 32 px cells, unchanged
provider/icon work plus linear traversal of inventory rows for page selection;
no copied texture collection or new registry. Generic managed lists are used by
sound/spawns/store/item properties and remain shared workbench state until C17.
ASSUMPTION: no optimization requirement — preserve access/order and report no speedup.
Simplify: reuse provider, grid markup and generated textures; no alternative
inventory model or separate creature/item implementation. Done: narrow APIs,
normal/sanitized real grid/equipment/cache/selection, invalid provider, shared
list/model and resource-drop checks pass; root bindings retain stable addresses.

## C15 result and self-check

Inventory/equipment rows, page/source-index selection, render cache and generated
item textures now belong to inventory_workbench_view. Root texture binding and
resource-drag adapters reference that owner's stable collection/snapshot; no
texture batch was copied. Generic managed lists remain shared across workbench
surfaces. SmallS retains layout, footprint and icon materialization policy.

All 38 focused cases passed, 5,532 ms normal and 30,770 ms ASan/UBSan, no skips.
New production-view fixtures exercised real creature equipment (18 DOM slots),
selected current-page rows, cached windows, valid selection retained on rebuild,
invalid page/selection reset, stale tabs, item/placeable grids and rejected 11-cell
footprints. Repository icon layers produced actual generated sources/textures;
the collection address/count remained stable through rebuild and row clearing.
Existing provider, item-model/template, managed-list and resource-drop/undo cases
passed. Both builds passed without warnings. The final incremental normal build
was measured at 12.273 s after correcting an explicit invalid-handle fixture
argument; this is a retry measurement, not a before/after speedup claim.

Simplification reuses validated rows, fixed equipment tables, current page
rendering and the existing icon cache. No alternative editor/model/registry was
added. Singleton, SDK borrow and generated-texture lifetime exceptions/range
policies are documented; no AppState enters the feature API. Formatting/diff and
dependency checks passed; main is 8,348 lines. Actual Vulkan texture upload,
desktop grid drag feel and shutdown ordering remain integration/manual coverage
gaps. C10b, resource/dialog composition and C16–C20 remain outstanding.

## C16 plan: appearance, color and sound selector ownership

Frame/data: move the current selector's five existing appearance catalogs, sound
catalog, dense matches, query/window/scroll caches, color channel and body-preview
identity. Existing catalog builders, SmallS visual providers/backend commands
retain rules and edits. Actual appearance rows contain int32 rule ID/model type
and owned labels/model/search text; sound rows contain WAV resrefs/names. Invalid
catalog rows are dropped by providers, unavailable catalogs retain diagnostics.
Creature colors use the existing 16×11 palette and 24 px input cells; supported
skin/hair palettes and channel/value bounds are checked before opening. Real
DockerDemo catalogs and actual creature/door/placeable/sound fixtures are available;
distributions and pointer rates remain unmeasured.

Transform/contracts: current module/resource generations invalidate existing
catalogs; object/selector field/query → owned catalog and dense indices; current
workbench target/ready-detail facts → stale-tab checks; current ranges/colors →
escaped SDK markup; current selection → existing backend edit; body-preview
transition restores the old creature before hiding equipment on the new creature.
Borrowed engine identities are validated by existing operations; DOM lasts one
synchronous call. Unavailable/stale selectors return false or diagnostic markup;
invalid color/palette/field parsing rejects. Preview failures preserve the existing
retry state. One displayed selector/preview are singleton exceptions; catalog,
filter and visual-row work remains batches.

Platform/cost: unchanged main-thread provider/filter/window work, owned strings,
existing five catalogs and current preview identity. Renderer-dependent preview
attachment stays in a separate runtime translation unit, keeping headless provider
and presentation checks independent. A current ready-details fact is added to
ObjectWorkbenchTarget because preview policy actually reads it; no row arrays or
whole base/root owner crosses that boundary.
ASSUMPTION: no optimization requirement — no measured performance claim.
Simplify: reuse existing builders/list controls, catalog generations and backend
edits; no generic selector framework. Done: no AppState dependency, normal/sanitized
catalog/window/color/stale-tab/sound/combobox/body-provider checks pass; old/new
preview restoration order is preserved and graphics gaps reported.

## C16 result and self-check

Appearance/color/sound selector state and generations now belong to appearance_view.
Current module/resource generations and workbench target/ready-detail facts are
passed explicitly; no AppState/backend borrow is retained. The pure SmallS preview
row helper is headless-testable; renderer attachment/restoration remains in the
separate appearance_view_runtime translation unit with the original ordering.

All 42 focused cases passed: 12,816 ms normal and 62,232 ms ASan/UBSan, no skips.
The extended body-preview assertion was rerun normally (984 ms) and is included
in the sanitizer matrix. New fixtures exercise real catalog query/window caching,
injected next-generation invalidation, field parsing, missing color channels,
actual color/sound backend edits and undo, invalid sound indices and stale tabs.
Body preview actually changes visual rows while hiding equipment and restores
models, attachment/kind/slot/part/flags and palette colors. Existing catalog,
combobox, appearance/body/color/accessory/sound edit and Rml template suites passed.
Both builds passed without warnings; dependency/format/diff checks passed.

Simplification removes unused state borrows from color rendering and desired-preview
selection, reuses existing catalogs/providers/backend batches and adds no selector
framework. Range/diagnostic/stale policies and singleton/borrow lifetimes are
explicit. No measured performance result is claimed. Actual project reload during
an open selector and renderer transitions are not exercised by the injected-generation
or SmallS provider fixtures; desktop selector/preview feel remains unverified.
Main is 7,396 lines. C10b, shared resource/dialog/content/workbench composition and
C17–C20 remain outstanding.

## C17a plan: resource and dialog content boundaries

Tier 1 within the Tier 2 refactor. Inputs are the active tab's kind/detail/ID,
current project path, owned ResourceDocument property/diagnostic batches and one
cached DialogDocumentSnapshot. Output is the existing resource inspector markup
or the current dialog snapshot/view. Resource paths come from actual project tabs;
unknown tab kinds/empty project or detail return unavailable, and existing loaders
reject escapes/missing/invalid files with diagnostics. Dialog cache identity is
exact tab ID/source path/nonempty status; repeated current identity preserves
selection, switching away clears it. Real Agent JSON and alue_ranger GFF fixtures
are available; row distributions are unmeasured.

Move resource load/markup into existing workspace_view and dialog acquisition
into existing dialog_view, accepting only project path/current tab. DOM borrows
last one call, text slices until document replacement. The displayed content is a
singleton; property/diagnostic/dialog rows remain contiguous batches. Cost stays
existing synchronous file parsing, row traversal and markup on the main thread.
Simplify by sharing the already-present escaping helper and removing inspector
compact mode: its only real caller always supplies false. No new cache/general
content registry. Done: production resource/dialog selection/cache/path rejection
and existing document/workspace/template suites pass normally and under ASan/UBSan.
C17b then consolidates shared content rendering and workbench activation while
retaining the observed content-only/full-view scroll and popup differences.

## C17a result and self-check

Resource-document acquisition/inspector markup now belongs to existing workspace_view;
current dialog acquisition/cache identity belongs to existing dialog_view. APIs
accept only the current project path/tab and owned document batches. Inspector
compact mode was removed because its only caller always selected ordinary mode.
The existing workspace escaping helper is reused; no new cache/registry was added.

Both builds passed without warnings. Normal checks verified the 35 existing
resource/dialog/browser/workspace/template cases plus the final new content case
(488 ms). The initial new fixture referred to a nonexistent outside file, which
correctly returned a missing-file diagnostic; creating an actual outside file
made the path-rejection assertion faithful to the existing loader contract.
The final combined ASan/UBSan matrix passed all 36 cases in 11,569 ms, no skips.
Actual Agent JSON inspector markup, ranger GFF acquisition, same-source retained
selection/storage, cached dialog windows, missing-source diagnostics and tab-away
clearing were exercised. SDK/engine/lifetime and singleton/batch exceptions and
unavailable/range behavior are documented. Formatting/diff checks passed; main
is 7,266 lines. No performance/desktop integration claim; C17b shared refresh,
C10b input ownership and C18–C20 remain outstanding.

## C17b plan: workbench composition and shared content refresh

Tier 2 continuation. Actual inputs are four existing workbench presentation owners,
current workspace/tab, current project/module summary, and validated object rows.
Output is one active workbench's markup/hydration and existing snapshot refreshes.
One of creature/item/door/placeable templates exists for the displayed object;
generic workbenches contain details/variables/haks/spawn/sound/store rows. Unknown
objects keep existing unavailable/diagnostic output. Objects/resources are stable
between generations; tab/focus/query/page/selection are volatile. Live event and
mutation distributions are unmeasured.
ASSUMPTION: repeated same-object refresh is common — affects preservation tests,
not a speculative cache or performance claim.

Own creature/inventory/appearance states and common managed-list render cache in
ObjectWorkbenchViewState; remove their separate root owners. Move typed hydration
and generic markup to that coordinator, preserving child provider ownership and
stable generated-texture addresses. Area placed-object list remains separately
scoped. Two actual activation paths share the original snapshot/scroll/page sequence;
same-object mutation retains feat/spell queries, class/metamagic, page/selection
where baseline does. Activation also retains existing creature queries: observed
code resets scroll/page but does not clear queries when switching creatures.
Do not merge structural/spatial/visual renderer refresh into activation.

Plan conflict: its inventory-only managed render-cache ownership contradicts the
observed single pass over sound/spawns/store/item-property lists. Common workbench
ownership costs one existing cache plus the existing host borrow. Partitioning
would add several caches, ID partition work and new reset cases. Use the smallest
change: keep one cache in the shared workbench owner; reorder remains input-owned.

Cost on the main-thread desktop platform: unchanged row/provider/string work;
existing child data moves ownership without duplication. Shared content rendering
removes one duplicate body. A two-case refresh intent retains observed differences:
content-only saves/restores appearance scroll and invalidates the spell popup;
full view updates tabs and omits those operations. One displayed workbench/content
are singleton exceptions; internal rows/filter/provider edits remain batches.
SDK DOM borrows are synchronous and reacquired by ID as before.

Simplify: one owner, one activation sequence, one content body, hydration only for
the actual displayed type; no callback framework or new general services bundle.
Done: normal/sanitized activation/clear, mutation filter/selection retention,
generic/typed markup, shared-control and resource-drop checks pass; dependency,
ordering and source audits confirm one content body and distinct renderer paths.
Plan B: smaller ownership/hydration move if the common API requires unrelated root
state. Frame integration/manual graphics gaps stay explicit until C20.

## C17b result and self-check

ObjectWorkbenchViewState owns the three child presentation states and one common
managed-list cache. Typed hydration and generic markup use those owners plus the
current Workspace/Backend; feature headers expose no AppState. Stable texture
cache addresses remain bound through the same application lifetime. Both object
activation sites use one production activation transform; mutation refresh is
distinct and rejects a different object. The content-only/full-view wrappers
share one content body with their original appearance-scroll and spell-popup
differences. Spatial/visual/structural renderer branches remain separate.

Simplification removed unused root wrappers, repeated feat/spell/inventory clear
calls after common clear, and terminal refresh/focus immediately after bottom-dock
refresh already performs them. Hydration selects the one displayed object type.
No new callback framework, configurable refresh policy, or duplicate cache. The
one displayed workbench/content and SDK DOM/engine borrows retain the documented
singleton/external-library exceptions; providers and list rows remain batches.

Final normal and ASan/UBSan client/test builds passed without warnings. The 48-case
production workbench/appearance/inventory/template/managed-list/resource-drop/content
matrix passed normally in 16,309 ms and under combined sanitizers in 85,743 ms, no
skips. Two new cases exercise activation versus mutation query/filter retention,
stale-object rejection, selector/page resets, clear, typed hydration and generic
sound-selector composition. Initial compile retries corrected extraction call
arguments and a new fixture's inventory member name; final builds/checks passed.

Source/order audit confirms one shared content body and distinct renderer mutation
partitions. Actual root frame refresh counts, Vulkan texture upload and desktop
control feel remain unverified here; C20 integration remains required. Main is
6,540 lines (7,266 before this checkpoint). Formatting/diff/dependency checks passed.
No performance gain or clean LeakSanitizer run is claimed. C10b and C18–C20 remain
outstanding; the full issue is still in progress.

## Dock captured-resize repair plan

Tier 1, separate behavior repair discovered during C10b numeric-boundary audit.
Actual input is one captured left/bottom resize's finite start coordinate, integer
start size and current SDL motion. Each coordinate can be int-representable while
their difference exceeds int range. Output is the existing constrained dock size;
nonpositive requested sizes keep the baseline no-op policy. Hardware/platform is
x86-64 desktop SDL/Rml on the UI thread; long is wider than int here, and narrowing
a large lround delta before size addition can wrap or cause signed overflow.
Sizes are bounded by existing dock/window limits; capture/motion is volatile.
ASSUMPTION: ordinary small deltas dominate — affects preserving their exact rounding,
not an optimization claim.

Characterize with the production Rml shell fixture and captured scalar state,
including opposite ±2-billion coordinates and a representable coordinate whose
addition overflows int. Then compute and round in double, cap positive requests
before integer conversion, and cancel nonfinite captured inputs before conversion.
No new resize state/option. Cost is two fixed scalar operations per active resize,
no allocation; existing clamps/DOM updates remain. Singleton pointer capture is
intentional. Simplify by sharing the two directions' bounded conversion; do not
change ordinary/nonpositive resize policy. Done: the regression fails first, then
normal/sanitized shell cases and client builds pass with explicit numeric policy.

## Dock captured-resize repair result and self-check

The new production shell regression failed before the repair: opposite large
coordinates left left/bottom sizes at 360/240 instead of their existing 640/624
limits; NaN retained capture. Active resize now rounds the delta in double, adds
the integer start size there, and bounds positive requests before narrowing.
Nonfinite capture/motion cancels the corresponding resize and preserves size.
Nonpositive requests remain no-ops; the regression verifies negative half-pixel
rounding and that policy. Rml measured initial sizes also validate/bound before
integer conversion, using existing fallback for unavailable/nonfinite sizes.

Both final client/test builds passed without warnings. All 13 shell view/input,
controller and preference cases passed normally in 61 ms and under combined
ASan/UBSan in 256 ms, no skips. Diff/format checks passed; no new state/option,
allocation or performance claim. The singleton captured pointer and existing
SDK/DOM borrows remain; bounded arithmetic removes the unsafe intermediate.
Actual desktop mouse capture and deliberately corrupted SDK layout sizes were
not exercised. This independent repair does not complete C10b routing.

## C10b1 plan: flat routes and shared UI eligibility

Tier 2 continuation, bounded checkpoint before moving the remaining native actions.
Actual inputs are ordered SDL events and freshly captured visible command UI/text
focus, normalized editor/PC map, current capture, and world-session availability.
Output is a flat native/UI recipient, forwarding phase/reason and PC held-source
eligibility. The three contexts/window remain main-thread singletons; SDK callbacks
can replace markup or change ownership, so capture is per event/call, never cached
for a frame. Default Rml recipient and held-sampling gates currently repeat UI
policy. Pointer values must pass the existing raw validator before UI queries.
Known tags/edges are bounded enums; invalid tags, stale world targets and unequal
spans clear/reject outputs and cannot fall through to editor.
ASSUMPTION: pointer motion dominates event volume — affects flat allocation-free
rows, not a latency claim; actual rates/entropy remain unmeasured.

Use a schema-1 equal-span O(N) pure transform in client core, with no SDL/Rml/DOM
strings/engine identities in rows. Role normalization maps player/DM to the same
PC map; F9 remains editor lifecycle and supplies its current map facts. Add a
production SDL/UI capture adapter and share visible UI/focus/capture facts with
held sampling. Initially replace modal/default forwarding selection and the held
source policy, leaving native feature handlers and exact release ordering intact
for the next independently buildable checkpoints. No new player/DM startup.

Cost on desktop main thread: linear fixed-size rows and O(1) scratch, no allocation;
SDK visibility/focus queries retain library cost. External DOM/window pointers
are synchronous SDK borrows, not a replacement indexed DOM. The one held sample
is a batch of size one over the same pure transform. Simplify by one UI capture
policy, one role-independent PC recipient and one enum protocol; no registry,
replay queue, keybinding table or configurable policy.

Done for this checkpoint: actual production capture/resolve/SDK forwarding tests,
unknown/mismatched/stale batch rejection, visible/hidden UI and focus eligibility
pass normally/sanitized; renderer-off core tests compile/pass. Native mouse-up
identity repair/delegation and complete authority remain C10b2+; no claim that
adding a lookup alone completes input routing. Preserve identity DPI conversion
and unit-density fixture; nonunit DPI stays manual/unverified.

## C10b1 result and self-check

Schema-1 routing is an equal-span core transform with bounded enums/flags, explicit
invalid/unavailable dispositions and no SDL/Rml/kernel/string/pointer row members.
Actual x86-64 row sizes are 12 input bytes and 7 output bytes (test XML properties).
Unknown tags/edge combinations and stale world facts select no editor fallback;
mismatched spans clear previous output. Player/DM normalize to the same PC map,
while editor/F9 lifecycle remains separately owned. PC source eligibility has one
small shared data header, avoiding navigation/kernel includes in the route header.

The production SDL adapter validates raw pointer input first, then captures current
visible UI and text focus. Held sampling uses the same UI capture and count-one
route transform. Default forwarding now resolves its obligation there as well;
it captures only palette facts it needs, avoiding an added toolset hit/focus
traversal. Exclusive command handling still precedes default platform/SDK-only
forwarding (touch and text-editing candidates retain their default path). Native
feature dispatch remains transitional and is explicitly not claimed consolidated.

Both final client/test builds passed without warnings. All 22 input/route/PC/runtime
sampling and relevant actual-template cases passed normally in 97 ms and under
ASan/UBSan in 359 ms, no skips. Renderer-off core/test build passed; all 10 pure
route/PC cases passed (0 ms reported by harness). Two new SDK cases exercise actual
visible/hidden production palette markup, capture→resolve→one SDK call, fresh
visible text focus, capture ownership for both PC roles and real runtime pending
edge/delta discard without replay. Three new pure cases cover bounded batch
recipients, forwarding phases, role/source equivalence and malformed/stale data.
The initial palette fixture lacked its relative panel.rcss; using the filesystem
fixture's UI directory and persistent real font bytes corrected that fixture.

Simplification: one pure role-independent PC route/source protocol and one UI
capture policy, no registry/replay/keybinding framework; shared eligibility data
removes an unnecessary heavyweight include. Borrowed SDK contexts/windows/DOM
remain synchronous external-library singleton exceptions. No performance gain,
nonunit-DPI agreement or physical-controller behavior is claimed. Formatting,
diff and pure-header dependency checks passed. C10b2+ native actions/identity and
C18–C20 remain required for whole-issue completion.

## C10b2 release-borrow repair plan

Tier 1 repair within the Tier 2 extraction. Patterns: main.cpp:4893 reads a tile
row's data-key after synchronous SDK mouse-up; the same switch reads scroll DOM
and creature row iterators afterward. client_input.cpp already owns once-only
SDK forwarding; existing decimal parsers use from_chars. No existing helper
captures a native row key around that forwarding boundary.

Actual input is one primary mouse release, its current DOM row and mutable owned
feature rows. Output is the captured bounded key and existing native action.
The SDL/Rml desktop UI thread can replace markup synchronously; ordinary keys
are signed decimal row indices, with negative/invalid/overflow keys rejected by
the existing range policy. DOM is stable only until dispatch, keys only until
their feature identity changes. ASSUMPTION: ordinary unchanged-owner clicks
dominate — affects keeping their path simple, not a performance claim.

Extend client_input.hpp/.cpp with the exact once-only row-release seam and wire
the tile branch to it; add an actual SDK replacement regression first. Repair by
parsing before dispatch, copying scroll disabled/direction and creature command
values before release, and reacquiring the sound field by owned ID afterward.
No retained DOM/row borrows, new event queue or configurable attribute API.
The event is a true singleton synchronous SDK operation; downstream edits remain
existing batches. Cost: fixed scalar copies plus the existing short key/command
strings on desktop; DOM hit/dispatch retain SDK cost. No speedup is claimed.

Simplify: retain existing SDK adapter and parsers; copy only values actually read
after dispatch, without copying all DOM attributes. Done: the tile regression
fails under ASan before repair, then input and affected production feature tests
pass normally/sanitized, builds and formatting pass. Owner/project/row identity
guards and native feature delegation remain required following this borrow repair.
Plan B: a smaller branch-local capture if the seam adds unrelated dependencies.

## C10b2 release-borrow repair result and self-check

The actual SDK replacement regression failed before repair under ASan: the key
was absent after release removed its row. This fixture did not report an ASan
memory error; removal invalidated the native read nevertheless. Production tile
input now parses the bounded key before once-only SDK release. Invalid/overflow
keys still release the SDK press without a native key; already forwarded input
rejects before touching the row again. Scroll button disabled/direction and
creature command values are captured before release. The sound field is
reacquired by copied ID and matching row after popup synchronization.

Both final client/test builds passed without warnings. The 39-case input/route,
PC/runtime and affected actual workbench/content matrix passed normally in
15,865 ms and under ASan/UBSan in 87,891 ms, no skips. After the explicit algorithm
include, all 13 input/validation/tile-editor/actual-palette cases passed normally
in 106 ms and sanitized in 461 ms. Two new SDK cases cover DOM replacement,
malformed keys, bounded conversion and once-only release. The root's scroll,
sound and creature read-order changes were source-audited; these tests do not
exercise every root action. Formatting/diff checks passed.

Simplification copies only needed values, retains the existing SDK adapter and
adds no state, queue or attribute configuration. UI release is the documented
singleton SDK edge; existing edit/list transforms remain batches. No speedup or
clean LSan claim. Callback owner/row identity guards and native delegation remain
required, followed by C18–C20; the whole issue remains in progress.

## C10b3 plan: callback owner identity

Tier 2 continuation. Patterns: Workspace owns one active tab/document/subtab
(workspace.hpp:37); object_workbench_target copies current display facts
(object_workbench_view.cpp:925); the native mouse-up seam is synchronous and
once-only (client_input.cpp:183). Existing mutation epochs deliberately allow an
unrelated edit's blur before a click. Neither the pure route nor the current
generational handles capture variable-length tab/resource/subtab identity.

Input is the active Workspace plus current script/display object identities,
module/resource generations, editor/PC map, preview phase, surfaces and operation
gates immediately before/after release. Output is one valid/unchanged-owner bit.
The ordinary case retains owner while blur commits a field or replaces markup.
Unknown tags, missing tabs, invalid subtab indices and malformed payload sizes
reject. ASSUMPTION: unchanged-owner releases dominate — affects straight-line
comparison, not a speed claim. Sources are existing owners; no new authority
state or frame cache. UI-thread callbacks are synchronous on desktop SDL/Rml;
all identity data must own its bytes across that callback.

Create client_ui_action.hpp/.cpp in client core: schema-1 fixed header plus one
owned UTF-8 payload with three bounded lengths for tab/resource/subtab identity.
Capture the true singleton displayed Workspace; compare equal spans of owner
records, clearing outputs on size mismatch. Keep pure routing rows unchanged.
Root supplies live object/map/phase/gate facts and rejects a native continuation
when callback ownership changes; preserve actions that precede SDK release.
Tests use actual Workspace transitions during real SDK release and malformed
batch/identity boundaries. Native row semantics remain feature-owned next.

Cost on the desktop main thread: O(identity bytes) capture/comparison and one
payload buffer per snapshot on native release only; no pointer-motion work.
Generational engine IDs retain the existing external table ABI, no DOM pointer
survives dispatch. Simplify: compare relevant owner identity, not every mutation
epoch/focus change; one payload, no callback getter, opaque AppState services,
registry or queue. Done: owner transition/unchanged blur and rejection tests,
normal/sanitized builds/checks and renderer-off core tests pass. Plan B: smaller
feature-specific guard if these shared owner facts cancel legitimate clicks.

## C10b3 result and self-check

The core owner protocol has a 72-byte fixed header (48-byte current context)
on this x86-64 build and one owned UTF-8 payload. Three bounded lengths distinguish
ambiguous concatenations. Capture rejects absent tabs, invalid subtab indices,
unknown context/tab tags and overlarge identity payloads. Equal-span comparison
rejects malformed rows independently; unequal counts clear all previous output.
No SDK/DOM types, retained root borrow or callback getter enters this module.

All 52 native release continuations use fresh before/after owner checks; the tile
key seam checks its owner around its own SDK dispatch. A changed owner cancels
the continuation and clears the armed recent-row index. Existing actions that
precede release retain their order. The full branch read audit also found a
sound-catalog key read after release; it now precedes release, using the same
already-characterized row-key boundary. No old DOM/creature-row borrow is read
after release in the audited chain; subsequently focused controls are reacquired.

Both final client/test builds passed without warnings. All 34 owner/input/route,
PC/runtime, actual-template and shell-view cases passed normally in 192 ms and
under ASan/UBSan in 862 ms, no skips. Renderer-off build passed, and all 14 core
owner/route/PC cases passed (0 ms harness time). Four new core cases cover actual
Workspace byte ownership, ambiguous identities, world/mode/generation/gate
changes and malformed batch contracts. One new SDK case switches the Workspace
and replaces markup during actual release; the prior replacement regression
also verifies an unchanged Workspace remains eligible. Initial builds corrected
the ObjectDocument accessor to its actual object() API; final builds passed.

Simplification compares owner identity rather than focus or all mutation epochs,
so unrelated blur edits/DOM replacement can proceed; one payload replaces several
separate string buffers. Capture is a true displayed-Workspace singleton and
comparison is batch/count-one. Engine IDs remain the existing generational table
ABI in this cold edge path. Formatting, diff and pure dependency checks passed.
Root guard placement was source-audited, not exercised through a full application
frame; native row semantics/feature delegation and C18–C20 remain outstanding.
No performance gain, desktop control feel or clean LSan result is claimed.

## C10b4 plan: owned variable and Details native commands

Tier 2 continuation. Patterns: six adjacent root branches parse variable/remove/
type and integer/boolean/door controls; object_workbench_view.cpp already owns
their rows, decimal parser, change commits and presentation. Existing backend
commands/SmallS prepare functions own actual edit policy and undo; reuse them.

Inputs are the current clicked DOM control and displayed Workspace/view/provider
metadata. Output is an owning cold command click with an explicit SDK release
phase, owner identity, positional CommandArgs and bounded property identity.
Ordinary valid clicks commit one existing undoable command. Variables/integer
release before native dispatch; boolean/door actions precede release. Invalid
integer controls retain the existing consumed/no-release behavior; invalid
boolean/door controls still release after no action. Missing/stale object/tab/
module/property identity rejects without edits. Mutable provider rows can change
across events before the frame's observer rebuild, so a dense row alone is not
a property identity. ASSUMPTION: unchanged metadata dominates — affects retaining
the existing actions, not a measured latency claim.

Extend object_workbench_view.hpp/.cpp with capture and one-shot execution APIs;
root coordinates only the phase/owner release guard. Capture retains no DOM row;
execution consumes the action before backend callbacks and compares current
SmallS row propset/field/element/editor identity before dispatching the existing
command. Backend remains the rule/undo owner. Add actual object/DOM/provider tests
for single command+undo, malformed input, stale owner and stale dense-row meaning.

Cost: cold owned CommandArgs and identity bytes; Details adds one bounded SmallS
row batch before the backend's existing policy preparation. No cache/epoch state,
command protocol extension or duplicate rule implementation; correctness has no
measured latency target. Singleton UI click/selected object are intentional;
SmallS row providers and backend edits remain existing batches. Simplify by one
root phase sequence and existing parsers/commands, with no opaque services or
action queue. Done: normal/sanitized feature/input checks, exact release phase,
one undo and stale/malformed no-edit tests pass with source/dependency checks.
Plan B: fewer control kinds if a shared descriptor requires unrelated state.

## C10b4 result and self-check

The workbench owns capture and one-shot execution for variable add/remove/type,
integer steps, booleans and door state. The root retains only the common release
phase/owner guard sequence. Requests contain no DOM/text-slice borrow, use owning
existing CommandArgs and consume their kind before backend/provider callbacks.
Stale object/tab/module/context or property metadata rejects without edits/logs.
Fresh SmallS propset/field/element/editor/value identity guards a dense token;
existing commands continue owning policy, edit batches, output and undo.

Simplification skips owner-byte capture for unmatched/invalid controls, reserves
the bounded argument batch once and replaces six root branches with one phase
sequence. No epoch cache, opaque services, new rule implementation or command
protocol extension. Details performs the documented extra bounded provider batch;
this cost has no measured latency target and no speedup is claimed. Selected UI
click/object are true singletons; row providers and backend edits remain batches.

Both final client/test builds passed without warnings. All 31 actual workbench,
input/owner/route, relevant template and scrollbar-lifetime cases passed normally
in 5,994 ms and under ASan/UBSan in 33,707 ms, no skips. Five new feature cases
verify owned variable arguments after DOM replacement, one undo/repeat rejection,
live boolean/integer/door edits+undo, replacement-object rejection, a simulated
stale dense slot with equal value but different live property identity, and
malformed control phases. The integer fixture initially assumed Sound had a
spinner row; its actual provider uses a separate volume slider. Inspection of
the SmallS provider and the corrected actual Door fixture resolved that test.

Formatting/diff/dependency/source checks passed; 115 native root lines were
removed. Main is 6,452 lines including preserved baseline whitespace. SDK/frame
integration of this phase coordinator still needs C20; the invalid integer
consumed/no-release path is retained here and will be characterized separately.
Other native feature handlers, complete input authority and C18–C20 remain
outstanding. No desktop control feel, nonunit DPI or clean LSan result is claimed.

## Invalid property release characterization plan

Tier 1, separate repair after C10b4 preserved the old path. Actual input is an
enabled SDK button press followed by a primary release whose integer row token
is malformed/stale. The native descriptor consumes that invalid control with no
SDK release. Output must reject the native edit and clear the prior SDK press
exactly once. On the UI thread, native consumption suppresses default forwarding;
the existing phase protocol can state the obligation without new press state.
ASSUMPTION: valid row tokens dominate — affects the common path, not a speed claim.

Use the production capture descriptor and SDK adapter in the actual dummy-window
fixture, checking mouse-up count and active pseudo-class before/after rejected
input. If the regression fails, give invalid integer controls the same before-
native release obligation as valid ones; retain boolean/door release order.
Cost is the existing SDK release on this error path, no allocations/state/options.
UI press/release is the documented singleton edge. Simplify with the existing
phase tag and once-only adapter. Done: regression fails first, then normal/san
input/workbench builds and checks pass with no native action or retained press.

## Invalid property release result and self-check

The new real SDL/Rml test failed before the repair: zero SDK mouse-up events,
no forwarding recipient and the button's active pseudo-class still set. Matched
integer controls now capture their before-native release phase before validating
the row. Invalid controls retain no command; the existing once-only adapter
clears the SDK press without an edit or undo. Boolean/door ordering is preserved.

Both client/test builds passed without warnings. All 32 affected cases passed
normally in 6,597 ms and under ASan/UBSan in 34,433 ms, no skips. The regression
checks one mouse-up, a cleared active pseudo-class, no native command/undo and
repeat-forward rejection. Simplification removes the redundant valid-integer
phase assignment. No new state, allocation, options or performance claim; the
documented singleton release and existing error policy satisfy the final check.
Formatting and diff checks passed. Full-frame integration and the remaining
C10/C18–C20 work are still outstanding; LSan and desktop/DPI gaps remain.

## C10b5 creature native command plan

Tier 1 within the Tier 2 refactor. The five adjacent native branches in main
parse class slots, spell/feat IDs and ±1 adjustments from ancestor controls;
they read current copied SmallS rows and dispatch four existing backend commands.
Class levels use provider bounds, feats/known spells use booleans, memorized
spells use nonnegative uses and the selected class/metamagic. Invalid tokens or
unavailable owners reject. The UI thread releases through synchronous SDK
callbacks before commands, so copied rows and selected filters can become stale.
ASSUMPTION: valid rendered controls dominate — affects the common path only.

Patterns/reuse: creature_workbench_view.hpp owns the presentation batches;
object_workbench.hpp supplies current target facts; toolset_backend.cpp:3079–3231
already owns class/feat/spell rule preparation and undo. Existing local ancestor
and from_chars idioms apply; no shared public parsing utility is needed. Extend
the creature view with a fixed scalar command header plus owned tab ID, one-shot
capture/execution, and existing release phase. Root keeps phase/owner coordination.

Data flow: hit + current view/target/tab/module -> owned scalar request -> SDK
release -> fresh target and selected filters -> fresh existing SmallS row batch ->
matching semantic current value -> existing backend command/result/undo. Preserve
old valid/error release phases; missing live rows, changed filters/value/owner,
malformed tags and repeated requests reject before dispatch. No retained DOM or
text slices; click/selected target are true singletons, provider/edit rows batches.

Cost: cold tab bytes and one fresh bounded provider batch per valid command,
including the existing provider's text storage; backend still prepares the edit.
No hot-loop allocations, queue, cache, rule implementation or performance claim.
Simplify five branches to one phase sequence and build CommandArgs only after
validation. Modify the paired creature files, main and existing real-object tests.
Done: actual class/feat/spell command+undo, owned DOM-replacement request, stale
filter/value/object/module rejection and repeat rejection; normal/sanitized
affected builds/checks. Plan B is fewer command kinds if unrelated state is needed.

The existing object_mutation_state epoch also invalidates a request if any toolset
edit committed during release; no new epoch is maintained. This conservatively
drops even an unrelated edit's concurrent click. Fresh value/selection checks
add protection against differing live provider data. Class slots have no class
ID in the current presentation protocol; the epoch guards toolset slot edits
without choosing policy from labels or extending the provider protocol. Direct
external writes without mutation notification remain outside this guarantee.

## C10b5 result and self-check

Creature view capture/execution now owns class adjustment, feat toggling, known
spell toggling and memorized increment/decrement. The root contains one release
phase sequence for these five controls. Requests own tab bytes and scalar IDs,
current values and selected class/metamagic; consume kind before provider/backend
callbacks. Fresh existing provider data, target/tab/module identity and the
existing mutation epoch reject stale requests without edits or result logging.
An unavailable increment row still releases as before, but cannot dispatch a
command. Existing backend commands retain policy, edit storage and undo.

Simplification builds bounded CommandArgs only after validation and removes two
unused root wrappers. No cache, epoch owner, queue, new rule or broader context
was introduced. Provider/edit rows retain batch paths; click/selected object are
documented singletons. Cold DOM/kernel/provider borrows are synchronous, and
requests retain no pointers/text slices. The documented extra bounded provider
batch has no measured latency target; no speedup is claimed.

Both final client/test builds passed without warnings after resolving one
else-if initializer shadow and removing the unused wrappers. All 41 affected
cases passed normally in 13,929 ms and under ASan/UBSan in 79,017 ms, no skips. Four new
cases exercise actual class/feat/known/memorized commands, DOM replacement, one
undo per edit, repeat rejection, memorized decrement then increment into the
freed live slot, changed filters/owner/module/current-value rejection, actual
intervening backend mutation and malformed controls. Three existing operation
tests verify the retained class/spell policy/undo paths.

Formatting, dependency and diff checks passed; 105 root lines were removed and
main is 6,347 lines. Whole-frame SDK coordination still awaits C20; the epoch's
conservative unrelated-edit cancellation and unsignaled-write limit are explicit
above. Native inventory/appearance/tile/shell/workspace input, full routing
authority and C18–C20 remain outstanding. No desktop/DPI or clean LSan claim.

## C10b6 inventory native action plan

Tier 1. Actual three controls are equipment slots 0..17, contiguous source item
indices, and pages 0..page_count-1 in the copied InventoryViewSnapshot. Creature,
Item and Placeable share the grid; only Creature has equipment. Output is
selection/page state or the existing equip/unequip backend command, followed by
one requested window sync. The UI thread can replace/reorder rows or change
selection during SDK release; current code reads equipment/selection afterward.
ASSUMPTION: valid rendered controls dominate — affects the common path only.

Reuse inventory_workbench_view.hpp state and object_workbench.hpp target facts,
Inventory.items/pages()/rows()/columns(), equip_item_ptr/inventory_item_ptr, and
backend commands/toolset mutation epoch. Extend the paired inventory module with
one scalar header+owned tab identity; root keeps common before-native release
and the requested window synchronization. Capture item/slot handles and current
selection/page before release. Execute consumes kind, validates target/tab/module/
epoch, matching current UI state and live grid dimensions, then checks exact
live dense item/slot identities before selection or existing backend dispatch.
Malformed/missing/stale records reject with no edit; preserve prior release
obligations. Empty equipment with no selection remains a no-command window sync.

Cost: cold tab bytes and bounded indexed live checks (three known owner types,
18 slots), no extra SmallS projection/icon decoding. Kernel/Rml pointers are
synchronous cold integration borrows; handle identities protect live generation.
Click/displayed grid are true singletons; inventory source storage/edit operations
remain batches. Simplify with existing IDs/generations and commands; no generic
drag/action service, queue, cache or duplicated policy. Modify inventory pair,
main, existing real-object tests. Done: actual select/page/equip/unequip effects
and undo, repeat rejection, stale dense row/slot/selection/owner and malformed
controls; normal/sanitized affected checks. Plan B is separate equipment/grid
extractions if one descriptor needs unrelated dependencies. No speed claim.

## C10b6 result and self-check

The inventory view owns capture/apply for equipment slots, source item selection
and pages. Root supplies common release/owner coordination and performs the
requested window sync. The owned request captures slot/source item handles,
page/selection, tab/module identity and the existing toolset mutation epoch.
Execution consumes kind and rejects mismatching live identity, dimensions,
selection, page, owner, module or epoch before changes. Live item/slot lookups
guard even unsignaled source changes; equipment commands retain existing backend
policy/results/undo. No-selection empty slots request a window sync without a
command. Explicit invalid tags/ranges/owners return no action.

Simplification uses indexed existing Inventory storage and live handles without
rebuilding SmallS rows or decoding icons. CommandInvocation/args transfer to the
backend instead of copying; the initial copy triggered a GCC maybe-uninitialized
warning in its variant payload. A first build also caught an incorrect command
field name and explicit ObjectHandle return; actual declarations resolved them.
No new policy/cache/epoch owner/queue or broad context. Cold kernel/DOM borrows
are synchronous; click/grid are documented singletons and source/edit data are
existing batches. No memory/latency speed claim is made.

Final client/test builds passed without warnings. All 36 affected inventory,
drop/input/owner/route, operation and managed-list cases passed normally in
8,158 ms and under ASan/UBSan in 47,046 ms, no skips. Four new cases exercise
select/equip/unequip and two undos restoring exact coordinates/infinite flag;
page/selection across actual Creature/Item/Placeable grids; an unsignaled live
swap of distinct handles from identical blueprints; changed selection/module/
owner and occupied-slot rejection; consumed requests and malformed controls.
The occupied-slot fixture refreshes only its protocol epoch to isolate the live
identity check after an actual backend edit; this is not a whole-frame callback.

Formatting/diff/dependency checks passed. Main is 6,305 lines, 42 removed from
the root. Whole-frame forwarding remains a C20 gap; malformed creature/inventory
consumed/no-release paths are preserved here for a separate characterization.
Remaining native features, complete input authority and C18–C20 remain open.
The existing LSan, physical device, nonunit DPI and desktop limitations remain.

## Rejected creature/inventory release characterization plan

Tier 1 repair, separate from C10b5/b6. Actual malformed creature spell and
inventory source controls return a matched no-command descriptor with no release
phase. Root consumes them, suppressing the SDK release of an enabled press.
Use the real dummy-window fixture and production capture/forwarding functions,
checking one mouse-up, cleared active pseudo-class and no command/undo for both
cases. If it fails, matched rejected controls retain the same before-native
release obligation as valid controls. Valid phases remain unchanged.
ASSUMPTION: valid rendered tokens dominate — affects common/error partition only.
Cost is one existing SDK dispatch on the error path; no state/allocation/options.
Release is a true singleton edge. Simplify by setting the obligation at control
recognition and removing duplicate phase writes. Done: fail first, then affected
normal/sanitized builds/tests pass with explicit no-edit and once-only evidence.

## Rejected creature/inventory release result and self-check

Both representative enabled-control cases failed first: zero SDK mouse-ups and
the active pseudo-class retained after native rejection. Matched creature and
inventory controls now capture before-native release at recognition, independently
of token/owner validity. Invalid requests still own no command/tab payload. Root's
existing phase coordinator and once-only adapter clear the SDK press without an
edit/undo. Valid release phases are unchanged; unmatched controls remain unclaimed.

All 31 affected cases passed normally in 11,844 ms and under ASan/UBSan in
66,244 ms, no skips; final client/test builds passed without warnings. The new
real SDK case checks both rejected controls, one release, cleared active state,
no command/undo and repeat-forward rejection. Existing malformed-control tests
also cover all captured control families' obligation. Simplification removes
four duplicate phase writes and the increment-specific phase branch. No new
press state, allocation, option or performance claim; singleton/error contracts
and formatting/diff checks passed. Whole-frame/root and remaining refactor work
remain open; no desktop/DPI or clean LSan result is claimed.

## C10b7 workbench surface plan

Tier 1. The one workbench tab control carries one of 13 data-surface strings.
Input is that attribute plus the live displayed object/type and current project;
output is existing surface/transient-selector state and details invalidation.
Unknown/unsupported surfaces retain the current surface after the existing
selector cleanup. Root still releases before action, updates body preview and
requests content/window refreshes. One active surface/click are true singletons.
ASSUMPTION: rendered valid surface controls dominate — affects the common path.

Reuse object_workbench.hpp enum, object_workbench_view state, child clear/close
functions, appearance_catalog_kind, backend project path and existing details
invalidation. Extend the core pair with a bounded allocation-free name decoder;
extend the view pair with scalar capture/one-shot apply and the existing SmallS
selector-close DOM helper. Preserve close-button synchronous dispatch order,
current type/project checks and no retained DOM borrow after dispatch. Root takes
only an owned enum request and coordinates SDK/renderer/cross-view refresh.

Cost: 13 fixed decoder rows, one scalar request, existing selector DOM traversal
and catalog refresh; no new state cache, provider/rule ownership or queue. Move
only actual surface policy, not refresh orchestration. Simplify repeated string
branches into a type-gated switch; keep the real capability checks. Modify both
pairs, main and existing fixtures. Done: actual control capture/DOM replacement,
surface/selector cleanup and unsupported/unknown/repeat behavior, normal/san
affected checks and renderer-off decoder build. Plan B: keep fewer view operations
if one function needs unrelated state. No speedup or new performance target.

## C10b7 result and self-check

The core enum decoder and workbench capture/apply now own surface policy and
selector cleanup. Root keeps one scalar request, guarded SDK release and the
existing renderer/cross-view refresh sequence. The shared SmallS close helper
returns immediately after synchronous dispatch, so replaced nodes are not read.
Three new actual-control cases verify cleanup order during DOM replacement,
one-shot consumption, unsupported/unknown/invalid surfaces and real live-type
capabilities. Haks rejection uses the actual empty project; positive project
Haks and GPU body-preview integration remain unverified here.

All 60 affected cases passed normally in 28,064 ms and under ASan/UBSan in
151,477 ms, no skips. Final client/test builds passed without warnings. The
renderer-disabled client-core/test build passed and all 14 owner/route/PC cases
passed. Main is 6,229 lines (76 removed this checkpoint). Formatting and diff
checks passed; feature APIs contain no AppState, retained DOM or new cache.
Simplification removes repeated string decisions and the obsolete invalidation
wrapper. Singleton, type/project rejection, selector ordering and original
refresh contracts remain explicit; no performance or clean LSan claim.
Remaining native input, metrics/lifetime consolidation and final root are open.

## C10b8 native sound selector plan

Tier 1. Actual inputs are sound_resource_add/back IDs and sound_catalog_row
nonnegative int32 data-key indices into the existing copied catalog. There is
one displayed Sound object on its sounds surface and one selector; these are
true singleton edges. Output is existing selector/query/scroll state or one
backend replace_sound_resources edit/undo. The SmallS provider retains list
validation and the 1,024-resource bound. Unknown/malformed/stale requests release
the SDK press but produce no edit or refresh. ASSUMPTION: valid visible controls
dominate — affects the common path, not a speed claim.

The platform remains main-thread SDL3/Rml/kernel; synchronous SDK and close
callbacks can change the tab, catalog/query, object or module. Catalog contents
are stable within resource generation, while query and live resource values can
change on any event. Read the existing catalog/target/workspace at capture;
copy only resource identity, tab/query and existing generations/mutation epoch;
read current facts again at apply. Borrow DOM only for capture, use indices for
fresh catalog validation, and retain no renderer/DOM/engine pointer. Root closes
the shared SmallS selector and coordinates document refresh/window/focus after
before-native SDK release; appearance_view owns Sound presentation and edits.

Cost: one fixed request header plus owned cold UTF-8 tab/query and one Resref,
three control ancestor searches, O(M) membership checks for M current match
indices, existing catalog filtering and backend batch transform. No second catalog, queue, rule logic or whole AppState API.
Simplification uses the existing sound commit and selector helpers; no new
provider query, precompute, buffer or general dispatcher. Plan B is a smaller
selector-only extraction if edit capture requires unrelated state.

Done: actual Rml controls, DOM replacement before apply, resource addition/undo,
selector opening/closing, malformed/unmatched/repeat requests and stale resource
identity/query/generation/tab/object/mutation rejection. Run normal/ASan/UBSan
focused affected cases and check builds, formatting, includes and source diff.
GPU rendering, physical module reload and desktop control feel remain manual
coverage gaps; no performance improvement is claimed.

## C10b8 result and self-check

Appearance owns the three native Sound controls through one owning request and
one consumed apply function. Root keeps before-native SDK release, shared SmallS
selector dispatch and content/window/focus coordination. Apply rechecks actual
object/tab/module/resource ownership, the existing mutation epoch, query and
selected row's copied Resref; stale requests do not edit, log or close selectors.
The existing 1,024-entry backend batch/undo transform remains authoritative.

Three new actual-control tests cover opening/closing, source DOM replacement,
addition/undo/repeat, malformed tokens and independent resource/query/catalog/
module/filter/object/tab/intervening-edit rejection. Initial normal validation
found a fixture assertion using the new active tab's undo count for the old
owner; assertions now check the original tab's stack explicitly and verify the
object and tab switches separately. Final 31 cases from 6 suites passed normally
in 11,328 ms and under ASan/UBSan in 68,634 ms, no skips. Final normal/sanitized
client/test builds passed without warnings; diff/format checks passed.

Main is 6,215 lines (14 removed). No new provider, catalog copy, queue, rule logic,
AppState API or retained DOM pointer. Simplification reuses the current commit
and close helpers and removes repeated native selector decisions. O(M) match
membership work and cold string ownership are explicit costs; singleton/error
contracts are documented. Actual physical module reload, root frame integration,
desktop/GPU feel, high DPI and clean LSan remain unverified. Remaining refactor
checkpoints are open; no performance improvement is claimed.

## C18 authoritative metrics plan

Tier 1 within Tier 2. Input is the actual renderer ViewerFrameStats and editor
ClientGpuFrameStats once per frame plus application CPU timing samples. Output
is the existing compact/verbose Rml HUD and smoothed timing history. Renderer
absence retains the last received counters; negative time samples retain their
previous latest/smoothed values. GPU stats are received after EndFrame, so their
HUD display remains one frame delayed. One HUD and its histories are true
singletons. ASSUMPTION: renderer snapshots are usually available — affects
predictable valid/null branching, not a measured performance requirement.

Observed x86-64 layouts are 704 bytes for ClientMetricsState, 2,872 for
ViewerFrameStats and 272 for ClientGpuFrameStats; both renderer DTOs are trivially
copyable. Existing state mirrors 88 counter/status fields individually. Replace
those mirrors/assignments with two owned last-received existing DTOs, and read
those DTOs directly in formatting. Keep the actual latest/smoothed timing pairs
because invalid samples and delayed/absent snapshots require their history.
Borrow input only during update; no renderer pointer persists across scene or
runtime teardown. Counters remain the renderer's native ranges/layout; null is
explicitly retained and negative timing policy is unchanged.

Cost: one HUD record grows by roughly 2.8 KiB (measure final sizeof); each valid
frame copies 2,872/272 bytes instead of assigning selected scalars. This is a
memory/copy cost on the desktop UI thread, not a performance improvement. Access
is linear snapshot copying and scalar formatting; no hot engine-pointer path.
The wider renderer header increases compile dependencies. The benefit is deleting
the second counter schema and its maintenance, not runtime speed. A smaller new
counter DTO would recreate that schema and is not built. Plan B is retain selected
mirrors only if actual renderer DTO lifetime/shape cannot meet ownership.

Simplification: reuse the existing renderer layouts once rather than maintaining
field mappings. No approximation, new telemetry cache/queue/table or smoothing
policy. Done: baseline exact compact/verbose HUD output for representative native
stats, null snapshot/history independence, negative timing retention, delayed GPU
semantics, normal/ASan/UBSan builds/checks and explicit size/dependency review.
No GPU timestamp accuracy, desktop overlay feel or performance claim is included.

## C18 result and self-check

Metrics now stores last-received ViewerFrameStats and ClientGpuFrameStats
records and reads their counters directly during HUD formatting. Removed 88
counter/status declarations and 88 per-field update assignments; 64 actual
latest/smoothed timing floats remain. Input records are borrowed for one update
only; null updates retain the owned history. No snapshot borrow survives source
mutation, scene replacement or shutdown. Renderer DTOs remain pointer-free
counter records with their existing shapes/ranges; their layouts were inspected
and both are trivially copyable. Smoothing and root frame/GPU update order did
not change.

The new production golden case passed against the original implementation in
separate compact/verbose processes before replacement. It exercises nested
model/skin, area, forward-plus, shadow/submission and delayed GPU counters,
including a 64-bit draw value above UINT32_MAX, source overwrite, missing
snapshots, negative timing samples and vanished GPU timers. After replacement,
the same explicit-mode formatter executable matches the original compact
162-byte and verbose 1,121-byte outputs exactly. All 34 affected metrics/input/
Rml-template cases passed normally in 267 ms and under ASan/UBSan in 1,115 ms;
all 3 metrics cases also passed in separate verbose normal/sanitized processes,
no skips. Both client/test builds passed without warnings.

Measured x86-64 state grows from 704 to 3,400 bytes (+2,696); the existing DTOs
are 2,872/272 bytes. This knowingly pays more retained memory and copying for
one HUD to eliminate a second counter schema and retain the existing null-history
contract. No trimmed counter DTO, cache/queue, option, approximation or new
telemetry system was built. Latest Ninja Release compile observations: metrics
3.550 s and main 14.133 s, versus the early recorded 4.180/22.107 s; sanitizer
4.643/18.182 s. These concurrent-build observations are not a controlled speed
comparison. Wider viewer-header dependency and memory/copy cost are explicit.

Removed-counter reference audit, formatting and diff checks passed. Singleton,
input lifetime/error policy, state ownership, existing DTO reuse and stated
done criteria passed self-check. GPU timestamp accuracy, running overlay feel,
high DPI and clean LSan remain unverified. Main remains 6,215 lines; C10 native
routing, C19 resource lifetime and C20 root/final integration remain open.

## C19a client native-result lifetime plan

Tier 1 prerequisite to Tier 2 runtime ownership. Actual producers are the three
SDL dialog launches in loading_view; one native callback copies a path/cancel/
thread-local SDL error into one heap result and queues the registered event.
Unix Zenity uses a detached SDL thread. Root permits clean-workspace quit during
an open dialog; a callback cannot assume its original owner, SDL events or event
registration still exist. Output on shutdown must be no later client SDL calls/
queue publication, plus disposal of already queued owned results. One desktop
consumer/delivery gate is a true singleton; requests/results are a queued batch.
ASSUMPTION: callback completion is uncommon relative to frame input — affects
cold-path ownership/locking, not a speed claim.
ASSUMPTION: event registration may recycle after SDL restart — affects the
late-result fixture expectation; verify before deciding that identity policy.

First characterize callback delivery after owner destruction and SDL event
restart/registration lifetime in the actual dummy SDL fixture, with a real callback thread.
Then attach each owning request to its desktop's heap delivery record (mutex and
accepting bit). Callback locks before any SDL error/queue call; shutdown closes
under that same lock and drains only this owner's registered payload events
while SDL is live. The permanently closed old record survives pending requests
and cannot be reopened by SDL initialization. Destructor closes delivery without
SDL calls; root explicitly drains before SDL_Quit. Unknown/null/invalid requests
are dropped; failed queue transfer destroys the result. No AppState borrow.

Cost: one shared heap record per desktop, one shared ownership copy per dialog,
one cold mutex lock per completion/closure, and O(N) queued-payload disposal.
An array index cannot own this synchronization record after its desktop dies;
shared lifetime is required by the existing asynchronous callback protocol and
adds no hot pointer path. Reuse the SDL result queue; no new worker, queue,
producer registry, global epoch or blocking wait for the native picker.
Simplification removes the need to assume completion or keep desktop state alive.

Done: failing-before/passing-after late callback after SDL restart,
queued-result drain, closed/null/invalid request drop, normal/error/cancel delivery
and repeat consumption, worker/close ordering, normal/sanitized affected checks
and stable ownership/format review. SDL's own native driver process/toolkit
cleanup and real native picker behavior remain integration gaps; this guard owns
client callback SDL work/result payloads, not SDL's internal driver resources.
The larger startup/resource-owner extraction remains separate. Plan B is retain
current runtime boundaries until this client callback lifetime contract passes.

## C19a result and self-check

The initial fixture falsified the registration-recycling assumption: IDs were
32,768 then 32,769 across an actual event/video restart. Vendored SDL_RegisterEvents
uses a monotonic atomic counter. The corrected late-owner regression then failed
against production: one orphaned heap result queued for the expired owner after
SDL restarted. Requests now own their original delivery record; the callback
locks before client SDL work and drops closed/null/out-of-range requests. Closure
serializes with publication, resets the consumer's gate/event identity and drains
its queued results before SDL teardown. Destructor only closes the record and
calls no SDL. Root invokes closure at the start of normal teardown; closed browse
APIs cannot launch a new picker. No wait for native completion or AppState borrow.

The three new cases verify actual SDL restart/late worker drop, two queued-result
disposals, preservation of an unrelated payload, pending record lifetime ending
once its callback returns, idempotent closure, closed browse rejection, live
worker error copying, malformed tags/null delivery and existing once-only result
consumption. All 47 affected cases from 6 suites passed normally in 4,184 ms and
under ASan/UBSan in 20,607 ms, no skips. Normal/sanitized client/test builds passed
without warnings; formatting and diff checks passed.

Measured x86-64 LoadingViewState is 312 → 328 bytes; delivery record is 48 bytes
plus shared-ownership allocation metadata, request is 4 → 24 bytes. One cold
allocation per desktop and a lock per completion/closure buy lifetime independent
of desktop destruction. Root and producer keep one owner; no extra worker,
event queue, registry or generation. Resetting the closed owner reference avoids
a second destructor lock and releases it immediately when no requests remain.
Explicit singleton/batch-disposal, cold-pointer justification, native-event range
and queue transfer/failure contracts passed self-check. Main is 6,216 lines.

This resolves client callback/result ownership without assuming completion at
shutdown. SDL driver process/toolkit teardown and a real native picker remain
unverified integration behavior; the guard makes no claim about SDL's internal
resources. Complete resource acquisition/unwinding, remaining native input and
root/frame integration remain open; no performance or clean LSan result claimed.

## C19b Rml file adapter plan

Tier 1. Actual UI package input is panel.rml (3,151 bytes), panel.rcss (81,721),
resource URLs with a ui prefix/query/protocol, and game TGA fallback (representative
black.tga is 272 bytes). Output is an owned resource/file stream consumed by
RmlUi's required Open/Read/Seek/Tell/Length/Close protocol. Package/game managers
are borrowed for the adapter lifetime; resource bytes belong to each open file
until its single Close. Read transforms a byte batch; SDK lifecycle calls remain
singular because that external protocol cannot be changed. One adapter is the
runtime's true singleton. ASSUMPTION: indexed UI resources are the common case
— affects memory/file partitioning, not a measured rate or optimization.

Move the existing adapter into client_rml_file_interface.hpp/.cpp and keep its
lookup/normalization, copied bytes, stdio fallback and error behavior. Root still
owns package/managers/Rml lifetime at this checkpoint; complete acquisition and
failure cleanup are separate. Null handles/buffers read zero, missing opens fail,
memory seek rejects invalid origin/negative/past-end offsets, fallback delegates
stdio. Nonzero SDK tokens must come from Open and remain live until Close.

Cost remains one cold file record allocation, resource-byte ownership, path
normalization/registry reads and O(bytes) read copies, plus one translation unit.
The existing cold pointer-sized file token is preserved; replacing it with an
index requires a new slot/lifetime table and changes the external handle contract
without an observed need. This is not a pointer-heavy engine hot path. stdio's
FILE borrow and memcpy's buffer pointers are required by their external APIs.
No new cache, lookup table, backend, generic file service or whole AppState API.

Simplification makes the actual file boundary directly testable and reuses
ResourceManager/stdio rather than another loader. Done: actual package URL reads,
owned bytes surviving provider mutation, TGA fallback, filesystem/file-protocol
reads, lengths/EOF/valid seeks/null/missing handling, normal/sanitized client/test
builds and affected checks. Extreme numeric seek behavior is characterized after
the move if needed, in a separate fix. Full Rml/SDL partial startup and GPU/desktop
integration remain open; no performance improvement is claimed. Plan B: keep
resource ownership in root until the separate lifetime contract can be verified.

## C19b Rml file adapter result

Moved all eight adapter methods without changing their normalized method bodies.
The executable and guarded Rml test target compile the same production source;
root retains manager/package lifetime. Four new tests use the actual UI files
and binary TGA fixture, verify URL forms, owned bytes after registry replacement,
EOF/seek bounds, stdio position preservation and explicit missing/null handling.
All 54 affected checks from 6 suites passed normally (326 ms) and under
ASan/UBSan (1,165 ms), no skips. Both builds and formatting/diff checks passed
without warnings. Main is 6,042 lines.

Self-check: SDK singleton lifecycle and batch-byte reads are documented, existing
cold file pointers are justified, no lookup/cache/service abstraction was added,
and file ownership/error contracts have direct tests. No measured performance
claim. Extreme long offsets remain a separate boundary characterization; partial
startup, native picker internals, desktop/GPU behavior and full integration remain
open.

## C19b2 numeric memory seek plan

Tier 1. Input is the SDK's signed long offset/origin and a live owned byte stream
with size_t position in [0, byte count]; output is either a new bounded position
or false with unchanged position. Real package seeks are small; the actual API
also accepts LONG_MIN/MAX. Characterize those boundaries under UBSan before
changing the existing signed addition. Reject invalid origins and offsets outside
the stream; keep stdio delegation unchanged. On this x86-64 desktop, bounded
unsigned arithmetic removes overflow without extra allocation or traversal.

Simplification: compare the displacement with remaining/prefix bytes before
arithmetic; no clamp, wider integer dependency, cache, table or new state. Seek
is a singleton SDK stream operation, Read remains the byte-batch transform.
Done: demonstrate the prior overflow, pass extreme offsets with unchanged cursor,
retain existing file cases, normal/sanitized builds and checks. No performance
claim; this separate correctness fix must not alter resource lookup.

## C19b2 numeric memory seek result

The new actual-package test failed before the fix under UBSan: signed overflow
from 1 + LONG_MAX in the production adapter. Memory seeks now compare positive
displacements with remaining bytes and negative magnitudes with the prefix before
updating size_t position; LONG_MIN magnitude is computed without signed negation
overflow. Invalid origins/out-of-range offsets leave the cursor unchanged, and
stdio is unchanged. All 5 file tests passed normally and under ASan/UBSan with
no skips. Both client/test builds passed without warnings; formatting/diff passed.
Self-check matched the bounded contract, kept batch Read and the SDK singleton
exception, and added no state or speculative utility. Other C19/C20 work remains
open; no performance or leak-check claim.

## C19c Rml resource ownership extraction plan

Tier 2 checkpoint. Input is the actual indexed UI directory, borrowed live game
ResourceManager and SDK renderer/window, five deployed font files and positive
logical context dimensions. Output is one live Rml singleton, three named context
borrows and owning font/package storage retained through Rml shutdown. File bytes
and fonts are cold startup batches; per-frame context access is fixed three slots.
The common case is the existing successful startup sequence; missing package,
font or context returns false with the existing diagnostic. No measured rates.

Move package validation, five font loads, document loading and Rml/context
acquisition into ClientRmlRuntime. Keep the primary-context renderer resize
between primary and overlay creation, the density override and exact root
listener/model/geometry/texture shutdown ordering. This commit keeps explicit
root shutdown; the next separate fix adds failure-safe guards and proves acquired
resource cleanup. SDK renderer/window/game resources remain external borrows
and must outlive shutdown. DOM/render interfaces are required cold SDK pointers;
indices cannot replace the SDK's own identities, and no indexed engine hot path
is introduced.

Cost: same five font byte arrays, resource registry, copied UI document strings
and three SDK contexts; one owner and a translation unit add no worker, cache,
queue or general application service. Runtime is a true process singleton; fonts
are a fixed five-row batch, documents use the SDK's singleton creation protocol.
Simplification reuses ResourceManager/FileInterface/stdio and fixed context roles,
removing root's loader and resource ownership without a whole-state API.

Done: normal/sanitized builds, actual package/font/document initialization and
affected Rml/input checks with explicit shutdown, unchanged resize/teardown
ordering and dependency audit. Partial-startup cleanup is deliberately tested
and fixed separately; real window/Vulkan/native picker/high-DPI remain
unverified where no fixture exists. Plan B is the narrower Rml owner; SDL/engine
ownership moves only after feature borrows are detached while still live.

## C19c Rml resource ownership extraction result

ClientRmlRuntime owns the actual StaticDirectory, UI ResourceManager/file adapter,
five stable font arrays and three fixed context roles. Root borrows the contexts
and retains its between-context renderer resize, density override and explicit
successful shutdown sequence. Existing font reader body is unchanged; five
existing font specifications are traversed once in order, with all five attempted
as before. Documents still load copied resource bytes with their resource URL.
No whole-state API, renderer duplication or general loader was introduced.

The new production-owner case loads all five actual font assets, creates the
three real SDK contexts, verifies overlay density inheritance, loads/layouts/
renders the actual panel and loads the actual modal document. Missing documents,
explicit release/shutdown, reset interface/context borrows and idempotent shutdown
are checked. All 56 affected checks from 7 suites passed normally and under
ASan/UBSan with no skips. Both builds passed without warnings; format/diff and
root ordering audit passed. Main is 5,932 lines.

Self-check: owned byte batches remain live through manual Rml shutdown, SDK
singleton/pointer contracts are documented and errors return explicit results.
This mechanical checkpoint preserves the existing early-return cleanup gap; the
next fix adds destructor/failure cleanup separately. SDL/window/engine ownership,
remaining native input and frame/root coordination are still open; no measured
speedup, real GPU/desktop or clean LSan result claimed.

## C19d Rml partial-startup cleanup plan

Tier 2 correctness checkpoint. Observed startup can return after Rml initialization
when any of five fonts fails, after primary context creation, or after either
overlay context fails. Later returns follow real workbench listener/texture-array
registration and Smalls/model/document initialization. Output must be zero live
owned contexts/interfaces, with only acquired models/listeners closed, and font/
texture/feature buffers still alive during SDK cleanup. Successful teardown
retains its existing order. SDK default FreeType is enabled in both real builds;
its disabled-engine Rml::Initialise failure branch is not observable here.

First demonstrate missing-font failure leaves SDK interfaces live after owner
destruction. Then add owner destructor shutdown, explicit invalid/repeated
initialization/context rejection, and root scope guards whose reverse declaration
order removes listeners before their local objects die. A feature guard closes
native delivery/gamepad, waits for renderer, clears active script ownership,
releases UI resources and models, shuts down Rml/rendering and clears workspace
while AppState/texture arrays still live. Guards inspect the owner's reset context
borrows so normal explicit shutdown does not access destroyed contexts.

Cost is fixed cold scope guards and one existing Rml shutdown; no allocation,
thread, table, event queue, extra application state or hot-path branch. Simplify
by reusing scope_exit and idempotent SDK/model cleanup; do not invent a cleanup
callback graph or failure injection framework. Runtime remains the documented
SDK singleton and font/file transforms remain batches. Done: prior failing case,
real incomplete-package/missing and empty-font/primary-only teardown/overlay collision and
automatic successful cleanup, renderer release evidence, guarded production root
build and affected normal/sanitizer checks. SDL/window/engine acquisition and
GPU/root failure injection remain separate/unverified until their owner exists.

## C19d Rml partial-startup cleanup result

The pre-fix missing-font regression failed with all three SDK interface pointers
still live after ClientRmlRuntime destruction. Destructor now shuts down acquired
Rml state before member font/provider storage dies. Invalid dimensions, repeated
live initialization and overlay creation before/after acquisition reject explicitly.
Root adds reverse-order listener guards and a feature-binding guard; failure
cleanup runs while AppState/generated-texture arrays and models remain live.
Normal explicit teardown resets context borrows, so these guards do no SDK work
on its destroyed contexts. Root's successful sequence remains unchanged.

Six production-owner cases cover actual package/fonts/docs, every missing font
(including late failures with four live font buffers), empty font, missing and
incomplete package, zero/negative dimensions, primary-only scope exit, actual
fps/palette name collisions (including acquired fps before palette failure),
fresh-owner restart, repeated acquisition rejection and automatic release of
actual rendered geometry with compiled/released counts equal. All 57 affected
cases from 7 suites passed normally in 5,779 ms and under ASan/UBSan in 32,134 ms,
no skips. Both builds were warning-free; format/diff/order checks passed.

Self-check retained fixed singleton/context/font-batch contracts and added no
general cleanup framework or hot-path work. Main is 5,965 lines; the added guards
are necessary until final composition owns these registrations. Headless tests
exercise the production Rml owner, not the complete root startup failure path.
GPU acquisition and root feature-guard injection, the SDK's disabled-FreeType
initialization failure branch, SDL/window/kernel acquisition, real native picker,
high-DPI and full integration remain explicitly unverified/open. No performance
or clean LSan claim.

## C19e SDL/window extraction plan

Tier 2 checkpoint. Actual process input is copied app version/identifier metadata
and the existing SDL VIDEO|EVENTS|GAMEPAD initialization. The sole desktop window
is 1280x720, Vulkan/resizable/maximized; output is its SDK borrow used by renderer,
Rml and device adapters. SDK errors log and return false. Preserve those flags,
show/query order and normal kernel-before-window/SDL shutdown order.

Move metadata/video/window acquisition and explicit shutdown into a narrow
ClientSdlRuntime alongside the existing bootstrap functions. Keep explicit root
cleanup in this mechanical commit; automatic cleanup and kernel/renderer guards
are the next separate change. SDK window pointer is required by SDL/Vulkan and
identifies a true singleton; it is not a pointer-heavy engine hot path.

Cost remains SDK allocation and initialization, one owning window pointer and
acquisition bit, no new service/configuration/options/thread/cache. Simplification
reuses SDL and avoids configurable window flags solely for tests. Real dummy-video
fixtures can prove initialization and the actual fixed Vulkan window failure,
while a successful Vulkan window is unverified without desktop/GPU support.
Done: normal/sanitized builds, real dummy/invalid-driver paths and idempotent
explicit cleanup, related input/loading/Rml checks, unchanged root ordering and
no whole-state dependencies. Batch semantics do not apply to the single process
SDL/window lifecycle. Full partial startup, successful Vulkan/native-picker/high
DPI and final routing/frame integration remain open.

## C19e SDL/window extraction result

ClientSdlRuntime owns the existing process initialization bit and SDL window;
metadata, VIDEO|EVENTS|GAMEPAD, fixed Vulkan/resizable/maximized 1280x720 creation,
show and explicit window-before-SDL cleanup moved unchanged. Root still queries
actual logical/pixel sizes and explicitly stops kernel before desktop shutdown.
The two real SDK cases prove copied metadata/subsystem flags, dummy driver's
actual Vulkan-window failure, invalid-driver failure and idempotent cleanup.
All 28 affected cases from 5 suites passed normally (207 ms) and under ASan/UBSan
(856 ms), no skips; normal/sanitized builds were warning-free, format/diff passed.
Main is 5,951 lines.

Self-check: no configurable window flags, worker, registry, whole-state API or
extra resource copy was added. Required cold SDK pointer and process singleton
contracts are documented. Automatic SDL/kernel/renderer failure cleanup is the
next separate change. A successful Vulkan window/renderer acquisition, physical
devices, native picker and full root/frame/input integration remain unverified/
open. No performance or clean leak-check result is claimed.

## C19f SDL/kernel/renderer failure cleanup plan

Tier 2 correctness checkpoint. Before the fix a scoped SDL owner that initializes
real dummy video and fails the fixed Vulkan-window creation leaves all subsystems
live after destruction. Demonstrate this through the production owner. Then make
the one acquisition bit mean initialization attempted, so even failed SDL_Init
is followed by SDL_Quit. Reject repeated live video/window acquisition and window
creation without video. A default untouched owner calls no SDL cleanup.

Add a narrow kernel lifetime scope over the existing start_client_kernel/services
shutdown protocol; construction failure invokes existing shutdown and propagates
the exception. Root constructs the untouched SDL owner before the kernel owner
and renderer guard: reverse cleanup is feature/Rml, renderer, kernel, window/SDL,
matching successful teardown. Renderer shutdown already handles its uninitialized
partition; its scope guard follows successful initialize before swapchain work.
No additional SDL/window flags/options, registry, services interface or AppState
is exposed. CLI continues using its existing bootstrap/cleanup.

Cost: the same singleton resources, one attempted bit, fixed cold validity tests
and stack cleanup. SDK pointers stay external API borrows; no engine hot loop is
changed. Simplification uses SDK state and existing idempotent shutdown instead
of separate ready/failed/window states or a cleanup graph. Done: prior failure,
real dummy/invalid-driver automatic cleanup and restart, repeated/invalid calls,
actual configured test-server kernel lifetime and native payload disposal before
kernel/SDL teardown, normal/sanitized builds and affected checks. Successful
Vulkan/renderer failure injection and native driver internals remain unverified;
full routing/frame composition is still required.

## C19f SDL/kernel/renderer failure cleanup result

The pre-fix dummy/Vulkan failure test left SDL_WasInit(0) = 25,120 after owner
destruction. SDL owner destructor now closes its attempted acquisition, even
a failed SDL_Init; untouched owner does no SDL work. Repeated initialization
and window creation without owned live video reject before changing metadata/
SDK state. Same-owner/fresh-owner restart works when its video hint is configured
again: the first test run exposed the false fixture assumption that SDL_Quit
preserves hints. No production restart policy was added.

ClientKernelRuntime owns the existing bootstrap/services lifetime and propagates
constructor errors after existing shutdown. Root declaration order now gives
feature/Rml → renderer → kernel → window/SDL on early return and success.
A renderer scope guard covers swapchain/Rml failures after successful renderer
initialization. The normal feature/Rml/render/workspace shutdown order remains
explicit, and guards avoid SDK calls on reset context borrows.

Six real SDL cases cover metadata/subsystems, fixed Vulkan-window rejection,
invalid driver, automatic failure cleanup, untouched-owner preservation of another
owner's video, repeated/invalid acquisition and restart. The configured dedicated
server kernel case starts real services, queues and disposes an owned native
result, verifies runtime/resource services gone while SDL video remains live,
then verifies zero SDK subsystems after desktop destruction. Original kernel
configuration is restored for the harness. All 32 affected cases from 5 suites
passed normally (1,225 ms) and under ASan/UBSan, no skips; builds warning-free,
format/diff/declaration-order checks passed.

Self-check: one attempted bit replaces ready/failed states, kernel scope adds no
allocation/interface/options, and SDK singleton/pointer/error contracts remain
explicit. Successful desktop Vulkan window/swapchain/renderer acquisition, GPU
failure injection, low-level allocation/constructor-exception injection, native
driver internals, physical devices/high-DPI and complete root integration remain
unverified. Remaining native input/one routing authority and final composition/
integration gates are still required; no performance or clean LSan claim.

## C10b9 native creature color input plan

Tier 1. Actual profile provider emits four color editor rows (hair, skin, two
tattoos) for valid humanoid colors, otherwise an empty batch. Rows carry numeric
channel, value [0,175] and one of two existing palette IDs. Actual UI has close,
channel, 16x11 palette, field and selector-container controls in that priority.
Input is one main-thread SDK mouse release, current live creature/tab/surface,
module/resource generations and existing mutation epoch; output is an existing
selector transition or backend color command/undo, plus content-refresh intent.
ASSUMPTION: valid palette clicks dominate this cold group — affects no allocation
or measured optimization. DOM geometry/strings are borrowed only during capture.

Capture an owned tab and scalar channel/value/palette/cell before SDK callbacks;
consume once and recheck current object/tab/generations/epoch and provider row
before applying a post-release edit. Keep channel opening before SDK release,
other controls after release, and shared SmallS-selector close before field
opening; recheck owners after that synchronous callback. Matched rejected
controls still discharge SDK release, following the existing C10 invalid/stale
press contract, while issuing no backend command. Root only applies refresh.
The release helper records the actual before/after phase already selected by
existing workbench actions; this changes no SDK dispatch count.

Cost: one cold tab-string copy and, for post-SDK field/palette operations, two
four-row provider reads/comparisons around dispatch; channel opening retains its
single existing provider lookup before SDK. No per-motion payload, new epoch,
lookup, event bus or AppState interface. Fixed palette math and existing SmallS
provider/backend remain authoritative. Reuse validated fresh rows instead of a
third provider lookup for field opening. Singleton selector edges cannot be
batched across synchronous callbacks; provider and color arrays remain batches.

Done: actual markup/geometry, pre-release channel ordering, owned selection after
DOM replacement, once-only backend edit/undo, independently stale channel/value/
owner/tab/generation/epoch rejection and malformed controls; normal/sanitized
builds and affected view/input cases. Full root routing, desktop control feel,
physical devices/high-DPI and final composition remain open. No speed claim.

## C10b9 native creature color input result

Moved five native color-control families into an owned capture/once-only apply
boundary in appearance_view. Tab, owner, channel, source value/palette and selected
cell survive SDK DOM replacement. Channel opens before SDK release; field/palette/
close follow release and fresh validation. Field caller closes shared SmallS
selector first; apply rechecks identity before touching native state. Release
helper now records the actual existing before/after order for color and workbench
commands. Matched invalid controls discharge SDK press without a backend command.
Root retains refresh and SDK forwarding coordination, not palette lookup/edit
policy. The two unused root color wrappers are removed. Main is 5,900 lines.

Four new cases use actual provider rows and markup/geometry. SDK mouseup observes
the newly opened channel and replaces DOM; one forwarding obligation is enforced.
An owned palette selection survives markup replacement, commits once and undoes
exactly. Shared selector close sees the old native selector/channel before its
DOM replacement and subsequent field opening. Real channel changes, actual direct
script color change without advancing toolset epoch, actual object replacement
and tab switch reject old requests without extra commands/undo/logs. Corrupted
module/resource/epoch/value/palette/cell metadata rejects independently; these
packet checks do not claim actual module reload. Malformed fields release real
SDK active presses and issue no commands.

All 45 affected cases from 5 suites passed normally in 20,245 ms and under
ASan/UBSan in 115,421 ms, no skips. Final builds passed without warnings after
adding the required public Rml vector include and explicit fixture float casts.
Formatting/diff checks passed. Self-check retained existing SmallS/backend/rule
ownership, fixed palette layout, singular callback-dependent selector edges and
batched provider/colors, reused fresh rows to avoid a third field lookup and
added no new epoch/table/general interface. Full production root routing, real
module/resource reload, physical controller/high-DPI/desktop feel and final
composition/integration remain open. No performance or clean LSan claim.

## C10b10 native appearance catalog plan

Tier 1. Actual input is copied, authoring-sorted appearance catalog rows and
filtered dense indices, SDK controls carrying semantic appearance IDs/field tags,
current Creature/Placeable/Door values and existing tab/module/resource/epoch
facts. Catalog volumes depend on the actual loaded rules tables; no distribution
or event rate is measured. Rows are stable within module generation; query, field,
selector and live values can change during synchronous release/close callbacks.
Output is an existing appearance/accessory command/undo or selector transition
and explicit refresh/open effect. Unsupported type/field, malformed ID, stale
owner/generation/query/value or unavailable catalog rejects without a command.

Keep Back's separate capture entry before surface clicks; group the four cycle
buttons, catalog field and semantic-ID row after surface clicks. Own tab/query
and scalar live values; Door needs both appearance kind and generic type so two
non-generic styles cannot compare equal merely because their generic value is
unavailable. Derive cycle's target using the existing sorted-row wrap/fallback
policy before SDK, then validate it and the live values after release. Consume
once, reuse existing commit/rebuild/close functions. Field caller closes shared
SmallS selector before fresh apply; root retains window sync/focus and refresh.

Cost: cold tab/query ownership, two live-value provider reads around SDK, existing
cycle lookup and O(matches) membership verification for selection. No per-motion
payload, new catalog/epoch/controller/framework, whole-state parameter or new
row pointer lifetime. Selection is a synchronous singleton edge; catalog/filter
providers remain batch transforms. Simplification precomputes the existing cycle
ID once and avoids its third live-value lookup during apply; backend/rules remain
authoritative. Done: actual selector/field/cycle controls, copied semantic ID after
DOM replacement, one command/undo, wrap/fallback, both Door selectors, independent
stale owner/query/field/value/generation and malformed-release checks, normal/
sanitized builds and affected suites. Full root authority/frame integration and
desktop/native-device/high-DPI behavior remain open; no speed claim.

## C10b10 native appearance catalog result

Captured Back separately before surface selection; the four Placeable/Door
cycle buttons, catalog fields and semantic-ID rows now use the appearance
owner's capture/apply functions. SDK release and shared-selector close precede
fresh validation. Copied tab/query/field/live values survive DOM replacement;
Door comparison includes both selectors. Existing catalog ordering, wrap and
non-generic fallback, commands, undo, selector retention and refresh remain
authoritative. Removed the unused root cycle delegate.

Normal and combined ASan/UBSan client/test builds pass without compiler
diagnostics. The selected appearance/input/owner/routes suites passed 34 tests
in four suites: normal 17,754 ms, sanitized 100,979 ms, no skips. After adding
actual replacement-object coverage, rebuilt both binaries and reran all three
new catalog tests: normal 2,949 ms, sanitized 16,678 ms, no skips. Actual
Bodak semantic selection after DOM replacement produces one command/undo; Back
closes without editing; Placeable and Door cycles wrap; two real non-generic
Door styles changed through rules without advancing the toolset epoch reject
the captured action. Independent query/field/selector/source-value/generation/
epoch/tab/object changes and malformed SDK presses reject without commands.
The test uses Bodak's actual catalog spelling `Bodak` (not a fabricated row).

Self-check: observed catalogs and provider values supplied the inputs; unavailable
and stale boundaries reject; cold strings and linear membership costs remain
explicit. Simplification retained the existing providers/commit path and derived
cycle target once. Single displayed selector/click exceptions are documented;
DOM borrows end at capture, engine handles retain their ABI, no feature receives
AppState, no new epoch/cache/framework or performance claim. Main: 5,900 to
5,846 lines. Full routing authority/frame composition and final integration,
desktop control feel, real module reload, high DPI and physical devices remain
open. Existing baseline VM LSan exclusion remains separate.

## C10b11 workspace controls plan

Tier 1. The existing SDL3/RmlUi main-thread client supplies one displayed tab
strip, one workbench strip, tab/subtab UTF-8 IDs, current WorkspaceState rows
and a parsed active dialog snapshot. Actual tab actions use semantic IDs; scroll
actions capture enabled/direction before release and calculate against fresh
layout afterward. Dialog selection currently happens before SDK release.
Tab counts/layouts and dialog row counts depend on loaded documents; event
distribution is unmeasured. IDs/document ownership are stable until switch/close;
focus, dirty state, strip layout and active selection can change in callbacks.

Own tab/subtab/source-active IDs and target kind/detail/document identity across
release, reject missing/stale targets, consume once, and return the existing
CommandInvocation. Root executes through its current prompt/result path and
calls one feature-owned tab synchronization function. Dirty state stays live so
close/save/discard policy remains backend-owned. Scroll uses bounded scalar
capture, fresh layout and existing clamp policy. Dialog row parsing/selection
is one immediate feature call before release; no owning DTO is needed because
no DOM borrow crosses SDK. Matched malformed presses still receive one release.

Cost: cold string/command-argument ownership, existing O(tabs/subtabs) lookup and
strip traversal, O(1) dialog index lookup; no per-motion allocation or new row
registry/epoch. Singleton UI actions are documented; backing row transforms
stay batch paths. Simplification removes four repeated command/result/tab-sync
branches and keeps prompts/undo/loading with existing owners. Done: actual
tab/subtab activate/close effects including dirty prompt, stale identity and
active-tab rejection, both fresh scroll layouts, dialog pre-release selection,
malformed/unmatched release, normal/sanitized builds and affected tests. Full
routing/frame composition and unavailable desktop/device checks remain open.

## C10b11 workspace controls result

Four tab/subtab branches now share copied semantic target/source identity,
consume-once command preparation and feature-owned tab synchronization. Root
keeps the existing prompt/result flow and content/full refresh/hover coordination.
Dirty state remains live: marking the captured tab dirty produces the existing
save/discard/cancel prompt instead of bypassing it. Both scroll buttons capture
enabled/direction and use fresh layout after release. Dialog row parsing and
selection are immediate before SDK; the adapter now records its actual
after-native release phase. Malformed matched controls release without changes.
Removed two unused root synchronization delegates.

Final normal and combined ASan/UBSan client/test builds pass without diagnostics.
Affected workspace/browser/content/input/owner/route checks: 34 tests in six
suites, normal 2,198 ms, sanitized 11,999 ms, no skips. New tests exercise actual
SDK release with DOM replacement and previous active/selected state, all four
backend tab/subtab commands, dirty close prompts, changed source/missing subtab/
active-tab rejection, both strips' changed scroll layouts, repeat consumption,
actual parsed dialog rows and malformed pressed controls. Initial failures were
fixture errors: file paths were supplied to open_tab's boolean flag, and the
workbench track lacked the actual class providing its flex layout. Corrected
fixtures use open_or_replace_tab and the real track class; no production layout
or WorkspaceState policy change was required.

Self-check: observed SDK/tab/dialog data and explicit cold-string/linear lookup
costs supplied the plan. Simplification removed repeated command/result/refresh
branches and constructs each ancestry class string once, rather than once per
ancestor. Dialog needs no retained DTO because apply precedes SDK. Singleton
actions and batched backing rows remain explicit; malformed/stale identities
reject, dirty policy stays backend-owned, no AppState feature API/new epoch/
registry/queue/performance claim. Main: 5,846 to 5,762 lines. Full routing/frame
composition and final normal/sanitized integration remain open. Native desktop
prompt interaction, device feel and high DPI remain unverified; baseline VM
LSan exclusion remains separate.

## C10b12 workbench combobox plan

Tier 1. Actual inputs are SDK option/field attributes, the current Sound details
property rows (semantic propset/field/element/editor/current value), Creature
spell-filter choices and selected class/metamagic/level/query, current combobox
selection/visibility, active tab/object and existing generations/epoch. Values
are Sound position 0..2, spell level -1 or 0..9, and provider-owned class/metamagic
keys. Row/choice counts depend on the real loaded actor; event rates/distribution
are unmeasured. Rules/schema stay stable within generation; callback focus,
query, owner, popup and selected filter can change during SDK release.

Preserve option-before-Sound-field-before-spell-field dispatch priority. Own
semantic property/filter facts and field ID before SDK; release once, validate
the fresh owner/presentation state, then use existing open/toggle/commit helpers.
Sound reuses the existing fresh-provider property comparison, including current
value, before backend commit. Root retains content refresh, window sync and
focus after effects. Invalid/missing/out-of-range or stale descriptors reject
without edits/logs; matched invalid controls still release the SDK press.

Cost: cold tab/query/field strings and bounded scalar facts, current combobox
choice lookup, O(details rows) fresh Sound snapshot and existing commit/rebuild.
No per-motion allocation, option registry, new epoch/controller/binding policy
or AppState feature API. Each displayed combobox/click is a true singleton;
choice/property providers remain batches. Simplification removes three root
branches and shares the existing fresh property comparison. Done: real Sound
open/option/undo and Creature filter open/toggle/select controls across DOM
replacement, repeat/malformed/stale owner/source/filter rejection, one SDK
release, affected normal/sanitized builds/tests. Full routing/frame composition
and unavailable desktop/device/high-DPI integration stay open.

## C10b13 tile palette and area surface plan

Tier 1. Input is one SDK area-surface/back/virtual-row click, the actual palette's
area/resource generation/current folder, indexed folder/action rows, current
query and brush (kind/value/orientation). Output is an existing surface request
or palette navigation/selection plus a presentation effect. Folder rows are
validated children; action keys are nonnegative int32 indices into the current
row batch. Malformed/stale keys reject; unavailable navigation retains the
existing feedback. Schema/resources are stable within generation; folder/query
and SDK owner can change on release. ASSUMPTION: motion remains the common
input event; cold palette-click frequency is unmeasured and affects no tuning.

On the existing main-thread SDL/Rml desktop, capture bounded scalar facts and
own the query before SDK release; consume once, compare fresh palette facts,
then reuse existing enter/leave/filter operations or select the existing brush.
Root retains renderer cancellation/preview clearing and content/window sync;
area surface remains a cross-editor coordination request. Cost is O(query
bytes) cold storage, existing folder filtering and match lookup, no new queue,
epoch, painter or copied row batch. DOM pointers are SDK call borrows only.
Displayed palette/surface are true singletons; providers remain indexed batches.

Simplification removes root folder/selection policy and duplicated attribute
parsing. No approximation or extra cache is justified. Done: actual loaded-area
palette navigation/action selection across DOM replacement, consumed/stale/query/
generation/malformed rejection, existing tile/input/window tests, warning-free
normal and sanitizer builds. Full routing/frame composition and unavailable
physical desktop/controller/high-DPI checks remain open.

## C10b12 workbench combobox result

Owning Sound property/filter descriptors replace three native root branches.
Sound fresh-provider comparison is shared with existing Details command clicks;
root retains refresh/window/focus work. Actual Sound open/option/undo, direct
propset mutation without a toolset epoch change, real wizard spell-filter
open/select/toggle, stale owner/filter/generation and malformed SDK press cases
are covered by three added production-control tests. Initial fixtures omitted
spell-row rebuilding and popup layout classes; corrected fixtures use the
actual activation/provider and popup layout. No product behavior fix was hidden.

Warning-free normal and combined ASan/UBSan client/test builds passed. All 41
checks in five affected suites passed: normal 15,579 ms, sanitizers 89,092 ms;
no skips. Leak detection remains excluded for the documented baseline VM leak.
The three new normal cases also passed separately in 2,742 ms. Root main was
5,762 to 5,718 lines before C10b13. No speed or desktop/device/high-DPI claim.
Self-check: actual data/platform/cost supplied the plan; singleton UI exception,
indexed provider batches and SDK borrow lifetimes are documented; invalid/stale
inputs consume once without commands/logs, existing commit/undo is authoritative,
no new epoch/controller/general binding was introduced. Full routing/frame
composition and final integration remain required.

## C10b13 tile palette and area surface result

Area surface attributes now produce typed requests in workspace_view. Palette
back/folder/action clicks own their area, generation, folder/query and indexed
row/brush facts; area_tile_editor consumes and validates them after SDK release,
then uses the existing folder/filter/selection operations. Root coordinates
renderer cancellation/preview cleanup and content/window synchronization.
The real ttr01 palette test navigates an actual folder and selects an actual
materialized action after DOM replacement; repeats, stale generation/folder/
query/brush/index and malformed keys reject, and root-folder Back keeps the
existing unavailable feedback. All three surface tags and unknown attributes
are characterized. No row batch, texture, epoch or binding was duplicated.

Final normal and ASan/UBSan client/test builds passed without warnings. All 63
checks in six affected tile/browser/workspace/input suites passed: normal
2,302 ms, sanitizer 12,495 ms; no skips. The added palette case separately
passed normally in 491 ms. Baseline LSan exclusion and unsupported physical
Vulkan/desktop/controller/high-DPI/manual control feel remain explicit.
Self-check: plan data/platform/cost and true-singleton exceptions hold; source
facts use existing generations and indices, DOM borrows end before release,
malformed/stale inputs reject, unavailable navigation reports existing feedback,
and simplification removes root policy without a new operation/controller.
Main: 5,718 to 5,631 lines. C10b12/b13 are committed together as a buildable
native workbench/palette checkpoint. Remaining routing/frame composition and
final integration are required before marking the parent issue complete.

## C10b14 focused workbench field keys plan

Tier 1. Actual inputs are one SDL keyboard edge, fresh SDK focus ancestors,
variable/integer/Sound-position attributes, integer text/min/max and current
workbench/combobox/command owner. Outputs are existing text adjustment, blur/
window sync, Sound option movement/open/commit, one existing command/undo and
an explicit content-refresh effect. Integer bounds/text are strict int32;
out-of-range/malformed text makes no adjustment or commit, while the matched
arrow is consumed. Up/Down repeat remains allowed; Enter/Escape retain their
existing nonrepeat gates and Ctrl/Alt/GUI exclusions. Focus/text/owner changes
on callbacks; schema and Sound option keys 0..2 are stable.

Keep the same location after shared SmallS-selector Escape and before color/
appearance/other gesture Escape. Move this contiguous field policy into
object_workbench_view, borrowing keyboard/context/document only synchronously;
copy command arguments before execution and reacquire focus for blur. Root
only reacts to handled/content-changed effects. Existing providers/commands,
window sync, variable listener and Sound commit remain authoritative. Cost:
existing SDK ancestor traversal, cold integer strings and existing command/
snapshot/window work; no new event queue/epoch or per-motion work. One displayed
focus/keyboard event is singular; row/undo transforms remain batches.

Simplification removes root field policy and redundant wrappers as their real
call sites disappear. Done: real bounded integer arrows/Enter/undo/modifier/
repeat handling, Sound open/move/Enter/undo, existing variable Enter/blur tests,
normal and sanitizer affected builds/tests. Full mode routing/frame composition
and unavailable desktop/device/high-DPI integration remain required.

## C10b15 catalog keys and selector Escape plan

Tier 1. Input is one SDL key edge, current SDK search focus, indexed Appearance/
Sound matches and virtual selection, selector/color state, active object/surface,
and current module/resource facts. Output is existing list movement/scroll/sync,
catalog commit/undo or selector close/rebuild and a content-refresh effect.
Arrow repeat, nonrepeat Enter, Ctrl/Alt/GUI exclusions and color-before-Appearance-
before-Sound Escape priority are preserved. Source lists have dynamic provider
counts; real queries/choices are observed in the loaded module fixtures, their
population/event frequency is unmeasured. Definitions stay stable within
generation; focus/filter/selection changes during synchronous callbacks.

Reuse client_input's visible focus classifier and existing appearance_view
commit/list/close helpers. Root excludes a visible command palette before the
catalog-key call and reacts to handled/content-changed effects; its existing
Escape location and interleaved gesture policies remain fixed. SDK borrows last
only for the call, semantic IDs are read from current indexed matches. Cost is
existing focus traversal, bounded virtual rows, provider lookup/rebuild and
commands; no new binding map, queue, cache or root-state API. One displayed
selector/focus is a true singleton; provider/undo rows stay batches.

Simplification removes root catalog key policy and wrappers with no remaining
callers. Done: real Appearance and Sound search arrows/Enter/modifier/repeat,
command/undo counts and Escape priority, affected normal/sanitizer builds/tests.
Full mode routing/frame composition and unsupported physical desktop/controller/
high-DPI/manual checks remain open.

## C10b14 focused field key result

The workbench view owns variable/integer Escape, bounded integer text arrows/
Enter, and Sound-position open/move/Enter policy. Root only handles the explicit
content-refresh effect at the original dispatch location. Three obsolete root
delegates were removed. Two real-control tests verify integer bounds/malformed
text/modifier/repeat handling and Sound option movement, one command/undo and
actual provider restoration. Existing variable Enter/blur coverage still passes.

Final normal and combined ASan/UBSan client/test builds passed warning-free.
All 37 affected checks in four suites passed: normal 11,394 ms; sanitizer
65,294 ms; no skips. The two new normal cases separately passed in 1,040 ms.
LSan remains excluded for the documented baseline leak. Main: 5,631 to 5,494
lines before the following catalog move. Self-check: observed data/platform/cost,
strict numeric boundaries, true-singleton focus/event and indexed provider/undo
contracts hold; SDK borrows end before return, fresh focus is queried on blur,
existing commands and listener semantics remain authoritative. No new state,
queue/epoch/binding or performance claim. Remaining routing/frame composition,
final integration and unsupported desktop/device/high-DPI/manual checks stay open.

## C10b16 workspace tab drag plan

Tier 1. Actual input is one current SDK tab/close hit and pointer point, owned
armed tab ID/start point/drag flag/scroll, current Workspace tab array, and the
rendered strip's bounds/index rows. Output is armed/dragging state, existing
clamped scroll and optionally one owning workspace.move_tab invocation. The
existing 5 px threshold and 28 px/14 px edge scrolling remain fixed; locked
prefix and valid target ranges remain existing workspace helper policy. Tab
counts and motion rates are unmeasured. Tab IDs persist until close; points,
layout/order and captured drag change between events.

Move arming and motion policy into workspace_view. SDK hit/bounds borrows end
at return; the ID owns its bytes. Motion below threshold produces no command;
dragging looks up current source/target indices, unknown source cannot reorder.
Nonfinite standalone input cancels; normal intake already rejects it centrally.
Root retains cursor choice, command execution and refresh at the original
locations. Cost: existing O(tabs) target traversal and scroll layout; owning
command strings only on an actual reorder, no new queue/controller or row copy.
One displayed strip/primary captured pointer is genuinely singular.

Simplification removes root gesture policy and two unused edge constants. Done:
actual strip arming/close exclusion/threshold/reorder/locked prefix/scroll/stale
source/cancel checks, existing browser/workspace/input tests, normal and sanitizer
builds. Full routing/frame composition and unavailable physical control-feel/
controller/high-DPI checks remain required.

## C10b15 catalog key result

Appearance/Sound search arrows/Enter and color/Appearance/Sound Escape now use
appearance_view's existing list, commit and close helpers. Root retains palette
exclusion, original Escape location and content refresh. Four obsolete delegates
and a color forward declaration were removed. The real catalog-key test checks
provider-selected Appearance IDs and a real Sound resource, repeat/modifier
behavior, exactly one command/undo, rule restoration and Escape priority.
The initial fixture set an Appearance query before the first generation reset;
corrected setup filters after reset and reads actual matches, rather than assuming
a fixed population. No product behavior change was hidden in that correction.

Final normal and ASan/UBSan client/test builds passed warning-free. All 35 checks
in four affected Appearance/input suites passed: normal 19,745 ms; sanitizer
102,282 ms; no skips. The added normal catalog-key case passed in 1,399 ms.
Baseline VM LSan exclusion remains. Main: 5,494 to 5,392 lines before tab drag.
Self-check: plan data/platform/cost and singleton focus/selector exception hold;
provider/undo rows remain indexed batches, SDK/event borrows end at return,
invalid/unavailable choices make no edit and existing commands stay authoritative.
No new queue/state epoch, binding, cache or performance claim. Remaining mode
routing/frame composition, final integration and unsupported physical desktop/
controller/high-DPI/manual control feel stay open.

## C10b16 workspace tab drag result

Workspace view now owns tab arming, the existing threshold, edge scrolling and
current-row reorder intent. Root executes the optional owning command and
refreshes at the original location. The actual strip regression checks close
exclusion, threshold, locked prefix, one reorder, scrolling, stale source and
nonfinite cancellation. Normal and ASan/UBSan client/test builds passed without
warnings; all 35 affected checks in five suites passed, normal 3,323 ms and
sanitizer 17,532 ms, no skips. The new case passed separately in 606/2,841 ms.
Baseline LSan exclusion remains. Main decreased from 5,392 to 5,338 lines.
Self-check: observed input/cost and singleton strip contract hold; DOM borrows
end at return, tab IDs own their bytes, unknown sources cannot reorder, existing
workspace operations remain authoritative. No new queue/state/cache or measured
performance claim. Full routing/frame composition and final integration remain.

## C10b17 browser row key fix plan

Tier 1. Actual browser hover/press/release reads SDK data-key strings and uses
unchecked strtol casts; 0tail can alias row zero. Output must be a fully parsed
int32 index or rejection, with each browser's existing nonnegative/array bounds
checks. The SDK strings change on DOM replacement, row order changes on refresh.
Reuse the strict parser already exercised by native SDK release tests, exposing
a call-scoped row query used by hover, press and release. SDK borrows end at
return; singleton pointer input and indexed feature arrays remain unchanged.
Cost is one existing copied attribute and linear numeric parsing per query.
Simplification removes three lenient conversions and duplicate parsing at the
release boundary. No forwarding/order/operation changes. Done: extend the actual
malformed-row SDK release test with 0tail/+0/whitespace and query rejection,
run input/browser checks and normal/sanitizer builds. Plan B is leave extraction
for the following checkpoint; this defect is committed separately.

## C10b18 browser click plan

Tier 1. Actual sidebar release compares the copied strict row index with the
armed index, selects the row, then either toggles a project container, saves a
Creature preview preference, opens a project resource or selects an area.
Folders refresh before default SDK release; resource/actor/area identities own
their bytes before early SDK release. Input is the current browser row array,
SDK attributes, backend project/generations, query and shell mode; output is
view changes or one owning resource/area/actor intent. These stable identities
must still match after callbacks; points, filters, rows and mode are volatile.
Volumes/rates are unmeasured. The desktop/main-thread/callback platform and SDK
borrow lifetime remain fixed.

Move selection/folder/actor-preference policy to browser_view using existing
refresh/save functions. Root performs early release, command-flow/preview calls
and resulting cross-feature refresh/focus. Consume the owning intent once;
reject changed mode/query/project/generation or semantic row identity after SDK
callbacks. One displayed sidebar gesture is a singleton; rows remain arrays.
Cost is existing folder rebuild/preference file write, copied cold identities,
and bounded source-row validation; no extra queue, epoch or controller.
Simplification removes nested browser policy from root without moving command
flow into the browser. Done: actual imported project folder/leaf and area rows,
SDK release with DOM replacement, one existing command, malformed/stale/consumed
intent checks; affected normal/sanitizer builds/tests. Preview renderer feel and
high-DPI/physical controllers remain unverified; full routing/frame work remains.

## C10b17 browser row key fix result

The three browser hover/press/release paths and native SDK release now share
complete int32 parsing. Malformed hover clears its index; malformed press clears
the armed index, so release cannot activate a previously armed row. The actual
SDK malformed-row test covers numeric tails, plus signs, whitespace and overflow,
with one release and no action. Both client/test builds passed warning-free;
all 24 affected checks in three suites passed (normal 2,901 ms, sanitizer
14,405 ms, no skips). LSan baseline exclusion remains. Main was 5,334 lines
before browser extraction. Self-check: observed defect, explicit rejection,
call-scoped SDK borrowing and singleton event contract hold; array bounds remain
feature-owned. No forwarding/operation/precedence change or performance claim.

## C10b19 editor key map plan

Tier 1. Actual final key-down fallback tries Tiles R, area Delete/R, then camera
W/S/A/D/Q/E/arrows/F/G. Tile and object edits exclude repeats and all modifiers;
camera allows repeats and Shift (scale 3), area/preview bindings differ and
preview G is unavailable. Visible text, palette, native dialog and current
derived PC map exclude editor actions. Missing viewport clears camera focus only
for recognized camera keys. The old root contains these binding/gate functions.
Input is copied SDL key/modifier/repeat plus freshly captured routing facts and
current viewport/focus/tile eligibility. Output is one flat edit/camera intent;
existing renderer, tile and object functions still execute it. Frequency and
distribution are unmeasured; key/focus/mode/layout are volatile per event.

Move bindings into editor_input's independent batch transform, use the actual
production facts/router for UI/map authority, and execute one returned intent at
the old fallback location. SDL/DOM/kernel borrows end before classification;
arrays own equal-length rows/results and invalid/mismatched inputs clear results.
Root processes count=1 because earlier events/callbacks change facts; batching
the whole frame would be incorrect. Cost is O(keys), fixed row scratch, no
allocation, copied world state, handler registry or second map authority.
Simplification removes three root key gates and camera binding switch; actual
commands/rules/camera mathematics are retained. Done: real Rml focus visibility,
captured routing facts, repeat/modifier/viewport/area-preview binding and PC/no
editor fallback checks, affected client/input/camera/tile normal/sanitizer builds.
Manual camera feel remains unverified. Full pointer/native UI routing and frame
composition stay open; reduce to mechanical binding extraction if precedence
cannot be preserved without changing an operation.

## C10b18 browser click result

Browser view owns strict pressed-row selection, folder toggling/refresh and
preview preference saves. Root retains release, command-flow/preview execution
and cross-feature refresh/focus. Owning intents consume once and reject changed
project/generations/query/mode or semantic row identity. The actual imported
project regression verifies folder-before-release and leaf DOM replacement,
one existing resource command, malformed/stale/consumed intents, a saved real
Creature preview preference and actual area selection. DockerDemo contains no
Creature blueprint resource; fixture setup now copies the existing Agent JSON
blueprint into the test project before opening it, rather than assuming one.

Normal and combined ASan/UBSan client/test builds passed warning-free. All 25
affected checks in three suites passed: normal 4,798 ms, sanitizer 28,086 ms;
no skips. The corrected added normal case passed separately in 2,062 ms.
Baseline LSan exclusion remains. Main: 5,334 to 5,284 lines. Self-check: observed
input/cost, explicit malformed/stale rejection and singleton sidebar/array row
contracts hold; no DOM/provider borrow survives release, existing save/command
operations remain authoritative. No queue/cache/epoch or performance claim.
Full mode routing/frame composition, final integration and unsupported physical
desktop/controller/high-DPI/manual preview feel remain open.

## C10b19 editor key map result

Editor input now classifies the existing tile/object/camera bindings through
fresh production routing facts and returns one flat intent. Root executes it at
the old fallback location. Three root gate/binding functions were removed;
unbound keys bypass viewport/focus capture. Pointer capture alone does not own
keyboard input; earlier exclusive gesture handling remains fixed. Area/preview
camera bindings, repeat/Shift scale, edit priority, missing-viewport focus,
malformed/mismatched rows, visible/hidden actual Rml focus and all PC roles are
covered. No camera/edit/rule operations were replaced.

Normal and ASan/UBSan client/test builds passed warning-free. All 78 checks in
eight affected input/PC/preview/camera/tile suites passed (normal 7,887 ms,
sanitizer 41,896 ms; no skips). Added two cases passed in 14/51 ms. XML properties
measure input/action rows at 24/16 bytes on this configured desktop build.
Baseline LSan exclusion remains. Main decreased 5,284 to 5,189 lines.
Self-check: observed keys/data/cost and explicit rejection hold; independent
batch rows retain no borrows and root uses count=1 for ordered callback facts.
No extra map authority, state, queue/cache or performance claim. Full pointer/
native UI routing, frame composition, final integration and unsupported physical
desktop/controller/high-DPI/manual camera feel remain open.

## C10b20 native UI release fix plan

Tier 1. Actual mouse-up completion sets native_handled after matched Home and
shell controls; invalid Home rows and shell output/dock commands can reach this
point with no SDK release. Default forwarding excludes native_handled, and the
explicit adapter also rejects it, leaving the SDK press active. Input is one
already completed native UI left release, its dispatch obligation and current
SDK context. Output is exactly one SDK release after native work; existing early
release paths must remain once-only. Context/event borrows end at return. Common
valid early-release controls keep their straight-line path; unfinished native
UI releases complete at the final matched-control boundary.

First add/run a regression on the production adapter showing a real SDK press
survives a consumed native release. Then allow explicit after-native left UI
release despite native consumption and complete that obligation in root. Keep
default forwarding suppressed and consumed keys/down/other events rejected.
Cost is bounded dispatch checks and the existing required SDK call; no queue or
new capture state. One UI context/release is a true singleton. Simplification
separates native consumption from its explicit SDK release obligation. Done:
failing/passing SDK active-state and one-release assertions, early-release no
duplicate and consumed keyboard checks, affected input/browser normal/sanitizer
builds/tests. Further Home/shell extraction follows this separate fix.

## C10b21 Home row plan

Tier 1. Actual Home controls select an indexed area, remove indexed recent
history with preference rollback, or refresh filesystem errors and queue opening
an indexed recent project. Input is the current SDK ancestor/key, Home area and
recent arrays, backend/catalog generations and preference path/docks. Output is
one owning area/project intent or persisted history change. Path/resref identities
are stable until catalog/history changes; row order/errors/focus/SDK markup are
volatile. Counts/rates are unmeasured. Desktop synchronous SDK callbacks require
copying identities before release and validating current source afterward.

Move row preparation/validation to browser_view, preserving area/remove/open
priority and recent filesystem refresh before release. Root keeps early release,
command/queue calls and resulting UI refresh/message boxes. Invalid matched rows
make no action and use the separately fixed after-native SDK completion. Move
the existing history copy/remove/save/rollback to client_preferences' batch API;
invalid indices reject all, empty batch skips I/O, save failure restores rows.
Cost is existing cold filesystem checks, identity copies, O(history) rollback
storage and existing remove/atomic-save work; no new queue/state/cache/controller.
One displayed Home pointer click is a singleton; catalogs/history remain arrays.
Simplification removes root row parsing/source references and preference
transaction policy. Done: real generated Home controls across SDK DOM replacement,
semantic stale/consumed/malformed source checks and real preference persistence/
rollback, affected normal/sanitizer builds/tests. Full pointer routing/frame
composition and unsupported physical desktop/high-DPI/manual checks stay open.

## C10b20 native UI release fix result

The production SDK regression failed before the fix: zero mouse-ups, no recorded
phase and a still-active SDK control after native consumption. Explicit
after-native left UI release now completes that obligation; consumed keys/down
and default forwarding remain excluded. Root finishes matched native toolset and
palette clicks after their existing actions; earlier release records reject a
duplicate. No DOM/source pointer is used after final dispatch.

Normal and ASan/UBSan client/test builds passed warning-free. All 67 checks in
six affected input/browser/workbench/appearance suites passed (normal 33,351 ms,
sanitizer 182,236 ms; no skips), including the previously failing real SDK press.
Baseline LSan exclusion remains. Main increased 5,189 to 5,193 lines before Home
extraction. Self-check: observed dispatch state/cost, explicit invalid/null/tag
rejection and singleton event/SDK borrow contract hold; native handling remains
independent of propagation, exactly one obligation is recorded before callbacks.
This is a separately reproduced fix; existing native actions and their ordering
remain unchanged. No queue/capture state/performance claim. Remaining routing,
frame composition and final integration stay open.

## C10b22 shell input plan

Tier 1. Actual shell native clicks use a dock widget attribute or four output
toggle IDs; output Ctrl/GUI+A/C reads visible current output focus and byte-range
selection. Output is one existing owning dock/channel command or selection/
clipboard intent. Current dock/output rows and flattened text belong to shell;
SDK attributes/focus are borrowed only for the call and change on refresh.
Counts/rates are unmeasured. Existing main-thread SDL/Rml lifetime/ordering holds.

Move click capture/consume and output key policy to shell_view. Root executes
existing backend commands, synchronizes visibility for dock activation and sends
owning clipboard text through the existing SDL system interface. Preserve native
actions before the separately fixed SDK release. Empty/unknown toggle metadata
does no command; unsupported nonempty dock widgets retain backend rejection.
Consume clicks once; clipboard ranges clamp to the current text boundary.
Cost: existing DOM ancestry/focus traversal, owning cold attribute/command/text
copies, no extra state/queue/registry. One displayed shell/current click or key
is a true singleton; output rows/text retain existing batch storage.
Simplification removes root widget/channel parsing and output shortcut policy.
Done: actual generated dock/output controls, SDK press/release, one command,
malformed/consumed metadata and UTF-8 clipboard/range/focus/modifier/repeat checks;
affected normal/sanitizer builds/tests. Full pointer routing/frame composition
and physical desktop/controller/high-DPI/manual checks remain open.

## C10b21 Home row result

Browser view now owns Home ancestor/key capture, owning area/project identities,
pre-release recent filesystem refresh and once-only current-source validation.
Root keeps release, command/queue and cross-feature refresh/message boxes.
Preferences own the existing batch remove/save/rollback transaction; empty
batches skip I/O, invalid indices reject all and boolean save failure restores
the full original rows. Existing allocation/exception propagation is retained.
Real generated Home controls verify SDK DOM replacement, stale/consumed/malformed
identities and current filesystem errors; real preference files verify history
order, persistence, failed-save rollback and project files retained. The existing
imported-project case also verifies current/stale loaded area intents.

Both client/test builds passed warning-free. All 39 affected checks in six
browser/preferences/input/loading suites passed (normal 5,141 ms, ASan/UBSan
26,076 ms; no skips). Two added/extended normal cases passed separately in
2,034 ms, sanitizer 10,869 ms. Baseline LSan exclusion remains. Main: 5,193 to
5,182 lines. Self-check: actual input/cost, explicit malformed/stale/invalid/save
boundaries and singleton click/batch history contracts hold; no DOM/source-row
borrow survives SDK release. Existing operations/rules and early-release order
remain authoritative; no new queue/state/cache or performance claim. Remaining
mode routing/frame composition, final integration and unsupported physical
desktop/controller/high-DPI/manual behavior stay open.

## C10b22 shell input result

Shell view owns dock/channel click capture, once-only owning command construction
and output selection shortcuts. Root retains command execution, dock visibility
synchronization and clipboard delivery. Actual shell controls verify one channel
command, one SDK release, dock activation, rejected empty/unknown metadata and
consumption. UTF-8 selection checks verify clipboard ownership, visible focus,
repeat/modifier rejection and clamped stale byte ranges.

Both client/test builds passed warning-free. All 33 affected checks in five shell,
input/routing/action/workspace suites passed (normal 738 ms, ASan/UBSan 3,549 ms;
no skips). The added real-control case passed separately in 502/2,657 ms.
Baseline LSan exclusion remains. Main: 5,182 to 5,147 lines. Self-check: current
data/cost, explicit metadata/range rejection and singleton event/DOM borrow
contracts hold; root policy and cold string ownership moved without adding a
queue, registry or cache. Existing backend commands and action-before-release
order remain authoritative. No performance claim. Remaining map routing, frame
composition and final integration remain open.

## C10b23 tab target validation plan

Tier 1, separately scoped defect. Actual generated workspace tabs carry decimal
data-index values and owning IDs; current strtoull accepts trailing bytes as a
different valid row. Input is the displayed tab array/DOM child range and one
pointer point; output is an existing reorder index. Main-thread Rml lifetime,
locked prefix and current tab count constrain the valid range. Generated keys
are normally canonical decimal; malformed keys are exceptional, event rates
unmeasured. ASSUMPTION: externally stale/malformed DOM metadata is possible —
affects rejection, not an added synchronization mechanism.

Add an actual generated-tab regression and run it against the current production
target helper before fixing. Strict full-size_t parsing rejects malformed keys;
retain existing bounds, locked prefix and final insertion policy. Cost: one
linear bounded child pass and existing attribute copies; no persistent state.
One displayed drag is a singleton; tab data remains a batch. Simplification uses
existing from_chars rather than adding a parser abstraction. Done: regression
fails before and passes after, generated valid/negative/tail/overflow key cases
and affected normal/sanitizer tests pass. Limit: no new tab reorder semantics;
plan B is retain the helper and fix only the confirmed parser defect.

## C10b24 viewport markup and classification plan

Tier 1. The actual root emits identical viewport elements for area/preview tabs,
with escaped resource detail and one kind-specific empty placeholder. The input
adapter instead checks a different ID. Inputs are the current owning tab and
current visible Rml hit ancestry; outputs are existing markup and routing facts.
Area/preview details are stable between tab/resource changes, hit/focus is volatile.
The desktop main-thread SDK and one displayed viewport constrain the transform;
there is no measured event distribution. ASSUMPTION: nonempty resource tabs are
the common case — affects no optimization or validity policy.

Move only the duplicated viewport fragment to workspace_view, preserving bytes
and rejecting other tab kinds with no output. Root retains surface composition.
Add real generated markup/hit regression, run against current production input
capture before correcting the ID. Hidden/detached elements remain excluded by
the SDK hit test; no cached DOM pointer survives a call. Cost: existing cold
markup allocation/escaping and SDK ancestry traversal, no retained state. The
one displayed viewport is a true singleton, not multiple duplicate DOM IDs.
Simplification removes duplicate markup and aligns the existing classifier with
the existing DOM. Done: exact fragments for populated/empty area/preview tabs,
real hit test fails before ID fix and passes after, affected builds/tests pass.
No camera/DPI/new map semantics; plan B remains this bounded fragment/ID fix.

## C10b23 tab target validation result

The real generated-tab regression failed before the fix: trailing, plus-prefixed
and space-prefixed keys all aliased row 2. Full from_chars size_t parsing now
skips malformed/overflow keys before the unchanged range/locked-prefix/insertion
policy. Valid generated keys still select the same reorder target.
Both client/test builds passed warning-free; all 20 affected browser/workspace
checks passed (normal 6,479 ms, ASan/UBSan 32,260 ms; no skips). Baseline LSan
exclusion remains. Self-check: actual source and cost observed, singleton drag
and batch tab contract retained, explicit parsing/range rejection, no new state
or abstraction. This separately reproduced fix precedes further extraction;
no performance claim. Main remains 5,147 lines at this checkpoint.

## C10b25 captured/blocked world route plan

Tier 1, separately tested routing defects before native integration. Actual root
viewport drags process motion before hovered panel hits, while current pure
routes prioritize hovered UI even for an editor/PC pointer capture. Root loading
gates also block world operations; current pure native routes expose an editor
recipient under world_input_blocked. Inputs are existing flat routing rows,
outputs existing recipients/source eligibility; no new data layout or owner.
One main-thread selected map and current capture are volatile per ordered event;
contiguous batches resolve independently. No measured event distribution.

Add failing production-route regressions for captures crossing both UI contexts
and blocked world rows before correcting precedence. Explicit pointer capture
claims pointer edges, not keys; actual command modals still win. Blocked unclaimed
world rows return unavailable and no native world recipient. Unknown tags/mixed
owners retain current invalid rejection. Cost: existing O(N) flat batch traversal,
bounded scratch, no allocations; linear access and state-dependent predictable
branches are a hypothesis, not a measured claim. Simplification removes competing
root/pure ownership decisions; no queue/new route type. Done: before-failure and
after-pass, existing route/PC/editor/input suites pass in both builds. Limit:
bindings/lifecycle/role policy remain unchanged; plan B is a smaller precedence
fix with the existing route protocol.

## C10b26 world pointer authority plan

Tier 1. Actual root down/motion/wheel paths choose PC controls independently from
the already-derived control map. After a tab switch in the same SDL batch, PC
down can reach editor orbit; while F9 placement is pending, wheel and captured
motion can reach editor camera/object actions. Inputs are ordered SDL pointer
events, current visible DOM, existing ownership facts and displayed viewport.
Outputs are existing editor operations or shared PC pending look/zoom/clicks;
F9 actor placement remains lifecycle work. Rates/branch entropy are unmeasured.

Use one current capture/resolve adapter over the existing flat route batch at
each world boundary after earlier blur/popover callbacks. Root derives ownership
once for that boundary; a non-area displayed viewport makes F9 world unavailable.
Only an editor recipient may reach editor operations; only an eligible PC
recipient may feed pending PC pointer input. Unavailable/blocked/invalid world
input is consumed without editor fallback. PC placement wheel/look is consumed
without changing the editor; existing active-PC translation remains authoritative.
Captured drags retain ownership across hovered panels; unavailable capture ends
the drag. True singleton current event/viewport; no queued copies or cached facts.

Cost: existing DOM/focus traversal plus one bounded routing row per reached world
boundary; no new allocations in the routing transform. SDL/Rml borrows are needed
for current SDK hit/focus and last only the call; canonical engine handles remain
necessary at the cold current-session identity boundary. Simplification removes
three independent world-map fallbacks and an unused lifecycle parameter. Done:
production adapter tests exercise fresh viewport/panel visibility, captures,
blocked/unknown/unavailable maps and shared player/DM routes; affected builds/tests
pass. Root event integration and physical desktop feel remain an explicit manual
gap; no claim of an end-to-end failing root GUI fixture. Limit: no new camera,
DPI, binding or F9 teardown semantics; plan B is this smaller map-authority guard.

## C10b24 viewport classification result

The production generated-markup regression failed before the ID fix: actual
area/preview viewport down, motion and wheel hits were classified as toolset UI,
including player/DM rows that should select PC or report unavailable. The
classifier now recognizes the actual workspace_viewer_viewport ID. Workspace
view owns the duplicated escaped viewport fragment; exact populated/empty
area/preview fragments and no output for other kinds are verified. Root retains
surface composition. Hidden generated viewports do not claim world hits.

Both client/test builds passed warning-free. All 34 affected input/browser/
workspace checks passed (normal 5,902 ms, ASan/UBSan 33,041 ms; no skips).
Baseline LSan exclusion remains. Main: 5,147 to 5,127 lines. Self-check: real
generated inputs, explicit kind/visibility rejection, singleton viewport and
call-lifetime DOM borrow hold; duplicate formatting work removed, no new retained
state or performance claim. Further native route integration remains open.

## C10b25a captured map change plan

Tier 1, captured-origin validation before native integration. Actual root stores
only a viewport dragging boolean and derives capture owner from the current map;
that can change a PC gesture into an editor gesture on F9 exit. Existing pure
facts reject editor capture under the PC map, but accept the inverse. Inputs are
existing flat map/owner facts after a mode change; output must be invalid/no
action, not a newly interpreted gesture. No measured volume/frequency.

Add a failing production batch regression for both mismatched world capture
origins, then make validation symmetric. Native integration replaces the boolean
with its actual captured owner, retaining the same number of fields and canceling
mismatches through the existing authority. Cost: existing row validity checks,
no allocations/new protocol. Batches remain independent; one pointer capture is
a true singleton. Simplification removes a map-derived guess about capture
provenance. Done: before-failure, after-pass and affected route/PC/editor checks
in both builds; no role/binding/lifecycle changes. Plan B: keep the existing flat
protocol and reject only the observed incompatible owner/map pair.

## C10b25 captured/blocked route result

The production-route regression failed before the fix: hovered UI stole captured
world motion/release, unavailable PC capture was reported as UI, and blocked
world rows retained native recipients. Pointer capture now precedes hovered
targets, command modals retain priority, keys remain UI-owned and blocked
unclaimed world rows return unavailable with no native action. Existing source
eligibility and invalid input handling remain authoritative.

Both client/test builds passed warning-free. All 26 affected input/routing/PC/
editor checks passed (normal 174 ms, ASan/UBSan 760 ms; no skips). Baseline LSan
exclusion remains. Main remains 5,127 lines. Self-check: actual root/pure contract
differences reproduced, O(N) allocation-free batch and explicit invalid/blocked/
unavailable policies retained; competing ownership decisions removed without
new route types or state. No performance claim. Native integration remains open.

## C10b25a captured map validation result

The production batch regression failed before the fix: an old PC capture under
the editor map selected an editor action. Map/world-owner validation is now
symmetric; both mismatched capture origins reject with cleared recipients and
source eligibility. Both client/test builds passed warning-free; all 27 affected
input/routing/PC/editor checks passed (normal 224 ms, ASan/UBSan 931 ms; no skips).
Baseline LSan exclusion remains. Self-check: actual map-change defect reproduced,
flat batch ownership and explicit invalid behavior retained, no new allocation,
state or abstraction. Main remains 5,127 lines at this checkpoint. Captured
provenance/native integration follows; no performance claim.

## C10b27 editor wheel plan

Tier 1. Actual editor wheel policy selects camera zoom, area sound radius, or
area creature/item/placeable scale/rotation using current resolved recipient,
viewport kind, object type, visible text focus and modifiers. Root currently
owns the selection and object command math/formatting. Inputs are flat owning
facts and finite nonzero wheel amounts; outputs existing wheel intents, then
existing backend commands or renderer zoom. Rates/common action distribution
are unmeasured; these facts vary per event, object/profile data per mutation.
The platform remains the single main-thread SDL/Rml/kernel/Vulkan client.

Move selection to an equal-span editor wheel batch transform; only a resolved
editor recipient can act. Unknown viewport/recipient and nonfinite/zero amounts
reject; other object types retain camera zoom. Move radius/transform execution
and exact float formatting to area_object_editor runtime; root keeps camera/
tile-preview coordination and gesture cancellation. Preserve sound min clamp,
nonfinite radius drop and backend numeric rejection/diagnostics for transforms.
Singleton execution borrows canonical backend/renderer handles synchronously;
indices cannot replace the current ObjectManager/renderer command APIs.
Cost: O(N) flat selection, existing pow/format/command storage for actual edits;
no new cache/state/queue, no performance claim. Simplification removes duplicate
focus checks and root object-wheel policy/formatting. Done: bindings/focus/map/
malformed batch checks and real radius/transform command counts with undo;
affected normal/sanitizer builds/tests pass. Limit: no binding/math/camera changes;
plan B is this narrow selector/executor, not a full pointer handler framework.

C10b27 observed integration boundary: tests exclude renderer-dependent area
runtime; linking the initial executor test failed. Keep command transactions in
the existing renderer-independent area_object_editor source and return the owning
optional result. Root presents it and synchronizes its fresh current selection
after command execution. This reduces the interface from renderer/shell/two
object borrows to backend/context/target and uses existing test linkage; no new
renderer test framework or source-library changes. Direct renderer integration
remains in its existing gate/manual coverage.

## C10b26 world pointer authority result

Native viewport down/captured motion/wheel now use the existing derived map and
one fresh capture/resolve adapter after earlier callbacks. Only editor routes
reach editor operations; PC routes feed pending click/look/zoom. F9 placement
consumes look/wheel without changing editor controls, and non-area/unavailable/
blocked routes cannot fall through. Capture stores its original editor/PC owner
instead of a boolean interpreted through the new map; startup/stop/failure and
focus loss reset it, incompatible/missing capture cancels and drops pointer data.
Default SDK forwarding uses the same singleton adapter over the flat batch.

Both client/test builds passed warning-free. All 86 affected checks in nine
input/routing/editor/PC/gesture/camera/tile/preview/runtime suites passed (normal
9,287 ms, ASan/UBSan 51,493 ms; no skips). Two new/extended real SDK adapter cases
passed separately in 32 ms normal, covering generated viewports, fresh DOM
replacement, panel capture, blocked/unknown/unavailable maps and shared F9/
player/DM ownership. Baseline LSan exclusion remains. Main: 5,127 to 5,160 lines
before editor wheel extraction. Self-check: current event/data/cost, explicit
map/source rejection, singleton borrow and canonical identity contracts hold;
three independent map fallbacks and an unused parameter removed without a queue
or cached facts. Root GUI integration/control feel remains unverified; no claim
of a failing end-to-end root GUI fixture or performance improvement. Remaining
native feature policies/frame composition/final integration stay open.

## C10b28 controller focus route plan

Tier 1, separately reproduced route defect before controller-edge extraction.
Actual PC held eligibility permits controllers while visible text fields own
keyboard input; root East/Back navigation cancellation follows that policy.
Current native routes instead classify all non-pointer focused input as UI,
including gamepad edges. Inputs are current flat gamepad/key rows with real
focus tags and PC availability; outputs existing recipients/source eligibility.
Focus changes per event; one desktop main-thread consumer, rates unmeasured.

Add a failing production-route regression, then constrain text focus ownership
to key/text sources. Actual command modal/palette blocking still owns controller
input, and UI keyboard focus still blocks keys. Cost: existing O(N) flat batch
selection, no state/storage/allocation; no performance claim. Simplification
aligns native edges with the existing held-source policy. Done: before-failure,
after-pass and relevant route/PC/input suites in both builds. No controller
binding/connection/lifecycle changes; plan B remains this bounded condition fix.

## C10b27 editor wheel result

Editor input owns flat wheel selection from the already-resolved native
recipient. The renderer-independent area editor owns existing radius/transform
math, exact float formatting and backend transactions, returning an owning
optional result. Root keeps gesture cancellation, camera/tile coordination,
result presentation and fresh post-command selection synchronization. No renderer
test stub or new source-library linkage. The real test required area-tab scope
and an explicit actor spatial component, matching the operation contracts.

Both client/test builds passed warning-free. All 28 affected checks in five
input/editor/PC/object-edit/workbench suites passed (normal 2,519 ms, ASan/UBSan
12,781 ms; no skips). Two added cases passed separately in 1,039 ms normal.
Real backend checks prove one undo per radius/scale/rotation edit, exact replay,
minimum-radius clamp, nonfinite-radius drop, backend overflow rejection and no
command for unsupported/nonfinite intents. Wheel input/action rows measure
20/8 bytes in this build. Main: 5,160 to 5,114 lines. Baseline LSan exclusion
remains. Self-check: current data/cost and flat batch/singleton transaction
contracts hold, explicit recipient/viewport/numeric policies, no speculative
state or abstraction; duplicate focus queries and root wheel math removed.
No performance claim; renderer synchronization remains in existing/manual
coverage. Remaining native feature policies/frame composition/final gates open.

## C10b29 shared controller edge plan

Tier 1. Actual native East/Back down edges OR navigation cancellation into PC
pending flags; fixed ticks consume it once, held-source gates discard claimed
controller input. The root owns those bindings. Inputs are physical controller
button/down/eligibility facts; outputs update existing PreviewInputSample flags
without changing movement, pointer edges or other payload fields. Ordered edges
vary per event; one displayed consumer, independent pending rows in a batch.
No observed event rates/controller distribution or physical device available.

Move the two bindings to the existing PC batch translator; SDL runtime normalizes
one borrowed gamepad event and enables it only for an eligible resolved PC
recipient. Disabled/other/up edges retain pending state; malformed tags/mismatched
spans clear only cancellation and reject. Root keeps F9 active-consumer lifetime,
connection handling, warnings and SDK forwarding. Navigation cancel is a shared
PC intent, not preview teardown. Cost: O(N) flat validation/update and one bounded
singleton adapter row, no allocation/state/queue. Existing pending batches and
device owner remain authoritative. Simplification removes root button bindings
and reuses routing/source policy, no role branch. Done: batch retained/invalid/
mixed-field checks plus real SDK focus/route → adapter → PC sample paths for F9,
player and DM, normal/sanitizer builds/tests. Physical controller feel remains
unverified. Limit: no rebinding/connection/device-selection or lifecycle change;
plan B is this two-button map extraction, not a controller framework.

## C10b28 controller focus route result

The production regression failed before the fix: both visible text focus tags
selected UI for a gamepad edge despite eligible controller sources. Native text
focus ownership now applies to keyboard/text, retaining PC controller edges and
existing command-modal blocking. Both client/test builds passed warning-free;
all 33 affected input/routing/PC/runtime/editor checks passed (normal 174 ms,
ASan/UBSan 811 ms; no skips). Baseline LSan exclusion remains.
Self-check: actual native/held contract mismatch reproduced, existing flat batch
and explicit UI/unavailable/invalid policies hold; one overly broad condition
removed, no added state/allocation or performance claim. Main remains 5,114 lines
at this checkpoint. Shared controller edge extraction follows.

## C10b30 command form key plan

Tier 1. Current root policy reads one visible command form, live Rml focus and
choice metadata, and owning prompt action/choice arrays. Output is the existing
popup/selection update or one action index. Form generations change on submit
and replacement; focus and popup state vary per ordered key. One overlay/context
is a true singleton, while choice rows retain existing virtual-combobox batches.
ASSUMPTION: ordinary nonrepeat keys dominate — affects no optimization; rates
and choice counts have not been measured on this desktop main-thread path.

Move only the existing key policy to CommandView, reusing strict metadata parsing
and open/close/sync/commit operations. Return handled plus an optional action
index; root keeps action execution and SDK forwarding. Preserve popup-first
Escape, Tab close then SDK, arrow selection, choice Return/keypad Enter and
general Return/modifier behavior. Invalid choice indices cannot change a field;
action execution retains existing bounds/busy gates. Cost: existing DOM ancestry,
action scan and virtual-row work, no new queue/state/controller. SDK DOM borrows
last one call; indices cannot replace the SDK focus API. Simplification removes
duplicate policy and uses current feature state. Done: actual generated-modal
focus/popup/selection/commit/action/repeat/modifier/malformed metadata checks,
both builds and affected presentation/input checks. Limit/plan B: this bounded
extraction without command semantics, DPI or lifecycle changes.

## C10b29 shared controller edge result

PC input now owns normalized East/Back navigation-cancel edges. The SDL adapter
normalizes buttons and applies the current native recipient/controller eligibility;
root retains F9 consumer lifetime, connections and existing SDK forwarding.
All pending fields survive disabled/up/other edges; malformed batches clear only
cancellation and reject. Actual SDK text focus, fresh routing and final PC samples
cover the same binding for F9, player and DM, including modal rejection.

Both final client/test builds passed without warnings. All 57 affected input,
routing, editor, PC, runtime and preview checks passed (normal 9,727 ms,
ASan/UBSan 51,418 ms; no skips). The two added checks also passed separately
(13/58 ms). Controller edge rows measure 3 bytes. Main: 5,114 to 5,113 lines.
Baseline LSan exclusion remains. Self-check: existing flat batch and singleton
SDL adapter contracts, explicit invalid/disabled behavior, role-free binding,
no new state/queue/allocation and no performance claim. Root raw bindings removed;
physical-controller feel, remaining feature input/frame composition and final
integration gates remain open.

## C10b30 command form key result

CommandView now owns live focus/metadata, popup-first Escape, Tab close, choice
selection/commit and action-index selection. Root retains action execution and
ordinary SDK forwarding. Three unused root wrappers and its duplicate integer
parser were removed. The actual generated-modal regression passes, including
repeat/modifier suppression, malformed metadata and unchanged general Return
behavior. Both final client/test builds passed warning-free; all 30 affected
command presentation and input/route checks passed (normal 267 ms, ASan/UBSan
1,088 ms; no skips). Main: 5,113 to 5,019 lines. Baseline LSan exclusion remains.
Self-check: singleton overlay documented, existing choice batches and validation
reused, no DOM borrow escapes, no new state/queue or speculative abstraction;
root choice policy removed without a performance claim. Remaining feature input,
frame composition and final integration gates stay open.

## C10b31 placed object view plan

Tier 1. Existing area workbench transforms category-ordered PlacedAreaObjectRow
batches into escaped buttons/count/empty markup, and live row/back targets into
renderer selection requests. Source is the active area's existing row builder;
one displayed workbench, row volumes/change rates unmeasured. Names are owning
UTF-8 strings, packed identities use the existing ObjectHandle protocol. Absent
area and loaded-empty area have distinct current prompts. Malformed/overflow
metadata or invalid handles consume a matched control without selection/release.

Move markup and target parsing to the existing ObjectWorkbenchView, using row
spans plus observed area availability; root retains row acquisition, ordered SDK
release and renderer focus/selection. Cost: existing O(N) markup allocation and
DOM ancestry, no extra row copies/storage. SDK DOM borrow lasts only capture;
handles are existing cold engine identity, validated before renderer operations.
Simplification removes root presentation/parser policy, no new view/controller.
Done: real area rows → generated SDK targets, names/IDs/count/empty prompts,
nested/back/malformed targets and relevant existing checks in both builds.
Limit/plan B: only these controls; renderer behavior remains existing/manual.

## C10b31 placed object view result

ObjectWorkbenchView owns area-list batch markup and owning row/back capture.
Root retains active-area row acquisition, existing SDK release and renderer
selection/focus. Real engine rows produce escaped names/count/buttons in the
SDK; nested/back targets, malformed/overflow tokens, destroyed identities and
distinct unavailable/loaded-empty prompts pass. Both client/test builds passed
warning-free; all 25 affected area/list/input/routing checks passed (normal
718 ms, ASan/UBSan 3,509 ms; no skips). Main: 5,019 to 4,975 lines.
Baseline LSan exclusion remains. Self-check: row-span batch and singleton
display/click contracts, no retained DOM borrow, explicit matched-invalid
behavior, existing cold engine identity and no performance claim. Root markup
and duplicate unsigned parser removed. Remaining feature input/frame composition
and final integration gates stay open.

## C32 workbench query refresh plan

Tier 1. Frame preparation reads four current SDK inputs (feat/spell/appearance/
sound queries), the owning workbench surface/target and current generations;
it updates existing query strings, match/list batches and windows only on a
change. A stale target retains the changed query without rebuilding, matching
current behavior. One displayed workbench is a true singleton; underlying rows
remain existing virtual batches. Query lengths/rates/catalog volumes unmeasured.

Move the existing ordered query block into ObjectWorkbenchView, borrowing its
current state, workspace/backend and resource generation. Preserve facet gates,
scroll reset and forced window synchronization. Cost: current query string reads
and conditional provider/filter work, no new cache/copies/queues. DOM borrows
last each call; no frame-wide cached focus or identity. Simplification removes
root feature query policy; no generic filter registry. Done: live SDK inputs
with real creature/provider rows, changed/unchanged query and stale-target checks,
both builds and relevant presentation/provider suites. Limit/plan B: this block
only, without mutation/render-stage reordering or catalog/rule changes.

## C10b33 creature spell filter key plan

Tier 1. Root reads one active generated filter field, current creature/tab
identity, modifiers and the existing virtual combobox. Arrows (including repeat)
select; nonrepeat Return/keypad Enter opens or commits. Ctrl/Alt/GUI and palette
ownership suppress this feature; Shift retains existing behavior. Current order
is popup-show → cross-workspace content refresh → move/sync, or commit → refresh
→ spell-row sync. The refresh can replace DOM, so no borrow can span it.

Move the policy into CreatureWorkbenchView with a bounded synchronous step:
begin applies show/commit and returns a flat refresh/finish intent; root refreshes
content; finish applies the existing movement/window synchronization with fresh
target facts. One displayed popup is a true singleton; underlying choices remain
existing batches. No queue, retained request or new mode state. Cost: current
focus ancestry and virtual-combobox operations; event rates unmeasured. SDK
focus pointers are required for one-call classification. Invalid/stale targets
retain existing unavailable behavior; existing choices validate commits.
Simplification removes root field policy without callback injection or changing
refresh order. Done: actual creature/filter markup and live SDK focus through both
steps, arrow/repeat/modifier/Return/open/commit/stale cases and both build suites.
Limit/plan B: this feature's ordered key steps; no rule/filter/refresh semantics
change and no performance claim.

## C19 final source audit

Current acquired resources and owners were inspected against actual startup and
normal teardown. SDL owner declaration precedes kernel construction; reverse destruction
keeps SDL live through kernel shutdown. Renderer backend rejects core creation,
destroys its core on context failure, and shuts down on Rml renderer failure.
Root's renderer guard covers subsequent swapchain/UI failures. Rml runtime owns
file/resource adapters, font bytes and contexts; its acquired flag and destructors
cover missing fonts/overlays. Listener guards detach before startup feature cleanup;
cleanup closes payload delivery/gamepad and releases UI bindings while feature
storage and renderer are live. Normal teardown also restores temporary preview/
appearance state before releasing UI/renderer and workspace/kernel objects.

App-owned native requests contain owning strings/shared delivery gate, no root
or DOM borrow. Gate closure and queue drain precede kernel/SDL teardown. The
detached vendor backend's post-callback cleanup is explicitly scoped under
client-native-dialog-sdk-shutdown.md; no thread completion guarantee is claimed.
Existing import/blueprint quit/publication gates remain unchanged. Component
tests observe dummy SDL, real kernel/payload closure, actual package/fonts and
SDK geometry teardown. Successful desktop Vulkan startup and hardware failure
injection remain unavailable here, and allocator failure/internal kernel partial
creation were not injected. Fresh component checks follow the current build.

## C34 shell log capture plan

Tier 1. Root's existing Loguru callback copies severity/channel and formatted
message strings from borrowed SDK messages into a mutex-protected FIFO capped
at 512 rows. Overflow drops oldest rows; empty formatted messages drop; allocation
failures drop at the noexcept callback. Main-thread drains transfer owning rows
to ShellController in batches. One registered callback ID is a true process
singleton; SDK callbacks can arrive from other threads. Rates/byte lengths and
thread contention have not been measured. Addresses must remain stable while
registered; callback removal synchronizes through the existing SDK mutex.

Move the current capture into ShellView and have its batch drain append to the
existing shell. Root keeps capture lifetime before desktop/kernel startup and
through their teardown, preserving captured diagnostics. Keep cap, severity,
format fallback and exception behavior. Cost: existing owning strings, bounded
row FIFO, mutex and drain vector, no extra queue/copy or logging abstraction.
SDK callback's userdata pointer is required for the stable registered singleton;
queued rows contain no borrowed pointers. Simplification removes root log policy
and append loop. Done: real Loguru severity/owned-text/filter/FIFO/drop/drain/
registration lifetime checks, worker callback delivery and both builds/suites.
Limit/plan B: mechanical capture ownership only; no new logging backend or
performance claim.

## C32 workbench query refresh result

ObjectWorkbenchView now owns feat/spell/appearance/sound query polling and the
existing changed-query provider/filter/list-window updates. Root retains frame
stage ordering and supplies current workspace/backend/resource facts. Window
targets are recaptured for each operation; no identity is cached across provider
work. Actual creature spell inputs prove changed queries update count/matches,
unchanged inputs retain DOM, stale tabs retain query without rebuilding, and
inactive facets/closed selectors are untouched. Four unused root delegates were
removed. Both final client/test builds passed warning-free. All 68 affected
workbench/provider and runtime checks passed (44 workbench, 24 runtime; normal
43,407 ms, ASan/UBSan 254,329 ms; no skips). New query test passed separately
in 1,388 ms normal. Main: 4,975 to 4,910 lines. Baseline LSan exclusion remains.
Self-check: singleton polling over existing row batches, explicit stale gates,
no retained DOM/identity or new registry/state, no performance claim. Remaining
feature input/frame composition and final integration gates stay open.

## C19 final audit validation

All 24 supported ownership checks passed in the fresh normal and ASan/UBSan
builds: actual file inputs/seeks, every font/overlay failure, SDK geometry
destruction/restart, failed/repeated SDL acquisition, real kernel-before-SDL
closure and queued/late native payload ownership. No skips in these suites.
Source audit confirms feature buffers/listeners/models outlive their cleanup
and renderer resources precede window/SDL shutdown. Native vendor thread
completion is scoped in its local issue, with app delivery already tested.
No clean LSan, successful Vulkan desktop, hardware failure injection or vendor
dialog completion claim. These explicit manual/dependency limits carry into C20
integration; no generic resource registry or speculative runtime state added.

## C35 workspace document composition plan

Tier 1. Root still builds area/preview toolbar/surface markup, delegates dialog
and resource inspectors and selects placed-object versus active workbench markup.
Input is one active WorkspaceTab plus actual surface, workbench/tile/dialog state,
backend resource ownership and current active-area identity. Output is owning
markup for the existing workspace_content. Tabs/state change on event/refresh;
one displayed content document is a true singleton, placed-object rows stay the
existing batch. Actual kind/surface enums and title/resource strings were read;
length/row distributions unmeasured. Missing resource and inactive area retain
current explicit placeholders, not a fallback edit/view.

Move this presentation composition to existing WorkspaceView; it delegates each
feature's current markup operations and acquires the same active-area row batch.
Root keeps refresh/blur/layout/renderer stages and passes only these actual inputs,
never complete application state. Cost: existing O(markup bytes/rows) formatting
and resource acquisition, no extra cached representation/row copies. Engine
identity is a cold validated boundary, DOM not read or retained during markup.
Simplification removes root surface markup/policy and obsolete resource delegate,
without generalizing templates or introducing a replacement presentation layer.
Done: actual generated area surface/preview/dialog/resource/generic markup and
SDK targets, escaping/empty/missing cases and relevant existing view tests in both
builds. Limit/plan B: only this synchronous presentation block; preserve refresh,
focus/scroll/render semantics and control precedence.

## C10b33/C34 spell filter and shell log results

CreatureWorkbenchView owns ordered key begin/finish steps; root only refreshes
cross-workspace content between them. Generated SDK focus and actual creature
choices prove show-before-refresh-before-move, repeat arrows/Shift, nonrepeat
Enter commits, hidden Enter opens, stale/palette/modifier gates and consume-once
finish. Invalid finish tags consume without work. No callback injection or queue.
ShellView owns the unchanged Loguru capture and batch shell transfer. Real SDK
messages prove severity filtering, owned UTF-8 text, last-512 FIFO order, worker
delivery, empty drains and synchronized callback removal/fresh-owner registration.
Allocation failure is not injected; existing best-effort callback drop retained.

Both client/test builds passed warning-free. All 36 affected creature/shell/log/
input/routing checks passed (normal 10,205 ms, ASan/UBSan 45,906 ms; no skips).
Spell key regression also passed separately in 1,386 ms normal. Fixed captured
row storage measures 64 bytes, excluding owning string payloads. Main: 4,910 to
4,781 lines. Baseline LSan exclusion remains. Self-check: real singleton SDK
callbacks/display documented, existing choice/FIFO batches and explicit error/
stale/cap policies hold, stable callback userdata justified, no extra state or
performance claim. Root field policy, logging body and append loop removed.
Workspace composition, remaining input/frame coordination and final gates open.

## C10b36 editor application shortcut plan

Tier 1. Root embeds editor application's palette/output/terminal and close/save/
undo/redo bindings in different positions of the ordered key path. Inputs are
current SDL key/modifier/repeat values; outputs are one existing application
action tag per independent key row. These are application shortcuts, preceding
world/editor/PC bindings; they do not select locomotion or role/panel availability.
Current caller is the editor root. Ctrl+Shift+P, Ctrl+J and plain/Shift grave allow
repeat; close/save/undo/redo reject repeat and conflicting modifiers. Caps/Num/
other SDK modifier bits retain current ignored behavior. Rates unmeasured.

Move only binding selection to the existing EditorInput batch API (already uses
SDL key types). Root resolves count=1 from immutable event values, applying each
tag at its original priority position; backend/refresh/lifecycle effects stay in
their current owners. Unknown keys produce none; unequal spans clear outputs
and reject. Cost: O(N) flat selection, bounded singleton row, no state/allocations
or new handler registry. Simplification removes root binding tests while keeping
UI/lifecycle precedence and shared PC controls. Done: current modifier/repeat
matrix, mixed/empty/mismatched batches, both builds and relevant editor/input/
route checks. Limit/plan B: these existing editor app bindings only; no commands,
authentication, DM capabilities, PC map or shortcut behavior changes.

## C35 workspace composition result

WorkspaceView now owns document-kind/surface markup composition and delegates
current workbench/tile/dialog/resource markup to existing views. Root passes
only actual presentation dependencies and retains refresh/renderer/focus/scroll
ordering. Active-area placed rows keep their existing builder. Generated SDK
area controls match all three surfaces and copied row identities; escaped title,
creature viewport, actual sound data-only presentation, dialog delegation,
missing resource and generic fallback pass. The initial fixture incorrectly
expected creature spells to be data-only; inspection confirmed the existing
sound/store/trigger/item-property policy and the fixture now covers both cases.
Production behavior was unchanged. Obsolete root resource and escaping helpers
were removed. Both final client/test builds passed warning-free; all 26 affected
workspace/area/input checks passed (normal 2,123 ms, ASan/UBSan 12,572 ms; no
skips), including actual project resource and dialog acquisition. Main: 4,781 to
4,638 lines. Baseline LSan exclusion remains. Self-check: displayed-content
singleton, existing row batches, missing/invalid identity and generic fallback
policies documented, no root-state API/cache/general presenter, no performance
claim. Remaining input/frame coordination and final integration gates stay open.

## C10b36 application shortcut result

EditorInput owns the existing application binding matrix as a flat batch.
Root resolves immutable event values once after preview/gesture gates, then
applies tags at their original positions around feature/UI handling. Backend,
refresh and shared PC control policy are unchanged. The 21-row matrix checks
repeat, strict close/save/undo modifiers, existing permissive palette/output
modifiers, ignored SDK lock bits, grave/Shift and unknown keys; mismatched outputs
clear and empty batches succeed. Both client/test builds passed warning-free.
All 40 affected editor/input/routing/PC/shell/log checks passed (normal 934 ms,
ASan/UBSan 4,937 ms; no skips). Input/action rows measure 8/1 bytes. Main: 4,638
to 4,636 lines. Baseline LSan exclusion remains. Self-check: batch/count-one
contract, unknown/mismatch policies, no cached focus/mode or additional state/
allocation/role map and no performance claim; root binding expressions removed.
Remaining feature input/frame coordination and final integration gates stay open.

## C10b37 workbench native click composition plan

Tier 2 within the input subsystem. Actual root mouse-up code already captures
owning DTOs for nine workbench families: color, combos, placed objects, property
commands, sound catalog, appearance catalog/back, surface, inventory and creature
commands. Each leaf owns source validation/transactions; root repeats their
release and presentation coordination. Input is one live SDK target/point plus
current workbench/workspace/backend generations. Output is one owning tagged
payload and its release phase, then flat renderer/content/finish intents.
Alternative DTO layouts are existing owning IDs/strings/indices, never DOM
borrows. One ordered displayed UI release is a true singleton; underlying rows
remain existing batches. No observed click/type frequency or payload sizes yet.

Extend ObjectWorkbenchView to compose these current leaf APIs, preserving their
exact current priority and before/after/no-early release. Root retains common
owner comparison around SDK dispatch, renderer/body work and cross-workspace
content refresh. Apply performs selector-close/leaf work; root applies returned
renderer/body and content intents; finish synchronizes current windows/focus
after fresh markup. No event queue/registration callbacks or application state
crosses this boundary. Costs: one stack tagged union/flat effect, existing owning
payload allocations/DOM ancestry and leaf transactions; measure sizeof during
tests, no performance claim. Rml target pointers last capture only; engine handles
are existing validated cold identities, rows remain indexed. Malformed matched
controls preserve their current consume/release; stale leaf sources reject;
unknown finish tags do nothing. True singleton calls need no parallel UI batch.

Simplification removes the root's workbench mouse-up chain and duplicate effect
policy while retaining existing leaf implementations and source validators.
Done: actual generated controls through composed capture → SDK release → leaf
apply → content/finish, preserved phases/priority/owning payload, stale/malformed/
consume-once/undo and affected leaf/input suites in both builds. Source audit
checks each old branch's ordering. Limit/plan B: only this contiguous workbench
chain; workspace/browser/loading/renderer lifetimes stay current. No general
event bus, second controller or new command semantics. Open design questions,
if found, go under issues/ before changing policy.


## C10b38 SmallS managed-list presentation plan

Tier 1. Observed root code drains the current UiListEvent batch, copies each
qualified callback name before script dispatch, refreshes existing language/model
bindings and synchronizes list rows before restoring captured focus. Inputs are
the shared bridge/binding/model instances, one displayed document and existing
managed-list render state; outputs are callback effects, shell errors and fresh
DOM/list focus. The process UI/kernel singleton and synchronous script/Rml
callbacks constrain lifetime: never retain event strings or DOM pointers across
refresh. Row volumes/callback frequencies have not been measured; no optimization
claim. Common case is existing ready bindings; absent bindings reject refresh,
missing callbacks drop their event, failed callbacks log and draining continues.

Move these actual transforms into smalls_view with direct existing dependencies,
keeping ownership and destruction order unchanged. Binding/model pointers are
cold SDK integration borrows for one call, not an indexed hot loop. The bridge is
one existing shared singleton used by backend/scripts; no array or new owner is
needed. Cost is existing O(events) dispatch and DOM refresh, callback-name copy,
no new state/allocation beyond those operations. Activation/cycling are singleton
ordered displayed gestures, while event draining remains batch-first. Root keeps
cross-feature/release coordination. Simplification removes application-state
access and duplicated dispatch/refresh/sync policy; no generic event bus is added.
Done: actual SDK/list activation, cycle/focus and callback error/drain behavior,
missing bindings, existing SmallS/managed-list suites, normal and ASan/UBSan.
Plan B: retain narrow root delegates if call-site expansion obscures ordering.


## C20 preparation: remaining query ownership plan

Tier 1. Observed frame code polls one tile-palette search, one home-area search
and one output filter. Inputs are SDK field values and their existing owner state;
outputs are existing filtered row indices/counts/scroll and output-dirty flags.
Tile/home row batches are already provided by their existing palette/catalog
transforms; no new data representation is needed. Normal case is unchanged text,
with existing incremental virtual-window sync. Changes reset the existing tile
scroll and preserve semantic selected row; home updates its catalog/window;
output sets the existing dirty flag. Hidden tile/home surfaces remain gated.
Missing fields produce empty strings as before, unavailable catalogs preserve
existing diagnostics, invalid selected rows become -1. Actual query frequency
and result-size distribution are unmeasured and do not justify optimization.

Move those three policies into their current area_tile_editor, browser_view and
shell_view owners. Root keeps their relative frame order, command-form sync,
recent-query polling, log draining and content refresh. The desktop UI is a true
singleton, with SDK pointers borrowed for one call; palette/catalog rows remain
indexed batches. Cost is existing string reads and O(matches) filtering/selected
lookup on changes, no new fields or caches. Simplification removes root access to
filter/list internals and avoids a general query controller. Done: actual SDK
fields drive existing palettes/home catalog/output dirtiness; unchanged/hidden
and missing-value behavior, affected tests in both builds. No performance claim.


## C20a frame-clock contract plan

Tier 1 within root composition. Observed loop owns two previous timestamps and
reads current SDL ticks/performance counter; raw seconds drive metrics/PC fixed
steps, while tick difference capped to 100 ms drives camera input. Startup raw
seconds are zero. Counter/tick regressions yield zero. SDL timing is a process
singleton; frame pacing and project-progress timing are distinct and stay put.
Input is a contiguous flat batch of current tick/counter/frequency samples and
matching caller-owned previous clocks; output is raw seconds plus capped camera
milliseconds. Normal production batch has count one. Stable SDK frequency is
positive; all uint64 timestamps are accepted. Zero frequency or span mismatch
rejects, clears outputs and leaves clocks unchanged, avoiding partial advancement.

Extract the current arithmetic into client_frame in rollnw_client_core, then
call its batch path at the original timestamp point. Keep timestamp acquisition
order, metric updates, jobs-before-events, UI-before-held-PC and pacing untouched.
Costs: O(frames) arithmetic, one flat 16-byte clock replacing the existing two
uint64 locals, and fixed stack input/output rows; measure row sizes in tests.
No performance claim. Simplification shares one clock transform and documents
why raw PC time cannot be collapsed into the camera cap. Done: startup, large
frame, regressions, independent batch clocks, invalid frequency/mismatch atomic
rejection and actual SDL sample comparisons; normal/sanitized builds. This is
only the first frame seam; root state, event and frame composition remain open.


## C10b37 result and self-check

Composed the nine existing owning click families in their original priority.
Root retains SDK/common-owner comparison, renderer/body requests and cross-view
content refresh; the view returns explicit refresh/window/focus intent. Both
apply and finish consume their slots; malformed/unknown/stale policies remain
leaf-owned. Audit compared every prior branch: catalog close before fresh
command facts, color after-native release before refresh, placed selection after
release, surface body/content before ordered window sync, and inventory window
sync without content refresh. The audit required a separate prepare call for
selector-close: callbacks can replace DOM/change ownership, so root constructs
command context only afterward. No command facts are cached across that close.

Actual SDK tests cover captured command arguments across release/DOM replacement,
one undo and repeated rejection, conflicting malformed-class priority/no-early
release, after-native invalid boolean, surface intents/finish reset, invalid phase
and unknown finish, generated sound options/focus and captured color close before
opening after callback DOM replacement. Both builds passed warning-free. All 88
affected checks passed (normal 55,456 ms, ASan/UBSan 285,703 ms; no skips).
After the prepare ordering adjustment, all 17 focused composition/SDK/list/bridge
checks passed (normal 14,158 ms, ASan/UBSan 83,044 ms; no skips). Those include
four composed command/sound/color checks and the next checkpoint's new list check.
Click/effect rows measure 216/16 bytes. Main: 4,636 to 4,503 lines. Baseline LSan
exclusion remains; desktop renderer/manual input checks remain unverified.
Self-check: existing semantic DTOs only, singleton release/batched rows documented,
no DOM retention/AppState API/queue/cache/registry, explicit invalid handling and
no performance claim. Remaining input/frame composition and final gates stay open.


## C10b38 result and self-check

Moved actual event-batch draining, shared binding synchronization/refresh and
list activation/cycling into smalls_view. The application still owns the same
bridge, language binding and data model at unchanged addresses/lifetimes. Root
retains gesture release and cross-feature coordination through narrow delegates.
Callback names are copied before script dispatch, missing callbacks drop, failed
callbacks append errors and draining continues; refresh borrows document/bindings
for one call and focus is captured before DOM synchronization. No new state or
callback registration mechanism was introduced.

The new actual SDK/generated-row check exercises activation, selected index,
cycle/current focus, missing bindings/null document, callback failure once,
empty repeated drain, missing callback and unrelated-control rejection. Both
builds passed warning-free; all 17 affected composition/list/bridge checks passed
(normal 14,158 ms, ASan/UBSan 83,044 ms; no skips). Source audit preserves each
original dispatch -> refresh -> list sync -> focus sequence, including activation
focus remaining a root request after release. Self-check: singleton gestures and
existing event/row batches, explicit failure/drop policies, cold SDK borrows
justified, no application-state API or speculative layer and no performance
claim. Main: 4,503 to 4,445 lines. Baseline LSan and desktop/manual gaps remain.
Root event/frame composition and final integration gates remain open.


## Query ownership result and self-check

Tile search now owns filter/count/semantic selection/scroll reset and forced vs
incremental window sync in area_tile_editor. browser_view owns home-area query
poll/catalog refresh/window sync; shell_view owns output-filter dirtiness. Root
retains their original frame order and surrounding form/recent/log operations.
Hidden surfaces retain state; missing fields mean empty query, unmatched rows
select -1 and existing unavailable/provider diagnostics remain unchanged.

Actual SDK tests exercise no-match/restored real tileset rows, hidden and
unchanged searches, home-area cards/no-match catalog/unchanged DOM and output
changed/unchanged/null-field dirtiness. Both client/test builds passed warning-free.
The initial normal affected suite passed 20 checks (6,782 ms). Final combined
query/frame/CLI/preferences/metrics checks passed 30/30 in both builds (normal
5,872 ms, ASan/UBSan 31,637 ms; no skips), including all 20 query/browser/shell
checks. Their final suite times total 5,835/31,461 ms respectively. Main: 4,445
to 4,403 lines. Self-check: existing row/state representation, singleton displayed
queries/batched indexed rows documented, no added cache/controller/application
state dependency, explicit hidden/missing/no-match handling, simplification
removed root filter/list access and no performance claim. Desktop/manual and
baseline LSan limitations remain; root composition/final integration are open.


## C20a result and self-check

The production frame loop now uses advance_client_frames at count one. Raw
counter seconds and capped camera milliseconds retain separate outputs; original
job/timestamp/event/metric ordering and pacing are unchanged. The two old previous
timestamp locals become one same-size clock row. Invalid SDK frequency fails the
frame, enters existing orderly shutdown and returns status 1. Pure batch rejection
clears outputs without partial clock advancement. No DOM/mode/focus/session fact
is cached by this transform.

Three tests cover startup, full raw seconds vs 100-ms cap, regressing/full-range
timestamps, independent clocks, zero-frequency/mismatched/empty batches and actual
SDL counter/frequency samples. All existing application CLI/preferences/metrics
tests remain present. Both builds passed warning-free; all 30 combined affected
checks passed (normal 5,872 ms, ASan/UBSan 31,637 ms; no skips). Clock/sample/delta
rows measure 16/24/8 bytes. Main: 4,403 to 4,407 lines. Self-check: flat batch
contract, index-based linear transform, atomic reject/clamp policy, no allocation/
queue/new ownership/general clock options, separate raw/camera time and no
performance claim. Renderer-off checks and complete frame/root extraction remain
for final integration. Baseline LSan and desktop/manual gaps remain explicit.


## C20b mutation-stage/root-state plan

Tier 2. Observed frame consumes the single current ObjectMutationState epoch,
compares displayed area/structure epoch, partitions structural/spatial/visual work,
refreshes matching workbench snapshots and restores owning managed-list focus.
Inputs are that existing epoch, root-owned feature states, displayed SDK document/
context and renderer. Outputs are observed epochs/stale-area status, renderer
updates and presentation refresh. There is one displayed scene and one current
mutation epoch: a true singleton coordinator over existing tile/object row batches,
not a new queued mutation system. Actual mutation-kind frequencies are unmeasured.
The desktop main thread and synchronous callbacks constrain lifetime and ordering.

Name the existing composed root state ClientApplicationState in a private root
header, preserving every field/order/address/lifetime. Only root coordinators may
include it; feature APIs stay narrow. Move mutation coordination into
client_application_workspace, retaining exact cancellation -> rebuild -> publish
-> content/window -> focus ordering and the existing spatial/visual partitions.
Move retained tile-selection/cursor refresh after rebuild into area_tile_editor
runtime, using existing indexed selection/preview batches. Invalid source tile
clears selection with its diagnostic; failed renderer rebuild retains stale area
and logs. Existing unknown mutation behavior is preserved, not generalized.

Costs: same composed state and epoch/DOM/renderer operations, an out-of-line stage
call and a private dependency header, no new fields/owners/queue/caches. SDK context/
document/renderer references are cold external borrows until runtime shutdown;
engine handles remain existing validated identities, row loops use indices.
Simplification removes root access to tile-selection internals and removes the
mutation stage from entry control flow. Done: source comparison of every branch,
feature-header dependency audit, normal/sanitized client/test builds and affected
selection/edit/workbench/application suites. Existing GPU-dependent scene tests
remain a final gate with explicit unsupported skips; desktop behavior is manual.
This stage alone does not finish root/input/frame extraction. No performance claim.

The C20b test boundary also moves the two pure frame-contract checks into an
always-built core test source. Actual SDL sampling/CLI/metrics checks retain their
existing desktop guard. This lets renderer/client-disabled builds exercise the
same flat frame transform without SDL/Rml dependencies or configuration changes.


## Remaining C10/C20 root composition plan

Tier 2. The remaining entry source contains private root helper coordination,
ordered SDL intake and frame stages; substantive workbench, browser, shell, tile,
object, command-form and PC operations already live in their owners. Real input
is the ordered SDL event stream (callbacks can replace DOM/focus), plus current
root feature owners and three stable SDK contexts/documents. Outputs are existing
forwarding dispositions, feature effects, renderer requests, updated root epochs
and presented frames. One process/application/scene is a true singleton. Actual
event-category frequencies are unmeasured. Keep every dispatch at its existing
point; no queue, category batching, cached ownership or recursive input during
synchronous loading. Existing pure feature/device transforms remain batch-first.

Finish by responsibility, not copying the file into application.cpp: runtime
owns existing window metrics/UI asset bootstrap helpers; root command/loading
coordination and listeners get a private root boundary; root workspace refresh/
activation stays distinct from mutation refresh; native SDL category coordinators
call existing feature APIs and one shared final forwarding function; frame
coordination explicitly calls jobs -> timing -> SDL -> mutation/query -> surface
prepare -> layout/UI -> held PC/viewer -> overlays/present/metrics -> pacing.
Main keeps build-info/logger/CLI selection and a desktop driver call. Root state
is private composition and cannot appear in feature/input-map API headers.

Use flat borrowed SDK surface/dimension records only for the actual shared
window/three contexts/documents and current four dimensions, replacing equivalent
existing root locals rather than mirroring ownership. SDL/Rml pointers are the
external SDK's cold identity protocol; no indexed engine row loop gains pointers.
Native handlers preserve early releases and current skip/default-forward paths;
common UI/viewport authority is always freshly resolved after callbacks. All
resource owners/guard declaration order and explicit C19 teardown remain stable.
No new acquisition or failure state is introduced by root relocation. Startup
invalid/missing resources retain current fail/shutdown behavior; malformed/stale
feature inputs retain owner rejection policies. Dominant cost is unchanged SDK,
backend/render work and existing strings; new stage calls/TUs add organization/
build maintenance cost, no measured runtime improvement is claimed.

Simplification removes entry's feature details and repeated cross-view sequences,
retains existing narrow leaf APIs, and avoids a replacement dispatcher/controller
or a generic registration bus. Done: main is entry/startup selection, application
shows stage order, native handlers are independently readable by real event kind,
feature include-direction/root-state audit passes, mechanical/source branch
comparison and full normal/sanitized integration including renderer-disabled core
and CLI/build identity. File-size targets are review guides; report actual sizes
and explain remaining coordination instead of hiding code in implementation
includes. Plan B is a smaller independently verified stage with the parent issue
left open; no behavior/policy change is bundled to achieve a line target. Human
GPU/window/controller/high-DPI feel checks remain explicitly manual.


## C20b result and self-check

ClientApplicationState is now private root composition with identical fields,
order and ownership. Named private root coordination permits the mutation stage
in client_application_workspace to call current root refresh/gesture seams; no
feature includes or receives that state. The stage retains original structural,
spatial and visual partitions, stale-area reporting and ordered presentation/
focus refresh. area_tile_editor runtime owns retained selection/cursor refresh
using the existing tile-source index and preview batches. Pure frame tests are
always built; SDK sampling and current CLI/preferences/metrics remain guarded.

Automated source comparisons confirmed exact root field/order identity and all
mutation branches/order after substituting the tile refresh helper. Include/
state-name audit finds only entry and private root state/workspace sources. Both
client/test builds passed warning-free; all 86 affected edit/selection/workbench/
input/list/application checks passed (normal 16,791 ms, ASan/UBSan 90,042 ms;
no skips). After test relocation, all 10 frame/CLI/preferences/metrics checks
passed again (normal 35 ms, ASan/UBSan 168 ms; no skips). Existing renderer-disabled
configuration built warning-free and all 39 selected pure frame/tile/PC checks
passed (2 ms; no skips). These times are validation durations, not speed claims.

Main: 4,407 to 4,205 lines; mutation coordinator: 150 lines; root state: 70 lines.
Self-check: unchanged owner/lifetimes/fields, singleton scene/epoch documented,
existing indexed tile batches, explicit invalid-source clear/stale-rebuild policy,
no feature-state dependency or new event/mutation queue and no performance claim.
Direct GPU scene/root-desktop behavior remains unverified here; final graphics
integration/manual matrix and baseline LSan exclusion remain explicit. Remaining
root input/frame/driver composition stays open with its plan above.


## C20c runtime bootstrap helper plan

Tier 1. Existing entry reads one shared SDL window's logical/pixel sizes, logs
metrics, resolves four ordered UI asset directory candidates and reads the single
ROLLNW_CLIENT_UNCAPPED configuration. Inputs are current SDK window/process/base/
cwd/source paths; outputs are copied sizes/path and startup pacing bool. Default
is unset flag/pacing enabled; UI assets require current package/panel/style files.
Move these actual helpers into client_runtime, keeping source directory (same
folder), candidate order, weak canonicalization/error behavior, pixel fallback and
false-token parsing. Expose pacing as a zero-argument singleton policy, not a new
generic environment API. Null windows return zero sizes/no log; missing assets
return empty and startup retains failure. Other SDK size ranges remain unchanged,
with frame's existing invalid-size skips. Positive SDK counter frequency remains
the platform contract; retain current metric counter helper at entry for now.
Costs: existing O(1) SDK queries, up to four filesystem candidate checks and flag
normalization/string allocation once; no new state/cache/ownership and no speed
claim. SDK pointers are cold borrows for a call. Simplification removes bootstrap
policy from entry without extending configuration. Done: actual dummy-window/
null-window checks, current false-token flag behavior/restoration, real UI assets
and runtime/application checks in normal and sanitizer builds. Root completion
remains open; desktop/Vulkan/high-DPI behavior remains manual.


## C20c result and self-check

client_runtime now owns current logical/pixel window queries/logging, four-candidate
UI directory resolution and the single pacing configuration. Source directory
remains tools/client; candidate order/required files/weak canonicalization, pixel
fallback and case-insensitive false tokens are unchanged. Only the current pacing
policy is public; no generic environment option or new owner/state was added.
Null windows return zero/no log; actual dimensions retain existing frame skips.

The new actual dummy-window check verifies 321x123 logical/pixel sizes, null
queries/logging, real required UI assets, unset/empty/false/mixed-case/true/unknown
flag values and restoration of the previous process flag. Both builds passed
warning-free; all 30 affected runtime/loading/files/frame/CLI checks passed
(normal 1,043 ms, ASan/UBSan 5,658 ms; no skips). Source audit preserves helper
bodies/candidates and the existing startup/event/frame call positions. Main:
4,205 to 4,122 lines. Self-check: true bootstrap singletons/cold SDK borrows
justified, explicit missing/null behavior, unchanged resource ownership, no new
options/cache/controller and no performance claim. Pixel fallback failure/high-DPI
and actual Vulkan/window behavior are manual; baseline LSan exclusion remains.
Remaining root dispatch/frame/desktop driver extraction and final gates stay open.


## C20d root command/loading coordination plan

Tier 1. Current root resolves owning CommandResult prompts, queues/polls current
project load/import and coordinates content/shell refresh from two SDK click
listeners. Inputs are existing backend/result/loading/view states and borrowed
window/document/context/renderer; outputs are the same command results, queued
project state and ordered view refresh. One displayed command/load application is
a true singleton; underlying jobs/rows remain existing batches. Frequency is
unmeasured; no optimization claim. Move this contiguous root responsibility into
client_application_commands with private root-state APIs. Move listener methods
out of entry into that source, preserving fields/registration lifetime and fresh
callback reads; header holds declarations only. Existing reject/cancel/prompt/
missing-result/error logging policies remain unchanged. Cost: existing result/
argument strings and SDK/backend calls, additional root TU/header maintenance,
no new fields/owners/state/options. Simplification removes command/loading body
and SDK callback policy from entry. Done: body/order/field source comparisons,
include direction, both builds and affected command/loading/CLI/SDK checks.
Remaining native category/frame/driver extraction and full integration stay open.


## C20d result and self-check

client_application_commands owns existing root prompt/result resolution, command
form/overlay coordination, project queue/poll/import coordination and the two
blueprint/home SDK listener methods. Headers hold declarations and identical
listener fields; constructors and callback methods are out of line. Root-state
APIs stay private; feature/input-map APIs still receive their existing narrow
states. Callback reads, argument ownership, prompt cancellation/error reporting,
project progress/present gate and all refresh order remain current.

Automated body comparison confirmed all 15 moved function bodies and both SDK
callback bodies are identical after formatting. Constructor initializer order
and listener fields/registration positions were audited unchanged. Include/state
name audit finds only private root sources and entry. Both client/test builds
passed warning-free; all 38 affected command/template/loading/CLI/frame checks
passed (normal 862 ms, ASan/UBSan 4,272 ms; no skips). Main: 4,122 to 3,775 lines;
command coordinator: 342 lines; private declaration header: 83 lines. Self-check:
existing singleton command/load ownership and row/job batches, cold SDK borrows
justified, unchanged reject/cancel/error policies, no new state/queue/options,
no application state passed into features and no performance claim. Interactive
native prompts/desktop and baseline LSan gaps remain. Native category/frame/
desktop driver extraction and final integration gates remain open.


## C10c/C20e native category coordination plan

Tier 1 within the Tier 2 root extraction. Inputs are the current ordered SDL
event, freshly read UI/map/feature state and the existing eight cold SDK borrows
and four logical/pixel dimension values. Output is the existing native effect
and explicit finish/next-event obligation. SDK callbacks remain synchronous;
no event batching/reordering or focus snapshot is introduced. Source inspection
finds no nested loops in the native cases; preserve nested switch breaks and
convert only poll-loop skips/outer-case breaks to category return values.
ASSUMPTION: pointer motion remains the frequent case; rates are unmeasured and
no speed claim depends on it. Null/stale/invalid input retains existing reject/
cancel paths. One desktop UI/event stream is a true singleton; underlying pure
input/row/tile paths remain indexed batches. Cost: one flat cold surfaces record
(eight pointers/four ints), category call/return and new TUs; no new owner, queue
or duplicated dimension cache. Simplification removes intertwined key, pointer
and release policy from entry, retaining one shared final forwarding boundary.
Done: exact transformed body/order audit, unchanged listener fields/lifetimes,
both builds and affected input/editor/PC/workbench/template/preview checks.
Actual desktop/GPU/controller/high-DPI feel remains manual.

## C20f/C20g root composition plan and observed fixture correction

Tier 1 within the committed Tier 2 completion plan. The native category body
comparison passed for all five handlers, including documented outer-loop/case
jump conversion; no nested loops were present. Move the 136 remaining root
coordination bodies into private input/shell/workbench/workspace/preview/editor/
command sources, preserving direct feature calls and keeping root state out of
feature APIs. Polling retains one ordered SDL stream and one final forwarding
block. Extract frame helpers at existing jobs/query/surface/layout/view boundaries,
with timing counters, held-input recapture, presented-load gate and pacing in their
original order. Desktop startup retains exact SDK/resource/listener declaration,
guard and shutdown order. CLI selection stays at entry. Additional TUs/headers,
one cold surfaces record and existing owning optional viewport output are the
maintenance/stack costs; no new owner, state cache, event queue or speed claim.
SDK pointer access stays cold and required by the external SDK; pure operations
remain batches. Null/stale/malformed inputs retain existing reject/cancel paths;
minimized/invalid-size/not-ready surfaces skip rendering with existing delays.
Success: body/order/dependency audits, both builds, full integration, renderer-off
and CLI gates. GPU/controller/high-DPI/manual feel remain explicitly unverified.

A broader unchanged feature-test sequence failed three fixture-dependent checks,
while all three pass in isolation. Logs show that a SmallS bridge initialization
changes the process kernel paths from dedicated test data to the detected desktop
install/home. The existing end-of-test service restoration restores mode but not
paths. Fix that test harness boundary by capturing its two configured paths and
restoring them before restarting services only when changed. This adds two cold
owning paths to the singleton test listener and no production behavior. The
already failing sequence is the regression evidence; verify it again with the
bridge test preceding the three checks. No new implementation-mirroring test is
needed. Broader feature validation remains pending rather than counted passed.

## C10c/C20e–g composition result and self-check

Entry is now 13 lines and selects build-info/CLI/desktop startup. The desktop
application driver is 309 lines and owns the same resource declarations, guards,
listeners and explicit shutdown sequence. The frame module is 469 lines and
separates job polling, mutation/query synchronization, surface readiness, layout
completion and workspace rendering at the existing timing boundaries. Held-PC
facts remain captured after UI callbacks/layout; overlays, presented-load gate,
GPU/CPU counters and pacing remain in their original order. Native category
sources contain real key/pointer/release coordination; the private input source
retains one ordered SDL poll loop and one shared final-forwarding block. Category
finish/next return values preserve outer-case breaks and event-loop skips, while
nested switches retain their breaks. Paired private headers expose only their
root responsibility, and features do not receive the composed application state.
The root state header is private composition, with no transitional feature API.

Automated audits confirm all five category bodies after documented jump changes,
135 root helper bodies after formatting, all five frame stage bodies after explicit
surface/viewer returns, both workbench listener methods, the complete poll body,
and desktop startup/failure-guard/shutdown bodies. Feature include/type audit finds
no application-state or private-root dependency. The only removed root body was
an unused tile-selection-preview delegate; the desktop API consumes just its
actual executable-name input rather than an unused argument count. SDK borrows
remain cold external API pointers; index/batch operations remain in their owners.
Bootstrap size locals are consumed once into the current surface dimensions, not
maintained as a second cache. Existing invalid/stale/missing input rejection,
gesture cancellation and minimized/not-ready frame skips remain unchanged. No
new feature states, queues, cached focus/modes, options or performance claims.

The minimal unchanged-feature regression sequence (SmallS bridge initialization
followed by inventory icon, appearance restoration and preview start checks)
reproduced three failures before test-path restoration. All four pass afterward
(normal 3,518 ms; ASan/UBSan 21,047 ms; no skips). The three fixture checks also
passed in isolation before the correction. This is test-harness isolation, with
no production policy change. Final full/client/sanitizer/renderer-off/CLI gates
are pending below; direct desktop/GPU/high-DPI/controller and baseline LSan gaps
remain explicit rather than counted passed.


## Final C20 integration and completion

Implementation complete from the 6182a1943 baseline. Entry is 13 lines, desktop
driver 309 and frame coordinator 469. All private composition consumers are
root files; no feature API accepts complete application state. Root resources
and feature states retain one owner and stable binding/lifetime order. Pure
routing and frame transforms have explicit batch contracts; SDK/displayed UI/
startup lifecycle operations remain documented true singletons. The simplification
pass reused operation modules, retained existing row/device storage and separated
actual event/frame responsibilities without a generic event bus or new owner.
No measured performance requirement or speed/memory-throughput claim is made.

Normal final client/test/all-versioned-tool builds passed. Full configured CTest
passed 15/15 (189.90 s): eight GoogleTest shards selected 2,250 cases, with
2,209 passed and 41 graphics cases skipped. Build identity and all tool version
checks passed. Combined ASan/UBSan client/test/all-versioned-tool builds
passed; complete Client* suites plus the three named renderer regressions selected
488 cases, with 485 passed, 3 skipped and zero failures (1059.325 s).
Leak detection remains disabled for the independently observed baseline VM LSan
limitation, not a clean leak claim. Expanded sanitizer mudl build has existing
double-promotion warnings at viewer_runtime.cpp:2412; changed client sources build
warning-free. Renderer-disabled/tools-disabled configuration remained unchanged,
core/test build passed warning-free and all 275 Client* tests
passed (144.737 s; no skips). These durations are validation times.

Actual no-window CLI checks with an invalid SDL video driver passed all 11 cases:
version/build-info/help, valid/repeated/literal-space init, invalid init, valid
JSON/legacy DockerDemo imports, missing module and conflicting/unknown flags.
Expected exits were 0/1/2. Import checks ran from the executable directory, where
cwd and executable package roots coincide. Earlier cwd=build/tests exposed duplicate
package rejection (exit 1); cwd=repository root exposed loss of executable-only
packages on module reload (SIGABRT). Baseline start_client_kernel/package registration
and kernel creation source are identical for these paths. This existing bootstrap
policy is scoped separately in [client-cli-package-paths.md](client-cli-package-paths.md);
it is not counted as a successful import from arbitrary directories. No desktop
client or GUI automation was launched.

Automated dependency/body/order audits and supported acceptance tests passed.
Expanded frame-loop comparison preserved all stage/counter/pacing order.
The three named GPU regressions were skipped in both normal/full and sanitizer
runs and are not claimed verified. Camera/group ghost/popup/reorder feel, F9
desktop transitions, high-DPI agreement, physical controllers and GPU-only startup
failure injection remain in [client-main-refactor-desktop-validation.md](client-main-refactor-desktop-validation.md).
Native SDK thread completion remains in its existing separate issue. No unresolved
feature/root ownership question remains; scoped verification/baseline policy gaps
are explicit local issues. Final self-check satisfies framing/data/cost, common-case
assumptions, simplification, explicit reject/cancel paths, batch/singleton contracts,
SDK pointer justification and done criteria without unmeasured performance claims.


## Final optional profiler include correction

A syntax compile of the moved frame TU using its actual configured compiler
arguments plus ROLLNW_ENABLE_TRACY/TRACY_ENABLE and the vendored Tracy public
include path failed: FrameMark was undeclared. Original main directly included
nw/util/profile.hpp. Restore that direct dependency in the frame source; no frame
body, state, control or timing policy changes. The same optional-feature syntax
check passes after correction. This is a compiler dependency regression/check,
not a full Tracy-enabled link/run or a measured performance result. Frame module
is now 469 lines. Normal/sanitized builds and build-identity/version metadata are
refreshed after this correction; renderer-disabled configuration remains unchanged.
The full supported integration runs above remain applicable to the unchanged
frame bodies and default feature configuration.


## User manual validation and acceptance — 2026-09-17

The user tested the refactor and reported that all looks good. This completes
manual acceptance and closes client-main-refactor-desktop-validation.md. The main
refactor issue is complete. Detailed manual test coverage was not itemized, so
the previously skipped GPU tests remain recorded as skipped; no new automated
test, high-DPI, controller or failure-injection result is inferred. Existing CLI
package-path and native-dialog SDK follow-ups retain their separate scope.


## Issue archive — 2026-09-17

The completed refactor, accepted desktop validation and three repaired defects
are archived in issues/closed/ with this checkpoint log. Original checkpoint
evidence and coverage limits are retained. The CLI package-path bug and native
dialog SDK shutdown follow-up remain active in issues/.
