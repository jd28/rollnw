# Client refactor desktop and GPU validation

Status: closed by user acceptance on 2026-09-17. The user tested the refactor
and reported that all looks good. Implementation and supported automated checks
are complete. See client-main-refactor-progress.md for actual counts and audits.
No desktop client/GUI automation was launched by the coding agent during the
refactor. The user's manual test coverage was not itemized; this acceptance does
not establish that skipped GPU tests or every targeted check below were run.

Supported evidence: production pure routing/device/frame batches, actual client
Rml templates, real density-1 dummy SDL forwarding, command/edit/undo counts,
gesture/resource identity checks, frame/body/order/dependency audits, full CTest,
combined ASan/UBSan client suite and renderer-disabled client tests. SDK pointer
borrows are required by SDL/Rml/renderer APIs; no shared index table can replace
those external SDK identities. Indexed tile/list/device transforms remain in their
existing owners.

Original targeted verification checklist, retained as a coverage reference:

- The three skipped renderer regressions: tile-preview retained rows, editable
  door live selection geometry, workspace documents across scene rebuilds.
- Camera navigation and group ghost/hover cleanup while painting, rotating,
  erasing, undoing and moving the camera; stationary modifier/no-brush selection.
- Popup selection, sound volume single-gesture commit, managed-list reorder and
  resource drops after object/tab/project switches and focus loss.
- F9 picker/drop/start/stop/Escape, camera/focus restoration, doors/navigation;
  physical controller disconnect/reconnect and held input around UI ownership.
- High-DPI agreement between current native pointer coordinates and SDK density
  conversion. Headless density-1 evidence does not establish higher densities.
- GPU-only initialization/scene failure cleanup at acquisition boundaries not
  reachable with the supported headless fixture.

Reject changed controls, missed/double edits, editor actions during PC ownership,
stale-resource writes or incorrect cleanup order. File concrete failures with
actual input and a failing production regression before behavior fixes. Costs:
estimate: one desktop review session plus GPU fixture runs; no new instrumentation framework,
binding system or speculative optimization is required. Existing baseline LSan
and vendor native-dialog thread completion are separate scoped limitations.
