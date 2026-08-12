# rollnw SmallS Conventions

- **Version**: 0.1.0
- **Last Updated**: 2026-08-01
- **Status**: Normative target architecture

## Purpose

This document is the entry point for deciding where SmallS and engine data,
code, and presentation belong in rollnw. It defines the stable boundaries.
Detailed storage and loading mechanics remain in
[`propset-architecture.md`](propset-architecture.md),
[`load-config.md`](load-config.md), and
[`profile-packages.md`](profile-packages.md).

Existing code that violates these rules is migration work, not precedent. The
active migration and its observed change surface are tracked in
[`issues/smalls-module-ownership-and-profile-boundaries.md`](../../../../issues/smalls-module-ownership-and-profile-boundaries.md).

## Real Platform And Constraints

The runtime has these fixed properties:

- One game profile is selected before kernel services start and remains selected
  for the service generation.
- SmallS modules are addressed by qualified logical names and resolved through
  configured package directories.
- Qualified propset names are durable serialization protocol identifiers.
- C++ owns engine object lifetime and native component invariants.
- Core SmallS owns the host ABI and reusable profile-neutral transforms.
- Profile SmallS owns game schema, interpretation, policy, and registrations
  that give reusable transforms profile-specific meaning.
- The common rules path resolves a small known set of object handles and joins
  their live state with shared indexed rules data.

The architecture is designed around those facts. It does not provide per-module
profile mixing, automatic profile guessing, or runtime package hot swapping.

## Dependency Layers

Use this fixed dependency direction:

```text
RML / RCSS
    -> toolset.<profile>.*
    -> <profile>.*
    -> core.*

The arrow means "may depend on".
```

Dependencies do not point upward. In particular:

- `core.*` imports only other `core.*` modules.
- profile code does not import toolset code.
- renderer code consumes resolved native rows and does not import profile or
  toolset policy.

`core.*` is the shipped, non-shadowable shared package. It is batteries
included: it may contain native declarations, ordinary SmallS implementations,
and reusable registries or transforms. A consumer may ignore or replace those
implementations at its own call or registration boundary; that does not require
a second package layer. Core must not contain profile propsets, profile table
interpretation, or imports from a profile.

A profile is replaced by selecting a different profile root, not by shadowing
individual modules inside the selected package. A toolset workflow is
replaceable because it owns presentation and authoring policy rather than
runtime data ownership.

## Classify Data From Its Use

Do not classify data from its name, source column, resource prefix, or an object
model. Record the actual readers, writers, frequency, lifetime, and invariants,
then apply these questions in order:

1. Does a native subsystem consume the datum directly, or must a mutation
   preserve engine ownership, spatial, renderer, inventory, or lifetime
   invariants? Use a native component and typed native operations.
2. Is it a reusable transform over core values and native operations whose
   contract does not require profile schema or constants? Put the implementation
   in `core.*`; consumers may choose whether to call or configure it.
3. Is it one fixed-shape value copied between C++ and SmallS? Use a native value
   declared in the relevant `core.*` module.
4. Is it shared, stable rules data loaded once and queried many times? Use a
   profile config batch indexed by a stable scalar key.
5. Is it live per-object state whose schema and normal policy belong to the
   selected game? Use a profile propset.
6. Is it only an authoring projection, selection, filter, label, or workflow?
   Use toolset SmallS.
7. Is it only document structure or presentation? Use RML/RCSS.

Each datum has one authoritative runtime owner. Conversion and serialization
may inspect another owner's data, but they do not become additional owners.

## Data Forms

| Form | Layout and owner | Lifetime | Normal access |
|---|---|---|---|
| Native ABI value | Fixed-layout copied value registered by C++ | Call or receiving container lifetime | Typed `core.*` function/value |
| Native component | Engine-owned row keyed by `ObjectHandle` | Live object | Invariant-preserving native operations |
| Profile config batch | Cached `array!(T)` indexed by an integer `[[index]]` | Selected runtime/profile | Domain lookup module |
| Profile propset | Pool row keyed by `ObjectHandle` and propset type | Live object | `get_propset!` |
| Toolset projection | Copied authoring rows with stable scalar keys | Active view/workspace | RmlUi data-model binding |

The forms are not interchangeable annotations:

- There is no native propset category.
- A config row is not a detached propset.
- A native value inside a config row or propset remains a copied ABI value; it
  does not transfer ownership of the containing schema to C++.
- Native component memory, propset pool memory, VM arena memory, and RmlUi
  element pointers never cross a system boundary as borrowed storage.

## Package And Module Names

`ConfigOptions::profile` is the selected profile package root. For the current
game it is `nwn1`. It is a logical namespace, not a filesystem path.

The root establishes conventional names:

```text
<root>.propsets        all persistent and transient profile propset schemas
<root>.init            normal profile initialization entry point
<root>.combat          combat policy used by the combat subsystem
<root>.effects         effect policy used by the effect subsystem
<root>.data.<table>    authored config-entry directory
<root>.<domain>        domain rules and data access
```

Domain modules are required by their consumers, not by a universal list. For
example, `nwn1.baseitems` is required by the NWN1 profile; a profile without the
NWN base-item concept is not required to invent that module.

