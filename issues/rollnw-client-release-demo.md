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
6. Save, reopen the resource, and show the persisted result.
7. End with the explicit current limits from `tools/client/README.md`.

## Cost And Constraints

Capture is one linear desktop recording plus small cuts and captions. The
target is clarity, not a produced trailer; recording and editing should fit in
one working session. Runtime performance claims are excluded unless frame data
is captured and reported from the demonstrated build.

## Done

- the demonstrated commit is on `main` and identified in the description;
- every shown operation works from a fresh process and clean imported project;
- no local debug instrumentation or private absolute path is visible;
- audio and 1080p text are legible; and
- README links directly to the published video.
