# Retained palette measurement, 2026-10-01

[Results and limitations](../rollnw-client-item-preview-palette-refresh.md#results-and-final-self-check)
describe the implementation, timing endpoints and validation. This comparison
isolates palette retention: both executables already retain the Item editor DOM.

`measurements.json` contains all seven scenarios, median/p95, phase call counts,
platform, baseline binary hash and restored-state hashes. Four compressed traces
contain 322 mutation frames each. Join the action files on `epoch_after` and omit
negative sample indices (warmup). Recompute and verify the summary with:

```sh
python3 issues/client-edit-transition-evidence/summarize.py issues/item-preview-palette-evidence
```

Fixtures, input hashes/distributions, native-event routing and the temporary
desktop replay are shared with the
[earlier client measurement](../client-edit-transition-evidence/README.md).
The generated and authored area files were not changed. The two armor screenshots
capture the same global Cloth 1 edit, with all UI assets loaded. Captures occur
on an idle warmup frame; screenshot readback is outside measured edits.

## Reproduce

Use the existing disposable project setup from the earlier measurement. Keep the
baseline beside the client executable for identical asset and library discovery.
Starting with the completed production palette changes:

1. Apply `issues/client-edit-transition-evidence/desktop-replay.patch`.
2. Apply `issues/item-preview-palette-evidence/baseline-palette.patch`. Build only
   `rollnw-client` and copy it to `build/tools/client/rollnw-client-palette-baseline`.
   Do not build the new tests against the temporary baseline API.
3. Reverse only `baseline-palette.patch` and rebuild `rollnw-client`. Both runs
   consume the current provider scripts; the baseline executable uses its prior
   clone/reload implementation. Fresh mannequin defaults are unchanged.
4. Create `/tmp/rollnw-palette-profile` and run
   `python3 issues/item-preview-palette-evidence/run_matched.py` with desktop access.
   It uses the existing isolated preferences under `/tmp/rollnw-edit-profile`,
   launches from the client asset directory and writes new output into
   `/tmp/rollnw-palette-profile`.
5. Reverse `desktop-replay.patch` and rebuild the production client and tests.

Build command:

```sh
env CCACHE_DIR=/tmp/ccache CCACHE_TEMPDIR=/tmp/ccache-temp cmake --build build --target rollnw-client rollnw_test -j4
```

No compiles or tests ran concurrently with desktop measurement. Process order is
before, after, after, before; each scenario has three warmup edit/undo pairs and
20 measured pairs per process. Each variant has 40 measured samples per operation.

## Validation

From `build/tests`, the following produced `regressions.xml` (105 passes):

```sh
./rollnw_test --gtest_filter='ClientEditTrace.*:ClientApplicationFrames.*:ClientInventoryWorkbench.*:ClientAppearanceView.*:ClientSmallsItemEditor.*:ClientCreatureWorkbench.*:ClientObjectWorkbench.*:RenderViewerPreparedDraws.PaletteBatches*'
```

The full installed game assets are required for the real model tests. The
dedicated-server tree lacks these render payloads. This produced `render.xml`
(six passes, no skips):

```sh
env NWN_ROOT='/home/josh/.local/share/Steam/steamapps/common/Neverwinter Nights' ./rollnw_test --gtest_filter='RenderViewerPreparedDraws.ItemMannequins*:RenderViewerPreparedDraws.StandaloneItemPreviews*:RenderViewerPreparedDraws.PlacedArmorEdits*:RenderViewerPreparedDraws.StandaloneItemVisualEdit*:RenderViewerPreparedDraws.LiveObjectPreviewIgnoresWorldPlacement:RenderViewerPreparedDraws.GroundItemsConsumeLiveSpatialRowsInGameAndToolsetModes'
```

The tests verify retained model/instance identities and material constants, plus
source isolation, inheritance/undo and explicit fallback/rejection. Native
replays additionally check exact serialized restoration, selection and retained
DOM. The initial attempt with dedicated-server assets skipped two full-body
tests and could not load the standalone test's world models; the full-resource
run resolves that environment limitation. Warm CPU timing is measured; cold
latency, GPU shader time, physical display latency and Windows are not.
