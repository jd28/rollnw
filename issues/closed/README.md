# Closed issues

These records preserve completed work and its verification history. They were
archived for the refactor on 2026-09-17 after the user accepted manual testing.
Later completed work records its own date and verification limits.

| Issue | Completion evidence |
| --- | --- |
| [Off-screen billboard batches](render-perf-particle-culling.md) | 340 tests, 14,880 matched frame pairs and 16 clean validation captures; player particle CPU 0.388 → 0.096 ms. Frame tails and full gameplay acceptance remain open. |
| [File-loaded Item palette retention](rollnw-client-file-item-palette-refresh.md) | Snapshot/mapping retention; nine focused graphics tests and 1,288 replay actions, including exact undo and ownership. |
| [Gameplay runtime baseline](gameplay-runtime-baseline.md) | Complete host-frame/fixed-step instrumentation; 17 CLI checks, four viewer checks, 24 hardware area runs and 45 component cases. Gameplay budget/coverage follow-ups remain in the active cluster. |
| [Zero-color Forward+ lights](forward-plus-zero-lights.md) | Exact-zero filtering; 16 focused checks and matched hardware/capture controls. Image variability limits remain recorded. |
| [Shared live model textures](model-texture-sharing.md) | Ownership/variant checks and 152 distinct tests; populated max-city load 1292.579 -> 371.803 ms. No frame-rate gain claimed. |
| [Area frame CPU reductions](area-frame-cpu-cost.md) | 48 checks, 3,960 paired counter comparisons and 12 clean validation captures; max-city host frame 8.232 -> 7.569 ms. |
| [Retained model poses](retained-model-poses.md) | 121 checks and 3,240 paired counter comparisons; combined image/validation requirements completed with retained shadow groups. |
| [Retained static shadow groups](retained-shadow-groups.md) | 122 checks, 11,880 paired counter comparisons and 32 clean validation captures; pose/group combined host frame 7.485 -> 6.714 ms, with image limits recorded. |
| [Dynamic shadow surface ranges](render-perf-shadow-surface-ranges.md) | 165 tests, 5,400 paired frames and 32 clean validation captures; matched max-city host 7.296 -> 6.865 ms. |
| [Storage descriptor reuse](render-perf-storage-descriptors.md) | 176 tests, 23,400 advancing frame pairs and 32 clean validation captures; longer matched max-city host 6.560 -> 6.099 ms. Outdoor p95 regression remains open in RP-08. |
| [Placed inventory item editing](rollnw-client-placed-inventory-item-editing.md) | Shared Item editing and nested navigation retain Area ownership, history and persistence; 166 affected regressions and 36 later icon/template regressions passed. Desktop interaction and icon appearance remain unverified. |
| [Shared Item workbench preview](rollnw-client-shared-item-preview.md) | Isolated armor and non-armor previews, appearance refresh and source/Area preservation have recorded regression and inspected rendering evidence. Full desktop interaction remains unverified; checkpoints retain detailed limits. |
| [Client release demo](rollnw-client-release-demo.md) | User accepted the already-published area-editing demo on 2026-09-30. Overview and area-editing videos are linked from the READMEs; the historical release checklist does not imply missing authoring features. |
| [Gfx resource submission and lifetime hardening](nw-gfx-resource-submission-hardening.md) | Batch uploads, truthful context/attachment contracts and bind-failure rejection; 420 affected cases verified across broad/focused runs, 100 frame-counter comparisons and five pixel-identical captures. Linux Radeon measurements retained; upload completion investigation remains active. |
| [Client main refactor](client-main-refactor.md) | Implementation, supported automated validation and user manual acceptance. |
| [Desktop validation](client-main-refactor-desktop-validation.md) | User tested the refactor and reported that all looks good; detailed coverage was not itemized. |
| [Sound slider ownership](client-sound-slider-owner.md) | Replacement-owner regression and focused normal/sanitized checks passed after repair. |
| [Browser selection highlight](client-browser-selection-rebuild.md) | Rebuild regression and focused normal/sanitized checks passed after repair. |
| [Scrollbar lifetime](client-rml-scrollbar-lifetime.md) | Independent lifetime regression and the 51-case normal/sanitized matrix passed after repair. |
| [CLI package paths](client-cli-package-paths.md) | Imports from four launch directories, service/module recreation, two desktop project opens, full CTest, and 45 affected ASan/UBSan cases passed on Linux. |
| [Area eraser and placement Output](client-area-eraser-and-placement-output.md) | Complete group/door erasure, atomic undo/redo and door lifetime, persistent placement errors, and stable palette rows passed the 2,270-test Release suite and eight focused ASan/UBSan cases on Linux. Manual interaction remains unverified. |
| [Area map regeneration](rollnw-client-area-map-regeneration.md) | Creation and save write derived maps; successful saves invalidate the exact RmlUi texture while failures preserve the CAF and warn. The 15 focused and 509 client tests passed on Linux; user visual inspection passed. |
| [Object workbench and property surfaces](rollnw-client-object-workbench-and-property-surfaces.md) | Shared live state and transactions, bounded lists, managed edits, and persistence have recorded regression evidence. Latest Encounter/Sound removal, selection feedback, and tab ordering passed focused checks; desktop interaction remains unverified. Inventory, Encounter, and spell feature follow-ups remain active and linked from the closure record. |

The [refactor checkpoint log](client-main-refactor-progress.md) retains the
sequence of changes, actual test counts and coverage limits. Earlier entries
describe the state at their checkpoint; the final acceptance entry records
completion. Skipped GPU tests remain recorded as skipped.

The [native-dialog SDK shutdown follow-up](../client-native-dialog-sdk-shutdown.md)
remains active.

The rendering reports above belong to the active
[rendering performance overhaul](../rendering-performance-overhaul.md) cluster.
Their closure does not mark the remaining gameplay performance work complete.
