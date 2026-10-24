# Retain palettes in file-loaded Item scenes

Status: closed, 2026-10-03. Implementation commit: `8fe0bbd92`.

Cluster: [Rendering performance overhaul](../rendering-performance-overhaul.md).

Palette retention, exact undo and ownership verified by the recorded replay and
regression checks.

Implementation checkpoint: implemented and verified, 2026-10-01.

## Tier 1 plan

### Patterns & Conventions Found

- `lib/nw/render/viewer/preview_scene.cpp:2824` loads an Item file and records
  model-to-visual-row indices, but never captures the matching visual snapshot.
- `preview_scene.cpp:4888` already captures that snapshot for live objects.
  `:2003` validates the complete palette batch before publishing constants.
- `preview_scene.cpp:6340` removes/replaces standalone models, invalidating the
  palette mapping. `append_render_models` preserves source order for models it
  successfully appends; use actual counts to reject incomplete transfers.
- `lib/nw/render/viewer/session.cpp:861` already tries palette retention before
  waiting for GPU use and reconstructing models. Keep this existing boundary.
- Existing tests use real Items, ObjectEditBatch forward/inverse edits, indexed
  instance stores and headless Vulkan. Follow local naming and clang-format.
  No external dependency or new utility subsystem is needed.

### Architecture Decision

Capture the existing scene-owned palette snapshot after loading an Item file.
After a complete standalone model replacement, transfer the replacement's dense
row mapping and recapture the current snapshot. Do not infer mappings from model
names. Reuse the validated material batch and existing reconstruction fallback.
The bounded objective is lower edit latency; renderer-wide invalidation or UI
binding work is outside this change. Plan B is the current full refresh.

Actual input: `cloth028.uti` from the development fixtures, six global palette
bytes and 19-by-six part colors (255 inherits), resolved visual rows, loaded
models and their material overrides. The recorded desktop workload alternates
one global Cloth 1 color edit with exact undo, 40 measured pairs per variant.
ASSUMPTION: this workload represents interactive color browsing — affects the
latency claim; tests additionally cover part colors and geometry transitions.
Geometry and resource generation are stable across these edits; colors vary.
Valid ranges, masks and source indices follow the existing visual contract.
Missing/stale mappings or changed geometry/resources reject the retained batch
before writes and use reconstruction. No change to authored object ownership.

### Component Design

`preview_scene.cpp`: factor the existing snapshot capture into a local helper;
call it for live scenes and file-loaded Items. At standalone replacement, move
the mapping only if every source model was appended and no old models remain.
The snapshot belongs to that scene until invalidated by topology/resources.
Existing palettes are copied to existing CPU material overrides at edit time;
no GPU resource lifetime or threading contract changes.

### Implementation Map

- Modify `lib/nw/render/viewer/preview_scene.cpp`: snapshot capture and complete
  standalone replacement mapping transfer.
- Modify `tests/render_viewer_prepared_draws.cpp`: real file-load palette edits,
  exact undo, model/instance/camera retention, and palette edits after geometry
  replacement. Keep invalid-batch/resource tests from the previous change.
- Create this issue and matched measurement evidence. Temporary desktop replay
  hooks are removed before delivery.

### Data Flow / Cost

Item file -> existing resolved visual rows and loaded models -> snapshot once ->
edit -> validate unchanged geometry/all destinations -> publish palette constants
-> existing draw. Geometry edit -> existing rebuild -> complete mapping transfer
-> snapshot -> subsequent colors use the same batch path.

Platform: Ryzen AI 9 HX 370 Linux desktop, one UI thread, Radeon 890M asynchronous
Vulkan rendering, Release/Tracy OFF. Existing trace measures CPU latency through
presentation return: 8.60 ms edit, with 2.53 ms in main-scene refresh; undo scene
cost is 3.55 ms. Savings are unverified until matched replay. Cost is a copied
visual snapshot plus existing four-byte model indices per eligible scene, and
O(visual rows + material rows) predictable linear validation/publication per edit.
There is no new allocation in the palette publication pass. Indexed model and
override stores remain; temporary asset/object pointers are synchronous borrows
required by existing APIs. Snapshot initialization is per genuinely singular
scene; the material transform and public refresh entry point already use batches.

### Build Sequence

1. Build the current code with the existing replay to preserve a baseline.
2. Implement the two snapshot boundaries and lifecycle regressions; build/test.
3. Run baseline/candidate/candidate/baseline desktop replays with identical
   assets, dimensions and warmups; record raw traces and exact undo hashes.
