# Runtime world placement and item transfers

Status: ground-item preview implemented and verified, 2026-10-24. Tier 2:
pickup -> equip -> unequip -> drop. Shared actor actions/animation timing remain
follow-ups. This is a functional milestone, not
another renderer performance campaign.

## Patterns & Conventions Found

- `tools/client/preview_session.cpp:284`: a headless owner already isolates a
  test actor and navigation/door state, with fixed 60 Hz input batches.
- `lib/nw/objects/equipment_changes.hpp:11`: validated atomic equipment batches
  preserve inventory occupancy and publish only after complete validation.
  `nwn1.item.change_equipment` adds gameplay eligibility and effect notifications.
- `lib/nw/objects/Area.hpp:114` and `Inventory.hpp:101`: existing area item
  membership and bounded inventory rows own items; reuse these containers.
- `lib/nw/nav/NavWorld.hpp:160`: strict position admission and existing path
  batches operate on the already-built navigation world.
- `tools/client/viewer_viewport.cpp:828`: transient visuals and door leases
  already provide renderer teardown. Extend these for preview-owned items.
- Existing client Rml listeners, input ownership, CMake targets, GoogleTests,
  resource fixtures and runtime equipment bridge supply the remaining pieces.
  Keep snake_case, flat rows, spans, explicit failure results and clang-format.

## Architecture Decision

Frame: finish one playable interaction while preserving authored data and item
identity within the running preview. Done means the user can pick up a placed
item, equip it, unequip it, drop it and pick it up again; exiting restores the
editor and destroys all preview-owned objects. Plan B on an integration failure
is to stop the preview safely and restore authoring visuals, never undo gameplay
through editor commands or claim a partial loop complete.

Real platform: Linux client plus Vulkan renderer and a renderer-independent
kernel. Object mutation is single-threaded; GPU resources retire through current
renderer waits. Windows uses the same portable code but is not available for
execution here. No extra worker, offline compiler, physics engine or scheduler.

Data: one controlled actor, its existing inventory/equipment, and the displayed
area's ground-item instances. Inventory capacity is 1024 rows with at most six
default 10x10 pages; actual item footprints come from native layout rows. The
common interaction is one item. Area populations are runtime inputs, not fixed
NWN model-name rules. Blueprints and authored area state stay stable; only the
detached preview's ownership, item locations and actor equipment change.

ASSUMPTION: the first playable policy is pickup within two world units along a
walkable route, drop at the actor's admitted navigation position, and immediate
equipment commit — affects interaction reach and UI help, not asset interpretation.
This intentionally covers reachable ground items; arbitrary table/ledge placement
and animation markers remain separate requirements. Closed-door traversals do
not authorize pickup through a door.

## Component Design / Data Flow

1. Native item-transfer batch: trusted runtime caller supplies area, actor,
   navigation world, reach and flat item/direction rows. Validate live identities,
   membership, inventory geometry, duplicates, reach/navigation and final capacity
   before publishing area membership, inventory/occupancy and item spatial rows.
   Rows are staged in input order; capacity must hold after each staged row.
   Invalid/stale/foreign/equipped/duplicate input and insufficient space reject
   the entire batch unchanged. Drop positions come from the actor, not UI hits.
   Empty input is a no-op. A synchronous owner call has no retained pointers.
2. Preview lifetime: allocate a detached area container containing only cloned
   ground items and the detached actor. Build navigation from authored geometry
   once and use its existing door updates. The detached area owns ground items;
   actor inventory/equipment owns carried items. Destroying the preview releases
   each exactly once. No copies or mutations of unrelated authored creatures.
3. Tick boundary: UI queues explicit item requests; the existing owner consumes
   them before the next simulation tick. Equipment uses existing Smalls policy
   and atomic native storage. Callback failure after commit is reported as such,
   never treated as an unchanged state or automatically retried.
4. Presentation: replace authored ground-item visuals with detached-item visuals
   for the preview, update only changed objects, and restore authored visuals on
   stop. Transient calls explicitly identify the simulation source area rather
   than assuming it is the editor scene's area. An inventory panel exposes
   pickup/equip/unequip/drop and reports rejected requests; Escape closes the
   panel before leaving preview. UI callbacks cannot mutate authoring history.

Ownership/ranges: object handles are required by the existing generation-checked
kernel; convert them to validated container indices during each batch. Existing
Area pointer rows are reused, not duplicated into a second ownership registry.
Navigation and model lifetimes remain with their current owners. New UI state
uses session-local indices/handles and is cleared on session changes. Nonfinite
positions, invalid reach, malformed footprints and out-of-range indices reject.

## Implementation Map / Build Sequence

1. Add `lib/nw/objects/item_transfers.hpp/.cpp` and focused headless tests; reuse
   Inventory, Area, spatial components and navigation. Register in existing builds.
2. Extend `tools/client/preview_session.*` with detached item lifetime, batch
   requests/results and borrowed inventory/ground-item views; cover restoration,
   failures, identity and repeated start/stop with real fixtures.
3. Extend existing transient viewer APIs and client renderer forwarding for item
   visuals/source-area association and equipment refresh. Add focused GPU checks.
4. Add the small preview inventory view and input/listener integration; reuse
   current Rml styling and event lifetime. Update client help and verify the full
   loop with the existing client test harness and one rendered demonstration.

## Critical Details / Simplification / Verification

