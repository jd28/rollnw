# Client spell presets, Clear, and Summary actions

Status: open requirements. Extracted 2026-09-26 from the completed
[object-workbench milestone](closed/rollnw-client-object-workbench-and-property-surfaces.md).

## Existing data and behavior

The current Spells view edits live known spells and exact memorized uses through
the same SmallS policy and shared transaction protocol as terminal commands.
Existing regressions cover real Wizard spell projections, class-aware levels
and filters, invalid object handles, and exact undo/redo of memorized tier/slot
rows including zero flags.

The old Qt screen also exposed Clear, Load, Save, and Summary. No preset artifact
format, scope across multiple classes/metamagic variants, or Clear/Summary
semantics has been selected. The presence of those old actions does not define
their inputs or required outputs.

## Decisions required before implementation

- Observe actual workflows and representative known/memorized spell arrays,
  including class, level, metamagic, slot, use, and flag values. Counts and action
  frequency are currently unmeasured.
- Define whether Clear affects the visible filter, a selected class/level, or
  another explicit scope, and how known and memorized state relate.
- Define a concrete preset artifact, its scope, versioning, and class/profile
  compatibility. Specify merge versus replacement, path/conflict behavior, and
  rejection of malformed or incompatible inputs.
- Define the Summary output and its consumers before adding a presentation.

Implement only actions justified by those requirements. If a workflow does not
need an old action, record that decision rather than porting it speculatively.

## Completion criteria

- Record the chosen inputs, outputs, scope, valid ranges, and explicit error
  behavior for each required action; record any action deliberately declined.
- Reuse the existing SmallS spell policy and shared transactions for mutations,
  preserving exact known/memorized state through undo/redo. Reject invalid or
  stale input before changing live spell state.
- For chosen preset actions, round-trip representative multi-class/metamagic
  inputs and verify malformed/incompatible artifacts fail without partial edits.
- Verify Clear scope and Summary output against the chosen real workflows,
  including empty states, and record UI and save/reload coverage as applicable.
- State measured costs for any imposed responsiveness or memory requirement;
  no performance result or generic preset framework is assumed here.
