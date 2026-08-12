# load_config

- **Version**: 0.2.0
- **Last Updated**: 2026-07-31
- **Status**: Normative data-loading contract

`load_config!` is the current path for exposing typed RPG ruleset data to
Smalls. A table may come from Smalls struct literals or from a registered
runtime converter; rules modules load the resulting arrays through one
intrinsic.

The ownership and package rules in [`conventions.md`](conventions.md) and
[`profile-packages.md`](profile-packages.md) apply throughout this document.

This is the "manifest RPGs" direction in practical form:

```text
ruleset schema
  -> typed Smalls structs with explicit fields and indexes
ruleset data
  -> `.smalls` struct literals or a registered runtime source
rules code
  -> load_config!(EntryType)("ruleset.data.table")
runtime
  -> cached array!(EntryType), indexed by the entry's [[index]] field
```

The common case is a rules module loading one table once, then serving small
lookup helpers from that cached array.

## Entry Schema

Config arrays are built from ordinary structs. The struct needs an integer field
marked with `[[index]]`; that field determines where the entry lands in the
returned array. For hot rules lookups, use `[[value_type]]` so entries are stored
inline in `array!(T)`.

A config row may contain registered native value types when they are exact
copied values used across the C++/SmallS ABI. A config row is not a propset and
must not contain a propset instance. Propsets require an object handle and
object-indexed pool storage; `load_config!` returns detached shared values.

```smalls
type StrRef(int);

[[value_type]]
type ClassEntry {
    [[index]]
    id:             int;
    name:           StrRef;
    hit_die:        int;
    saves_table:    int;
    attack_table:   int;
    spellcaster:    bool;
    caster_ability: int;
};
```

The `[[index]]` field is policy, not the filename. Filenames are just stable
entry names for authors and diffs.

## Entry Files

Each config entry is one Smalls file containing a single struct literal. Current
NWN1 class data lives under `lib/nw/smalls/scripts/nwn1/data/classes/`:

```smalls
ClassEntry {
    id = 4,
    name = StrRef(8),
    hit_die = 10,
    saves_table = 5,
    attack_table = 0,
    spellcaster = false,
    caster_ability = -1
}
```

The table path maps to the directory:

```text
nwn1.data.classes -> nwn1/data/classes/
nwn1/data/classes -> nwn1/data/classes/
```

Both dot and slash forms are accepted. Only direct children of the table
directory are loaded.

## Loading From Script

The intrinsic is declared in `core.prelude`:

```smalls
[[intrinsic("load_config")]] fn load_config(path: string): array!($T);
```

Call it with an explicit entry type. Required modules expose one initialization
function for the profile bootstrap, then hide the raw table behind lookup
helpers with explicit out-of-range behavior:

```smalls
from nwn1.rules import { ClassEntry };
from core.types import { Class, Ability };
import core.array as Array;

var _classes: array!(ClassEntry);

fn init(): bool {
    _classes = load_config!(ClassEntry)("nwn1.data.classes");
    return Array.len(_classes) > 0;
}

fn exists(class_id: Class): bool {
    var index = class_id as int;
    if (index < 0 || index >= Array.len(_classes)) {
        return false;
    }
    return Array.get(_classes, index).id == index;
}

fn caster_ability(class_id: Class): Ability {
    if (!exists(class_id)) {
        return Ability(-1);
    }
    var entry = Array.get(_classes, class_id as int);
    if (!entry.spellcaster) {
        return Ability(-1);
    }
    return Ability(entry.caster_ability);
}
```

The selected profile calls `init` once and rejects `false` before normal rules
execution. `load_config!` loads data; the rules module owns the required-table
invariant and invalid-ID policy. Accessors do not carry a repeated loaded-state
branch.

## Initialization Policy

Required profile tables load during the selected profile's initialization
phase. The initializer validates the complete required table batch before normal
rules or object work begins. Lookup functions then perform only their domain ID
and row-validity checks; they do not repeatedly decide whether required startup
work happened.

Lazy `_ensure_loaded` wrappers are reserved for optional tool data or a module
that can be used outside normal profile startup. Their initialization function
returns an explicit success value, and callers cannot silently continue with a
partially initialized required table.

An empty table is not universally an error. The owning domain declares whether
zero rows are valid. A required domain with a non-empty invariant rejects the
empty result during initialization.

## Runtime Contract

`load_config!(T)(path)`:

- normalizes dot and slash paths before lookup
- requires `T` to be a struct with an `int` `[[index]]` field
- rejects `T` when it is a propset or contains a propset field
- for a `twoda_only` converter, imports every row from the active 2DA directly
- otherwise, loads and sorts direct `.smalls` entries below the table path
- sizes a 2DA-backed array to the source row count
- sizes an entry-file array to the highest non-negative index plus one
- caches the returned array by normalized path and entry type
- roots the array so garbage collection does not reclaim config data

Out-of-range and bad data behavior is explicit:

- missing table path logs a warning and returns an empty array
- missing registered 2DA logs a warning and returns an empty array
- invalid entry type, propset-containing entry type, missing `[[index]]`, or
  non-`int` index fails the intrinsic
- entries that fail to load, have non-`int` indexes, or have negative indexes
  are skipped
- duplicate indexes are last-wins after filename sorting

Because sparse IDs produce array holes, rules modules should bounds-check before
reading and should define the fallback value at the API boundary.

## 2DA Bridge

Some paths can be backed by a registered 2DA converter instead of `.smalls`
entry files. The script API is the same:

```smalls
var feats = load_config!(FeatEntry)("nwn1.data.feats");
```

This keeps old NWN data sources usable while the rules-facing code moves toward
typed Smalls manifests. A future authored RPG can use native `.smalls` entries
directly without changing the lookup code.

## Relationship To Propsets

`load_config!` is for Smalls-owned stable ruleset data: classes, races, feats,
spell rows, progression tables, and similar data that is loaded once and
queried many times.

NWN1 base items demonstrate a split shared-data boundary. Checked-in
`BaseItemDefinition` struct literals are the canonical row source.
`load_config!` loads one indexed definition batch. Profile SmallS retains the
`BaseItemRules` policy rows and publishes a copied `BaseItemInfo` fact batch to
native `core.item` consumers that require layout, model, icon, and inventory
geometry. The two projections join by the same stable base-item ID; neither is a
second authoritative source.

Legacy 2DAs remain valid converter inputs for tables still using registered
runtime converters. For canonical base-item definitions, 2DA conversion is an
offline import path rather than a second runtime source.

Propsets are for per-object game state: hit points, ability scores, combat
state, class levels, inventory-facing state, and other data attached to a live
object. A combat rule commonly joins both:

```text
class/race/base-item rules from load_config! plus native base-item facts
  + live creature/item propsets
  -> rule result
```

Keeping these separate prevents static rules manifests from becoming object
storage, and prevents live object state from becoming global rules data.
