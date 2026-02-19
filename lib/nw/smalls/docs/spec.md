# Smalls Language Design Document

**Version**: 0.9.0
**Last Updated**: 2026-02-09

This document mixes language contracts and current implementation notes. It is a work-in-progress.

## I. Vision & Philosophy

Smalls is a statically-typed language for implementing RPG rulesets, combining a modern type system with zero-overhead C++ interop.

### Goals & Philosophies

- **Modern type system** — sum types, pattern matching, newtype wrappers, generics
- **Zero-overhead C++ interop** — script types map directly to C++ memory layouts
- **Simple, streamlined syntax**
- **Fast compilation** — optimized for hot-reloading
- **Zero Is Initialization** — All types are initialized to zero, i.e. `memset(blob, sizeof(Type), 0)`.
- **Config & Serialization Are Struct Literals** — Why have separate file formats (TOML, ini, JSON, etc) when there's already a parser for struct literals?
- **Bytecode as an Implementation Detail** — no bytecode exposure
- **Single-Threaded Execution** — Each script runs on a single thread. No concurrent access to VM state. Engine manages threading externally.

### Language Contracts vs Implementation Details

Scripts can rely on these **language contracts**:
- **Type semantics**: Memory layouts, field ordering, alignment rules
- **Value type guarantees**: `[[value_type]]` structs and `T[N]` arrays are stack-allocatable with C-compatible layout
- **Module semantics**: Import resolution, namespace scoping, circular dependency detection
- **Operator behavior**: Defined semantics for all operators on all types
- **Sum type layout**: Tag + payload structure, pattern matching exhaustiveness

These are **implementation details** controlled by the engine:
- **GC scheduling**: When minor/major collections occur (allocation-triggered GC is advisory, not contractual)
- **GC thresholds**: `GCConfig` values are tuning parameters, not guarantees
- **Gas/limits**: Resource limits configured per-execution (default-on, can be disabled)
- **Debug hooks**: `gc_collect()`, tracing, profiling APIs are for testing/debugging only
- **Bytecode format**: Internal representation may change between versions
- **Register allocation**: Compiler internals, not observable from scripts

### System Limits

Practical constraints to prevent resource exhaustion and ensure predictable behavior:

| Limit | Value | Rationale |
|-------|-------|-----------|
| VM Registers (per frame) | 256 | VM opcodes address registers with 8-bit indices |
| VM Register Stack (total) | 8192 | Global register file backing all call frames |
| Max Call Depth | 64 | Prevent stack overflow from infinite recursion |
| Max String Length | TBD | Prevent memory exhaustion from large strings |
| Max Array Size | TBD | Prevent memory exhaustion from large arrays |
| Gas Limit | Default 100k, 0=unlimited | Prevent infinite loops and runaway scripts |
| Max Struct Fields | TBD | Keep type metadata manageable |
| Max Generic Nesting | TBD | Prevent `map!(map!(map!(...)))` pathological cases |

**Notes**:
- TBD limits will be determined during implementation based on practical needs
- Gas metering is enabled by default per execution; pass `gas_limit=0` for unlimited
- Gas is decremented on backward jumps and function calls; exhaustion fails with "Script exceeded execution limit"
- Limits enforced at compile-time where possible, runtime otherwise
- Engine can override limits for trusted scripts if needed

**GC Control Policy**:
- The **engine** is responsible for scheduling garbage collection, not scripts
- Allocation may trigger minor/major GC based on `GCConfig` thresholds, but this is advisory
- `gc->collect_minor()` / `gc->collect_major()` are **test/debug APIs only**, not part of the scripting surface
- Scripts must treat GC as opaque; correctness cannot depend on when collections occur

#### Implementation

**Status**: Complete

**Key files**:
- `lib/nw/smalls/runtime.cpp` — registers `core.prelude.gc_collect`
- `lib/nw/smalls/scripts/core/prelude.smalls` — declares the `core.prelude` API surface (`[[native]] fn ...`)
- `lib/nw/smalls/GarbageCollector.cpp` — implements `collect_minor()` / `collect_major()`

---

## II. Type System

### Builtin Types

| Syntax        | C++ Equivalent | Notes |
|---------------|----------------|-------|
| `int`         | `int32_t`      | Fixed-size integer |
| `float`       | `float`        | 32-bit float |
| `bool`        | `bool`         | Boolean |

### Type Definitions & Forms

```go
// Type alias (different name for abbreviation, documentation, etc)
type Gold = int;

// Type alias with generics
type StringMap = map!(string, string);
type IntList = array!(int);

// Newtype (distinct type at compile-time, same representation at runtime)
type Feat(int);

// Product type (struct) - note: semicolon-delimited members
type Point {
    x: float;
    y: float;
};

// Multiple identifiers with same type
type Point3D {
    x, y, z: float;
};

// Tuple - anonymous product type with structural equality
(int, float, string)

// Sum type (tagged union / algebraic data type)
type Result = Ok(int) | Err(string);

// Sum type with unit variants
type Color = Red | Green | Blue;

// Opaque types - not declared in scripts, registered from C++ engine code
```

### Struct Types

#### Member Syntax
- **Separator**: Required semicolons (`;`) after each member declaration
- **Multiple identifiers**: Supported — `x, y, z: Type;` expands to multiple fields
- **Type annotation**: Uses colon syntax — `name: Type;`

#### Brace Initialization

Three forms supported for aggregate type initialization (structs, arrays, maps):

1. **Named field syntax**: `{ field = value, field2 = value2 }`
   - Fields can appear in any order
   - Uninitialized fields default to zero
   - Example: `Point { y = 2.0, x = 1.0 }`
2. **Positional syntax**: `{ value1, value2, value3 }`
   - If initializing a struct: Values must match field declaration order, all fields not provided default to zero.
   - Example: `Point { 1.0, 2.0 }` or `{ 1.0, 2.0 }` in places type can be inferred.
   - This syntax is also used to initialize array types.
3. **Key/Value syntax**: `{ key1: value1, key2: value2 }`
   - Used only in constructing mapping types, hashtables, etc.

**Note**: Trailing commas are allowed in all three syntax forms.

**Examples**:
```go
type Point { x, y: float; };
type Color { r, g, b, a: float; };

// Named field initialization
const p1 = Point { x = 1.0, y = 2.0 };
const p2 = Point { y = 3.0, x = 4.0 };  // Order doesn't matter

// Positional initialization
const p3 = Point { 1.0, 2.0 };
const c1 = Color { 1.0, 0.5, 0.0, 1.0 };

// Mixed with nested structs
type Line { start, end: Point; };
const line = Line {
    start = { 0.0, 0.0 },
    end = { 10.0, 10.0 }
};
```

### Newtype Wrappers

Newtypes create distinct types at compile-time while sharing the same runtime representation as their underlying type. This provides type safety without runtime overhead:

```go
type HP(int);
type Gold(int);

fn damage_creature(creature: object, amount: HP) { ... }
fn buy_item(cost: Gold) { ... }

var health: HP = HP(100);
var money: Gold = Gold(50);

damage_creature(obj, health);  // ✓ Correct
damage_creature(obj, money);   // ✗ Compile error: Gold is not HP
buy_item(health);              // ✗ Compile error: HP is not Gold
```

**Key properties**:
- **Distinct at compile-time**: `Feat(1)` and `Gold(1)` are different types
- **Identical at runtime**: Both stored as `int32_t`, no wrapper overhead
- **Explicit construction**: Must wrap value: `Feat(42)`, not just `42` (casts to newtype are rejected)
- **Explicit unwrapping**: Access underlying value with cast: `(feat as int)`
- **Type safety**: Prevents accidental mixing of semantically different values

#### Implementation

**Status**: Complete

**Key files**:
- `lib/nw/smalls/AstResolver.hpp` — resolves newtype declarations
- `lib/nw/smalls/types.hpp` — `TK_newtype` type kind

### Sum Types & Pattern Matching

Sum types (also known as tagged unions or algebraic data types) allow a value to be one of several variants. Each variant can optionally carry payload data.

#### Sum Type Declaration

```go
// Simple enum-style (unit variants)
type Color = Red | Green | Blue;

// Variants with payloads
type Result = Ok(int) | Err(string);

// Mixed variants (some with payloads, some without)
type Option = Some(int) | None;

// Multiple payload types (tuple syntax)
type Shape = Circle(float) | Rectangle(float, float) | Point;
```

**Key features**:
- Pipe-separated variant list: `Variant1 | Variant2 | ...`
- Unit variants have no payload: `Red`, `None`
- Single payload: `Ok(int)`, `Circle(float)`
- Multiple payloads use tuple syntax: `Rectangle(float, float)`
- Nominal typing: Each sum type declaration creates a distinct type

#### Variant Construction

Variants are constructed using qualified access (Rust-style). Variants are members of their sum type's namespace:

```go
type Result = Ok(int) | Err(string);
type Color = Red | Green | Blue;

// Payload variants - use qualified call syntax
var r: Result = Result.Ok(42);
var e: Result = Result.Err("something failed");

// Unit variants - use qualified member access
var c: Color = Color.Red;
```

**With modules**:
```go
from core.errors import { Result };
var r: Result = Result.Ok(42);  // Qualified access

import core.errors as err;
var r: err.Result = err.Result.Ok(42);
```

**Type aliases do not create variant namespaces**:
```go
type Result = Ok(int) | Err(string);
type MyResult = Result;  // Just another name for Result
var r: MyResult = Result.Ok(42); // ✓ Use original type name
// NOT: MyResult.Ok(42) - aliases don't have their own variants
```

**Construction rules**:
- Payload variants: `TypeName.VariantName(args)` — CallExpression with arguments
- Unit variants: `TypeName.VariantName` — DotExpression with no call
- Argument types must match variant payload (single type or tuple)
- Variants are members of the sum type namespace (prevents naming conflicts)

