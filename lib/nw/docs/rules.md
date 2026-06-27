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
