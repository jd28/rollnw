# NWScript Migration Guide (Draft)

This is a pragmatic mapping guide for moving small rules/data scripts from NWScript to Smalls.

## Key Differences

- Smalls is statically typed and module-based.
- The stdlib is imported explicitly (e.g. `import core.string as str;`).
- Engine objects are represented as handles (not raw pointers).

## Typical Mappings

- `int`, `float`, `string` map directly.
- Arrays/maps use `array!(T)` and `map!(K, V)`.
