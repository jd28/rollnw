# load_config

- **Version**: 0.2.0
- **Last Updated**: 2026-09-02
- **Status**: Normative data-loading contract

`load_config!` is the current path for exposing typed RPG ruleset data to
Smalls. A table may come from authored Smalls struct literals or from a
registered package data spec; rules modules load the resulting arrays through
one intrinsic.

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

An authored config entry is one Smalls file containing a single struct literal:

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

For authored packages, the table path maps to the directory:

```text
nwn1.data.classes -> nwn1/data/classes/
nwn1/data/classes -> nwn1/data/classes/
```

Repository sources and data specs use the dotted form. The runtime accepts the
slash form only at its external compatibility boundary and normalizes it to the
same key. Only direct children of the table directory are loaded. NWN1's
production paths are spec-backed and do not read entry files; this entry-file
path remains available for authored configuration in other packages.

## Generated Snapshot Contract

Rows generated from an installed game's resolved resources are optional local
review artifacts. They are not runtime inputs and are not committed. NWN1
materializes every production `load_config!` path directly from the active
resource set through its package data specs.

Generate local snapshots under an ignored build tree with:

```console
./build/tools/smalls-datagen/smalls-datagen --nwn <nwn-dir> --out build/generated-smalls --entity appearance
```

Reproduce and compare that local snapshot through a temporary tree without
overwriting it with:

```console
./build/tools/smalls-datagen/smalls-datagen --nwn <nwn-dir> --check build/generated-smalls --entity appearance
```

When a spec enables snapshots, each filename comes from its declared label,
StrRef, or referenced-resource name. Datagen sanitizes it to lowercase ASCII,
uses underscores for spaces and hyphens, and removes other punctuation. The
numeric source row remains inside the definition as `id`; it is never encoded
in the filename. Empty names reject generation. Collisions receive an ordinal
suffix (`name_2`, `name_3`) so filenames remain independent of row identity.
Specs without a meaningful stable name still materialize for verification but
do not emit snapshots.

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
- gives a registered structured data spec authority over filesystem rows for
  that path
- structurally validates a spec against the active resolved resource batch,
  materializes valid rows, and leaves rejected source IDs as indexed holes
- otherwise, loads and sorts direct `.smalls` entries below the table path
- sizes a 2DA-backed array to the source row count
- sizes an entry-file array to the highest non-negative index plus one
- caches the returned array by normalized path and entry type
- roots the array so garbage collection does not reclaim config data

Out-of-range and bad data behavior is explicit:

- missing table path logs a warning and returns an empty array
- missing registered 2DA logs a warning and returns an empty array
- a structured data spec logs row-local diagnostics, drops only those rows,
  and retains the source row extent so valid rows remain available at their
  original IDs; every hole has `[[index]] == -1`, including source row zero
- a missing source or required column returns a cached typed empty array; it
  does not fail the calling script or profile startup
- invalid entry type, propset-containing entry type, missing `[[index]]`, or
  non-`int` index fails the intrinsic
- entries that fail to load, have non-`int` indexes, or have negative indexes
  are skipped
- duplicate indexes are last-wins after filename sorting

Because sparse IDs produce array holes, rules modules should bounds-check before
reading and should define the fallback value at the API boundary.

## Structured Data Specs

The 17 production NWN1 paths are described by JSON files in the package's
`data_specs/` directory. These are installed package resources: runtime,
datagen, and the language server parse the same files. They are not embedded in
C++ or copied into generated headers. A missing, ambiguous, or invalid required
package spec aborts NWN1 profile startup because that is a program/package
error, not bad game data.

The private format implements the transforms observed in those tables:
`row_index`, `constant`, `column`, `enum`, `reference_index`, `fixed_array`,
`indirect_grid`, `column_array`, and `struct_array`. It supports row `reject`,
typed `default`, and `omit_row` policies. It has no format version, published
schema, plugin interface, virtual source/sink framework, or general dependency
graph.

Runtime and `smalls-datagen` accept this one package format. The former
datagen-only `@...` transform strings and their separate JSON specs have been
removed, so offline snapshots cannot silently use a different transform.

The active primary 2DA defines the output row extent. If an entire requested
column is absent from an old active table, the transformer reads that column
from the installed base-game table. It does not fall back for an individual
missing cell: the field's declared default or rejection policy applies. The
same whole-column rule applies to referenced 2DAs.

Boolean columns read zero as false and any nonzero value as true. Values outside
`[0, 1]` are warned about unless the spec identifies the column as a legitimate
non-boolean integer encoding. A malformed row is diagnosed and left as an
indexed hole; it does not discard valid rows or prevent the client/module from
loading. A structurally unusable source produces a cached typed empty array.

## 2DA Bridge

NWN1's production config paths are backed by package data specs over resolved
2DAs. The script API remains the same:

```smalls
var feats = load_config!(FeatEntry)("nwn1.data.feats");
```

This keeps module/hak precedence in the resource manager while one shared
transform defines the SmallS shape. A future authored RPG can use native
`.smalls` entries directly without changing the lookup code.

## Relationship To Propsets

`load_config!` is for Smalls-owned stable ruleset data: classes, races, feats,
spell rows, progression tables, and similar data that is loaded once and
queried many times.

NWN1 base items demonstrate the split shared-data import boundary. The shared
`baseitems.2da` spec materializes `BaseItemDefinition`, combining a
C++-registered `BaseItemInfo` value and a SmallS `BaseItemRules` value beneath
one stable source ID. The NWN1 initialization module publishes the complete
positional `BaseItemInfo[]` batch into canonical C++ storage and retains
`BaseItemRules[]` for gameplay.

SmallS is the one-time import adapter, not the semantic owner of
`BaseItemInfo`. Native runtime consumers read only the published C++ array.
The loader must not retain a second `_infos` array or answer native fact queries.
The current config cache still roots the immutable combined definition batch;
that retained source copy is a measurable memory cost, not a second writer.
Do not add a one-shot config loader without measuring that cost.

Mixed tables do not retain independent native and SmallS converter lists. The
data spec is the single rules-definition transform; native facts are published
from the mixed definition batch or read by their native owner where no SmallS
projection is involved.

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
