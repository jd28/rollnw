# Language Reference

This reference describes the Smalls language as shipped in this repository.

## Modules

- Module paths are dot-separated (e.g. `core.string`).
- A module resolves to a `.smalls` file (e.g. `core/string.smalls`).
- Modules are loaded from directories registered with `Runtime::add_module_path(...)`.
- A directory containing a `package.json` is treated as a "package root"; its directory name becomes the top-level namespace prefix.

## Types

- Primitives: `int`, `float`, `bool`, `string`, `any`
- Generic containers: `array!($T)`, `map!($K, $V)`
- User types: `type Name { ... }`, `type Name!($T) { ... }`
- Functions: `fn(args...): Ret` and function values like `fn(int): int`

## Imports

Aliased import:

```go
import core.string as str;
```

Selective import:

```go
from core.test import { test, reset, summary };
```
