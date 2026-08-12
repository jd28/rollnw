# Persist Remaining Native Authoring Tables

Status: open for appearance and placeable data; base-item persistence is
resolved.

## Problem

Creature-appearance and placeable-appearance source rows are split into
disjoint runtime projections, but those projections do not have an authored
persistence or override format. Base items no longer have this problem:
checked-in SmallS `BaseItemDefinition` rows are the one authored source.

## Current Contract

- Base-item input: checked-in, label-named
  `lib/nw/smalls/scripts/nwn1/data/baseitems/*.smalls` definitions.
  `baseitems.2da` is a legacy offline import source only.
- Each `BaseItemDefinition` owns the sole numeric `id`, one `BaseItemInfo`
  value, and one opaque `BaseItemRules` value. Consumers do not join two
  catalogs.
- SmallS loads the definition batch once, retains it for the runtime lifetime,
  and publishes one dense positional `BaseItemInfo` batch to C++. Sparse IDs
  are represented by invalid holes; a failed publication preserves the
  previous native table.
- C++ owns `RuleTypeArray<BaseItem, BaseItemInfo>`. It does not declare,
  inspect, or copy the `BaseItemRules` schema.
- Legacy `ReqFeat0..4` columns are normalized by the offline importer into the
  semantic `requirements: array!(RequirementQualifier)` field. Sentinel
  values are not persisted.
- Appearance and placeable inputs remain the active resource manager's
  `appearance.2da` and `placeables.2da`, after module resources are
  established.
- A missing required table fails NWN1 rules initialization loudly.
- The renderer receives resolved model and visual protocol data; it does not
  read these NWN1 policy tables.

## Unresolved Work

- Define an authored row or patch format only when a real workflow requires
  changes to appearance or placeable tables.
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
