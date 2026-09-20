# Closed issues

These records preserve completed work and its verification history. They were
archived for the refactor on 2026-09-17 after the user accepted manual testing.
Later completed work records its own date and verification limits.

| Issue | Completion evidence |
| --- | --- |
| [Client main refactor](client-main-refactor.md) | Implementation, supported automated validation and user manual acceptance. |
| [Desktop validation](client-main-refactor-desktop-validation.md) | User tested the refactor and reported that all looks good; detailed coverage was not itemized. |
| [Sound slider ownership](client-sound-slider-owner.md) | Replacement-owner regression and focused normal/sanitized checks passed after repair. |
| [Browser selection highlight](client-browser-selection-rebuild.md) | Rebuild regression and focused normal/sanitized checks passed after repair. |
| [Scrollbar lifetime](client-rml-scrollbar-lifetime.md) | Independent lifetime regression and the 51-case normal/sanitized matrix passed after repair. |
| [CLI package paths](client-cli-package-paths.md) | Imports from four launch directories, service/module recreation, two desktop project opens, full CTest, and 45 affected ASan/UBSan cases passed on Linux. |
| [Area eraser and placement Output](client-area-eraser-and-placement-output.md) | Complete group/door erasure, atomic undo/redo and door lifetime, persistent placement errors, and stable palette rows passed the 2,270-test Release suite and eight focused ASan/UBSan cases on Linux. Manual interaction remains unverified. |

The [refactor checkpoint log](client-main-refactor-progress.md) retains the
sequence of changes, actual test counts and coverage limits. Earlier entries
describe the state at their checkpoint; the final acceptance entry records
completion. Skipped GPU tests remain recorded as skipped.

The [native-dialog SDK shutdown follow-up](../client-native-dialog-sdk-shutdown.md)
remains active.
