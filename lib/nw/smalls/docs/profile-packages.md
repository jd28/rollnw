# Profile Packages And Bootstrap

- **Version**: 0.1.0
- **Last Updated**: 2026-09-02
- **Status**: Normative architecture

## Purpose

This document defines how one configured profile root selects a SmallS package,
how conventional module names are derived, and what must be ready before object
creation or deserialization. Ownership rules are defined in
[`conventions.md`](conventions.md).

## Inputs And Outputs

Inputs:

- one logical profile root from `ConfigOptions::profile`;
- an ordered set of filesystem module search paths supplied before runtime
  initialization;
- the shipped `core` package; and
- the selected profile package, including `resources.json` and sorted
  `data_specs/*.json` declarations.

Outputs:

- one resolved selected profile package;
- one validated resource-container batch expanded from `resources.json`;
- registered and validated profile propset schemas;
- validated mandatory profile hooks, initialized native/config domains, and
  optional gameplay registrations; and
- a runtime ready to create or deserialize objects.

The selected profile is a true singleton for one kernel service generation.
Batch semantics apply inside its schema, config, and native data transforms, but
there is no useful second profile to initialize in the same runtime.

## Root Versus Filesystem Path

The configured root is a logical module prefix:

```text
profile = "nwn1"
```

It is not `stdlib/nwn1`, a source-checkout path, or an install directory. Module
paths tell the runtime where to search. A package directory containing
`package.json` establishes its directory name as the top-level namespace. If
`<package path>` ends in `nwn1`, then:

```text
<package path>/package.json
<package path>/propsets.smalls   -> nwn1.propsets
<package path>/baseitems.smalls  -> nwn1.baseitems
<package path>/data/classes/*    -> nwn1.data.classes
```

The actual runtime currently registers package directories such as
`stdlib/core` and `stdlib/nwn1` directly. Development tools may discover those
directories from a workspace; installed tools receive them from packaging.

A profile root must match `[a-z_][a-z0-9_]*`, which is the lowercase subset of
one SmallS module-path identifier, and therefore cannot contain a dot or
filesystem separator. An invalid root is rejected during configuration. A
required module that resolves from more than one provider is a configuration
error; search order must not silently choose a profile ABI or persistence
schema.

## Conventional Module Roles

Consumers derive names by appending a fixed role to the selected root. Full
module names are not repeated in normal configuration.

| Role | Derived name | Required when | Consumer and failure point |
|---|---|---|---|
| Resource declaration | `<package>/resources.json` | Always | Resource preflight; missing or invalid fails startup |
| Propset schema | `<root>.propsets` | Always | Runtime bootstrap; missing or invalid fails startup |
| Profile hooks | `<root>.profile` | Game mode | Runtime bootstrap; a missing hook or invalid signature fails startup |
| Profile initialization | `<root>.init` | Normal game/runtime startup | Runtime; tools may explicitly disable execution |
| Combat policy | `<root>.combat` | Game mode | Runtime bootstrap; missing exports or an invalid result layout fails startup |
| Effect policy | `<root>.effects` | Effect subsystem enabled | Effect subsystem initialization |
| Config table | `<root>.data.<table>` | Owning domain requires the table | Domain/profile initializer |
| Domain policy | `<root>.<domain>` | Owning profile or subsystem requires it | That consumer's initialization |

`nwn1.baseitems` is therefore a required NWN1 domain module because the NWN1
profile consumes base-item definitions. It is not a universal requirement for a
profile whose data has no base-item concept.

Explicit full-name overrides are test or tool seams. Their default is empty,
meaning derive the conventional name from the root. An override does not change
the selected profile or permit modules from multiple profile roots to be mixed
in one runtime.

## Profile Package Layout

The NWN1 target layout is:

```text
nwn1/
    package.json
    resources.json
    propsets.smalls
    profile.smalls
    init.smalls
    combat.smalls
    effects.smalls
    baseitems.smalls
    classes.smalls
    feats.smalls
    item.smalls
    creature.smalls
    ... other domain policy modules ...
    data_specs/
        appearances.json
        ... one source-domain declaration per file ...
    data/
        baseitems/
            longsword.smalls
            ...
        classes/
            fighter.smalls
            ...
```

Conventions define roles, not a closed module inventory. A domain module exists
when an observed consumer requires its data or policy.

All persistent and transient propset declarations for the selected profile live
in `propsets.smalls`. The current NWN1 schema has 16 production propsets and all
are required for normal object import/export, so a per-domain schema-module list
adds states without avoiding work. One module also gives durable propset names a
single predictable prefix.

Schema helper values that exist only to define profile state live in
`<root>.propsets`. Fixed-layout copied C++ values remain declared in `core.*`
and may be imported as propset fields when the propset field contract permits
them. `core.*` never imports `<root>.propsets`.

## Bootstrap Transform

The startup transform is ordered and fails before ordinary object work:

```text
configured module search paths + selected root
    -> resolve exactly one selected package directory
    -> validate resources.json and expand the enabled container batch
    -> initialize the resource search path
    -> register VM types, intrinsics, and native core ABI
    -> load and validate <root>.propsets
    -> register PropsetSchema[] and prime pools once
    -> register sorted data_specs/*.json independently
    -> load and validate mandatory <root>.profile hooks
    -> load and validate mandatory <root>.combat policy
    -> <root>.profile::init initializes every native/config domain
    -> execute <root>.init when enabled
    -> permit object creation and deserialization
```

Boundary contracts:

1. Module paths are complete before the service start phase invokes runtime
   initialization.
2. `resources.json` is validated completely before any declared container is
   applied. Missing declared containers are optional and skipped.
3. Native value layouts are registered before profile source using them is
   resolved.
4. `<root>.propsets` is required and has no module-level dependency on profile
   initialization or toolset code.
5. Every propset declaration is validated as a batch before pools are used.
6. Pools are primed once after the complete schema module is valid.
7. `<root>.profile` must provide `init`, `object_instantiated`, and
   `match_qualifier` with their exact signatures. Compiled-function references
   live only for the Runtime service generation.
8. Each data-spec file is registered independently. A malformed file disables
   only its domain; profile initialization attempts every remaining domain.
9. `<root>.combat` must provide
   `resolve_attack(Creature, object)` and
   `resolve_attack_cooldown_ticks(Creature, int) -> int`. The first result is a
   value struct containing the integer fields `attack_type`, `attack_result`,
   `attack_roll`, `attack_bonus`, `armor_class`, `nth_attack`, `damage_total`,
   `critical_multiplier`, `critical_threat`, `concealment`, and
   `iteration_penalty`; the boolean fields `is_ranged` and
   `target_is_creature`; and the array fields `effects_to_apply` and
   `effects_to_remove`. The native scheduler caches the validated module,
   functions, and field offsets for the service generation.

The selected profile and configuration are singletons. The schema declarations,
config entries, and native publications they produce are batches. Changing a
combat-policy override requires a new service generation; requests reject a
configuration that no longer matches the cached policy.

## Propset Schema Contract

Input:

- one resolved `<root>.propsets` module;
- its complete batch of `[[propset(ObjectType)]]` declarations; and
- native field types already registered under `core.*`.

Output:

- one registered schema row per qualified propset type;
- object-type-to-propset indexes; and
- empty, ready propset pools owned by the runtime.

Lifetime and identity:

- schema metadata and pools live for the runtime service generation;
- pool rows are keyed by propset type and `ObjectHandle`;
- the exact qualified type name is the durable serialization key; and
- transient propsets share the namespace but are omitted from durable output.

Failure behavior:

- missing module, parse/type diagnostics, duplicate qualified names, unsupported
  object targets, or invalid field layouts fail profile initialization;
- no `core.*` schema fallback is loaded; and
- no partially registered profile is allowed to create objects.

