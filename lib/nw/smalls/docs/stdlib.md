# Standard Library

The standard library ships as file-based modules under `lib/nw/smalls/scripts/core/`.

## core.prelude

- `print(message: string)`
- `println(message: string)`
- `assert(cond: bool)`
- `error(message: string)`
- `panic(message: string)`
- `gc_collect()` (debug/testing)

## core.string

Intrinsics and native helpers for strings.

- `format(fmt: string, args: array!(string)): string`
- plus `len`, `substr`, `split`, `join`, case conversion, trimming, etc.

## core.array

- Intrinsics: `push`, `pop`, `len`, `get`, `set`, `clear`, `reserve`
- Helpers: `map`, `filter`, `reduce`, `sort`

## core.map

- Intrinsics: `len`, `get`, `set`, `has`, `remove`, `clear`
- Helpers: `has_key`, `keys`, `values`

## core.math

- `sin(x: float): float`
- `cos(x: float): float`
- `sqrt(x: float): float`
- `abs(x: float): float`

## core.effects

- Handle-backed `Effect` and minimal native API (`create`, `apply`).
