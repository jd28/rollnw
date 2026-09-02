# Persist Remaining Native Authoring Tables

Status: open for appearance, placeable, and base-item authored overrides.

## Problem

Creature appearance, placeable appearance, and base-item source rows are
materialized from active 2DAs. Their runtime projections do not have a separate
authored persistence or override format.

## Current Contract

- Base-item input: active `baseitems.2da`, transformed by the package data spec
  into `BaseItemDefinition` rows at runtime.
- Each `BaseItemDefinition` owns the sole numeric `id`, one `BaseItemInfo`
  value, and one opaque `BaseItemRules` value. Consumers do not join two
  catalogs.
- SmallS loads the definition batch once, retains it for the runtime lifetime,
  and publishes one dense positional `BaseItemInfo` batch to C++. Sparse IDs
  are represented by invalid holes; a failed publication preserves the
  previous native table.
- C++ owns `RuleTypeArray<BaseItem, BaseItemInfo>`. It does not declare,
  inspect, or copy the `BaseItemRules` schema.
- `ReqFeat0..4` columns are normalized by the shared transformer into the
  semantic `requirements: array!(RequirementQualifier)` field. Sentinel
  values are not persisted.
- Appearance and placeable inputs remain the active resource manager's
  `appearance.2da` and `placeables.2da`, after module resources are
  established.
- Bad source rows are diagnosed and retained as indexed holes. A structurally
  unusable source produces an empty typed domain without aborting the client.
- The renderer receives resolved model and visual protocol data; it does not
  read these NWN1 policy tables.

## Unresolved Work

- Define an authored row or patch format only when a real workflow requires
  changes beyond resource-manager 2DA overrides.
- Define how authored rows preserve or allocate stable numeric IDs.
- Define the explicit reload boundary for a long-lived tool session.

## Constraints

- Do not reintroduce separate base-item info/rules catalogs or a runtime join.
- Do not expose the `BaseItemRules` layout to C++.
- Preserve source row IDs unless an authored format explicitly replaces that
  contract.
- Keep wings, tails, robes, phenotypes, classes, feats, and other existing
  Smalls-owned configuration out of this issue.
- Measure import time and retained bytes before adding caching or incremental
  reload machinery. No current performance requirement justifies either.
