# Profile Packages And Bootstrap

- **Version**: 0.1.0
- **Last Updated**: 2026-08-01
- **Status**: Normative target architecture

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
- the selected profile package.

Outputs:

- one resolved selected profile package;
- registered and validated profile propset schemas;
- initialized subsystem policy modules and required profile config batches; and
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
| Propset schema | `<root>.propsets` | Always | Runtime bootstrap; missing or invalid fails startup |
| Profile initialization | `<root>.init` | Normal game/runtime startup | Runtime; tools may explicitly disable execution |
| Combat policy | `<root>.combat` | Combat subsystem enabled | Combat subsystem initialization |
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
    propsets.smalls
    init.smalls
    combat.smalls
    effects.smalls
    baseitems.smalls
    classes.smalls
    feats.smalls
    item.smalls
    creature.smalls
    ... other domain policy modules ...
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
    -> resolve shipped core and selected profile packages
    -> register VM types, intrinsics, and native core ABI
    -> load and validate <root>.propsets
    -> register PropsetSchema[] and prime pools once
    -> load modules required by enabled native subsystems
    -> initialize required profile config batches
    -> execute <root>.init when enabled
    -> permit object creation and deserialization
```

Boundary contracts:

1. Module paths are complete before the service start phase invokes runtime
   initialization.
2. Native value layouts are registered before profile source using them is
   resolved.
3. `<root>.propsets` is required and has no module-level dependency on profile
   initialization or toolset code.
4. Every propset declaration is validated as a batch before pools are used.
5. Pools are primed once after the complete schema module is valid.
6. Required subsystem modules and config batches reject startup before object
   creation if their invariants are not satisfied.

The selected profile and configuration are singletons. The schema declarations,
config entries, and native publications they produce are batches.

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
empty must be declared as such by its consumer; otherwise an empty required
table is rejected. Sparse holes and out-of-range IDs are handled by the domain
lookup API, not by unchecked array access.

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
| `<root>.propsets` absent or invalid | Fail startup before object load |
| Required subsystem policy absent | Fail that subsystem's initialization |
| `<root>.init` disabled explicitly | Skip only init execution; schemas remain required |
| Required config table absent or invalid | Fail profile/domain initialization |
| Optional tool data absent | Empty projection plus bounded diagnostic |

## Current Deviations

Implementation deviations are tracked by
[`issues/smalls-module-ownership-and-profile-boundaries.md`](../../../../issues/smalls-module-ownership-and-profile-boundaries.md).
New code follows the target contract rather than extending a deviation.

## Verification

The profile-package migration is complete when tests prove:

- one configured root derives every default module role;
- module paths are registered before runtime service initialization;
- missing or duplicate required package providers fail startup;
- `<root>.propsets` registers the complete schema batch and pools are primed
  once before the first object;
- disabling init does not disable schema registration;
- required domain config failures stop initialization;
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
