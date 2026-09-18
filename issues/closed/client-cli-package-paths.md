# Client kernel package paths depend on working directory

Status: closed 2026-09-18; implemented and verified on Linux.

Original observations and the pre-implementation plan are preserved below.
Completion evidence and remaining platform/interaction limits follow the plan.

Observed during final CLI validation: running the built client from build/tests
with its copied stdlib/nwn1 and the executable's tools/client/stdlib/nwn1 causes
JSON import to exit 1 before importing. Runtime reports multiple directories
providing the selected package nwn1. client_runtime::register_smalls_packages
adds the executable package path, while kernel service creation adds the cwd
stdlib package path. Both registrations existed before final root extraction.
The executable-directory CLI check and invalid arguments are separate gates;
this failure is not counted as a successful import.

Reproduce with the real DockerDemo.mod fixture, a new /tmp project path,
SDL_VIDEODRIVER=client-cli-invalid-driver, NWN_ROOT set to the dedicated-server
nwn directory and NWN_HOME set to build/tests/test_data/user; run
../tools/client/rollnw-client import --json test_data/user/modules/DockerDemo.mod
/tmp/client-cli-path-repro from build/tests. No window is involved.

Before changing policy, decide whether current-directory packages or installed
executable packages are authoritative for this client. Add a failing production
subprocess regression for the chosen policy, then repair only that bootstrap
registration path. Preserve the runtime's duplicate-package rejection for other
callers. Cost: startup filesystem/package resolution and a focused regression;
no new package registry, per-event work or performance claim is warranted.

Repository-root JSON import also aborts on module reload: startup adds executable
packages, load_module shuts down/recreates services, and kernel creation registers
only cwd/stdlib paths, which do not exist there. Exception: selected package nwn1
not found. Both cwd/executable registrations and module-reload policy match source
baseline 6182a1943. Actual JSON/legacy imports and expected 0/1/2 exits passed from
the executable directory, where package paths coincide; arbitrary-cwd support is
not claimed. The smallest fix must make client package roots survive reload without
changing process cwd or adding a global handler registry; characterize both missing
roots and duplicated roots before selecting that policy.


## Implementation plan (2026-09-18)

Tier 1: a bounded bootstrap/configuration change. This plan was recorded before
implementation; completion evidence is recorded below.

### Frame and real data

Make documented imports and repeated project opens independent of launch cwd.
At `a74dea6be`, the real DockerDemo.mod import from repository root aborted with
SIGABRT: selected package `nwn1` missing; no project was created. Capture the
build/tests duplicate-provider failure as a second production regression.

Input: one process configuration and two package directories, core and the
selected profile (currently nwn1). Executable/package location is normally stable;
module loads recreate Runtime storage. Observed cwd cases are repository root,
executable directory, and build/tests. Usage frequencies are unmeasured; ordinary
successful imports and repeated opens are the common supported operations.

Platform: Linux/Windows desktop, one kernel thread, synchronous service recreation,
stdlib beside the executable. CLI imports must work without video/GPU initialization.

ASSUMPTION: executable-relative packages are authoritative for the client —
affects ignoring valid competing cwd copies; other callers retain their default.

ASSUMPTION: packaged files remain available throughout the process — affects
reload success; removed or malformed packages must fail explicitly.

### Patterns and conventions found

Reuse persistent ConfigOptions (`lib/nw/kernel/Config.hpp:12`), client bootstrap
(`tools/client/client_runtime.cpp:130`), and service creation
(`lib/nw/kernel/Kernel.cpp:189`). Runtime already canonicalizes/deduplicates paths
and rejects distinct profile providers (`lib/nw/smalls/runtime.cpp:3228`).
Extend the production SDL_Process fixture (`tests/rollnw_client_application.cpp:20`)
and existing scope_exit configuration restoration
(`tests/rollnw_client_sdl_runtime.cpp:163`). Follow nearby naming/includes and
WebKit clang-format; no new utility, module, or dependency is needed.

### Architecture decision and component design

Add one owned `ConfigOptions::stdlib_path`, defaulting to `"stdlib"` to preserve
existing callers' cwd-relative behavior. Nonempty relative/absolute roots are
valid; reject empty roots during configuration. Existing Runtime checks continue
to report missing/malformed packages and duplicate providers.

Client bootstrap resolves an absolute stdlib root beside the executable once,
before service creation. If SDL_GetBasePath cannot resolve the executable base,
report bootstrap failure rather than silently selecting cwd packages. Unrelated
UI asset search remains outside scope. Services::create registers core and the
selected-profile directory from this configuration on every game generation;
remove the separate client registration helper. Language services stay unchanged.

Config owns the root across reloads; Runtime owns registrations for one generation.
This is true process-singleton configuration feeding a fixed two-path batch.
At the existing import CLI boundary, convert module-load/import std::exceptions
to a diagnostic and exit 1; success remains 0 and invalid arguments remain 2.
Kernel/library exceptions and the blueprint worker boundary retain their policy.

### Implementation map