Every production propset for one profile is declared in the single required
`<root>.propsets` module. Schema-local value types live with those propsets.
Copied native field types remain declared under `core.*`.

See [`profile-packages.md`](profile-packages.md) for resolution, bootstrap, and
failure rules.

## Cross-Boundary Batch Contract

Every system boundary uses an explicit value or homogeneous row batch:

```text
source-owned InputRow[count]
    -> validate complete input
    -> transform or copy rows
    -> commit complete OutputRow[count]
    -> receiver owns accepted output
```

Contract:

- Input layout is a typed value or contiguous homogeneous batch.
- The caller owns input storage for the call duration.
- The receiver copies accepted output and owns it for its documented lifetime.
- Stable scalar IDs or `ObjectHandle` values identify rows; array positions are
  not durable identity unless the protocol explicitly defines them as IDs.
- Counts, handles, ranges, duplicate destinations, and stale preconditions are
  validated before mutation.
- Invalid input rejects the complete operation unless the protocol explicitly
  documents row dropping as an import policy.
- A singular operation is the same path with `count = 1`. The selected profile
  and configuration are true singletons and are initialized once.

No performance result is implied by this boundary. If a change claims a latency
or throughput improvement, measure its representative input before and after.

## Configuration And Live State

`load_config!` produces shared rules data. Its common path is one load per
`(normalized path, entry type)` followed by indexed reads from the cached array.
Required tables are initialized during profile startup so missing data fails
before normal rules execution. Optional tooling tables may load lazily only when
their API makes the failure explicit.

Propsets contain live object state. The engine creates or finds the row from an
object handle; scripts do not construct free-standing propset values. If config
data supplies defaults for object state, load an ordinary value batch and apply
an explicit initialization transform into the destination objects' propsets.

## Native Components And Side Effects

Scripts access native component data only through typed `core.*` operations.
Operations protect the owning subsystem's invariants. Examples include:

- position changes that update spatial state;
- inventory/equipment changes that preserve ownership and occupancy;
- visual row replacement that preserves renderer invalidation; and
- native property or ability-loadout batches with bounded layouts.

Profile scripts decide meaning and policy, then send resolved copied rows or
commands to the native owner. Runtime C++ does not read propset fields to make
profile-policy decisions.

Core may also supply the shared scripted transform around those operations. For
example, `core.item.process_item_properties` consumes an item-property array,
uses caller-populated property-row and effect-row registries, and applies the
valid results. `nwn1.item` owns the NWN1 constructors and default registrations;
a different consumer may register different mappings or not use the processor.

Native functions use ordinary domain names such as `item_properties` and
`apply_item_effect`. A leading `__` is reserved for compiler/runtime intrinsics
and generated implementation details; it is not a blanket native-function
prefix. Renaming a native declaration and its registration happens together,
without a compatibility alias unless an observed external consumer requires
one.

## Persistence And Migration

Qualified names are protocol identifiers, not implementation details:

```text
nwn1.propsets.ItemStats
nwn1.propsets.CreatureStats
components.item_visuals
```

Changing a profile root, propset module, propset type, native component section,
or field representation is a persistence migration. Move the schema, GFF
conversion policy, JSON import/export, fixtures, tests, and authored corpus in
one domain slice.

Migration policy:

- no aliases or forwarding propsets;
- no old-name fallback lookup;
- no profile guessing from serialized section names;
- no two authoritative representations; and
- reject ambiguous archives that provide old and new representations together.

Reimport is the migration path for authored legacy data.

## Error Policy

| Boundary | Invalid input behavior |
|---|---|
| Profile root or required package | Fail configuration/startup |
| Required module missing or has diagnostics | Fail the owning subsystem's initialization |
| Invalid propset schema | Fail profile initialization before object load |
| Required config batch violates its declared invariant | Owning profile initializer rejects it |
| Sparse or out-of-range config ID | Domain accessor returns its documented invalid value |
| Invalid or stale object handle | Reject the complete operation |
| Invalid native mutation batch | Reject without partial writes |
| Obsolete durable section | Reject and require reimport |
| Optional tool projection unavailable | Publish an empty view plus a bounded diagnostic |

Error behavior belongs at the boundary that understands the data. Do not add a
`maybe` check to every downstream read when startup can reject invalid required
state once.

## Conformance Checklist

A change conforms when:

- every datum has one named owner and lifetime;
- imports follow `core -> profile -> toolset`;
- native declarations live under `core.*` and native state is reached through
  invariant-preserving operations;
- profile propsets live in `<root>.propsets`;
- config data and live object state remain separate;
- cross-boundary values are copied and batch contracts reject partial mutation;
- required profile data is validated during initialization;
- durable qualified names and conversion tests change together; and
- no compatibility layer, extension point, or second representation was added
  without observed data that requires it.

## Cost

The intended runtime shape is one fixed profile-package bootstrap, one propset
schema load, one pool-prime pass, and cached config batches. This is a structural
contract, not a measured performance claim. Keeping reusable implementations in
`core.*` avoids another installed package and its search/import/packaging states.
The primary cost is migration: profile-specific propsets and policy must move
without creating upward imports, and durable data must be reimported when
qualified names change.