## Config Batch Contract

Config paths are derived from the root but remain separate from module roles:

```text
<root>.data.<table>
    -> sorted direct entry resources
    -> validate typed rows and integer [[index]] values
    -> cached array!(EntryType)
    -> domain lookup/policy module
```

The runtime owns the cached array for the service generation. Rows are ordinary
values; they may contain registered native values but are not propsets and do
not contain propset instances. If config rows seed object state, a profile batch
transform applies ordinary seed rows to destination object handles.

Required tables load during profile initialization. A table that is validly
empty must be declared as such by its consumer. Structural source failure
publishes an empty domain and reports degraded initialization without
discarding successful siblings. Sparse holes and out-of-range IDs are handled
by the domain lookup API, not by unchecked array access.

## Tool And Packaging Contract

A process that starts the selected profile runtime must have the selected
profile package available before services start. This includes an LSP binary
that constructs full kernel services rather than a language-only runtime.

Packaging therefore copies complete package directories, including
`package.json`, instead of selecting source files by current import reachability.
The packaged package is the same data shape used by the game/runtime; packaging
must not silently omit a required convention module.

A language-only tool that does not initialize a game profile may load `core`
without a profile package. That is a different entry point and must be explicit;
failure to locate a selected profile is not downgraded merely because the
process is called a tool. Such a process starts kernel services with
`ServiceMode::language`, which constructs the string table, resource manager,
and SmallS runtime, initializes only the runtime, and performs no profile
schema, rules, resource, or object bootstrap.

## Failure Matrix

| Failure | Result |
|---|---|
| Empty or invalid profile root | Reject configuration |
| Required package directory absent | Fail startup |
| Duplicate provider for required ABI/schema module | Fail startup |
| `resources.json` absent, malformed, or unsafe | Fail startup before applying containers |
| Declared install/user container absent | Skip the optional container |
| `<root>.propsets` absent or invalid | Fail startup before object load |
| `<root>.profile` absent or missing a required hook | Fail startup before rules/object work |
| `<root>.combat` absent or its required contract is invalid | Fail startup before rules/object work |
| Required subsystem policy absent | Fail that subsystem's initialization |
| `<root>.init` disabled explicitly | Skip only init execution; schemas remain required |
| Required config table absent or invalid | Publish an empty owning domain, report degraded initialization, and preserve siblings |
| Optional tool data absent | Empty projection plus bounded diagnostic |

## Focused Follow-up Work

The package/bootstrap ownership migration is complete. Remaining presentation
cleanup belongs to
[`issues/rollnw-client-object-workbench-and-property-surfaces.md`](../../../../issues/rollnw-client-object-workbench-and-property-surfaces.md),
not to the closed ownership audit.

## Verification

The profile-package migration is complete when tests prove:

- one configured root derives every default module role;
- module paths are registered before runtime service initialization;
- resource declarations are validated and applied in exact order before the
  Runtime reads active game data;
- missing or duplicate required package providers fail startup;
- `<root>.propsets` registers the complete schema batch and pools are primed
  once before the first object;
- missing `match_qualifier` rejects game startup before requirement evaluation;
- missing or structurally invalid combat policy rejects game startup, and a
  structurally valid package-specific result type drives native scheduling;
- disabling init does not disable schema registration;
- row-local domain config failures preserve valid indexed entries, while a
  structural source failure publishes an empty domain without stopping
  unrelated profile initialization;
- packaged game, client, and LSP distributions contain complete required
  packages; and
- serialized propset names, GFF conversion policy, fixtures, and tests all use
  `<root>.propsets.*` with no fallback names.

## Cost

Module-name derivation and schema validation occur at startup. No performance
change is claimed. The migration cost is repository-wide because changing the
propset module changes durable keys and because moving schemas out of `core.*`
also requires moving scripted consumers to preserve the dependency direction.
The existing issue records the observed change surface and verification work.