**Generic sum constructors**:
- Payload variants can infer type arguments from argument types
- Unit variants require an expected type context (e.g., a variable annotation or function return type)

#### Pattern Matching

Pattern matching on sum types is done via `switch` statements:

```go
type Result = Ok(int) | Err(string);

fn process(r: Result): int {
    switch (r) {
        case Ok(value):
            return value;
        case Err(msg):
            return -1;
    }
}
```

**Pattern syntax**:
- **Unit variant**: `case VariantName:` — Matches variant with no payload
- **Single binding**: `case Ok(x):` — Binds payload to variable `x`
- **Multiple bindings**: `case Rectangle(w, h):` — Binds tuple elements to `w` and `h`
- **Guard expression**: `case Ok(x) if x > 0:` — Additional condition on binding

**Examples**:

```go
// Unit variants
type Color = Red | Green | Blue;
fn describe_color(c: Color): string {
    switch (c) {
        case Red:   return "red";
        case Green: return "green";
        case Blue:  return "blue";
    }
}

// Single payload with bindings
type Option = Some(int) | None;
fn unwrap_or(opt: Option, or_value: int): int {
    switch (opt) {
        case Some(value): return value;
        case None:        return or_value;
    }
}

// Multiple payloads (tuple destructuring)
type Shape = Circle(float) | Rectangle(float, float) | Point;
fn area(s: Shape): float {
    switch (s) {
        case Circle(radius):        return 3.14 * radius * radius;
        case Rectangle(width, height): return width * height;
        case Point:                  return 0.0;
    }
}

// Guard expressions
type Result = Ok(int) | Err(string);
fn get_positive(r: Result): int {
    switch (r) {
        case Ok(x) if x > 0: return x;
        case Ok(x):           return 0;
        default:               return -1;
    }
}
```

**Type checking & validation**:
- Variant names must exist in the sum type
- Binding count must match payload type (1 for single, N for tuple)
- Bindings are automatically typed based on payload
- Guard expressions must be boolean
- **Exhaustiveness checking**: Compiler errors if not all variants are covered (unless `default` is present)

**Switch semantics**:
- **No implicit fallthrough**: Unlike C/C++, cases do not fall through. Each case is independent and execution exits the switch after the case body completes. This applies to all switch types (int, string, sum types).
- `break` statements are unnecessary (but allowed for compatibility)

**Memory layout**: C-compatible tag + union structure. Tag is `uint32_t` at offset 0. Union for payloads aligned after tag. `SumDef` stores `payload_offset`, `variant_count`, and per-variant info (`name`, `payload_type`). The `contains_heap_refs` flag is `true` if ANY variant has a heap-referencing payload.

**Initialization enforcement**: Sum type variables must have an initializer at the declaration site — `var x: MySum` without a variant expression is a compile error. The compiler always emits `SUMINIT` immediately after `NEWSUM`, so a zero-filled but uninitialized sum cannot appear in valid compiled code. As a secondary defense, the `SUMGETTAG` handler validates the tag against `variant_count` and calls `fail()` if it is out of range, catching memory corruption or compiler bugs.

***See [VII.Type-Specific Tracing]***

#### Implementation

**Status**: Complete

**Key files**:
- `lib/nw/smalls/Bytecode.hpp` — `NEWSUM`, `SUMINIT`, `SUMGETTAG`, `SUMGETPAYLOAD` opcodes
- `lib/nw/smalls/runtime.hpp` — `alloc_sum`, `write_sum_tag/payload`, `read_sum_tag/payload`
- `lib/nw/smalls/AstCompiler.cpp` — variant construction and pattern match dispatch
- `lib/nw/smalls/Validator.hpp` — exhaustiveness checking

**Opcodes**:
- `NEWSUM rA, Bx` — Allocates sum value on heap, stores pointer in rA. Bx is type index.
- `SUMINIT rA, B, C` — Sets tag (B) and payload (rC) for sum in rA. C=255 means no payload (unit variant).
- `SUMGETTAG rA, rB, _` — Extracts tag value from sum in rB, stores in rA.
- `SUMGETPAYLOAD rA, rB, C` — Extracts payload from sum in rB for variant index C, stores in rA.

### Tuple Types

Tuples are anonymous product types with structural equality. The primary use case is **multiple return values**.

**Structural Typing**: Tuple types are compared by structure, not by name:
- `(int, int)` in module A is the same type as `(int, int)` in module B
- Tuples with identical element types are deduplicated at runtime

**Multiple Return Values**:
```go
fn swap(x: int, y: int): (int, int) {
    return y, x;  // Implicit tuple creation
}
```

**Tuple Destructuring**:
```go
fn test() {
    var a, b = swap(1, 2);  // Declaration destructuring
    a, b = swap(b, a);      // Assignment destructuring
}
```

**Tuple Indexing**:
```go
fn test() {
    var tuple = (42, 3.14, "hello");
    var x = tuple[0];  // x: int = 42
    var y = tuple[1];  // y: float = 3.14
    var z = tuple[2];  // z: string = "hello"
}
```

**Key Features**:
- Destructuring in declarations and assignments
- Index access with `[N]` syntax where N is a compile-time constant
- Variable count must match tuple element count in destructuring
- Bounds checking at compile-time
- Works with both `var` and `const` declarations

#### Implementation

**Status**: Complete

**Key files**:
- `lib/nw/smalls/types.hpp` — `TupleDef` (size, alignment, element_count, element_types[], offsets[])
- `lib/nw/smalls/Bytecode.hpp` — `NEWTUPLE`, `GETTUPLE` opcodes
- `lib/nw/smalls/AstCompiler.cpp` — tuple construction and destructuring

### Function Types & Closures

Functions are first-class values with explicit function types and lexical closures.

**Function type syntax**:
- `fn(int, string): bool`
- `fn(int, string)` (implicit `void` return)

**Lambda syntax**:
```go
fn(x: int): int { return x + 1; }
fn(x: int) { print(x); } // void return inferred
```

**Return type inference**:
- If the return type is omitted, the compiler infers it from `return` statements
- No returns → `void`
- Multiple returns must agree on a compatible type

**Closure semantics**:
- Lexical capture by reference (Lua-style upvalues)
- Mutations in a closure update the captured variable
- Closures share upvalues when capturing the same variable
- Upvalues close when the owning frame exits (return or error unwind)

#### Implementation

**Status**: Complete

**Key files**:
- `lib/nw/smalls/Bytecode.hpp` — `CLOSURE`, `GETUPVAL`, `SETUPVAL`, `CLOSEUPVALS`, `CALLCLOSURE` opcodes
- `lib/nw/smalls/runtime.hpp` — `Upvalue` struct, `ClosureObject`
- `lib/nw/smalls/AstCompiler.hpp` — `compile_lambda()`, upvalue index management

### Generics

**Status**: Complete — Full support for generic functions with type inference and on-demand monomorphization.

Smalls supports **lightweight monomorphization** for generic functions — think **type-safe C macros**. Use `$T` syntax for type parameters that get inferred at call sites and specialized at compile-time.

**Syntax**:
```go
// Just use $T in the signature - no explicit type parameter list needed
fn max(a: $T, b: $T): $T {
    return a > b ? a : b;
}

// Multiple type parameters
fn map(arr: array!($T), f: fn($T): $U): array!($U) {
    var result: array!($U);
    for (var item in arr) {
        result.append(f(item));
    }
    return result;
}
```

**Key Design Principles**:
1. **Structural typing** — Type parameters constrained by usage (duck typing), not explicit bounds
2. **Monomorphization** — Compiler generates specialized version for each concrete type usage
3. **Compile-time only** — No runtime type parameters or vtables
4. **Full type inference** — Type arguments deduced from call site, no manual specification needed
5. **Zero ceremony** — No explicit type parameter lists, just use `$T` inline

**How It Works**:
1. Parser sees `$T` in signature — marks function as generic
2. At call site `max(10, 20)`, compiler infers `$T = int` from arguments
3. Clones function AST and replaces all `$T` → `int`
4. Type-checks specialized version (does `int` support `>`? yes)
5. Compiles specialized bytecode as `max_int`
6. Caches by `(func_name, [type_args])` — reused for future `max(int, int)` calls

**Call Site Examples**:
```go
var x = max(10, 20);              // $T inferred as int
var y = max(1.5, 2.5);            // $T inferred as float
var squared = map(nums, fn(x) { return x * x; });  // $T = int, $U = int
```

**Type Parameter Rules**:
- `$T`, `$U`, `$V`, etc. — Single uppercase letter prefixed with `$`
- Scoped to function declaration (not visible outside)
- Can appear in parameter types, return type, and function body
- Inferred from call site arguments — no manual specification

**Generic Types (Structs/Sum Types)**:

Smalls supports generic struct and sum type definitions using explicit type parameter syntax:

```go
// Generic struct
type Pair!($A, $B) {
    first: $A;
    second: $B;
};

// Generic sum type
type Option!($T) = Some($T) | None;

// Usage - explicit type arguments required
var pair: Pair!(int, string) = { 42, "hello" };
```

**Limitations**:
- Monomorphization increases binary size (one copy per type combination)
- Compile-time only (no dynamic dispatch or runtime generics)
- Function type parameters must be fully inferrable from arguments
- Type parameters for generic types usually require explicit specification at usage
- Sum variant constructors can infer type arguments from payloads or the expected type
- No `where $T: Comparable` syntax (structural typing only)
- No explicit instantiation for functions (type arguments always inferred)

#### Implementation

**Status**: Complete

**Key files**:
- `lib/nw/smalls/AstResolver.hpp` — `get_or_instantiate_type()`, type substitution maps
- `lib/nw/smalls/AstCompiler.hpp` — `compile_instantiated()`, generic function compilation
- `lib/nw/smalls/runtime.hpp` — `get_or_instantiate()` for specialized function skeletons

**Key data structures**:
- `AstResolver::type_substitutions_` — maps `$T` names to concrete TypeIDs during instantiation
- `CallExpression::inferred_type_args_` — stores inferred type arguments at call sites

