# rollnw Client Release Demo

Status: open.

## Goal

Record one short public video after the client history is integrated into
`main`. Screenshots can show renderer output but cannot show the interactions
that define the current product: project navigation, camera preservation,
selection, object focus, Details, focused editors, undo/redo, and save.

The output is one 4--6 minute YouTube walkthrough linked from the repository
README. It presents the client as a useful module viewer and bounded authoring
workbench, not as a complete NWToolset replacement.

## Capture Input

- one legally redistributable or locally available module with several areas;
- one area with varied tiles, placed objects, triggers, and encounters;
- one dynamic humanoid creature with equipment and PLT colors;
- one item and one dialog resource; and
- a clean imported JSON project produced by the documented command.

The module name and local paths are presentation inputs only. The demo must not
depend on resource-name special cases.

## Walkthrough

1. Import the module and launch the client.
2. Use Home search and area previews to open an area.
3. Navigate the viewport, switch tabs to show camera preservation, select a
   placed object, and focus an object from the placed-object list.
4. Show Details and one focused Creature appearance/PLT edit with undo/redo.
5. Show inventory or item editing and a dialog view.
6. Edit the current area and a blueprint, switch back to show retained edits/undo,
   use Save All, then reopen the resources and show the persisted results.
7. End with the explicit current limits from `tools/client/README.md`.

## Cost And Constraints

Capture is one linear desktop recording plus small cuts and captions. The
target is clarity, not a produced trailer; recording and editing should fit in
one working session. Runtime performance claims are excluded unless frame data
is captured and reported from the demonstrated build.

## Placement release checks

The [placement authoring slice](rollnw-client-area-object-placement.md) is complete;
these release checks remain open:

- From a fresh process, verify blueprint drop, selection, drag/cancel, undo/redo,
  and save/reopen; confirm navigation-overlay legibility.
- Exercise Save All through Ctrl+Shift+S and the palette with multiple modified
  tabs. Verify quit Save All / Discard / Cancel and project replacement with
  unsaved work. Automated ownership/save tests do not replace this UI pass.
- Confirm area navigation reuses the one pinned Area tab, including Save /
  Discard / Cancel before replacing an edited area.
- Observe retained memory and tab-switch/save latency with the largest routinely
  edited area and several blueprint tabs. Open-document graphs remain resident; a
  practical memory limit has not been measured.
- Measure pointer-motion CPU time and begin/commit/cancel frame time in Tracy
  on a small area and the largest routinely edited area. Record the area size
  and demonstrated commit; small-area query timings do not establish large-area
  responsiveness. Visible input stalls require investigation before capture.

## Done

- the demonstrated commit is on `main` and identified in the description;
- every shown operation works from a fresh process and clean imported project;
- no local debug instrumentation or private absolute path is visible;
- audio and 1080p text are legible; and
- README links directly to the published video.
