# Desktop edit evidence, 2026-10-01

See [results and limits](../rollnw-client-area-edit-transition-measurement.md#desktop-results-2026-10-01).
`measurements.json` contains pooled median/p95, phase counts, platform, run order
and exact restored-state hashes. Four compressed JSONL traces retain all 322
changed frames per run. Action labels join on `epoch_after`; discard negative
sample numbers (three warmup pairs). Each remaining operation has 40 samples
per variant. `summarize.py` independently recomputes and checks the stored results.

`armor-before.png` and `armor-after.png` show the same native color edit in the
baseline and retained-editor builds. They are code before/after comparisons,
not two different selected colors. Screenshot readback ran on a subsequent idle
warmup frame, outside measured edits. The desktop driver never saves edited
resources. It compares full serialized roots after every undo pair and verifies
selection; candidate runs additionally check a marker on the retained template.

## Reproduction on this machine

Requires the configured Release build, installed NWN resources and the connected
desktop. Paths in the temporary driver and runner deliberately describe this
one recorded experiment. Run only with disposable project/preferences copies.
The authored area remains outside the repository; its expected hash and path
are in `input-hashes.json`.

Prepare `/tmp/rollnw-edit-profile-project` using the client's `import --json`
command with `tests/test_data/user/modules/DockerDemo.mod`. Copy `wizard_pm.utc`,
`pl_agent_001.utc` and `cloth028.uti` from `tests/test_data/user/development` into
its `shared` directory. Copy The Awakening's `pvp_area_2.caf.json` into
`shared/areas`. Decompress `bench32.caf.json.gz` there as `bench32.caf.json` before
opening the project. It is the current deterministic 32x32 initializer output;
its exact hash, tile-ID counts and orientations are recorded. Create
`/tmp/rollnw-edit-profile/preferences` for isolated application preferences.

Starting with the production changes in this revision:

1. Apply `desktop-replay.patch` using `git apply`. This installs the temporary
   driver at the frame boundary and an idle screenshot hook.
2. Apply `baseline-dom.patch`, build `rollnw-client`, and copy the executable to
   `build/tools/client/rollnw-client-edit-matched-baseline`. Keep it beside the
   original so shared-library and asset discovery stay identical.
3. Reverse only `baseline-dom.patch` with `git apply -R`, then rebuild
   `rollnw-client`. Both binaries now have identical tracing and replay hooks.
4. Run `python3 issues/client-edit-transition-evidence/run_matched.py` with desktop
   access. It launches sequentially from `build/tools/client`, sets
   `ROLLNW_CLIENT_UNCAPPED=1`, injects native clicks/undo, and writes to
   `/tmp/rollnw-edit-profile`. Do not compile or run tests during measurement.
5. Reverse `desktop-replay.patch` and rebuild the production client.

Build invocation used:

```sh
env CCACHE_DIR=/tmp/ccache CCACHE_TEMPDIR=/tmp/ccache-temp cmake --build build --target rollnw-client rollnw_test -j4
```

Tests ran from `build/tests`:

```sh
./rollnw_test --gtest_filter='ClientEditTrace.*:ClientApplicationFrames.*:ClientInventoryWorkbench.*:ClientAppearanceView.*:ClientSmallsItemEditor.*:ClientCreatureWorkbench.*:ClientObjectWorkbench.*'
```

All 103 passed; `regressions.xml` retains the results. Native event dispatch is
also covered by the desktop replays because the application loop is not part
of these unit-test fixtures. The replay/screenshot hooks are absent from the
production sources and are not a supported automation API.

## Data limitations

Area tile edits call the existing backend batch API inside the input interval;
they do not replay mouse brush hit testing. Character and item edits click the
real controls through SDL; every undo uses Ctrl+Z. Equipment uses a belt swap,
not a new chest mesh. The class fixture emits optional equipment-resource
warnings; raw logs preserve them. Corrected runs have no missing UI textures.
The recorded frame size is 3841x2028, not the older renderer harness's 1280x720.

Initial exploratory runs launched from the repository root and missed UI PNGs.
Those numbers are excluded. A first setup attempt also failed because it created
the generated area after catalog discovery; precreating the archived fixture
avoids that startup-only failure. CPU submission return is measured; physical
display, GPU shader duration, cold first-edit and Windows results are unavailable.