### Arrays & Maps

Smalls has two distinct array forms with different semantics:

| Syntax | Category | Storage | Resizable | Use Case |
|--------|----------|---------|-----------|----------|
| `array!(T)` | Dynamic array | Heap object (HeapPtr) | Yes | Collections, lists |
| `T[N]` | Fixed array | Value type (inline) | No | Vectors, matrices, buffers |

**Dynamic Arrays** (`array!(T)`):
```smalls
fn process_nearby_objects(center: vec3, radius: float) {
    var nearby: array!(object) = {};  // VM heap allocation
    for (var x in nearby) {
        // ... process ...
    }
}
```

Properties:
- Heap-allocated, GC-managed
- Resizable via `push`, `pop`, `resize`
- Stored as HeapPtr (4 bytes inline)
- Element storage: TypedArray for primitives, StructArray for value-type structs

**Fixed Arrays** (`T[N]`):
```smalls
[[value_type]]
type Matrix3x3 {
    data: float[9];  // 36 bytes inline
};

fn test() {
    var buffer: int[256];  // 1024 bytes on stack
    buffer[0] = 42;
}
```

Properties:
- Value type with C-compatible layout
- Inline storage (no indirection)
- Stack-allocatable, struct-embeddable
- Size known at compile time: `sizeof(T) * N`

**Contract**:
- `array!(T)` is the resizable heap collection.
- `T[N]` is the fixed-size inline value type.

***See [VII.Write Barriers]***

**Maps** (`map!(K, V)`):
```smalls
var scores: map!(string, int) = {"alice": 100, "bob": 85};
```

Properties:
- Heap-allocated hash table
- Keys are policy-restricted to `int`, `string`, and newtypes over `int`/`string`
- `float` and `bool` are never valid map keys
- Iteration order is unspecified

#### Implementation

**Status**: Complete

**Key files**:
- `lib/nw/smalls/AstResolver.cpp` — resolves fixed arrays (`T[N]`) and computes layout metadata
- `lib/nw/smalls/types.hpp` — type kinds and `contains_heap_refs`
- `lib/nw/smalls/Bytecode.hpp` — `NEWARRAY`, `GETARRAY`, `SETARRAY`, `NEWMAP`, `MAPGET`, `MAPSET` opcodes

### Strings

Strings in Smalls are heap-allocated, immutable sequences of characters.

**Representation**: Strings are stored as `StringRepr` objects on the `ScriptHeap`. Each `StringRepr` contains a backing buffer pointer, offset, and length — enabling zero-copy substrings (views into existing string data).

**Script-side**: The `string` type is a 4-byte `HeapPtr` — a handle into the script heap.

**Interning**: The empty string is interned (single canonical instance). All string operations return new strings rather than mutating existing ones.

**Native interop**: `ScriptString` is a 4-byte POD struct wrapping a `HeapPtr`, used for string fields in native structs. Access the string contents via `ScriptString::view(rt)`.

#### Implementation

**Status**: Complete

**Key files**:
- `lib/nw/smalls/runtime.hpp` — `StringRepr`, `ScriptString`, `alloc_string()`, `get_string_view()`
- `lib/nw/smalls/Intrinsics.hpp` — all `String*` intrinsic IDs

**Key data structures**:
- `StringRepr::backing` — HeapPtr to backing buffer
- `StringRepr::offset`, `StringRepr::length` — substring view into backing

### F-Strings

F-strings provide string interpolation with embedded expressions.

**Syntax**: `f"text {expr} more text"`

Expressions inside `{...}` are evaluated and converted to strings. For user-defined types, the `[[operator(str)]]` function is called for conversion.

**Examples**:
```go
var name = "World";
var greeting = f"Hello, {name}!";      // "Hello, World!"

var x = 42;
var msg = f"The answer is {x}";        // "The answer is 42"

var p = Point { 1.0, 2.0 };
var desc = f"Position: {p}";           // Calls [[operator(str)]] on Point
```

**Supported interpolation types**:
- `int` — converted via integer-to-string
- `float` — converted via float-to-string
- `bool` — converted to `"true"` or `"false"`
- `string` — inserted directly
- User types — requires `[[operator(str)]]` to be defined

F-strings are also foldable at compile time by the constant evaluator when all interpolated expressions are constant.

***See [III.Operator Aliasing]*** for the `str` operator that enables f-string support for user types.

#### Implementation

**Status**: Complete

**Key files**:
- `lib/nw/smalls/Lexer.hpp` — tokenizes `f"..."` strings, splits into parts and expression spans
- `lib/nw/smalls/AstConstEvaluator.hpp` — constant folds f-strings with known-value expressions
- `lib/nw/smalls/AstCompiler.cpp` — emits string concatenation for runtime f-strings

### Memory Layout & Value Types

**Memory Layout**: Matches C++ rules — fields aligned to natural alignment, padding inserted, total size aligned to strictest member. Field order preserved.

#### Value Type Attribute

The `[[value_type]]` attribute guarantees a struct has C-compatible memory layout, enabling stack allocation and predictable memory representation.

**Syntax**:
```go
[[value_type]]
type Vector {
    x, y, z: float;
};
```

**What it guarantees**:
- Fields laid out sequentially in memory (no reordering)
- Natural alignment for each field
- Padding inserted to maintain alignment
- Total size aligned to strictest member
- Identical memory layout to equivalent C struct

**Stack allocation**:
```go
[[value_type]]
type Point { x, y: float; };

fn test() {
    var p: Point;  // Stack-allocated, not heap
    var grid: Point[10];  // Array of 10 Points on stack
}
```

**Restrictions**:
- Can contain heap types (strings, arrays) as HeapPtr fields
- No virtual methods or runtime polymorphism

**Contrast with heap types**:
- Regular structs: Heap-allocated, GC-managed
- `[[value_type]]` structs: Can be stack or heap allocated
- Fixed arrays (`T[N]`): Always value types, same guarantees

#### Value-Type ABI

Value types use a distinct calling convention from heap types. The runtime tracks this via `ValueStorage` (`immediate`, `stack`, `heap`).

**Storage Rules**:
- **Primitives** (`int`, `float`, `bool`): `immediate` — stored directly in Value union
- **Value-type structs** (`[[value_type]]`): `stack` — stored in per-frame stack buffer
- **Fixed arrays** (`T[N]`): `stack` — stored in per-frame stack buffer
- **Heap types** (strings, `array!(T)`, `map!(K,V)`, closures): `heap` — HeapPtr in Value

**Frame Stack Allocation**: Each `CallFrame` maintains a separate `stack_` buffer for value-type locals. When a value-type variable is declared: `stack_alloc(size, alignment, type_id)` allocates space, data is copied into the stack buffer, `Value::make_stack(offset, type_id)` creates a reference, and `stack_layout_` records the slot for GC tracing.

**Function Returns**: Value types are returned by copying into caller's frame stack. Heap types return HeapPtr (no copy). The caller allocates stack space before the call for value-type returns.

**GC Integration**: Frame stack roots are enumerated via `CallFrame::enumerate_stack_roots()`, which uses `Runtime::scan_value_heap_refs()` to find any HeapPtr fields within value-type data.

***See [VII.Memory Management]***

#### Native Struct Layout

- Native modules must have a corresponding script module with the same path
- Script module uses `[[native]]` attribute to declare types and functions
- Compiler validates script declarations against registered C++ interface
- Type names are automatically qualified with module path (e.g., `"Position"` → `"core.effects.Position"`)
- **Native structs** (`native_struct<T>()`) validate field names, offsets, and types
  - Supported field types: `int`, `float`, `bool`, `vec3`, `object` (ObjectHandle), `ScriptString` (string), `ScriptClosure` (function types)
  - Field order, padding, and alignment must match exactly
  - Config structs use `[[native]]` for zero-copy access — the heap struct IS the C++ struct
- **Value types** (`value_type<T>()`) only validate size and alignment

#### Implementation

**Status**: Complete

**Key files**:
- `lib/nw/smalls/Bytecode.hpp` — `STACK_ALLOC`, `STACK_COPY`, `STACK_FIELDGET/SET` opcodes
- `lib/nw/smalls/VirtualMachine.hpp` — call frames and stack root enumeration
- `lib/nw/smalls/runtime.hpp` — `ValueStorage` enum, `Value` struct

### Cast & Type Test Expressions

Smalls provides `as` for casting and `is` for type testing.

**Cast expressions** (`expr as Type`):
```go
var x: int = 42;
var f: float = x as float;      // int → float
var n: int = 3.14 as int;       // float → int (truncates)
var b: bool = 1 as bool;        // int → bool
var i: int = true as int;       // bool → int

// Newtype unwrapping
type Feat(int);
var feat = Feat(42);
var raw: int = feat as int;     // Unwrap newtype
```

**Supported casts**:
- `int` ↔ `float` — numeric conversion (float→int truncates)
- `int` ↔ `bool` — zero/nonzero conversion
- Newtype ↔ underlying type — wrapping/unwrapping

**Type test expressions** (`expr is Type`):
```go
var val: int = 42;
var check = val is int;    // true
var check2 = val is float; // false
```

Both `as` and `is` are foldable at compile time by the constant evaluator when the operand is constant.

#### Implementation

**Status**: Complete

**Key files**:
- `lib/nw/smalls/AstConstEvaluator.hpp` — constant folds casts and type tests
- `lib/nw/smalls/AstCompiler.cpp` — emits `CAST` and `IS` opcodes

**Opcodes**:
- `CAST rA, Bx` — cast value in rA to type at index Bx
- `IS rA, Bx` — test if value in rA is type at index Bx, stores bool

### Default Values & Nullability

This section complements **Zero Is Initialization** in [I. Vision & Philosophy] and the type-specific sections in [II. Type System].

#### Zero/default values

