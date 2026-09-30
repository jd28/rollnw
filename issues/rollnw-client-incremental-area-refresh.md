# Preserve live area playback during edits (Tier 2)

## Patterns & Conventions Found
- `preview_scene.cpp:4843` removes model rows while retaining unrelated instance handles and particle rows; `:4955` updates tile instances; `:5891` stages visual replacements. Extend these paths, not a second renderer.
- `scene_debug.cpp:1197` stages and compacts indexed overlay arrays. Extract its replacement step for additions/deletions as well as edits.
- `client_application_workspace.cpp:57` falls back to replacing the entire scene; `session.cpp:733` destroys playback state. All same-area synchronization must retain the PreviewScene.
- C++ spans, flat result structs, RAII staging, kernel generation handles at ownership boundaries, indexed render columns, clang-format, existing headless gfx tests.

## Architecture Decision
Frame: eliminate unrelated particle/animation resets for all audited edit and recovery paths. Opening a different area and explicit Reload still load a new scene, as requested by those operations. No world/renderer redesign.
Real platform: Linux desktop CPU plus asynchronous GPU; edits run on the main thread; model resources must outlive submitted frames; live particle state is scene-owned. Hardware timing is unmeasured, so this is a correctness change, not a speed claim.
Data: authored area object arrays (nine supported types), row-major AreaTile rows, model-instance columns, owned debug ranges and lights. Ordinary edits change one object or one tile stroke; undo/redo and blueprint replacement can change batches. ASSUMPTION: single-object edits are the common interactive workload—affects prioritizing unchanged-row retention; no measured distribution exists.
Stable: area dimensions, root handle, unchanged model instances and playback. Volatile: object membership, changed appearances/markers, tile identity/height/orientation/light fields. Limits: finite transforms, valid generation handles, unique batches, uint32 indices, tile orientation 0..3, tile ID -1 (void) or an existing tileset row. Reject malformed input with a diagnostic; keep scene ownership and unrelated playback intact.
Cost: same-area reconciliation scans/sorts object identities and scans tile snapshots only on structural notifications; O(N log N + T) CPU work, O(N+T) retained snapshots and O(changed data + affected render/overlay columns) staging. Model removal compacts columns and repairs indices; existing removal/overlay ownership checks cost O((M+G)*D) for M model rows, G overlay ranges and D changed identities. The session waits for prior GPU work before visual/structural synchronization, including no-op structural notifications. No per-frame journal or generalized event system.
Simplification: do not recreate unchanged objects or tiles. Reuse existing model/overlay/tile batch transforms; snapshot comparison handles coalesced notifications without another mutation queue. Cache only the last successfully displayed authored rows. No performance approximation or lookup table needed.
Plan B: report a stale view and retry the incremental reconciliation after failure; never silently reset the whole scene to hide an edit failure.

## Component Design
- PreviewScene: retain sorted authored object handles and dense displayed tile values. Ownership stays with scene; handles identify kernel objects across undo/replacement, render traversal continues to use indices. This is edit-time scanning, not a pointer-heavy frame path.
- Object visual batches: stage models and overlays for additions/replacements, remove only requested model/light/debug rows, repair render/tile/particle indices, append staged data. Removed kernel handles may already be stale; removal matches stored identities without dereferencing them.
- Tile batches: support missing/void model rows and removals, preserving unaffected instances and emitters. Maintain displayed tile snapshot only after success.
- ViewerSession: reconcile live membership and tile differences in place, preserve camera/time and resolve selection again. Root area is a genuine singleton per session; changed objects/tiles use batch APIs.
- Client: route appearance and Sound edits precisely; use same-area reconciliation for structural changes/cancel/rollback; refresh only missing play-preview door visuals.
Error model: invalid shape/ranges reject before replacement construction; staged load failure leaves existing rows available. Recoverable cache failure rebuilds render metadata from retained instances, never scene playback. Allocation/resource failures report a diagnostic and leave retryable state.

## Implementation Map
Modify `preview_scene.hpp/.cpp`, `scene_debug.hpp/.cpp`, `session.hpp/.cpp`, client viewport/renderer wrappers and edit routing, object edit classification, focused renderer/editor tests, and client README. If light ownership lacks a removal key, extend existing SceneLocalLight rows and scene_lights calls rather than introduce a registry.

## Data Flow
Editor mutation -> targeted visual batch or authored snapshot comparison -> staged changed models/debug/tile rows -> removal/append and index repair -> refresh render caches -> selection resolve. Unchanged particle systems/model instances remain owned by the same scene. Failure -> diagnostic/stale retry.

