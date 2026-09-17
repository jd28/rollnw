# Pending sound slider edit loses its owner

Status: closed on 2026-09-17; repaired after the separate C13 extraction checkpoint.

The workbench change listener stores row/current/desired for a sound volume
gesture, then dispatches `object.details.set_integer` against the bridge's
current active object on release/blur. It retains no original object, tab or
module generation. Switching to another sound with the same row/current value
can therefore change that replacement and create undo history in its tab.

The production characterization at `fd46d0eb3`,
`ClientObjectWorkbench.BaselineSoundGestureCommitsAgainstTheReplacementObject`
creates two real sound objects, stages the first slider, activates the second in
another tab, and confirms the second changes while the first remains unchanged.
This behavior is intentionally preserved in the mechanical C13 move.

Repair: make the pending gesture carry object/tab/module identity and validate
that identity, ready row/editor/current value and backend active selection before
dispatch. Clear pending state on workbench clearing/replacement. Coalesce a valid
gesture into one existing backend edit/undo record. Reject non-finite/out-of-range
slider values before rounding. Require desired regression failure before repair,
then normal/sanitized same-object, replacement-object, tab, stale-row and invalid
value checks. No new edit implementation or command queue is needed.

The desired replacement-object regression failed before repair (replacement
volume 89 instead of 0, one undo entry instead of zero). Pending state now owns
object/tab/module identity. Staging and commit check current selection/ready row,
replacement/clear cancels the gesture, and invalid slider floats discard pending
state before rounding. All 11 focused workbench/sound/property cases passed in
normal and combined-sanitizer builds. Real pointer capture and a module reload
during the gesture remain desktop/integration checks; generation validation is
present and reviewed, not claimed exercised by that matrix.
