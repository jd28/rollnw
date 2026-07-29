# nw Library Compiler Warnings

Resolved 2026-07-29. `set_project_warnings(nw OWN_SOURCES)` now applies the
whole project warning set to `nw`'s own translation units, and a clean build of
the tree emits nothing outside googletest's own headers.

## What it started as

`set_project_warnings` applied its options with `INTERFACE` only, which reaches
a target's consumers and never its own translation units. `nw` warned everything
that linked it and never itself: 155 of its 198 translation units carried no
`-W` flags at all, including all 38 in `lib/nw/smalls`. Enabling the set
surfaced 1,017 warnings.

## Three real defects

The warnings were not only style. Each of these was a live bug:

- `-Woverloaded-virtual`: visitors deriving from `NullVisitor` declared a subset
  of the `visit` overloads, hiding the rest. `Validator` never entered an
  expression as a result, so a lambda body was never validated -- `break` inside
  a lambda compiled silently, and an enclosing loop was treated as if it applied
  across the lambda boundary.
- `-Wlogical-op`: `NssLexer` bounded an octal literal with
  `cur >= '0' || cur <= '7'`, true for every character, so an octal literal
  consumed the rest of the buffer.
- `-Wconversion`: `Palette` writes a node's faction as a JSON string but read it
  with `get<int>()`, which throws on a string, so that path could not read
  anything the writer had produced.

## Two deliberate suppressions

- `-Wpedantic` is off for `smalls/VirtualMachine.cpp`. All 492 of its hits were
  the computed-goto dispatch, a deliberate optimization behind a
  `__GNUC__`/`__clang__` guard with a switch fallback for MSVC.
- `-Wuseless-cast` is off for `nw`. The flag resolves typedefs on the compiling
  platform, so what it calls redundant here can be a real widening on Windows,
  where `size_t` is `unsigned long long` rather than `unsigned long`. Casts that
  are type-identical everywhere were still removed; width-dependent ones were
  left, including the `CHECK_RANGE` macros where `size` is a macro parameter.
- `gfx/backends/vulkan/vulkan_all.cpp` suppresses several warnings because it
  compiles the vendored `vk_mem_alloc.h`.

## Coverage

333 of 334 first-party translation units compile with the set. The one
exception is a generated precompiled-header source for the vendored glslang,
under `build/`.

`nw-gfx` was the last gap: it does not link `nw`, so it inherited nothing, and a
`set_source_files_properties` call meant for it sat in `lib/nw/CMakeLists.txt`
where its source is not declared, making it a silent no-op. It now carries the
set itself. Its single translation unit exists to compile vendored
implementations -- VulkanMemoryAllocator, glslang, SPIRV-Reflect -- and is
silenced, since 224 of the 225 warnings it reports come from `vk_mem_alloc.h`.

`set_project_warnings` grew a `NO_INTERFACE` option for that target. Without it
the INTERFACE half propagated to consumers that compile vendored imgui sources,
which turned a clean build into 3,117 warnings.

## Lesson recorded

Renaming a shadowed variable is not mechanical. Where the outer name still
exists, a partial rename compiles cleanly and silently rebinds to the outer
variable. That happened three times here: twice in `complete_dot`-style loops
where an `else` branch still referenced the old name, caught by the test suite,
and once in `TwoDA` where the row buffer print reverted to the outer buffer,
caught only by grepping each renamed scope for leftovers. Audit every renamed
scope; the compiler cannot.
