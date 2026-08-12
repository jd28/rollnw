# Rules

This page preserves the useful context for the older C++ rules and modifier
system. New rules authoring work is moving toward [Smalls](../smalls/docs/index.md)
and [propsets](../smalls/docs/propset-architecture.md) so rule data can be
authored, tested, and embedded without growing hardcoded C++ policy.

## Original Goals

- Rules should be overridable, expandable, or removable through configuration
  or programmatic registration.
- Rules should be queryable for a concrete situation, such as the modifiers
  affecting one creature attacking one target with one weapon.
- Rule constants should avoid being permanently tied to 2da row numbers when a
  ruleset-specific mapping is the real policy.

## Useful Definitions

**Attribute**: A feature inherent to an object, such as creature ability scores.

**Profile**: A ruleset-specific layer that decouples rule values from the core
rules service.

**Type**: A typed rule value, such as an armor class category. The rule system
defines the type and invalid value; profiles define valid values.

**Flag**: A compact flag built from a rule type.

## Imported Table Boundary

An active profile may project one source table into native `Info` rows and
Smalls `Rules` rows. The source row index is the join key. The projections are
disjoint: a source column has one runtime owner, never a native mirror and a
Smalls mirror.

`Info` contains data required by native engine storage, tool indexing, or asset
addressing. `Rules` contains profile behavior and numeric tuning consumed by
Smalls. The distinction is based on the consumer, not on whether a source
column happens to look visual or mechanical.

The current NWN1 partitions are:

| Source | Native `Info` | Smalls `Rules` |
| --- | --- | --- |
| `baseitems.2da` | identity, model/icon lookup, item-property column, inventory geometry, equip mask, stacking/container shape | cost, requirements, armor/damage, weapon behavior, feat mappings |
| `appearance.2da` | identity, model lookup, model assembly type | size/movement and attachment/equipment visual policy |
| `placeables.2da` | identity and model lookup | lighting and static profile policy |

At profile initialization, C++ performs one linear import into contiguous
native arrays. Each semantic Smalls module performs one `load_config!` import
into a contiguous value array on first use. Both projections live until the
next rules/runtime reload. A missing required table fails profile
initialization; an invalid indexed lookup returns a row with `id == -1`.

Native getters expose compact read-only protocol projections to Smalls. They do
not expose complete C++ structs and they do not make policy decisions. Smalls
joins `Info` and `Rules` by row ID, resolves behavior, and writes explicit
object/component rows for native systems to consume.

This removes three unnecessary paths:

- no combined table-row model shared by both runtimes
- no generated per-row Smalls modules for runtime table data
- no renderer or tool code reading profile table columns directly

Persistence, authored overrides, and long-lived reload behavior remain separate
work in [`issues/persist-native-authoring-tables.md`](../../../issues/persist-native-authoring-tables.md).

## Modifiers

The older modifier system is built on integer, floating-point, string, and
function inputs stored in the global rules service. Modifier resolution calls a
registered callback or uses helpers such as sum/max resolution for common cases.

```cpp
auto mod = nw::make_modifier(
    mod_type_hitpoints,
    20,
    "dnd-3.0-epic-toughness",
    nw::ModifierSource::feat,
    { nw::qualifier_feat(nwn1::feat_epic_toughness_1) });

nw::kernel::rules().modifiers.add(mod);
```

This remains useful compatibility context, especially for existing NWN profile
behavior. It should not be treated as the only rules direction for new authored
game systems.