4. Remove replay hooks, rebuild production, check formatting and final diff.

### Critical Details / Done

Simplification: avoid reconstructing unchanged models; capture snapshot only at
load/rebuild, and reuse the existing small index table. No approximation, cache,
worker, shader change or extra dirty-state protocol. The tests must prove first
color edits and post-geometry color edits retain models, constants follow actual
visual rows, undo restores serialization exactly, and camera/ownership survive.
Invalid batches retain existing rejection behavior. A stale color, wrong mapping,
ownership failure or unchanged measured latency disproves the approach. Report
CPU median/p95 and phase timings; GPU/display latency remains unmeasured.

## Results and final self-check

File-loaded Item scenes now capture their existing indexed palette mapping's
visual snapshot. Complete standalone geometry replacements transfer their source
indices and recapture the snapshot, so the next palette refresh is eligible.
The common batch updates existing material constants or does nothing for models
without PLT materials. It avoids the resource-destruction wait and reconstruction.
Incomplete mappings, changed resources/geometry and invalid materials still
reject the retained path before publishing any colors. Ownership transfers to
the scene before the allocating snapshot copy, preserving one cleanup owner.

The final matched desktop run used the same fixtures, installed assets,
3841x2028 framebuffer and baseline/candidate/candidate/baseline order. Each
operation below has 40 warm measured samples per variant, after three warmup
pairs per scenario/process. The baseline already includes the previous Item DOM
and mannequin palette improvements.

| CPU interval | Before | After |
|---|---:|---:|
| Color edit, median | 10.929 ms | 7.216 ms |
| Color edit, p95 | 16.115 ms | 8.734 ms |
| Undo, median | 10.600 ms | 5.094 ms |
| Undo, p95 | 13.553 ms | 8.235 ms |
| Main scene refresh during color edit, median | 2.367 ms | 0.0052 ms |
| Main scene refresh during undo, median | 3.574 ms | 0.0058 ms |

The matched edit median fell 34.0%; undo fell 51.9%. Compare within this run,
not against the older 8.60 ms session: desktop CPU/UI costs varied between
sessions. These are dispatch-to-CPU-presentation-return measurements, not GPU or
display latency. Inclusive phases must not be added as disjoint contributions.
No builds or tests ran concurrently with timing. An earlier exploratory replay
preceded the ownership-order correction; only the final replay is archived.

All 1,288 replay actions completed with identical input/restored-state hashes
across variants. Every undo restored the serialized object exactly, selection
was preserved, and the Item DOM remained retained. Screenshots were inspected:
both show the same selected color, armor and inventory icon. The two regression
checks failed on the baseline as intended; the final nine focused graphics
tests passed without skips. They cover first global/part edits, model/instance
identity, exact undo, camera, ownership transfer/destruction, real geometry
replacement and undo, post-rebuild palette eligibility, material constants and
invalid mapping/resource rejection, plus area/document lifecycle behavior.

The observed original armor scene contains one model and one visual row. Its
four-byte mapping and 88-byte snapshot object already existed; the new retained
snapshot adds one 72-byte visual model row plus normal vector capacity/allocation
overhead. No new GPU resources or palette-path allocation are introduced. The
complete-replacement path moves the existing index vector instead of copying it.

The same complete-replacement retention also applies to standalone creatures.
The replay's same-base belt swap measured 30.119 -> 11.714 ms median after the
warmup rebuild established the mapping. This does not imply that equipment
changes which alter geometry avoid rebuilding. Class/area timing differences
are not attributed to this change; those paths receive no new palette behavior.

Simplification reused one snapshot helper and the existing validated batch;
there is no second renderer path, per-frame work, additional cache, UI change or
new configuration. Mapping order is accepted only after complete replacement;
the existing batch validates indices/material destinations. No new pointer-heavy
hot path or public interface was added. Done criteria were checked, with
post-geometry eligibility and palette correctness covered by complementary
tests. Final Release build, clang-format and source diff checks passed; temporary
replay/screenshot hooks were removed. Cold first-edit performance, physical
display latency, GPU timing and Windows remain unmeasured.

Raw replay logs, screenshots and reproduction files remain under
`/tmp/rollnw-file-palette-profile`. The earlier issue-local evidence folder
is no longer present; these temporary artifacts are not tracked.
