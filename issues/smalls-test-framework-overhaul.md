# Smalls Test Framework Overhaul

## Data

Smalls tests currently live in three different shapes:

- C++ gtest cases that embed Smalls source strings for parser, resolver,
  runtime, native binding, propset, and engine integration behavior.
- Script corpus modules under `lib/nw/smalls/scripts/tests/`, each with a
  `main()` function that calls `core.test.test(name, bool)`.
- C++ discovery glue in `tests/smalls_corpus.cpp` that manually lists every
  `tests.*` module and executes `main()`.

That gets coverage, but the test unit is often the wrong data shape. A local
language rule or stdlib helper becomes a whole module, a C++ string, or a manual
entry in a C++ array. Failures do not naturally report source span, test block,
module, setup cost, or expected failure mode.

Common case:

- a small pure-Smalls behavior check near the function, operator, propset, or
  config rule it validates
- no engine object setup
- no custom C++ fixture
- assertion failure should identify module, test name, file, and line

Larger cases still exist:

- native binding layout/link checks
- engine integration tests that need kernel services and object fixtures
- corpus/regression tests for many input files
- compile-fail and diagnostics tests

## External Reference Shape

- D has module-local `unittest { ... }` blocks that can sit next to the code
  they test and are only run when unit tests are enabled:
  <https://dlang.org/spec/unittest.html>
- Rust uses named `#[test]` functions discovered by the test runner, plus
  documentation tests for examples:
  <https://doc.rust-lang.org/book/ch11-01-writing-tests.html>
  <https://doc.rust-lang.org/rustdoc/write-documentation/documentation-tests.html>
- Odin uses `@(test)` procedures run by `odin test`, and `#+test` files that are
  ignored except during test builds:
  <https://odin-lang.org/docs/overview/>

The useful lesson is not to copy one syntax. The useful data contract is:
discovered test rows with name, module, source span, expected mode, and an
executable body.

## Direction

Use D-style locality as the authoring model and Rust/Odin-style discovered test
entries as the runtime model.

Target authoring shape:

```smalls
fn damage_bonus(damage_type: Damage, dice: int, sides: int, bonus: int, category: DamageCategory): ItemEffectRow {
    return { /* ... */ };
}

test "damage_bonus row layout" {
    var row = damage_bonus(Damage(1), 2, 6, 0, DamageCategory(0));
    assert(row.op == item_effect_op_damage_bonus);
    assert(row.value0 == 2);
    assert(row.value1 == 6);
}
```

Compiler/runtime lowering:

- Parse test blocks as module-level declarations.
- Lower each test block to a hidden zero-argument function plus a `ScriptTest`
  row: module name, test name, source file, source span, flags, function id.
- Resolve and compile test functions only when test mode is enabled.
- Production module load should not register or execute tests.
- The runner consumes a flat array of `ScriptTest` rows. A single test is a
  batch of size one.

This makes inline tests cheap to write while keeping the execution protocol
flat, filterable, and reportable.

## First Slice

Do not start with property testing, fuzzing, snapshots, doctests, or a new
general test DSL.

Implement the smallest row protocol first:

- `ScriptTest` metadata row in the runtime: module, name, source span, flags,
  hidden function id.
- Runtime option to include tests when loading modules.
- `Runtime::module_tests(module)` or equivalent read-only view of discovered
  rows.
- `Runtime::execute_test(row)` or equivalent runner path that reports pass/fail
  and error text.
- C++ gtest that loads one module with two passing inline tests and one failing
  inline test, proving discovery and source-span reporting.

If adding a new parser form is too large for the first patch, use the existing
annotation machinery as a temporary bridge:

```smalls
[[test]]
fn damage_bonus_row_layout() {
    /* assertions */
}
```

That bridge should lower to the same `ScriptTest` rows. The D-style `test
"name" { ... }` block can then be syntax sugar over the proven data path.

## Follow-On Slices

- Convert `lib/nw/smalls/scripts/tests/*.smalls` from `main()` plus
  `core.test.test(name, bool)` calls into discovered test rows.
- Remove the manual module list from `tests/smalls_corpus.cpp`; discover test
  modules from package paths or a manifest.
- Add compile-fail tests as explicit rows with expected diagnostic code/span.
- Add `#+test`-style test-only files if we need heavy fixtures that should not
  parse or resolve during normal module loading.
- Add doc/example tests only after inline tests have stable source-span and
  failure reporting.
- Keep native/engine integration tests in C++ until they can be expressed as
  data fixtures without hiding setup cost.

## Contracts

- Test discovery output is a flat row array.
- Test execution does not mutate production module exports.
- A failing assertion reports module, test name, file, line, and message.
- Test mode is explicit. Normal runtime load does not execute tests.
- Out-of-range behavior:
  - duplicate test names in one module: fail test-mode load
  - invalid test function signature: fail test-mode load
  - test block using unavailable native bindings: fail test-mode load
  - failed assertion: test failure, not module load failure
  - runtime panic/error inside test: test failure with stack/source span

## Cost

Paid platform: developer machines and CI test runs.

- Parser/resolver cost: one new declaration kind or one existing annotation path.
- Runtime memory cost in test mode: one metadata row per discovered test plus
  compiled test bytecode.
- Production cost should be zero beyond parsing tokens already needed to load the
  module. If test blocks make production parsing meaningfully slower, add
  test-only file gating before moving large corpora inline.
- Maintenance cost: one runner protocol and one assertion/reporting path instead
  of C++ strings, manual corpus lists, and ad hoc `main()` test modules.

## Done Criteria

- Smalls can list tests in a module without executing them.
- Smalls can execute one named test or all tests in a module.
- A failure reports module, test name, source path, and line.
- Existing `tests.*` corpus can migrate without losing test count.
- CTest can still report individual failures usefully.
- Normal non-test module loading does not run test code.

## Do Not Carry Forward

- Manual C++ arrays listing every Smalls test module.
- Whole-module `main()` as the only script test unit.
- Test-only imports in production modules unless test mode is enabled.
- Framework features before the basic row protocol is proven.
