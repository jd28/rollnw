# NWScript Migration Guide (Draft)

This is a pragmatic mapping guide for moving small rules/data scripts from NWScript to Smalls.

## Key Differences

- Smalls is statically typed and module-based.
- The stdlib is imported explicitly (e.g. `import core.string as str;`).
- Engine objects are represented as handles (not raw pointers).

## Typical Mappings

- `int`, `float`, `string` map directly.
- Arrays/maps use `array!(T)` and `map!(K, V)`.

## Engine Context Boundaries

- Prefer `core.effects` constructors for pure, context-free effect values.
- Equip-driven item properties are context-sensitive (owner, slot, base item, and runtime ownership) and should be routed through contextual APIs (for example `core.item`/`core.creature`), not raw constructor+apply patterns.
- Keep ownership explicit at boundaries: script-created transient effects can be VM-owned, while equip/generated effects should be engine-owned.
- Use `core.creature` equip APIs for slot-aware item application/removal; this preserves existing engine item-property processing semantics.
