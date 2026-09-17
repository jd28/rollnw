# Closed issues

These records preserve completed work and its verification history. They were
archived on 2026-09-17 after the user accepted manual testing of the client
refactor.

| Issue | Completion evidence |
| --- | --- |
| [Client main refactor](client-main-refactor.md) | Implementation, supported automated validation and user manual acceptance. |
| [Desktop validation](client-main-refactor-desktop-validation.md) | User tested the refactor and reported that all looks good; detailed coverage was not itemized. |
| [Sound slider ownership](client-sound-slider-owner.md) | Replacement-owner regression and focused normal/sanitized checks passed after repair. |
| [Browser selection highlight](client-browser-selection-rebuild.md) | Rebuild regression and focused normal/sanitized checks passed after repair. |
| [Scrollbar lifetime](client-rml-scrollbar-lifetime.md) | Independent lifetime regression and the 51-case normal/sanitized matrix passed after repair. |

The [refactor checkpoint log](client-main-refactor-progress.md) retains the
sequence of changes, actual test counts and coverage limits. Earlier entries
describe the state at their checkpoint; the final acceptance entry records
completion. Skipped GPU tests remain recorded as skipped.

The [CLI package-path bug](../client-cli-package-paths.md) and
[native-dialog SDK shutdown follow-up](../client-native-dialog-sdk-shutdown.md)
remain active issues.