| File | Change |
| --- | --- |
| `lib/nw/kernel/Config.hpp` / `Config.cpp` | Add/document stdlib_path; reject an empty root. |
| `lib/nw/kernel/Kernel.cpp` | Register both directories from persistent configuration on every game-service creation. |
| `tools/client/client_runtime.cpp` | Set the absolute packaged root; remove duplicate registration. |
| `tools/ui/rml_smalls_bridge.cpp` | Preserve existing kernel configuration; persist roots when the bridge creates services; register only toolset scripts. |
| `tools/client/client_cli.cpp` | Report import/module-load exceptions through the operational-failure exit contract. |
| `tests/rollnw_client_application.cpp` | Real subprocess imports, launch-directory/path semantics, and failure exits. |
| `tests/kernel_load_module.cpp` | Root validity, reload/unload, default compatibility, and distinct-provider rejection. |
| `tests/rollnw_client_rml_smalls_language_binding.cpp` | Existing-kernel and bridge-owned bootstrap, then repeated project opens/reloads. |
| `tools/client/README.md` and this issue | Package policy, evidence, verification limits, and issue closure. |

### Data flow

Executable base -> owned absolute root in ConfigOptions -> Services::create ->
core/profile registrations -> validated Runtime -> module/import output.
Recreation rebuilds registrations from the same configuration. Relative module
and destination arguments, including an omitted destination, still use caller cwd.
Only configuration/Runtime registration and ordinary import output are written;
tests use isolated temporary destinations. Process cwd is never changed.

### Build sequence and verification

1. Add failing production subprocess regressions for repository-root missing
   packages and build/tests duplicate copies. Use absolute real fixture/output
   paths, explicit NWN_ROOT/NWN_HOME, and an invalid SDL video driver.
2. Add scoped configuration/recreation tests: explicit shutdown/start, successful
   and failed module loads, unload, and second load. Verify default stdlib callers,
   empty/missing/malformed roots, and rejection of distinct explicit providers.
3. Implement the persistent root, sole service registration path, client policy,
   and import error boundary. Avoid preserving Runtime pointers or selected-package
   state across generations.
4. Verify JSON imports from repository root, executable directory, build/tests,
   and an unrelated temporary directory using SDL's child cwd property. Check
   manifest, module IFO, CAF, representative blueprint data, and area maps; also
   test relative arguments, omitted destination, spaces, competing cwd packages,
   legacy imports, and operational/usage failure exits.
5. Build rollnw-client/rollnw_test; run affected ClientCli, Kernel,
   ClientProjectImport, ClientBlueprintJobs, ClientSdlRuntime, and Smalls config
   tests, then required CTest because bootstrap is shared. Run clang-format and
   git diff --check; run affected sanitizer checks in the existing build if
   available and report skips.
6. Check a fresh desktop launch and two successive project opens from unrelated
   cwd. Report desktop/GPU and Windows evidence separately from headless Linux
   results. Record evidence here before closing the issue.

### Cost, simplification, and done

Estimate: roughly half a working day including validation. Shared configuration
changes require a rebuild and compatibility checks. Cost is one owned cold path
plus the existing two filesystem registrations per service generation; no new
per-frame work or performance claim. Access is a fixed sequential path batch on
the kernel thread; no pointer-heavy hot path is added.

Simplification: resolve the stable client root once, remove the second registration
pass, and constrain client lookup to packaged directories. No root cache, queue,
lookup table, reload callback, global handler registry, or cwd arbitration is needed.

Done: all directory cases import valid projects without video/GPU; reload/unload
retains roots; user paths keep their meaning; failures exit 1 instead of aborting;
distinct explicit providers still reject; default callers/language services remain
compatible; required checks pass with unverified platforms/interactions disclosed.

A failure with correct persisted roots, default-caller regression, or accepted
ambiguous providers disproves completion. If another bootstrap dependency appears,
retain its concrete regression and scope that dependency rather than broaden
package-selection policy; executable-directory launch remains the interim workaround.

Planning checkpoint self-check: framing, actual data, cost, assumptions, singleton/batch
ownership, explicit errors, reuse, simplification, and completion evidence are
stated. Source and repository-root reproduction were checked; implementation and
its verification were pending at that checkpoint.

### Observed dependency during implementation

The first real desktop check from an unrelated cwd exposed a second bootstrap
authority in `tools/ui/rml_smalls_bridge.cpp`: initial bridge initialization
replaced ConfigOptions with defaults even when client services already existed.
On project replacement the configured root became cwd-relative again. The bridge
also registered independently resolved core/profile paths, which can introduce
competing providers. The desktop process exited normally but neither project was
recorded as successfully opened; script initialization failed after replacement.

Extend the bounded fix to this observed caller. An existing kernel supplies its
configured stdlib root; the bridge must not replace that configuration. When the
bridge creates its own kernel, retain its existing root discovery and store the
resolved root in ConfigOptions before service creation. Service creation owns
core/profile registration; the bridge adds only the toolset scripts path. Keep
toolset asset discovery and the existing generation checks unchanged.