**HeapPtr(0) semantics (single rule):** `HeapPtr(0)` is a null sentinel. It is only a stable language value for nullable closures (`fn(...)`). For non-nullable heap categories (`string`, `array!(T)`, `map!(K,V)`), zero-initialized storage is eagerly materialized to concrete empty values by the runtime/compiler default-initialization path.

Materialization occurs during default-initialization of locals/fields (compiler or runtime), before the value becomes observable by script code.

Unless noted otherwise, values are zero-initialized recursively.

| Type category | Default value | Notes |
|---|---|---|
| `int`, `float`, `bool` | `0`, `0.0`, `false` | Primitive zero values |
| Newtypes (`type HP(int)`) | underlying zero | Same runtime representation as underlying type |
| `string` | `""` | Materialized empty string value (not nullable); see [II.Strings] |
| `array!(T)` | empty array | Materialized empty array value (not nullable); see [II.Arrays & Maps] |
| `map!(K,V)` | empty map | Materialized empty map value (not nullable); see [II.Arrays & Maps] |
| `T[N]` | all elements zero | Fixed-size value type |
| Structs / tuples | fields/elements zeroed | Recursive zero-initialization |
| Sum types | no implicit default variant | Declaration requires explicit initializer; see [II.Sum Types & Pattern Matching] |
| Closures (`fn(...)`) | null closure value (`HeapPtr(0)`) | Only nullable category |

#### Nullability policy

Smalls supports **nullable closures only**.

- `string` is not nullable; use `""` for "no text"
- `array!(T)` is not nullable; use empty array
- `map!(K,V)` is not nullable; use empty map
- `fn(...)` is nullable; `HeapPtr(0)` represents "no callback"

This preserves predictable zero-initialization while allowing optional callback wiring.

#### Null/empty testing

Use closure truthiness to test whether a callback is present:

```smalls
if (!on_hit) {
    // no callback
}
```

You can also use positive checks:

```smalls
if (on_hit) {
    on_hit(target);
}
```

Use `is` only for type tests (`expr is Type`), as defined in [II.Cast & Type Test Expressions]; `is` is not a null/empty-check operator.

---

## III. Language Features

### Variables & Constants

- **Pattern**: Follows Go-style syntax — `var/const identifier_list [: Type] [= expr_list] ;`
- **Type annotation**: Optional if initializer is present
- **Multiple identifiers**: Supported — `var x, y, z: int;` or `var x, y = 1, 2;`
- **Initializers**: Must match identifier count if provided
- **Examples**:
  - `var x: int;` — Variable with type
  - `var x = 42;` — Variable with type inference
  - `var x: int = 42;` — Variable with both type and initializer
  - `var x, y, z: int;` — Multiple variables with same type
  - `var x, y = 1, 2;` — Multiple variables with separate initializers
  - `const PI = 3.14;` — Constant (initializer required)

### Conditional (Ternary) Expressions

Smalls supports C-style conditional (ternary) expressions:

```go
var result = condition ? true_value : false_value;
```

The condition is evaluated first. If truthy, the true branch is evaluated and returned; otherwise the false branch. Both branches must produce compatible types. Conditional expressions are foldable at compile time when the condition is constant.

```go
var x = 10;
var abs_x = x >= 0 ? x : -x;
var label = score > 90 ? "A" : score > 80 ? "B" : "C";  // Nesting supported
```

#### Implementation

**Status**: Complete

**Key files**:
- `lib/nw/smalls/Parser.hpp` — `parse_expr_conditional()`
- `lib/nw/smalls/AstConstEvaluator.hpp` — constant folds ternary with known condition
- `lib/nw/smalls/AstCompiler.cpp` — emits `JMPF` + branch bytecode

### Control Flow

#### If/Else Statements

```go
if (condition) {
    // then block (braces required)
}

if (condition) {
    // then block
} else {
    // else block (braces required)
}

if (condition) {
    // first branch
} else if (other_condition) {
    // second branch
} else {
    // final branch
}
```

- Block braces `{}` are always required
- Condition expressions must be in parentheses

#### For Loops

Three forms (Go-style):

1. **Infinite loop** — `for { ... }` — loops until break/return
2. **While-style loop** — `for (condition) { ... }` — checked before each iteration
3. **C-style loop** — `for (init; condition; increment) { ... }` — traditional three-part form

```go
for {
    if (done) { break; }
}

for (i < 10) {
    i = i + 1;
}

for (var i = 0; i < 10; i = i + 1) {
    print(i);
}
```

- Block braces `{}` always required
- Loop variables declared in init are scoped to the loop
- `break` and `continue` supported

#### For-Each Loops

**Array iteration** — Single binding receives element values:
```go
var nums: array!(int) = {1, 2, 3, 4, 5};
for (var x in nums) {
    print(x);  // x is int, inferred from array element type
}
```

**Map iteration** — Two bindings receive key-value pairs:
```go
var scores: map!(string, int) = {"alice": 100, "bob": 85};
for (var key, value in scores) {
    print(key);    // key is string
    print(value);  // value is int
}
```

- Type inference: Binding types inferred from collection
- `break` and `continue` supported
- Iteration order: arrays iterate in index order; maps iterate in unspecified order

#### Pattern Matching Switch

***See [II.Sum Types & Pattern Matching]*** for full pattern matching documentation.

Pattern matching on sum types is accessed through `switch` statements with `case VariantName(bindings):` syntax. This section covers the type layout; the switch dispatch and binding mechanics are documented alongside sum types.

### Operator Aliasing

Operator aliasing allows user-defined types to participate in operators and language features through attribute-annotated free functions.

#### Why Operator Aliasing?

Beyond syntactic convenience, operator aliasing provides **extensibility**:
- User types can define string conversion for f-strings
- User types can define mathematical operators
- User types integrate with language features seamlessly

#### Syntax

**Basic Binary Operator:**
```go
type Point { x, y: float; };

[[operator(plus)]]
fn add(a: Point, b: Point): Point {
    return Point { a.x + b.x, a.y + b.y };
}

var p3 = p1 + p2;  // Calls add(p1, p2)
```

**Unary Operator:**
```go
[[operator(minus)]]
fn negate(p: Point): Point {
    return Point { -p.x, -p.y };
}

var p2 = -p1;  // Calls negate(p1)
```

**Commutative Operators:**

By default, operators only work in the exact order written. Use `commutative` to handle both orderings:

```go
[[operator(times, commutative)]]
fn scale(p: Point, s: float): Point {
    return Point { p.x * s, p.y * s };
}

var r1 = p * 2.0;  // Calls scale(p, 2.0)
var r2 = 2.0 * p;  // Calls scale(p, 2.0) with swapped arguments
```

**String Conversion:**

The `str` operator enables custom string conversion for user types:

```go
[[operator(str)]]
fn point_to_string(p: Point): string {
    return "Point(" + str(p.x) + ", " + str(p.y) + ")";
}

var msg = f"Location: {p}";  // Calls point_to_string(p)
var s = str(p);               // "Point(1.0, 2.0)"
```

***See [III.Modules (core.string)]*** for string intrinsics.

#### Key Rules

1. **Default behavior**: Operators only work in the exact parameter order written
2. **Type inference**: Parameter and return types inferred from function signature
3. **Arity from parameters**: 1 parameter = unary, 2 parameters = binary
4. **No duplicates**: Error if same operator+types registered twice
5. **Explicit commutativity**: Use `commutative` attribute to handle both argument orderings

#### Important Operators

**Essential** (needed for language features):
- `str` — String conversion (for f-strings, debug output)
- `hash` — Hash function (for hash maps, sets)
- `eq` (==) — Equality (foundational comparison)
- `lt` (<) — Less-than (foundational ordering)

**Useful** (math):
- `plus` (+), `minus` (-), `times` (*), `div` (/) — Arithmetic operators

**Note**: Other comparison operators (>, >=, !=, <=) are synthesized from `eq` and `lt` at the type checking level.

**Note**: Bitwise operators are not language operators; use intrinsic functions from `core.bit` (e.g., `bit.and(a, b)`).

#### Synthesized Comparisons

Given `eq` and/or `lt`, the compiler synthesizes the remaining comparison operators:

| Operator | Synthesized from     |
|----------|----------------------|
| `!=`     | `eq`                 |
| `>`      | `lt(b, a)`           |
| `<=`     | `lt(a, b) \|\| eq(a, b)` |
| `>=`     | `lt(b, a) \|\| eq(a, b)` |

`!=`, `>`, `<=`, `>=` are never aliased directly; they are always derived.

#### Default Structural Operators

Struct, sum, and array types automatically receive default structural operators (`eq`, `lt`, `hash`) when their contents recursively support the specific operator — no explicit `[[operator(...)]]` annotation required.

**Eligibility**: A type is eligible for a default operator if its contents recursively support that operator. Specifically:
- Primitives (`int`, `float`, `bool`) and `string` are eligible for default `eq` and `lt`
- Primitives `int`, `bool`, and `string` are eligible for default `hash`; `float` is not default-hashable
- A struct is eligible for an operator if all its fields are eligible for that operator (checked after explicit aliases are registered, so struct-of-struct dependencies work)
- A sum type is eligible for an operator if all variant payloads are eligible for that operator (unit variants with no payload are always eligible)
- A dynamic array type (`array!(T)`) is eligible for an operator if its element type `T` is eligible for that operator
- A fixed array type (`T[N]`) follows the same recursive eligibility rule as other value aggregates
- Maps, closures, and handles are **not** eligible for any default operator

Because default `hash` is recursive, any type that contains a `float` anywhere in its structure (field, payload, or array element) does not receive a default hash.

**Semantics**:
- **Struct `eq`**: field-by-field equality in declaration order; returns `true` iff all fields compare equal
- **Struct `lt`**: lexicographic ordering in declaration order; first differing field determines the result
- **Struct `hash`**: combines per-field hashes using XXH3 streaming
- **Sum `eq`**: compares variant tags first; if tags match and the variant has a payload, compares payloads
- **Sum `lt`**: compares by tag order first (declaration order); if tags match, compares payloads
- **Sum `hash`**: hashes the tag, then the payload (if any), via XXH3 streaming
- **Array `eq`**: element-by-element equality; arrays of different length are never equal
- **Array `lt`**: lexicographic; shorter prefix-equal arrays are less than longer ones
- **Array `hash`**: hashes the length, then each element, via XXH3 streaming

