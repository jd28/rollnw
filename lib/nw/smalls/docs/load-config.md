# load_config

`load_config!` is the current path for moving RPG ruleset data out of hardcoded
C++ tables and into Smalls-owned manifests. The data is still typed code: each
entry is a Smalls struct literal, and rules modules load arrays of those entries
through one intrinsic.

This is the "manifest RPGs" direction in practical form:

```text
ruleset schema
  -> typed Smalls structs with explicit fields and indexes
ruleset data
  -> one `.smalls` struct literal per entry
rules code
  -> load_config!(EntryType)("ruleset.data.table")
runtime
  -> cached array!(EntryType), indexed by the entry's [[index]] field
```

The common case is a rules module loading one table once, then serving small
lookup helpers from that cached array.

## Entry Schema

Config arrays are built from structs. The struct needs an integer field marked
with `[[index]]`; that field determines where the entry lands in the returned
array. For hot rules lookups, use `[[value_type]]` so entries are stored inline
in `array!(T)`.

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

Call it with an explicit entry type:

```smalls
import core.array as Array;
from nwn1.rules import { ClassEntry };

var classes = load_config!(ClassEntry)("nwn1.data.classes");

fn fighter_hit_die(): int {
    var fighter = Array.get(classes, 4);
    return fighter.hit_die;
}
```

Most modules hide the raw table behind lookup helpers so callers get explicit
out-of-range behavior:

```smalls
from nwn1.rules import { ClassEntry };
from core.types import { Class, Ability };
import core.array as Array;

var _loaded = false;
var _classes: array!(ClassEntry);

fn _ensure_loaded() {
    if (_loaded) {
        return;
    }
    _classes = load_config!(ClassEntry)("nwn1.data.classes");
    _loaded = true;
}

fn exists(class_id: Class): bool {
    _ensure_loaded();
    var index = class_id as int;
    return index >= 0 && index < Array.len(_classes);
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

That wrapper shape is deliberate. `load_config!` loads data; the rules module
owns invalid-id policy.

## Runtime Contract

`load_config!(T)(path)`:

- normalizes dot and slash paths before lookup
- requires `T` to be a struct with an `int` `[[index]]` field
- loads all direct `.smalls` entries below the table path
- sorts entry files by resource path before inserting them
- returns an `array!(T)` sized to the highest non-negative index plus one
- caches the returned array by normalized path and entry type
- roots the array so garbage collection does not reclaim config data

Out-of-range and bad data behavior is explicit:

- missing table path logs a warning and returns an empty array
- invalid entry type, missing `[[index]]`, or non-`int` index fails the intrinsic
- entries that fail to load, have non-`int` indexes, or have negative indexes are skipped
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

`load_config!` is for stable ruleset data: classes, races, feats, base items,
spell rows, progression tables, and similar data that is loaded once and queried
many times.

Propsets are for per-object game state: hit points, ability scores, combat
state, class levels, inventory-facing state, and other data attached to a live
object. A combat rule commonly joins both:

```text
class/race/base-item config from load_config!
  + live creature/item propsets
  -> rule result
```

Keeping these separate prevents static rules manifests from becoming object
storage, and prevents live object state from becoming global rules data.