Add a fresh-process regression for configured roots across bridge bootstrap and
two real project opens. Cost: a startup Runtime-presence check, existing script
path resolution/registration, and a focused integration test; no new lifecycle
state, hot path, registry, or dependency. Done also requires this regression and
a repeated desktop check. This amendment removes the observed configuration
overwrite and remaining duplicate core/profile registration work.

## Implementation evidence (2026-09-18)

Implemented the configured stdlib root, sole core/profile registration in service
creation, executable-relative client bootstrap, bridge configuration preservation,
and the import exception boundary. Default kernel callers retain cwd-relative
`stdlib`; language-mode startup is unchanged. The bridge persists its discovered
root when it owns bootstrap and reuses existing configuration otherwise.

Before the fix, the production subprocess regression reproduced SIGABRT from
repository root and unrelated cwd, exit 1 for duplicate providers from build/tests,
and success from the executable directory. The additional fresh-process bridge
regression reproduced ConfigOptions being reset to `stdlib` and two `nwn1`
providers. Both regressions pass after repair.

JSON and legacy imports pass from repository root, executable directory,
build/tests with competing packages, and an unrelated directory. The real
DockerDemo.mod fixture is augmented with the real nw_chicken.utc blueprint to
exercise conversion: native manifest, IFO, CAF with 16 tiles, creature appearance
31/resref nw_chicken, and a valid 128x128 area-map image are checked. Legacy output
files are checked separately. Relative arguments, omitted destinations, literal
spaces/semicolon/dollar characters, unchanged cwd, and operational/usage exits
1/2 pass without video initialization. Success exits 0.

Kernel checks cover empty-root rejection without replacing valid options,
relative/absolute roots, missing/non-directory/malformed package roots,
shutdown/start, successful and failed loads, unload, and another load. Default
roots, canonical-path deduplication, and selection-time rejection of distinct
explicit profile providers remain covered. Bridge checks cover an existing
configured kernel across two project opens and a bridge-owned kernel across reload.

The default CTest run passed all 15 entries (eight test shards, temporary-directory
fixture, build identity, and five tool version checks): 2,219 cases passed,
42 render cases skipped because headless graphics or marker models were unavailable.
The final full-suite run includes the corrected bridge-owned test fixture.

A fresh real desktop launch from unrelated cwd initialized SDL/XWayland and the
client renderer. Temporary SDL event injection entered two successive
`project.open` commands through the real terminal/command/backend path, then quit
with exit 0. Isolated preferences contained both successfully opened project paths;
the initial failing desktop run had recorded neither. This verifies those desktop
operations; it is not user manual acceptance or a native-file-dialog check.

A staged CMake installation and refreshed `bin/rollnw-client` also completed native
imports from unrelated cwd with exit 0 and the invalid video driver. The local
binary uses CMake's installed `$ORIGIN` library path and was replaced atomically.
Dedicated-server fixtures lack one map texture; imports retain the existing
missing-tile markers and report degraded maps. Windows execution is unverified.

Normal and combined ASan/UBSan builds passed. All 45 affected cases passed under
ASan/UBSan without skips or sanitizer diagnostics, using
`ASAN_OPTIONS=abort_on_error=1:detect_leaks=0` and
`UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`. The final default CTest run also
contains all 45 affected cases passing. Changed-range clang-format and
git diff --check passed.

Simplification removed both extra core/profile registration helpers, the bridge's
obsolete kernel-started flag, and its repeated toolset-path resolution. Config is
the true process singleton; service creation transforms its root into the fixed
two-path batch per generation. No hot pointer path, registry, new per-frame work,
speculative option, or unmeasured performance claim was added.

Final self-check: the plan and observed-dependency amendment state actual inputs,
ownership, assumptions, error behavior, and cost. Successful bootstrap/import is
the common path; invalid roots and import failures reject explicitly. Config owns
the singleton root across arena recreation, and registrations form the fixed
two-path batch. The simplification pass removed states and registration work;
no speculative machinery or hot pointer path was introduced. Defined acceptance
gates passed with Windows, native dialogs, and user manual acceptance unverified.
The issue is closed on this evidence; the separate native-dialog follow-up remains
active.

Reproducible automated checks: `ctest --preset=default -j4`; from either test
binary directory, run `./rollnw_test` with the filter below (and the sanitizer
environment above for the sanitized build):

```text
ClientCli.*:Kernel.*:ClientProjectImport.*:ClientBlueprintJobs.*:ClientSdlRuntime.*:SmallsConfig.*:SmallsLspBootstrap.*:ClientRmlSmallsBridge.*
```

Session logs are under `/tmp/rollnw-package-*`: before-test.log,
bridge-before-test.log, bridge-after-test.log, bridge-owned-test.log, ctest.log,
sanitizer-build.log, sanitizer-test.log, desktop-before.log, desktop.log,
installed-import.log, and local-bin-import.log. These are temporary session
artifacts; this record preserves their results and limits.