#### Map Key Policy

Map key eligibility is policy-based (not just operator availability).

- Allowed key categories: `int`, `string`, and newtypes whose underlying type is `int` or `string`
- Disallowed key categories: `float`, `bool`, handles (`object`/typed handles), arrays, maps, functions/closures, structs, sums, and newtypes over disallowed categories

This keeps map keys intentionally narrow while preserving semantic-ID ergonomics (`type FeatId(int)`) without broadening to general user-defined keys. Richer custom/default hashing is still valuable for non-map hashed collections (for example, a future `hash_set`).

**Newtype keys**: Newtypes whose underlying type is `int`/`string` are permitted as map keys. Newtype key types are not interchangeable with their underlying type or with other newtypes; the key type must match the map's `K` exactly.

**Operator precedence**: Explicit `[[operator(... )]]` definitions take precedence over default structural operators.
**Note**: Operator availability does not expand map-key eligibility; map keys remain restricted by the allowlist policy.

**Constraints**:
- Explicit `[[operator(lt)]]` requires explicit `[[operator(eq)]]` — custom ordering must pair with custom equality.
- Explicit `[[operator(hash)]]` requires explicit `[[operator(eq)]]` — custom hash must pair with custom equality.
- Default `hash` is **not** synthesized when a type has an explicit `[[operator(eq)]]`. If you define custom equality, structural hash may not satisfy `a == b → hash(a) == hash(b)`, so you must provide an explicit `[[operator(hash)]]` as well.
- Explicit `[[operator(hash)]]` remains allowed for float-containing types and other user-defined types, but this does not make them valid `map` keys unless the type is `int`/`string` or a newtype over `int`/`string`.
- Default structural operators always form a consistent triple: default `eq`, `lt`, and `hash` are all field-by-field and mutually consistent.

#### Implementation

**Status**: Complete

**Key files**:
- `lib/nw/smalls/AstResolver.hpp` — detects and registers operator aliases; `sync_operator_alias_summaries()` propagates defaults to the Validator
- `lib/nw/smalls/AstCompiler.cpp` — emits `CALLEXT` directly to operator functions for user types
- `lib/nw/smalls/runtime.hpp` — operator registry; `register_default_struct_operators()` for the synthesis pass

**Key data structures**:
- `AstResolver::operator_alias_summary_` — tracks which operators each type has defined (eq, hash, lt)
- `Runtime::operator_alias_info_` — runtime mirror of the above; updated by both explicit and default registration
- `Runtime::native_hash_ops_` — maps `TypeID` to a native C++ hash executor for default hash implementations

### Modules & Imports

Enable cross-file references with two import styles: aliased and selective.

#### Import Syntax

**Aliased Import** — Import a module with an explicit alias:
```go
import core.math.vector as vec;
var p: vec.Vector;
```

**Selective Import** — Import specific symbols directly:
```go
from core.math.vector import { Vector, Point };
var p: Vector;
```

#### Design Principles

- **Module paths** are dot-separated (e.g., `core.math.vector`) and map to file paths
- **Imports are declarations** — They participate in normal scoping like variables and types
- **Scoped imports** — Can be declared at any scope (global, function-local, etc.)
- **Everything exported by default** — All top-level declarations are visible to importers (initially)
- **C++ modules are supplemental** — Native modules provide interfaces that script modules link to via `[[native]]` declarations
- **Qualified types** — Use imported types via alias: `vec.Vector` where `vec` is the alias

#### Intrinsic Functions

Intrinsics are module-scoped functions declared with `[[intrinsic("name")]]` and accessed through imports like any other function. Intrinsic declarations have no body and cannot be combined with `[[native]]`.

```go
// core/bit.smalls
[[intrinsic("bit_and")]] fn and(a: int, b: int): int;
[[intrinsic("bit_or")]] fn or(a: int, b: int): int;
[[intrinsic("bit_not")]] fn not(a: int): int;
```

**Complete intrinsic list** (from `Intrinsics.hpp`):

| Module | Intrinsic ID | Script Name | Signature |
|--------|-------------|-------------|-----------|
| `core.bit` | `bit_and` | `and(a, b: int): int` | Bitwise AND |
| | `bit_or` | `or(a, b: int): int` | Bitwise OR |
| | `bit_xor` | `xor(a, b: int): int` | Bitwise XOR |
| | `bit_not` | `not(a: int): int` | Bitwise NOT |
| | `bit_shl` | `shl(a, b: int): int` | Shift left |
| | `bit_shr` | `shr(a, b: int): int` | Shift right |
| `core.array` | `array_push` | `push(a: array!($T), v: $T)` | Append element |
| | `array_pop` | `pop(a: array!($T)): $T` | Remove and return last |
| | `array_len` | `len(a: array!($T)): int` | Element count |
| | `array_get` | `get(a: array!($T), i: int): $T` | Get element at index |
| | `array_set` | `set(a: array!($T), i: int, v: $T)` | Set element at index |
| | `array_clear` | `clear(a: array!($T))` | Remove all elements |
| | `array_reserve` | `reserve(a: array!($T), n: int)` | Pre-allocate capacity |
| `core.map` | `map_get` | `get(m: map!($K,$V), key: $K): $V` | Get value by key |
| | `map_set` | `set(m: map!($K,$V), key: $K, val: $V): bool` | Set key-value pair |
| | `map_len` | `len(m: map!($K,$V)): int` | Entry count |
| | `map_has` | `has(m: map!($K,$V), key: $K): bool` | Check key existence |
| | `map_remove` | `remove(m: map!($K,$V), key: $K): bool` | Remove entry by key |
| | `map_clear` | `clear(m: map!($K,$V))` | Remove all entries |
| | `map_iter_begin` | `iter_begin(m: map!($K,$V)): int` | Begin iteration |
| | `map_iter_next` | `iter_next(m: map!($K,$V), it: int): int` | Advance iterator |
| | `map_iter_end` | `iter_end(m: map!($K,$V), it: int): bool` | Check iterator end |
| `core.string` | `string_len` | `len(s: string): int` | String length |
| | `string_substr` | `substr(s: string, start, len: int): string` | Substring |
| | `string_char_at` | `char_at(s: string, i: int): int` | Character at index |
| | `string_find` | `find(haystack, needle: string): int` | Find substring |
| | `string_contains` | `contains(s, substr: string): bool` | Contains substring |
| | `string_starts_with` | `starts_with(s, prefix: string): bool` | Prefix check |
| | `string_ends_with` | `ends_with(s, suffix: string): bool` | Suffix check |
| | `string_to_upper` | `upper(s: string): string` | Uppercase |
| | `string_to_lower` | `lower(s: string): string` | Lowercase |
| | `string_trim` | `trim(s: string): string` | Trim whitespace |
| | `string_replace` | `replace(s, from, to: string): string` | Replace occurrences |
| | `string_split` | `split(s, delim: string): array!(string)` | Split by delimiter |
| | `string_join` | `join(parts: array!(string), sep: string): string` | Join with separator |
| | `string_concat` | `concat(a, b: string): string` | Concatenate |
| | `string_append` | `append(s, suffix: string): string` | Append string |
| | `string_insert` | `insert(s: string, pos: int, sub: string): string` | Insert at position |
| | `string_reverse` | `reverse(s: string): string` | Reverse string |
| | `string_from_char_code` | `from_char_code(code: int): string` | Char code to string |
| | `string_to_int` | `to_int(s: string): int` | Parse integer |
| | `string_to_float` | `to_float(s: string): float` | Parse float |

The resolver validates intrinsic names and signatures, and the compiler emits intrinsic call opcodes. Runtime errors (bounds, empty pop) abort script execution.

#### Module Resolution

1. Convert module path to file path: `core.math.vector` → `core/math/vector.smalls`
2. Load and parse the module from the resource manager (searching all configured module paths)
3. During resolution, imports trigger recursive module loading
4. If the module contains `[[native]]` declarations, validate against registered C++ module interfaces
5. If the module contains `[[intrinsic]]` declarations, validate intrinsic ids and names
6. Cache loaded modules to avoid re-parsing

#### Module Search Paths and Packages

The runtime loads modules from a list of filesystem directories registered via `Runtime::add_module_path(path)`.

- **Package directories**: A module path directory may contain a `package.json` file. If present, the directory name becomes a top-level namespace prefix ("package") for all modules under it.
  - Example: module path `.../scripts/core/` with `package.json` causes `core/math.smalls` to resolve as module `core.math`.
- **Non-package directories**: If `package.json` is absent, resources are rooted directly under the directory.

**Shipped stdlib layout** (file-based modules):
- `lib/nw/smalls/scripts/core/` — standard library modules under the `core.*` namespace
- `lib/nw/smalls/scripts/tests/` — script test suite under the `tests.*` namespace

`core.prelude` and `core.test` are shipped as script modules that link against native interfaces registered by the runtime.

#### Circular Dependency Detection

The runtime maintains a loading stack to detect circular dependencies:
- Before loading a module, checks if it's already in the loading stack
- If found, reports the full dependency cycle and fails to load

#### Module-Scope Variables

Modules can declare `var` and `const` at the top level. These are stored in per-module global slots and initialized by a synthesized `__init` function that runs automatically when the module is first compiled.

```go
var counter = 0;
const MAX_RETRIES = 3;

fn increment(): int {
    counter = counter + 1;
    return counter;
}
```

**Key properties**:
- Module globals are private to the module (not importable across modules)
- Functions access them via `GETGLOBAL`/`SETGLOBAL` opcodes
- Assignment to `const` variables is a compile error
- Lambdas access module globals via opcodes, not upvalue capture
- Module globals are enumerated as GC roots