Remove work: copy only participating items at startup, reuse navigation, dispatch
equipment through current rules, and refresh changed visuals only. No full-area
reload per interaction, no generalized command bus or gameplay persistence format.
For A ground items, I carried/equipped rows and T requests, identity validation
costs O((A + I) log(A + I) + T log T), staging searches cost O(T * (A + I)),
and pickups pay an existing navigation query per item. Scratch costs O(A + I + T)
plus bounded navigation buffers and the existing 1024-row Inventory allocation.
This is event-time work, not a new per-frame scan or a claimed optimization.
Clones add memory proportional to ground-item state for the preview lifetime.
Renderer refresh can wait for GPU use to finish; report this existing cost.

Verify atomic rejection (including mixed valid/invalid batches), full inventory,
foreign/stale/equipped items, blocked/out-of-range pickup, unchanged item identity,
equipment effects, grounded rendering, exact authored state after exit, and no
live-object growth across restarts. Check input routing and stale UI actions.
Use affected tests and real NWN fixtures; report GPU/desktop and Windows limits.
No performance improvement is claimed. Open animation-marker questions remain in
[runtime equipment animation](runtime-equipment-animation.md).

## Verification / final self-check

- Linux Release client and test executable build successfully; changed C++ follows
  clang-format and `git diff --check` is clean.
- 37 preview/equipment/transfer/view tests and 30 input tests pass. These cover
  ordered atomic failure, capacity, invalid ownership/layout/reach, repeated item
  identity, equipment effects, exact authored item serialization and equal live
  object counts after stopping with items carried, equipped and dropped.
- Two offscreen Vulkan tests pass using desktop NWN resources. The new test clicks
  the actual Rml controls, verifies that clicks only queue requests, checks visual
  removal/addition and equipment model changes, and restores editor selection.
  The existing transient-visual regression also passes. Vulkan validation reports
  zero errors and warnings in the new test.
- Captured inventory/equipment/drop UI was inspected. The 1000x760 authored scene
  captures before and after preview are pixel-identical (RGBA byte comparison).
  Local evidence: `build/gameplay-item-loop/{headless,input,gpu}.xml` and
  `build/tests/tmp/preview-items-*.png`.
- Simplification retained existing ownership containers, navigation, equipment
  rules and visual replacement. Area teardown explicitly calls `clear()` before
  destruction; the lifetime test caught and now guards that requirement.
- No new steady-state inventory scan, worker pool, generalized command bus or
  serialization format. Input bounds/errors and borrowed lifetimes are stated in
  the batch headers. Remaining design questions are below and in linked issues.
- No FPS/latency claim, Windows run, manual desktop/controller acceptance, or
  animated pickup/equip acceptance is included in these results.

## Remaining boundaries

### Item hover (implemented, 2026-10-24)

One optional viewport pixel produces one highlighted ground Item. Missing,
non-finite or out-of-viewport input clears the highlight. The existing batch
triangle picker supplies six-pixel assistance; direct and assisted Item hits
respect tile/object occlusion. Hover never changes editor selection or ownership.

The Linux/Ryzen/Radeon client runs the picker on its owner thread after the
simulation/camera update. CPU queries read scene bounds/triangles; highlight
transitions copy only the hovered models' material rows into existing per-instance
overrides. Original bindings are restored before removal/replacement. Generational
instance/material handles guard this cold lifetime operation across compaction;
the model lookup uses the existing sorted object-to-model index. No shader,
readback, GPU wait or additional draw pass was added for hover.

ASSUMPTION: target changes are less frequent than rendered frames — affects the
choice to allocate temporary material rows only on transitions. Steady frames
perform the picking query but retain identical override handles. Memory is
proportional to the hovered item's models/materials. Picking latency has not been
benchmarked; no performance improvement is claimed.

Simplification reused picking, UI input routing and material overrides. Hover
clears under modal/UI capture, camera dragging, pointer/window exit and preview
teardown; a stationary pointer is rechecked when camera/world state changes.
This viewport pointer is a genuine singleton using the count-one batch picker.

Verification: 57 headless input/preview tests and 26 GPU/picking tests pass
(`build/gameplay-hover/{headless,gpu}.xml`). The rendered item-loop test verifies
stable override counts across repeated hover, exact binding restoration, camera
movement, visual replacement and zero Vulkan validation errors/warnings. The
1000x760 image changes only within the item's 70x19 pixel region while hovered;
clearing hover and exiting preview both restore the original RGBA bytes exactly.
Desktop pointer/controller acceptance and Windows remain unverified.

### Shared runtime work

- Shared actor action sequencing now owns move-to, pickup, equip, unequip, drop
  and cancellation in `nw-gameplay`. Pickup requests the existing movement path,
  revalidates at commit and emits one terminal result. World clicks and inventory
  buttons enter the fixed tick; direct movement cancels the owned approach.
  There is one active action per actor with replacement semantics. A general
  FIFO, general animation markers and world-state serialization remain unimplemented.
  Pickup includes a simulation-timed grab and reverse recovery using the actor's
  authored clip duration; transfer occurs once at the end of the grab. Cancellation
  before commit leaves the item on the ground. Missing clips use immediate pickup.
- Elevated surfaces, drop-target searches, containers, persistence and timed equip
  animations are outside this ground-item preview. Existing navigation is reused;
  no renderer hit or editor undo command authorizes a gameplay transfer.
- [Equipment proficiency aggregation](nwn1-equipment-proficiency-alternatives.md)
  needs a separate rules decision; preview respects the current profile policy.
- Interaction latency and startup memory have not been benchmarked. Set a budget
  from an actual gameplay workload before adding scheduling or caching machinery.