## Build Sequence
1. Extend shared overlay/model/tile batch operations and snapshot reconciliation.
2. Replace audited client rebuild routes; narrow Sound changes and repair missing door visuals locally.
3. Build client/test targets; test live-emitter continuity, undo/redo, placement cancel/promotion, mixed overlays, appearance lights, blueprint handle replacement, void/restore tiles, coalesced edits, invalid input and actual rendered output.

## Critical Details
Done: every audited same-area edit path avoids set_scene/bootstrap_scene_playback; unchanged particles retain ages/positions/emitter times/seeds; unchanged model handles, camera, selection semantics, lighting, tile picking and undo/redo remain correct. Failure evidence: any unrelated particle reset, stale owner/index, ghost left behind, or missing light/model after edit.
Validation: headless gfx with real NWN assets plus existing CPU/editor tests. Report test counts/skips and any unverified desktop/platform behavior. No numerical latency claim without measurement. Keep unresolved questions here under issues; no speculative extension points.

## Observed data and implementation notes
- The DockerDemo `test_area` fixture is 16 x 16 (256 tile rows), with 261 initial model rows and all nine authored object categories represented. Sound toolset marker meshes are absent in this fixture; their supported geometric fallback is covered. Candle appearance 384 supplies real native emitters with live particles after one second.
- No per-frame data protocol changed. Scene snapshots hold only authored membership and displayed tile values; caller-owned preview leases must be restored before structural synchronization, as the client already does.
- `synchronize_area_rows` is the batch transform; session synchronization is a single active-area boundary. Scene-local generation handles survive model-vector compaction; model/particle/tile/light relationships use remapped indices. Table lights retain the kernel owner identity even when no model node exists.
- Tile ID -1 removes its render rows; restoration appends a row and repairs the coordinate mapping. Optional light-debug overlays have a distinct category and are regenerated without recreating the scene.
- Simplification pass removed dead area branches from the standalone visual refresh and reused one loader for placement previews and committed additions. No mutation queue or second model registry was introduced.
- Final indirect-reload audit: blueprint document publication also incremented the global area reload generation. Removed that redundant signal: the viewport already reloads a replaced document root, and current-area live instance publication has its own structural mutation. Project/module opens and the explicit Reload command retain their reset behavior.

## Verification and final self-check (2026-09-29)
- Release builds passed for `rollnw-client`, `mudl`, and `rollnw_test`; the final build log has no compiler warnings/errors. Log: `/tmp/rollnw-area-refresh-build.log`.
- Broad regression run: 218 tests, 216 passed, 2 skipped for absent native Sound/Store marker model fixtures. Includes prepared rendering, selection, lighting, object edits, tile edits, blueprint commands/jobs, encounter points and wheel input. Log: `/tmp/rollnw-area-refresh-full-tests.log`.
- Three renderer regressions also passed with `ROLLNW_VIEWER_TILE_LIGHT_DEBUG=1`: mixed area edits, encounter points, and retained tile previews. Log: `/tmp/rollnw-area-refresh-debug-tests.log`.
- Real candle particles retain ages, positions, emitter clocks, seeds and spawn accumulators through placement/cancel, promotion, duplication, deletion, undo/redo, appearance replacement, Sound marker fallback changes, blueprint handle replacement, missing Door visual repair, coalesced tiles, void/restoration and subsequent model compaction. Model handles, camera, owner indices and rendered frames are checked. Playback advances on the next tick.
- Authored materials replace translucent placement materials. Stale table-light owners disappear. Invalid tile IDs reject without replacing the scene/model rows. Existing marker selection and sound-radius tests pass.
- Source audit: no `rebuild_live_area` or `rebuild_live_viewer_area` path remains. Same-area structural recovery uses snapshot reconciliation; preview Door repair uses a visual batch. Only explicit load/reload, document-root replacement and standalone preview rebuilds replace a scene.
- Simplification/self-check: batch operations and finite/range validation are explicit; no new frame-time state machine, callback protocol or speculative extension point. Existing kernel-owned pointer arrays are read once per edit into sorted identity rows, since replacing kernel object storage is outside this renderer fix. Model traversal remains indexed. Scene and UI selection are genuine singletons; their member wrappers call batch transforms.
- `clang-format --dry-run --Werror` and `git diff --check` passed. No unresolved design questions remain. No latency improvement is claimed or benchmarked; the verified requirement is playback continuity. Interactive desktop operation, Windows, and native Sound/Store mesh fixtures were not verified here.