#### Future Considerations

- Explicit export control (currently everything is exported)

---

## IV. Data & Engine Integration

### Config & Serialization

Following the philosophy "Config & Serialization Are Struct Literals", Smalls uses bare struct literal syntax as the primary data format. Each `.smalls` config file contains a single struct literal:

```smalls
// fireball.smalls
SpellInfo {
    name = "Fireball",
    school = SpellSchool.Evocation,
    innate_level = 3,
    on_impact = fn(caster: object, loc: Location) {
        apply_area_damage(loc, roll_dice(6, caster_level(caster)), DamageType.Fire);
    },
}
```

**C++ API**: Types are registered via `ModuleBuilder`, then configs loaded with `load_config<T>()`:

```cpp
auto* spell = rt.load_config<SpellInfo>("fireball", "game_types");
```

***See [IV.Native Interfaces]*** for the Registration API used to define config types.

**Implementation details**:
- Config prelude types use `[[native]]` so the smalls heap struct has identical layout to the C++ struct
- `load_config<T>` wraps the file content as `var __config = <content>;`, loads it as a throwaway module, then returns a direct pointer into the script heap (zero-copy)
- The HeapPtr is rooted in `config_roots_` so GC doesn't collect it
- `ScriptString` is a 4-byte POD wrapping a HeapPtr; access via `ScriptString::view(rt)`
- `ScriptClosure` is a 4-byte POD wrapping a HeapPtr for storing function references in native structs; invoke via `Runtime::call_closure()`

**Benefits**:
- No separate serialization format needed (no TOML, JSON, XML parsers)
- Type-safe — validated at compile time
- Same syntax as code — one language to learn
- Closures in config files enable data-driven behavior (e.g. spell effects, AI actions)

#### Implementation

**Status**: Complete

**Key files**:
- `lib/nw/smalls/runtime.hpp` — `load_config<T>()`, `ScriptString`, `ScriptClosure`
- `lib/nw/smalls/ConfigArena.hpp` — config root management

### Property Sets

Property sets are script-defined components for the "Entity-Component-System" (ECS)-like architecture. They allow game data layout to be defined in script while memory is managed by the engine's object manager.

```smalls
[[propset]]
type CreatureStats {
    hp, max_hp: int;
    str, dex, con, int, wis, cha: int;
    level: int;
    xp: int;
};

[[propset]]
type Position {
    x, y, z: float;
    area_id: int;
};
```

**Key Characteristics**:
- **Schema in script** — Data layout defined using Smalls type system
- **Memory in engine** — Object manager allocates components in contiguous chunks for cache efficiency
- **C++-compatible layout** — Memory matches C struct layout for zero-overhead access

#### Property Sets: v1 Contract (Normative)

**Contract (v1)**: `[[propset]]` storage is **not GC-rooted**.
To keep propsets invisible to GC, propset fields MUST NOT store `HeapPtr` values.

**Allowed Field Forms (v1)**:
| Field form | Allowed | Storage | Reassignable | Indexable |
|---|---:|---|---:|---|
| `int`, `float`, `bool` | Yes | Inline POD bytes | Yes | N/A |
| `int[N]`, `float[N]`, `bool[N]` (N > 0) | Yes | Inline POD bytes | Yes | Yes (variable index) |
| `[[value_type]]` struct (no heap references) | Yes | Inline POD bytes | Yes | N/A |
| `T[N]` where T is `[[value_type]]` POD struct | Yes | Inline POD bytes | Yes | Yes (variable index) |
| `array!(int\|float\|bool)` | Yes | Inline `TypedHandle` to unmanaged `IArray` | No | Yes (via IArray API) |
| All other forms | **No** | N/A | N/A | N/A |

**Explicitly Rejected**:
- `string` (requires GC pointer)
- `array!(string)` or `array!(non-primitive)`
- `map!(...)`, `tuple`, `sum`, `function`
- Any heap-backed handle type
- Regular structs (not marked `[[value_type]]`)
- `[[value_type]]` structs containing heap references (strings, arrays, maps, etc.)

**Lifecycle Rules**:
1. Propset-owned unmanaged arrays are created by engine/runtime and destroyed deterministically when the owning object is destroyed.
2. Script aliases to those arrays become stale after owner destruction; operations fail with a deterministic runtime error.
3. Because propset storage has no GC pointers in v1, propset slots are excluded from GC root enumeration.

**Fixed Array Semantics**:
- Fixed arrays `T[N]` in propsets are stored inline (contiguous POD bytes) matching C struct layout.
- Full indexing supported: both constant (`arr[0]`) and variable (`arr[i]`) indices.
- Bounds checking enforced at runtime; out-of-bounds access fails deterministically.
- Assignment to array field replaces the entire inline array (not element-by-element).

**Dynamic Array Semantics**:
- Dynamic array fields `array!(T)` are non-reassignable; the field binding is immutable.
- Mutation is via IArray API (`push`, `set`, `clear`, etc.).
- Direct assignment (`ps.arr = other_arr`) is a compile-time error.

#### Native Property Sets

For engine-managed data or functionality not yet in script, native property sets use bridge functions:

```smalls
[[native]] fn has_feat(obj: object, feat: Feat): bool;
[[native]] fn add_feat(obj: object, feat: Feat);
```

**Native vs Script Property Sets**:
- **Native propsets** — Always exist for engine systems (physics, rendering, pathfinding)
- **Script propsets** — Gameplay data (stats, skills, etc.)
- **Bridge pattern** — Native functions provide interface to native propsets

### Native Interfaces

Native interfaces bind C++ code to scripts for RPG game scripting. This is **not a general-purpose binding library**. Native interfaces are not modules in themselves — there is always a script module (of the same name) which "links" and is validated against the native interface.

#### Design Philosophy

- **Constrained scope** — Only expose what's needed for RPG game scripting
- **Purpose-built** — Not trying to be pybind11 or handle arbitrary C++ code
- **Types bend to the binder** — C++ APIs should adapt to the binding system, not vice versa
- **Keep it simple** — Generational handles + ownership modes + mark-sweep GC

#### Handle System

Native types use **generational handles** to reference C++ objects without copying. `TypedHandle` is a 64-bit generational index with embedded type (32-bit id, 8-bit type tag, 24-bit generation). The generation prevents ABA problems — stale handles are detected, not reused.

Typed handle wrappers (e.g., `EffectHandle`) provide type safety and metadata at the C++ API boundary, converting to `TypedHandle` for engine lookups.

**Heap Layout**: Handle values on the VM heap are just `[ObjectHeader | TypedHandle]`.

**Runtime Registry**: A `HandleEntry` per engine handle tracks `vm_value` (HeapPtr) and `OwnershipMode` (VM_OWNED, ENGINE_OWNED, BORROWED).

**Key APIs**:
- `alloc_handle(TypeID)` — allocate typed handle cell on VM heap
- `intern_handle(TypedHandle, OwnershipMode)` — get or create heap cell, set ownership
- `enumerate_handle_roots(visitor)` — root ENGINE_OWNED/BORROWED handle cells during GC marking
- `register_handle_destructor(TypeID, callback)` — invoked when VM_OWNED handle is swept

**Key properties**:
- Script-visible handle values are just `TypedHandle` stored on the VM heap
- Handles never reused (generation prevents ABA problem)
- No raw pointers passed to scripts — only handles

***See [VII.GC Integration Points]***

#### Registration API

**C++ Side** — Register native module interface:
```cpp
ModuleBuilder mb(&runtime, "core.effects");
mb.value_type<glm::vec3>("vec3");
mb.native_struct<Position>("Position")
    .field("x", &Position::x)
    .field("y", &Position::y)
    .field("z", &Position::z);
mb.handle_type("Effect", RuntimeObjectPool::TYPE_EFFECT);
mb.function("create_damage", &create_damage_effect);
mb.finalize();
```

**Script Side** — Link to native module with `[[native]]` declarations:
```go
// core/effects.smalls
[[native]] type Position { x, y, z: float; };
[[native]] type Effect;

[[native]] fn create_damage(amount: int): Effect;
[[native]] fn apply(effect: Effect): void;

// Can mix native and script-side code
fn create_and_apply_damage(amount: int) {
    const dmg = create_damage(amount);
    apply(dmg);
}
```

- Function signatures must match exactly (parameter count, types, return type)
- Script modules can mix native declarations with regular script code
- Handle types are opaque references

**Ownership modes**:
- **VM_OWNED** — Created by scripts, can be GC'd (e.g., `create_effect()`)
- **ENGINE_OWNED** — Engine manages lifetime, never GC'd (e.g., GameObject)
- **BORROWED** — Temporary reference, never GC'd (e.g., iterator)

#### Implementation

**Status**: Complete

**Key files**:
- `lib/nw/smalls/runtime.hpp` — `TypedHandle`, `HandleEntry`, `intern_handle()`, `alloc_handle()`
- `lib/nw/smalls/runtime.cpp` — `ModuleBuilder`, registration API

---

## V. Compilation Pipeline

### Pipeline Overview

Smalls compiles scripts through a multi-stage pipeline:

```
Source → Lexer → Parser → Resolver → Compiler → Bytecode → [Verifier]
         │         │         │            │          │           │
       Tokens    AST    Typed AST    BytecodeModule  │    Validation
                                     (instructions,  │    (optional)
                                      constants,     │
                                      functions)     │
                                                     ↓
                                              VirtualMachine
```

Each stage is a separate component with well-defined inputs and outputs. The pipeline is designed for fast compilation (important for hot-reloading).

### Lexer & Parser

The **Lexer** (`Lexer.hpp`) performs tokenization: converts source text into a stream of `Token` values. It handles string literals, f-string splitting, numeric literals, identifiers, keywords, operators, and comments. It builds a `line_map` for source location tracking.

