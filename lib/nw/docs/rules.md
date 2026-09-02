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

A BioWare 2DA is a legacy source container, not an ownership unit. One table
may contain native engine/resource facts and SmallS gameplay inputs. Ownership
is assigned per consumed field from its actual readers and invariants; the name
of the table or column does not decide the owner.

The imported-data boundary is fixed:

- C++ canonically stores raw resource facts consumed by the engine, renderer,
  resource resolution, native persistence machinery, or native editor.
- SmallS exclusively owns gameplay legality, interpretation, formulas, and
  composition. A source field read only by gameplay policy is a SmallS
  gameplay input.
- A field has exactly one semantic owner. If both runtimes need a raw fact,
  C++ owns it and exposes a narrow copied lookup or batch. A cold native tool
  may call a narrow SmallS profile rule directly; hot native consumers receive
  an already resolved flat native row.
- Unused source fields are explicitly ignored. They do not enter either
  runtime speculatively.

The package-local data spec is the decomposition authority. Runtime and
`smalls-datagen` apply the same shared batch transform to their selected
resolved source. The offline sink can emit one local, uncommitted review
snapshot per valid source row; the runtime sink materializes the active batch
directly. A mixed
definition has an `info` group using a C++-registered native value type and a
`rules` group using a profile SmallS value type. The source row index remains
their stable parent key; sparse rows are not compacted.

The NWN1 SmallS initialization module is the import adapter: it loads the
combined definitions once, publishes the complete `Info[]` batch to canonical
contiguous C++ storage, and retains `Rules[]` for gameplay. Runtime C++ and
SmallS must not maintain independent converter lists for the same source.

The accepted NWN1 field partitions begin with:

| Source | Native facts | SmallS gameplay inputs |
| --- | --- | --- |
| `appearance.2da` | identity, labels, model/assembly, attachment scales, weapon scale, arm availability, personal space | size category, base movement category |
| `wingmodel.2da`, `tailmodel.2da` | identity, label, model | none |
| `placeables.2da` | identity, label, model, lighting/static scene facts | none for current consumers |
| `baseitems.2da` | identity, model/icon and inventory-layout facts; remaining overlap fields require a reader/invariant audit | cost, requirements, armor/damage, weapon behavior, feat mappings |

Materialized definitions preserve the source row extent. A malformed row-local
value emits a diagnostic and leaves that indexed slot empty with an index of
`-1`, while valid rows remain in the batch. A missing required source or column
is a structural failure and produces a typed empty config array so the rest of
the profile and module can continue loading. Native publication validates and
atomically replaces the resulting indexed `Info[]` array, including an empty
array that marks only that domain unavailable. Native profile arrays
and SmallS gameplay arrays live for the selected profile lifetime and use
explicit invalid-ID behavior at lookup boundaries.

The nine NWN1 base movement rates are profile rules, not imported configuration.
`nwn1.creature_speeds.get_base_moverate` contains the complete mapping and
returns `0.0` outside its `0..8` input domain. During creature initialization
and appearance changes, `nwn1.creature.update_movement_rate` resolves the
creature and appearance policy once and pushes the resulting float into the
native spatial row. Native preview reads that row without calling into SmallS.

Creature body parts use the same ownership rule but a variable-length native
resource protocol rather than a fixed NWN1 slot array. An assembly publishes
only its supported part rows and each part's available model choices. Stable
part and option IDs, anchors, labels, mirror relationships, and model
associations are native facts. NWN1 naming and `0`/`255` sentinel conversion
stay inside the NWN1 adapter. Profile SmallS owns persisted selection schema and
gameplay legality. A future model may expose fewer, different, or zero parts
without padding to NWN1's shape or teaching generic code to infer model names.

The implemented private data-spec contract is documented in
[`smalls/docs/load-config.md`](../smalls/docs/load-config.md#structured-data-specs).

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