The **Parser** (`Parser.hpp`) performs recursive-descent parsing: converts the token stream into an AST (`Ast`). It handles all expression precedence levels (from assignment down to primary), all statement forms (blocks, if/else, for, foreach, switch), and all declarations (functions, structs, sum types, newtypes, imports, variables). The parser has a configurable error limit (default 20) and synchronization for error recovery.

#### Implementation

**Status**: Complete

**Key files**:
- `lib/nw/smalls/Lexer.hpp` — `Lexer` struct, tokenization
- `lib/nw/smalls/Parser.hpp` — `Parser` struct, recursive-descent parsing
- `lib/nw/smalls/Ast.hpp` — AST node types
- `lib/nw/smalls/Token.hpp` — `Token`, `TokenType` definitions

### Semantic Analysis (Resolver)

The resolver (`AstResolver`) orchestrates three phases that transform a raw AST into a fully typed, validated AST:

**Phase 1: Name Resolution** (`NameResolver`):
- Resolves `import` and `from ... import` declarations
- Triggers recursive loading of dependent modules
- Populates scope with imported symbols

**Phase 2: Type Resolution** (`TypeResolver`):
- Resolves type annotations to `TypeID` values
- Performs type inference (variable initializers, return types, lambda captures)
- Type-checks all expressions and statements
- Resolves generic instantiation — infers type arguments at call sites and triggers monomorphization
- Detects and registers operator aliases (`[[operator(...)]]`)
- Resolves intrinsic function declarations
- Manages closure capture analysis (upvalue detection)

**Phase 3: Validation** (`Validator`):
- Validates `break`/`continue` usage (must be inside loops)
- Checks switch exhaustiveness for sum types
- Validates basic switch cases (no duplicate labels)
- Validates operator consistency (e.g., if `eq` is defined, `hash` should be too)
- Validates map key allowlist policy (`int`/`string` and newtypes over `int`/`string`); rejects `float`, `bool`, handles, arrays, maps, functions/closures, structs/sums, and other user-defined categories as map keys

#### Implementation

**Status**: Complete

**Key files**:
- `lib/nw/smalls/AstResolver.hpp` — `AstResolver` orchestrator, scope management, type resolution
- `lib/nw/smalls/NameResolver.hpp` — `NameResolver`, import resolution
- `lib/nw/smalls/TypeResolver.hpp` — `TypeResolver`, type inference and checking
- `lib/nw/smalls/Validator.hpp` — `Validator`, exhaustiveness, consistency checks

### Constant Folding (CTFE)

The `AstConstEvaluator` performs compile-time function evaluation (CTFE), folding constant expressions into values during resolution. This reduces runtime work and enables constant propagation.

**What it folds**:
- **Primitives**: integer, float, boolean, string literals
- **Struct literals**: constant brace-init expressions (both positional and named-field)
- **Tuple literals**: constant tuple construction
- **F-strings**: when all interpolated expressions are constant
- **Binary/comparison/logical expressions**: using `Runtime::execute_binary_op()`
- **Unary expressions**: using `Runtime::execute_unary_op()`
- **Cast expressions**: int↔float, int↔bool, newtype wrapping/unwrapping
- **Type test expressions** (`is`): resolved at compile time when operand type is known
- **Conditional expressions**: when the condition is constant, only the taken branch is evaluated
- **Identifier resolution**: follows `const` variable initializers to their values
- **Tuple indexing**: constant index into constant tuple
- **Path expressions**: field access on constant struct values

The evaluator is invoked by the resolver for expressions marked `is_const_`. Foldable types are checked recursively (a struct is foldable if all its fields are foldable types).

#### Implementation

**Status**: Complete — The evaluator computes constant values at resolve time. The compiler (`AstCompiler`) consults `AstConstEvaluator` directly at code-generation time via `try_emit_const()`, emitting a single constant-load instruction for any expression marked `is_const_` instead of the full sub-expression sequence.

**Key files**:
- `lib/nw/smalls/AstConstEvaluator.hpp` — `AstConstEvaluator` visitor, `is_foldable_type()`
- `lib/nw/smalls/AstCompiler.cpp` — `try_emit_const()`, `const_is_truthy()`, DCE in control flow
- `lib/nw/smalls/runtime.hpp` — `execute_binary_op()`, `execute_unary_op()` used by CTFE

### Compiler

The `AstCompiler` performs single-pass AST → bytecode compilation. It visits the typed AST and emits instructions into a `BytecodeModule`.

**Key characteristics**:
- **Linear register allocation**: Simple bump allocator within 256-register frames, with a free-list for register reuse
- **Jump patching**: Control flow emits placeholder jumps, patched when targets are known
- **Expression compilation**: Leaves result in a target register
- **Statement compilation**: Emits instructions with no result register
- **Lambda compilation**: Separate function compilation with upvalue descriptor emission
- **Generic instantiation**: `compile_instantiated()` compiles monomorphized generic functions on demand
- **Constant folding**: `try_emit_const()` is called at the top of every foldable expression visitor; on success a single LOADI/LOADK/LOADB replaces the full sub-expression sequence
- **Dead code elimination (DCE)**:
  - `if`/ternary with a constant condition compiles only the taken branch — no jump instructions emitted
  - `&&`/`||` with a constant LHS compiles only the necessary branch (short-circuit without a runtime jump)
  - `for` with a constant-false condition compiles only the init clause and skips the body entirely
  - `BlockStatement` stops compiling after the first unconditional terminator (`return`/`break`/`continue`) via `block_terminated_`

#### Implementation

**Status**: Complete

**Key files**:
- `lib/nw/smalls/AstCompiler.hpp` — `AstCompiler`, `RegisterAllocator`, `ControlScope`
- `lib/nw/smalls/Bytecode.hpp` — `BytecodeModule`, `CompiledFunction`, `Instruction`, `Opcode`

### Bytecode Verifier

The `verify_bytecode_module()` function validates compiled bytecode post-compilation. It checks:
- Register indices are within bounds for each function's register count
- Jump targets are within instruction bounds
- Constant pool indices are valid
- Function call argument counts are consistent

The verifier is a safety net — it catches compiler bugs that would otherwise cause VM crashes. It returns `false` and fills an error message on invalid bytecode.

#### Implementation

**Status**: Complete

**Key files**:
- `lib/nw/smalls/BytecodeVerifier.hpp` — `verify_bytecode_module()` declaration
- `lib/nw/smalls/BytecodeVerifier.cpp` — per-instruction validation logic

### Tooling (LSP Support)

Smalls provides several AST visitor utilities for IDE integration:

**AstHinter** — Generates inlay hints (parameter name annotations at call sites). Walks the AST within a source range and produces `InlayHint` objects (message + position) for function call arguments.

**AstLocator** — Locates symbols in source for go-to-definition, hover, and reference tracking. Given a symbol name and cursor position, finds the declaration and produces a `Symbol` result with kind (variable, function, type, param, field), type information, and source view.

**Symbol** — Represents a located symbol with `node`, `decl`, `comment`, `type`, `kind`, `provider` (which script it's from), and `view`.

**CompletionContext** — Collects completion candidates by aggregating symbols from the current scope, module exports, and dependencies.

**SignatureHelp** — Provides function signature information at call sites, including the active parameter index for cursor-position-aware help.

**Script API** — The `Script` class exposes `locate_symbol()`, `complete_at()`, `complete_dot()`, `inlay_hints()`, and `signature_help()` methods for LSP integration.

#### Implementation

**Status**: Partial — Core infrastructure (symbol location, completion, inlay hints, signature help) is implemented. Remaining work: go-to-definition for sum type variants and their members (`SumDecl`/`VariantDecl` in `AstLocator`), rename symbol, find-all-references, semantic tokens, and diagnostics push.

**Key files**:
- `lib/nw/smalls/AstHinter.hpp` — `AstHinter` visitor, `InlayHint` generation
- `lib/nw/smalls/AstLocator.hpp` — `AstLocator` visitor, symbol location
- `lib/nw/smalls/Smalls.hpp` — `Script`, `Symbol`, `CompletionContext`, `SignatureHelp`, `InlayHint`

---

## VI. Virtual Machine

### Architecture Overview

Smalls uses a register-based VM following Lua's design philosophy: **256 registers per frame, fixed-width 32-bit instructions, single-pass compilation, switch-based dispatch**.

### Instruction Encoding

**32-bit fixed width:**
```
Bits 31-24: Opcode (8 bits)
Bits 23-16: Register A (8 bits)
Bits 15-8:  Register B (8 bits)
Bits 7-0:   Register C (8 bits)
```

**Alternative encodings:**
- **ABx format**: Opcode + RegA + 16-bit immediate (LOADK, LOADI)
- **Jump format**: Opcode + 24-bit signed offset (JMP, JMPT, JMPF)

### Core Components

**BytecodeModule** — Compiled script container: instruction sequence, constant pool (int, float, string), function metadata (param count, register count), debug info (source locations, optional by debug tier).

**VirtualMachine** — Bytecode interpreter: shared register file (256 Values per frame), call stack (CallFrame array with PC, base register, return register), fetch-decode-execute loop (switch on opcode).

### Instruction Set

**Arithmetic & Logic:** ADD, SUB, MUL, DIV, MOD, NEG, NOT, AND, OR

**Comparisons:** EQ, NE, LT, LE, GT, GE

**Test and Skip:** ISEQ, ISNE, ISLT, ISLE, ISGT, ISGE (skip next instruction if condition true)

**Constants & Moves:** LOADK, LOADI, LOADB, MOVE, LOADNIL

**Memory:** GETFIELD, SETFIELD, GETTUPLE, GETARRAY, SETARRAY, NEWSTRUCT, NEWTUPLE, NEWARRAY, NEWMAP, MAPGET, MAPSET, NEWSUM, SUMINIT, SUMGETTAG, SUMGETPAYLOAD, GETGLOBAL, SETGLOBAL

**Optimized Field Access:** FIELDGETI/FIELDSETI (int), FIELDGETF/FIELDSETF (float), FIELDGETB/FIELDSETB (bool), FIELDGETS/FIELDSETS (string), FIELDGETO/FIELDSETO (object), FIELDGETH/FIELDSETH (heap) — each with immediate and register-indexed variants

**Stack Value Types:** STACK_ALLOC, STACK_COPY, STACK_FIELDGET/SET, STACK_INDEXGET/SET

**Control Flow:** JMP, JMPT, JMPF, CALL, CALLNATIVE, CALLEXT, CALLEXT_R, CALLINTR, CALLINTR_R, RET, RETVOID

**Type Operations:** CAST, TYPEOF, IS

**Closures:** CLOSURE, GETUPVAL, SETUPVAL, CLOSEUPVALS, CALLCLOSURE

**Module Globals:** GETGLOBAL, SETGLOBAL

### Key Design Decisions

**Register-based (not stack-based):**
- Fewer instructions per operation (no stack shuffling)
- Simpler register allocation (linear bump allocator)
- Better compiler optimizations (explicit data flow)

**Fixed 256 registers:**
- Fits in 8-bit operands (no multi-byte register indices)
- Sufficient for all realistic functions (most use < 32)

**Single-pass compilation:**
- No IR layer (AST → bytecode directly)
- Fast compilation (important for scripts loaded at runtime)

**Reentrant execution:**
- VM tracks entry frame depth on each `execute()` call
- Only resets VM state on top-level calls (when no frames active)
- Execution stops when returning to entry depth (preserves outer frames)
- Script → Native → Script call chains work correctly
- Frame cleanup on execution failure (prevents register file corruption)

**Safe arithmetic:**
- Division/modulo by zero checks in both VM and Runtime layers
- Checks performed before operation (prevents SIGFPE hardware exception)
- Returns VM error with descriptive message instead of process crash

### Integration Points

**Operator dispatch:** Two-tier execution:
1. **Inline fast paths** in the VM for primitive types (int, float, bool, string). Arithmetic, comparison, logical, and test-and-skip opcodes check operand types directly and execute without function call overhead.
2. **CALLEXT for user types**: The compiler detects script-defined operators at compile time and emits direct CALLEXT instructions.
3. **Slow path fallback**: `Runtime::execute_binary_op()`/`execute_unary_op()` for mixed types, coercions, and CTFE.

***See [III.Operator Aliasing]***

**Memory allocation:** VM calls runtime helpers (`alloc_struct()`, `alloc_array()`, `alloc_string()`). All allocations live on ScriptHeap.

***See [VII.Memory Management]***

**Field access:** VM uses runtime helpers with type metadata (`read_struct_field_by_index()`, `write_struct_field_by_index()`). Type safety enforced by resolver.

**Bytecode caching:** Runtime caches compiled bytecode per script to avoid recompilation overhead.

**Error reporting:** `ExecutionResult` contains: `value` (return value), `failed` flag, `error_message`, `stack_trace`, `error_module`, `error_location`, and `error_snippet`. Error sources: compilation failures, module loading failures, VM runtime errors, native exceptions.

**Debug tiers:** Runtime diagnostics are configurable:
- **none**: no debug locations or snippets
- **source_map**: bytecode locations + on-demand source lines
- **full**: retains AST and full source text

#### Implementation

**Status**: Complete

**Key files**:
- `lib/nw/smalls/VirtualMachine.hpp` — `VirtualMachine`, `CallFrame`, dispatch loop
- `lib/nw/smalls/Bytecode.hpp` — `Opcode` enum, `Instruction`, `BytecodeModule`, `CompiledFunction`
- `lib/nw/smalls/runtime.hpp` — `ExecutionResult`, `Value`, operator dispatch

---

## VII. Memory Management

### GC Overview

Smalls uses a **Luau-style incremental, generational, non-moving garbage collector**. This design prioritizes stable memory addresses (critical for FFI) while still achieving generational benefits.

### Design Philosophy

- **Non-moving**: Objects never relocate after allocation. HeapPtr offsets remain stable for the lifetime of the object, enabling safe C++ interop without pinning.
- **Generational**: Objects are tagged as young or old via a flag in the ObjectHeader, not physically separated into different memory regions. Promotion simply flips the flag.
- **Incremental**: Tri-color marking (white/gray/black) spreads work across allocations to minimize pause times.
- **Write barriers**: Card marking tracks old→young references to enable efficient minor collections.

### Heap Layout (ObjectHeader)

Each heap allocation includes an ObjectHeader with GC metadata: `alloc` (allocation handle), `type_id`, mark color (2 bits: white/gray/black), generation (1 bit: young/old), age (4 bits: survival count 0–15), `next_object` (linked list for sweep), and `alloc_size`.

Objects are linked via `next_object` to enable efficient sweep-phase iteration.

### Collection Algorithms

**Minor Collection** (young generation only):
1. Mark roots (VM registers, stack, upvalues)
2. Scan dirty cards for old→young references
3. Process gray stack (trace young objects only)
4. Promote survivors (age++, flip generation flag if age >= threshold)
5. Sweep unreachable young objects
6. Clear dirty cards

**Major Collection** (full heap):
1. Reset all marks to white
2. Mark roots
3. Process entire gray stack (trace all objects)
4. Sweep all unreachable objects
5. Clear dirty cards

### Triggering & Scheduling

Allocation *may* trigger GC based on `GCConfig` thresholds — this is the mechanism, not a contract. The engine controls timing indirectly by tuning `GCConfig`, and can trigger collections explicitly via `collect_minor()`/`collect_major()` for testing.

1. **Minor GC**: When young generation exceeds threshold (default 256KB)
2. **Major GC**: When old generation exceeds threshold (80% of committed heap)
   - Major GC is **incremental**: marking work spreads across allocations
   - Each allocation does a small amount of marking work (default 100 objects)
   - Sweep happens when marking completes

### Write Barriers

Write barriers are inserted at all pointer-store sites to maintain the card table. When target is old and stored value is young, the card table is marked dirty.

Barrier sites include:
- Struct field writes (`write_struct_field_by_index`, `write_field_at_offset`)
- Array element writes (`array_set`)
- Map key/value writes (`map_set`)
- Tuple element writes (`write_tuple_element_by_index`)
- Sum payload writes (`write_sum_payload`)
- Upvalue writes (SETUPVAL opcode)

### Type-Specific Tracing

The GC traces heap references using type metadata:

| Type Kind | Tracing Method |
|-----------|----------------|
| `TK_struct` | Uses `StructDef::heap_ref_offsets` array |
| `TK_tuple` | Iterates `TupleDef::element_types` |
| `TK_array` | Iterates elements if element type is heap type |
| `TK_map` | Iterates key-value pairs |
| `TK_function` | Traces closure upvalues |
| `TK_sum` | Reads tag, traces only active variant payload |

The `contains_heap_refs` flag on `Type` enables fast skipping of types with no heap references.

**The `contains_heap_refs` Invariant**: This runtime invariant must be correctly maintained for GC correctness. Rules:
- **Primitives** (`int`, `float`, `bool`): `false`
- **Heap types** (strings, arrays, maps, closures): `true`
- **Structs/tuples**: `true` if ANY field/element `contains_heap_refs`
- **Sum types**: `true` if ANY variant payload `contains_heap_refs`
- **Fixed arrays** (`T[N]`): inherits from element type
- **Newtypes/aliases**: inherits from underlying type

Computed by `TypeTable::compute_contains_heap_refs()` during type registration and never changes.

### Configuration

GC behavior is configurable via `GCConfig`: `young_threshold` (default 256KB), `promotion_threshold` (default 2 survivals), `major_threshold_percent` (default 0.8), `incremental_work_budget` (default 100 objects per mark step).

### Statistics

`GCStats` tracks: `minor_collections`, `major_collections`, `objects_freed`, `bytes_freed`, `total_pause_time_us`, `max_pause_time_us`.

### GC Integration Points

**Closures and upvalues**: Closure's `upvalues` vector is traced. Closed upvalues (value copied to `Upvalue::closed`) are traced. Open upvalues are reachable via VM register file.

**Native handles**: Handle ownership modes interact with GC:
- **VM_OWNED**: Created by scripts, eligible for collection. When swept, removed from registry and optional destructor invoked.
- **ENGINE_OWNED**: Engine manages lifetime, treated as GC roots (never collected).
- **BORROWED**: Temporary reference, treated as GC roots (never collected).

***See [IV.Native Interfaces]***

#### Implementation

**Status**: Complete

**Key files**:
- `lib/nw/smalls/GarbageCollector.hpp` — `GarbageCollector`, `GCConfig`, `GCStats`, `ObjectHeader`
- `lib/nw/smalls/ScriptHeap.hpp` — `ScriptHeap`, offset-based allocation
- `lib/nw/smalls/runtime.hpp` — `scan_value_heap_refs()`, `enumerate_handle_roots()`
- `lib/nw/smalls/runtime.cpp` — write barrier insertion, root enumeration

---

## VIII. Roadmap

### Migration Strategy

**Current Phase:**
1. C++ structs loaded from GFF/JSON files
2. Native property sets for engine systems
3. Script types defined but not yet used for data

**Medium-term:**
1. Write converters: GFF/JSON → `.smalls` files with struct literals
2. Dual loading: Keep C++ loaders, add script config loading
3. Property sets with value types only
4. Native bridge functions for complex collections

**Long-term:**
1. Remove C++ data loaders
2. All game data in `.smalls` config files
3. Property sets with heap-allocated collections
4. Native property sets only for engine integration (physics, rendering, etc.)
5. Gameplay logic entirely in script (combat, AI, dialogue, etc.)

### Future Language Features

- Explicit export control for modules (currently everything is exported)
- Explicit generic instantiation syntax for functions
- Constraint syntax for type parameters (e.g., `where $T: Comparable`)
- `set!(T)` collection type
- String length limits and array size limits (currently TBD)
