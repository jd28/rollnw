# Shader toolchain migration: DXC to Slang

Status: Phase A, Phase B's two prerequisites, and **the compiler swap itself
are all done and verified**. The swap reaches parity with DXC: the full render
suite is back to its 3 pre-existing baseline failures, and the 18-entry
creature visual corpus renders **9 of 10 byte-identical** to DXC with the
tenth differing by a single byte (one pixel, one channel, 158 vs 159 out of
3,686,400). Phases C and D remain. All of it lives uncommitted in the
`rollnw-combat-rules` worktree on `main`.
Prerequisite for
[macOS / iPadOS port and nw::gfx Metal backend](macos-ios-metal-port-plan.md)
— that plan's Phase 0 assumed Microsoft ships `libdxcompiler.dylib` for
macOS, which is false (see Current state). This document owns the shader
toolchain decision; the Metal plan has been amended to defer to it.

Scope is deliberately narrow: replace the shader *compiler*, preserve the
shader *sources* and the renderer's observable behavior. This is not a
shader-authoring overhaul.

## Why now

The trigger is Apple, but the motivation is not "Slang is a nicer
language." Two concrete problems:

1. **DXC has no Apple artifact, and no arm64 artifact of any kind.** The
   latest release (v1.9.2609) ships exactly three assets:
   `dxc_2026_09_29.zip` (Windows), `linux_dxc_2026_09_28.x86_x64.tar.gz`,
   and a PDB bundle. There is no macOS build and no arm64 Linux build, so
   `lib/nw/render/CMakeLists.txt`'s hardcoded `x86_64` Linux URL is not a
   gap that upstream will close. There is also no core Homebrew formula —
   `brew install directx-shader-compiler` 404s, contrary to the advice
   `lib/nw/render/CMakeLists.txt:74-76` prints. What remains is building
   DXC from source, which means building a fork of LLVM/Clang 3.7 on an
   officially untested host configuration, or depending on a third-party
   tap/prebuilt mirror for a load-bearing build dependency.
2. **The Metal path otherwise needs a second shader tool with its own
   binding-layout opinions.** The Metal plan's original decision was
   DXC -> SPIR-V -> SPIRV-Cross -> MSL, and it correctly flagged a
   "known SPIRV-Cross risk surface to validate early" covering storage
   buffer register mapping, the compute shaders, and the shared
   `.inc.hlsl` includes. That validation burden exists *because* two
   independently-evolving tools must agree about descriptor layout. Slang
   emits SPIR-V and MSL from one front end with one layout engine.

Slang v2026.22.1 ships 38 release assets covering linux-x86_64 (glibc
2.27 and 2.28), linux-aarch64, macos-x86_64, macos-aarch64, windows-x86_64,
windows-aarch64, and wasm. The two platforms DXC cannot supply at all are
both covered.

## Current state (grounded in code, not assumption)

- 28 shader files, 2,624 lines total, in `lib/nw/render/shaders/` — 24
  entry points (`*.vs.hlsl`, `*.ps.hlsl`, `*.cs.hlsl`) plus 7 shared
  `*.inc.hlsl` headers. (An earlier revision of this plan said 24 and 4;
  the real counts are 21 and 7.) One of the seven, `shadow.inc.hlsl`, was
  included by no entry point — dead since `cafea4e7b` moved the shadow
  sampling everything actually uses into `scene_shadow.inc.hlsl` — and has
  since been deleted, leaving six. `bin/shaders/` is a build-output copy of the same
  file list, gitignored (`.gitignore:33`), produced by the copy-stamp
  targets in `tools/client/CMakeLists.txt:277-291` and
  `tools/mudl/CMakeLists.txt:93-110`, both globbing `*.hlsl`, and installed
  to `bin/shaders` by `tools/client/CMakeLists.txt:358` and
  `tools/mudl/CMakeLists.txt:150`.
- Compilation is **runtime only** — there is no offline bake.
  `ShaderProvider::compile_hlsl` (`lib/nw/render/shader_provider.cpp:207`)
  drives `IDxcCompiler3::Compile` with a fixed argument list: `-E main`,
  `-T {vs,ps,cs}_6_0`, `-spirv`, `-fspv-target-env=vulkan1.3`, `-O3`.
  Shader stage is inferred from the filename (`.vs.`/`.ps.`/`.cs.`) at
  `shader_provider.cpp:167-169`.
- Shaders are requested by literal filename through
  `ShaderProvider::get_shader`, 23 call sites across
  `model_gpu_backend_common.cpp:256-263`, `fog_renderer.cpp:46-47`,
  `forward_plus_renderer.cpp:396`, `viewer/scene_debug.cpp:126-201`,
  `particle_renderer.cpp:189-191`, and `tools/mudl/app_runtime.cpp:951`.
  Each passes a string ending in `.hlsl`.
- `#include` is hand-rolled. `expand_shader_includes`
  (`shader_provider.cpp:90`) does textual splicing with a depth limit of 8
  and include-once semantics via a shared `seen` set, emitting `#line`
  directives so diagnostics still point at the original file. It exists
  because includes resolve through `ResourceManager::demand`, so shaders
  can be overridden from a module or hak — which is why
  `IDxcIncludeHandler` was not usable.
- **The descriptor binding contract is hardcoded in C++ and agrees with
  the shaders only by way of DXC's default register mapping.** This is the
  single most load-bearing fact for this migration. The shaders carry no
  `[[vk::binding]]` attributes at all — only plain HLSL `register()`
  declarations — and `vulkan_context.cpp` builds descriptor set layouts
  from literal set/binding numbers:

  | Resource | HLSL declaration | SPIR-V (set, binding) | C++ site |
  | --- | --- | --- | --- |
  | Primary uniform | `register(b1)` (12 uses) | (0, 1) | `vulkan_context.cpp:575`, `:845` |
  | Secondary uniform | `register(b4)` (2 uses) | (0, 4) | `vulkan_context.cpp:584` |
  | Storage slot 0 | `register(t2, space0)` / `u2` | (0, 2) | `graphics_storage_binding_for_slot`, `vulkan_context.cpp:26` |
  | Storage slot 1 | `register(t3, space0)` / `u3` | (0, 3) | same |
  | Storage slot 2 | `register(t5)` / `u5` | (0, 5) | same |
  | Storage slot 3 | `register(t6, space0)` | (0, 6) | same |
  | Storage slot 4 | `register(t7, space0)` | (0, 7) | same |
  | Bindless textures | `register(t2, space1)` (6 uses) | (1, 2) | `vulkan_context.cpp:629`, `:2597` |
  | Bindless samplers | `register(s3, space1)` (6 uses) | (1, 3) | `vulkan_context.cpp:2597` |

  The comment at `vulkan_context.cpp:28-31` ("Graphics pipelines reserve b4
  for the optional secondary uniform span. Keep storage slots 0/1 on the
  existing t2/t3 bindings and place later storage slots after b4") only
  makes sense because HLSL register *numbers* map directly to Vulkan
  binding numbers across one shared number space — DXC's behavior with no
  `-fvk-*-shift` flags in play. Any compiler that assigns bindings by a
  different rule silently breaks every pipeline.
- `glslang` and `SPIRV-Reflect` are vendored and linked by
  `lib/nw/gfx/backends/vulkan/CMakeLists.txt:8-21,55-57` but **not
  `#include`d by a single file** in `lib/`, `tools/`, or `tests/`
  (verified by grep for `glslang`, `GlslangToSpv`, `spirv_reflect.h`,
  `SpvReflect`, and `#include <SPIRV`). The Metal plan's claim that
  SPIRV-Reflect supplies "binding reflection" is not true today — the
  bindings are the hardcoded table above. Both are dead build
  dependencies.
- `macos`, `macos-dev`, `macos-dev-debug`, `ci-macos` and
  `ci-macos-tools-package` presets already exist
  (`CMakePresets.json:492-540`); none enable the renderer.
- `ResourceType::hlsl` is `0xFFFF - 101` (`lib/nw/resources/assets.hpp:42`),
  with extension mapping at `:242` and `:444`.

## Findings from the spike (2026-11-22)

All 21 entry points were compiled with both toolchains and compared
decoration-for-decoration. Reusable capture scripts live in this session's
scratchpad; they take a shader directory and emit a normalized
`set/binding/variable` report per entry point, which is what makes these
claims checkable rather than asserted.

**1. The embeddability gate is passed.** `slang-2026.22.1-linux-x86_64-
glibc-2.28.tar.gz` (28.6 MB) contains `include/slang.h`,
`include/slang-com-ptr.h`, `include/slang-com-helper.h`,
`lib/libslang.so`, `lib/libslang-compiler.so`, `lib/libslang-rt.so`,
`bin/slangc`, and `lib64/cmake/slang/slangConfig.cmake` +
`slangTargets.cmake`, so `find_package(slang)` and a `slang::slang` link
target both work. Note `lib/slang-standard-module-2026.22.1/` ships
`.slang-module` files that are a *runtime* dependency — they need the same
install/copy treatment `ROLLNW_DXC_RUNTIME_LIBS` gives `libdxcompiler.so`
today.

**2. Phase A's stated justification was wrong, and the phase is still
worth keeping.** This plan claimed Slang's default SPIR-V binding
assignment would differ from DXC's, silently renumbering every descriptor.
It does not. Slang honors HLSL `register(xN, spaceM)` with exactly the
same `space -> set, N -> binding` mapping DXC uses: compiled against the
*unannotated* pre-Phase-A shaders, Slang's binding report is identical to
DXC's across all 21 entry points and all 57 decorated variables. So the
attributes are insurance and documentation, not a fix for a demonstrated
divergence. They cost nothing (all 21 modules stay byte-for-byte identical
under DXC) and the contract they document was genuinely undocumented, so
keeping them is right — but the risk framing that made Phase A mandatory
was overstated and is corrected below.

**3. The profile matters, and the default is wrong for this project.**
With `-profile sm_6_0`, Slang emits SPIR-V **1.3** and lowers HLSL
`clip()`/`discard` to `OpKill`. DXC with `-fspv-target-env=vulkan1.3`
emits SPIR-V **1.6** and uses `OpDemoteToHelperInvocation`. These are not
equivalent: `OpKill` terminates the invocation, so a killed lane stops
contributing to its quad, and implicit-LOD/derivative results along an
alpha-cutout edge can change; demote keeps the lane alive as a helper and
only discards its writes. Three pixel shaders use `clip()`
(`render_pbr_static.ps`, `render_nwn_static.ps`, `render_pbr_shadow.ps` —
alpha cutout and colour-key) and `render_particle_billboard.ps` uses
`discard`, so this is live behaviour, not a theoretical difference.
Passing `-profile spirv_1_6` gives SPIR-V 1.6 *and*
`OpDemoteToHelperInvocation`, matching DXC on all 21 entry points. **Use
`-profile spirv_1_6`, not `sm_6_0`.**

That the demote path is intended here is not an inference: the backend
already sets `features_13.shaderDemoteToHelperInvocation = VK_TRUE`
(`vulkan_core.cpp:240`). Letting Slang fall back to `OpKill` would quietly
stop using a device feature this project deliberately turns on.

**4. The real blocker: `SV_InstanceID` semantics differ, and this project
depends on DXC's.** The two compilers disagree about whether
`SV_InstanceID` includes the draw's `firstInstance`:

- DXC maps it straight to `gl_InstanceIndex`, which in Vulkan *includes*
  `firstInstance`. (This is a known DXC divergence from D3D, where
  `SV_InstanceID` is draw-local.)
- Slang is faithful to D3D: it emits `gl_InstanceIndex - gl_BaseInstance`,
  declaring `OpCapability DrawParameters` and the `BaseInstance` builtin
  to do it. Confirmed in the disassembly (`OpISub` of the two builtins).

The renderer relies on DXC's behaviour. `render_pbr_static_instanced.vs
.hlsl:33-36` reads `uint instance_id : SV_InstanceID` and indexes
`g_model_surface_instances[instance_id]` as an **absolute** row, while
`model_renderer.hpp:86` documents the protocol as "first_instance selects
this primitive's contiguous rows" in one shared frame allocation, and
`vulkan_context.cpp:2164` passes that value as `vkCmdDrawIndexed`'s
`firstInstance`. Two consequences of a naive swap:

- **Hard failure first.** `OpCapability DrawParameters` requires the
  `shaderDrawParameters` feature, which this backend does not enable
  anywhere (grep finds no `shaderDrawParameters` /
  `VkPhysicalDeviceVulkan11Features` in `backends/vulkan/`). Pipeline
  creation for the three affected vertex shaders would fail outright.
- **Silent wrong rendering if it were enabled.** Slang's draw-local index
  would address row 0 of the batch instead of row `first_instance`, so
  every instanced batch past the first would read another batch's
  transforms.

Three shaders are affected — `render_pbr_static_instanced.vs`,
`render_particle_billboard.vs`, `render_sound_debug_dot.vs` — i.e. every
shader that consumes `SV_InstanceID`.

No compiler flag resolves this. `slangc -h` offers no base-instance
switch (`-fvk-*-shift`, `-fvk-use-dx-layout`, `-fvk-invert-y`,
`-fvk-use-dx-position-w` and friends do not cover it), and the obvious
source-level escape is closed from both ends: Slang rejects
`[[vk::builtin("InstanceIndex")]]` as an unknown attribute, and DXC
rejects `InstanceIndex` as a permitted `vk::builtin` value (it allows only
PointSize, HelperInvocation, BaseVertex, BaseInstance, DrawIndex,
DeviceIndex, ViewportMaskNV).

**Recommended fix — move the row selection off `firstInstance` and onto
the storage binding.** `gfx::StorageSpan` already carries `offset` and
`size` (`gfx.hpp:294-307`). Bind the instance span offset by
`first_instance * sizeof(ModelSurfaceInstance)` and issue the draw with
`firstInstance = 0`. Then `SV_InstanceID` is draw-local under *both*
compilers, because absolute and local coincide when the base is zero. This
needs **no shader change and no constant-buffer change** — it is confined
to the two call sites in `model_renderer.cpp` (around `:1059` and `:1155`)
and the `cmd_draw_indexed_base_instance` usage behind them.
`ModelSurfaceInstance` is two `float4x4` = 128 bytes, so every offset is a
multiple of 128 and satisfies `minStorageBufferOffsetAlignment` on any
real device. Avoiding a `ModelDrawConstants` layout change is deliberate:
that is exactly where this project has had a verified padding bug before
(`ParticleDrawConstants`, C++ offset 156 vs HLSL 160).

This change is behaviour-preserving under DXC today, so it can land and be
verified *before* any compiler swap — which is what makes it the right
first task of Phase B rather than a Slang-specific workaround.

**But the storage-offset fix alone is not sufficient — `shaderDrawParameters`
must also be enabled.** Slang emits `OpCapability DrawParameters` whenever a
shader reads `SV_InstanceID`, unconditionally, whether or not the draw's base
instance is zero. So all three affected vertex shaders declare it even after
the offset fix makes every base instance zero. The capability requires
`VkPhysicalDeviceVulkan11Features::shaderDrawParameters`, and this backend's
feature chain (`vulkan_core.cpp:219-258`) goes
`VkPhysicalDeviceFeatures2 -> features_13 -> features_12 ->
VkPhysicalDeviceDescriptorBufferFeaturesEXT` with **no
`VkPhysicalDeviceVulkan11Features` link at all.** Phase B therefore needs a
`features_11` node added to that chain with `shaderDrawParameters = VK_TRUE`,
plus a support check alongside the existing `multiDrawIndirect` /
`drawIndirectFirstInstance` guard at `:223-231`. The feature is core in
Vulkan 1.1 and supported by MoltenVK, so this is a small addition rather than
a portability problem — but it is a hard requirement, not an optimization:
without it, pipeline creation for those three shaders fails outright.

Scope check: only two call sites in the whole tree use
`cmd_draw_indexed_base_instance` (`model_renderer.cpp:1058` and `:1154`).
The other two `SV_InstanceID` consumers — `render_particle_billboard.vs`
(`particle_renderer.cpp:724`) and `render_sound_debug_dot.vs` — already draw
through `cmd_draw`/`cmd_draw_indexed` with no base instance, so they need the
capability but not the semantic fix.

**5. Remaining capability deltas, all benign.** After `-profile
spirv_1_6`, version and termination opcodes match on all 21 entry points.
Four shaders still differ in declared capabilities:
`render_fog_composite.ps` (Slang adds `SampledImageArrayNonUniform
Indexing` and omits `OpExtension "SPV_EXT_descriptor_indexing"`, which is
core from Vulkan 1.2 — Slang is the more modern of the two), and the three
instanced vertex shaders (`DrawParameters` per finding 4, plus
`OpExtension "SPV_KHR_storage_buffer_storage_class"`, redundant from
SPIR-V 1.3 on). Once finding 4 is fixed, only the fog-composite delta
remains, and it requires no device feature this project does not already
rely on for its bindless path.

## Goal

1. Slang replaces DXC as the only shader compiler on Windows and Linux,
   producing SPIR-V for the existing Vulkan backend, with no change to
   rendered output and no change to the descriptor binding contract.
2. The shader toolchain is obtainable as a pinned, hash-verified
   prebuilt on every platform the project targets, including Apple
   Silicon — so the Metal port never has to build a shader compiler.
3. One tool, one layout engine, serves SPIR-V and (later) MSL, removing
   the two-tool agreement problem from the Metal plan's critical path.

## Non-goals

- **Rewriting shaders in Slang idioms.** No generics, no `interface`-based
  material polymorphism, no module system refactor. Slang accepts the
  existing HLSL essentially as-is; that is the point. The three PBR vertex
  variants are 43/53/61 lines — permutation pressure is not a real problem
  yet, and conflating a language migration with an authoring redesign
  would make the diff unreviewable and the bisect useless.
- **Changing renderer behavior.** Same passes, same pipelines, same
  binding numbers, same visual output.
- **Introducing the offline bake in the same step as the compiler swap.**
  Offline `.metallib`/`.spv` baking is genuinely wanted (it is the iOS
  requirement, and it removes compile cost from startup), but it changes
  *when* shaders compile, not *what* compiles them. Separate phase.
- **Renaming `.hlsl` to `.slang` early.** Deferred to Phase D for the same
  reason — it touches `ResourceType`, 20+ string literals, two CMake
  globs, and two install rules, none of which help prove the compiler swap
  is correct.

## Sequencing rationale

Do the whole thing on Linux/Vulkan, before any Apple work. The Metal
plan's own Phase 0 reasoning applies verbatim: a Metal bug and a shader
codegen bug look identical from the renderer's point of view. If the
compiler swaps during Metal bring-up, every wrong pixel is ambiguous
between "Metal backend wrong" and "Slang codegen differs from DXC." Swap
it first against the Linux oracle, where the existing `render_gfx_*` suite
and a known-good reference image are available, and Apple then starts from
a toolchain that downloads instead of one that has to be built.

The binding contract is pinned *before* the compiler changes so that if
output diverges, the binding layout is already excluded as a cause. Note
the spike showed Slang would have matched DXC's binding assignment anyway
(Finding 2), so this is cheap insurance and documentation rather than a
necessary repair — the genuinely necessary pre-work turned out to be the
instancing fix in Finding 4.

### Phase A — pin the binding contract explicitly (under DXC, no compiler change) — **DONE**

Goal: the SPIR-V descriptor set/binding assignment becomes an explicit
property of the shader source instead of an emergent property of the
compiler's default register mapping.

Implemented 2026-11-22: 39 `[[vk::binding]]` attributes across 23 files,
plus the two cross-reference comments below. Verified stronger than the
acceptance criterion asked for — **all 21 SPIR-V modules are byte-for-byte
identical** to their pre-change builds under DXC, and all 21 pass
`spirv-val --target-env vulkan1.3`. Since the compiler emits literally the
same bytes, no runtime behaviour can have changed.

- Add `[[vk::binding(binding, set)]]` to every resource declaration in the
  28 shaders, with numbers taken from the table above. Both DXC and Slang
  honor this attribute and map it to SPIR-V `DescriptorSet` + `Binding`
  decorations, so it is portable across the swap.
- Keep the existing `register()` declarations alongside it. They remain
  meaningful for any future D3D12 target and removing them is unnecessary
  churn.
- Add a comment in `lib/nw/gfx/backends/vulkan/vulkan_context.cpp` near
  `graphics_storage_binding_for_slot` pointing at the shader-side
  attributes as the other half of the contract, and vice versa in
  `scene_constants.inc.hlsl`. Right now nothing in either file tells a
  reader that the other exists.
- Verification: disassemble each compiled module before and after
  (`spirv-dis`, available from the vendored SPIRV-Tools) and diff only the
  `OpDecorate ... DescriptorSet` / `... Binding` lines. They must be
  identical. Then the full `render_gfx_*`/`render_*` suite green.

This phase is worth doing on its own merits even if the Slang decision is
later reversed: before it, the agreement between `vulkan_context.cpp`'s
literal numbers and the shaders' register numbers was implicit,
undocumented, and dependent on a compiler default that no comment anywhere
mentioned. It is *not*, however, repairing a divergence Slang would have
introduced — see Finding 2. Treat it as hygiene that happened to be free,
not as the thing that made the swap safe.

### Phase B — swap DXC for Slang, Linux and Windows, SPIR-V only

Goal: `NW_RENDER_HAS_DXC` and `IDxcCompiler3` are gone; shaders still
compile at runtime, still from `.hlsl` sources, still to the same SPIR-V.

- ~~First task: confirm the Slang release tarballs ship embeddable headers
  and libraries.~~ **Resolved — they do.** See Finding 1 for the exact
  contents, including the `lib/slang-standard-module-*/` runtime
  dependency that needs install/copy treatment.
- ~~Blocking prerequisite: the instancing fix and the capability from
  Finding 4.~~ **DONE, under DXC, verified on the render suite.** Both
  landed before the compiler swap deliberately, so they stay independently
  bisectable:
  - `model_renderer.cpp` gained a file-local `instance_rows_for_batch`
    helper that narrows the instance span to the rows a batch draws; both
    former `cmd_draw_indexed_base_instance` sites now bind that subspan and
    issue a plain `cmd_draw_indexed`, so every draw has a zero base
    instance and `SV_InstanceID` is draw-local under either compiler. The
    arithmetic is behaviour-preserving: previously the shader read absolute
    row `first_instance + local` from a span starting at row 0; now it
    reads row `local` from a span starting at row `first_instance`. The
    descriptor range also narrows from the whole span to exactly the rows
    drawn, which is strictly safer.
  - `cmd_draw_indexed_base_instance` now has no callers outside the Vulkan
    backend. Leaving it in `gfx.hpp` is deliberate — it is a reasonable
    part of the API surface and removing it is unrelated churn.
  - `vulkan_core.cpp` gained a `VkPhysicalDeviceVulkan11Features` node in
    the device feature chain with `shaderDrawParameters = VK_TRUE`, plus a
    `vkGetPhysicalDeviceFeatures2` support check that refuses the device
    with a clear message, mirroring the existing `multiDrawIndirect` /
    `drawIndirectFirstInstance` guard. `features_11` was added to
    `vulkan_internal.hpp` beside `features_12`/`features_13`, and the
    chain is now `features2 -> 13 -> 12 -> 11 -> descriptorBuffer`.
- Replace the `if(WIN32)/elseif(APPLE)/elseif(UNIX)` DXC acquisition block
  in `lib/nw/render/CMakeLists.txt:29-105` with a single `FetchContent`
  declaration selecting the right Slang tarball by host platform and
  architecture, hash-pinned, following the shape the Windows branch
  already uses. Delete the `brew install directx-shader-compiler`
  message, which advertises a formula that does not exist. Replace
  `ROLLNW_DXC_RUNTIME_LIBS` with the Slang equivalent so the existing
  runtime-library copy logic keeps working.
- Rewrite `ShaderProvider`'s compile path (`shader_provider.cpp:120-146`
  for init, `:207-296` for compile) against Slang's
  `IGlobalSession`/`ISession`/`IModule`/`IComponentType` API. **Target
  profile `spirv_1_6`, not `sm_6_0`** — see Finding 3; the default profile
  silently drops to SPIR-V 1.3 and turns `clip()`/`discard` into `OpKill`
  instead of `OpDemoteToHelperInvocation`. Keep `get_shader`, the handle
  cache, and `clear_cache` unchanged — the public surface in
  `shader_provider.hpp` should not move.
- Keep `expand_shader_includes` **in place and in use** for this phase.
  Replacing it with an `ISlangFileSystem` override bridged to
  `ResourceManager` is the better end state and is listed in Phase D, but
  doing it here would mean the include mechanism and the compiler change
  in the same diff.
- Verification, in order: the binding decorations diffed exactly as in
  Phase A (now across compilers, which is the real test of whether the
  pinning worked); the full `render_gfx_*`/`render_forward_plus`/
  `render_particles`/`render_shadow_culling` suite green on
  `linux-renderer-dev`; `ci-linux-renderer` and `ci-windows-renderer`
  green; and a manual side-by-side of an imported module area against the
  pre-swap build. Consistent with how
  `rendering-performance-overhaul.md` treats new measurement frameworks,
  do not build a golden-image harness for this — the comparison is manual.
- The spike already cleared the surfaces this plan originally worried
  about: both compute shaders and all 12 bindless `space1` declarations
  compile under Slang and land on identical bindings. The one residual
  delta is `render_fog_composite.ps`'s capability set (Finding 5), which
  needs no device feature the bindless path does not already require.
  Slang's known reflection/SPIR-V disagreement when `[[vk::binding]]`
  coexists with a global-scope `uniform` (shader-slang/slang#14013) still
  does not apply, because nothing here reflects bindings.

### Phase C — delete what the swap orphans — **DONE**

Removed 2026-11-23. `glslang` and `SPIRV-Reflect` were linked by
`lib/nw/gfx/backends/vulkan/CMakeLists.txt` and included by nothing: no
`#include` and no symbol reference anywhere in `lib`, `tools` or `tests`, and
`vulkan_all.cpp` compiles only volk, VMA and the three Vulkan sources (a stale
comment in that CMakeLists claimed otherwise and has been corrected). Both
`add_subdirectory` calls, both include directories, all four link entries and
both git submodules are gone, along with a stale `lightweightvk` entry that
was in `.gitmodules` but unregistered and referenced by nothing. SDL is now
the only submodule. Verified by a fresh configure plus the full suite at
1103/1111, unchanged. Drops 55 MB from a clone and removes three repositories
from every `submodules: true` CI checkout.

Original scope:

- Remove the `glslang` and `SPIRV-Reflect` link dependencies and
  `add_subdirectory` calls from
  `lib/nw/gfx/backends/vulkan/CMakeLists.txt`, and drop the submodules if
  nothing else claims them. They are already unused today; this phase just
  does the deletion once the shader toolchain churn has settled, so the
  removal is not tangled with the swap.
- If Phase B kept a `spirv-dis`-based verification step, note which
  SPIRV-Tools provenance it depends on before removing the glslang
  subtree that currently supplies it.
- Acceptance: all existing CI legs green with a smaller dependency
  surface, no behavior change.

### Phase D — the improvements the swap makes available (not required by it)

Each of these is independently landable after Phase B, and none should be
bundled into it:

- ~~`ISlangFileSystem` bridged to `ResourceManager`.~~ **DONE 2026-11-23.**
  `ResourceFileSystem` in `shader_provider.cpp` resolves `#include` through
  `ResourceManager::demand`, so Slang's own preprocessor runs and module/hak
  shader override now applies to headers as well as entry points. The
  hand-rolled `expand_shader_includes` and its `ShaderIncludeResolver` are
  gone from the public header, along with the 10 tests that covered the
  splicer; `WaterShadersUseCommonRenderModelContracts` stays because it reads
  the shader files directly rather than exercising the expander. Net −90
  lines. Required adding `#pragma once` to all seven `.inc.hlsl` headers:
  the old splicer deduplicated by filename, so the headers had never needed
  guards, and a real preprocessor would otherwise double-include them. (No
  live diamond existed — the headers are leaves — so this was insurance, not
  a fix.) Verified by the creature corpus staying 9/10 byte-identical to the
  DXC baseline with the same single 1-byte outlier, and the suite unchanged
  at 1093/1101 — the 10 fewer tests are exactly the removed ones.
- **Offline bake as a build step.** `slangc` compiles all 21 entry points
  to SPIR-V at build time; the runtime loads blobs and only falls back to
  compiling when a module/hak overrides a shader. This is what the Metal
  plan's iOS requirement actually needs, and it removes shader compilation
  from client startup on every platform.
- **`.hlsl` -> `.slang` rename**, if wanted: `ResourceType::slang`
  alongside `hlsl` in `assets.hpp`, the 20+ `get_shader` string literals,
  the two CMake globs, the two install rules. Mechanical, zero risk once
  the compiler already works, and genuinely optional.
- **MSL generation**, which is Phase 1 of the Metal plan rather than this
  one — but note that after Phase B it is a target flag on a compiler
  already in the build, not a new dependency with a new layout engine.

## Phase B swap: implemented, with one open regression

The swap is written and runs. What exists:

- `lib/nw/render/CMakeLists.txt` fetches the official Slang release per
  platform/arch, hash-pinned, replacing the three-branch DXC block: Windows
  x86_64/aarch64 (`.zip`, link `lib/slang.lib`, DLLs from `bin/`), macOS
  arm64/x86_64 (link `lib/libslang.dylib`), Linux x86_64/aarch64 (the
  glibc-2.28 archives, link `lib/libslang.so`). `NW_RENDER_HAS_DXC` becomes
  `NW_RENDER_HAS_SLANG`, and `ROLLNW_DXC_RUNTIME_LIBS` becomes
  `ROLLNW_SHADER_COMPILER_RUNTIME_LIBS` (consumed by
  `tools/mudl/CMakeLists.txt`). The `lib/slang-standard-module-*` tree is
  deliberately *not* installed: the core module is embedded in the library,
  verified by compiling with that directory moved aside.
- `ShaderProvider` now drives `slang::IGlobalSession` / `ISession` /
  `IModule` / `IComponentType`, keeping its public surface and cache
  unchanged. `expand_shader_includes` is retained, so Slang receives one
  pre-expanded translation unit and never needs a file system — hak/module
  shader override keeps working untouched. A `ShaderStage` enum replaces the
  wide profile string, because the shaders carry no `[shader(...)]`
  attribute and the stage must be passed to `findAndCheckEntryPoint`
  explicitly.
- Diagnostics are logged after every step, not only on failure: Slang
  reports warnings through the same blob on success. The only warning this
  shader set produces is `E41012` ("profile implicitly upgraded") on the five
  texture-sampling pixel shaders, which is benign — the emitted module's
  declared capabilities still match DXC's.

**A refcount bug worth recording, since the symptom was so misleading:**
`loadModuleFromSourceString` returns a pointer the session's module cache
still owns. Taking it with `Slang::ComPtr::attach()` (which does not addRef)
over-releases, and the result was `free(): corrupted unsorted chunks` →
`SIGABRT` → loguru's signal handler hanging, so the test binary looked like
an infinite hang rather than a crash, and the stray process then blocked
every later run on the GPU. Assign (`ComPtr<IModule> ref{module}`), never
attach. An isolated single-shader repro did *not* reproduce it; it needed the
engine's many-modules-one-session pattern.

### Resolved regression — matrix layout (the one that mattered)

**Root cause: `SessionDesc::defaultMatrixLayoutMode`.** Slang's C++ API
defaults to `SLANG_MATRIX_LAYOUT_ROW_MAJOR`, but HLSL's default — and
therefore what DXC produced for these sources, and what the C++ side assumes
when it uploads `glm` matrices — is **column-major**. Setting
`SLANG_MATRIX_LAYOUT_COLUMN_MAJOR` on the session fixes it.

Why it survived so much checking: **the `slangc` CLI and the C++ API disagree
on this default.** Every offline comparison in the Findings above was made
with the CLI, where the default already matched DXC, so bindings, offsets,
strides, majorness decorations and stage interfaces all compared clean. The
divergence only appeared once the compiler ran through `ISession` inside the
engine. A SPIR-V `RowMajor`/`MatrixStride` decoration diff cannot catch this
either, because both compilers emit the same decorations — what changes is how
the source's `mul()` operands are interpreted before that point.

The symptom was also misleading. Only three unit tests failed, all in the
skinned path, which read as "something specific to skinning". The visual
corpus showed the truth: **all ten creature renders differed**, and looking at
them, geometry was drawn at wildly wrong scale (a creature that should fill a
small part of the frame rendered as one enormous limb). Transposed transforms
degrade smoothly rather than failing outright, so most tests still saw
*something* rendered and passed.

**Lesson for the rest of this migration:** pixel-exact unit tests caught a
symptom; the corpus caught the actual bug. Run
`mudl corpus ./tests/test_data/user/development/creature_corpus.json --user
./tests/test_data/user --output <dir>` against both compilers and diff the
PNGs before trusting any further shader-toolchain change. The renders are
deterministic — two DXC runs are byte-identical across all ten — so a plain
`cmp` is a valid oracle, no image-diff framework required. Other checked-in
corpuses worth the same treatment: `item_corpus.json` (5),
`model_corpus.json` (7), `particle_corpus.json` (14), `pbr_corpus.json` (7),
`spell_corpus.json` (14), `area_corpus.json` (1, needs `--module`).

(The 8 of 18 creature entries that report failures do so identically under
both compilers — pre-existing content/fixture gaps around optional equipment
rows, not a toolchain problem.)

**Corpus parity achieved.** Four corpuses were rendered under both compilers
and compared byte-for-byte — 28 renders total:

| Corpus | Identical | Differing | Worst difference |
| --- | --- | --- | --- |
| `creature_corpus` (10 rendered of 18) | 9 | 1 | 1 byte, 1 pixel, delta 1 |
| `model_corpus` (7) | 5 | 2 | 3 bytes, delta 1 |
| `item_corpus` (5) | 5 | 0 | — |
| `pbr_corpus` (6 of 7) | 3 | 3 | 3 bytes, delta 1 |
| **total** | **22** | **6** | **≤3 bytes of 3,686,400, delta 1** |

Every remaining difference is a last-bit rounding difference in a handful of
final colour bytes, which is expected from two independent compilers'
instruction selection and float contraction. No structural, geometric or
shading divergence remains.

`particle_corpus` and `spell_corpus` produced no renders under either compiler
(they appear to need a `--module`, as the README's `area_corpus` example does)
and so are untested here rather than passing — worth wiring up before Phase C.

### Historical: how the regression first presented

Three tests in `RenderGfxResources` fail under Slang and pass under DXC:

- `ModelCullingPreservesMirrorsAndDoubleSidedMaterialsAcrossBatchPaths`
- `IndexedGroupsPreserveInterleavedAssetsMaterialsAndSpaces`
- `AnimatedRigidGroupsMatchScalarPixelsAcrossPosesAndEdits`

This is a real regression, not test-subset flakiness: the identical isolated
filter (`--gtest_filter='RenderGfxResources.*'`) passes **16/16** with DXC and
fails 3 with Slang, same binary otherwise. The full run is
`Render*:ShaderIncludes*:Client*:Model*` → **1100/1111 passed, 6 failed**:
these 3 plus the 3 that already fail on clean `main`
(`RenderViewerPreparedDraws.StandaloneItemPreviewsRenderWithoutChangingWorld
Session`, `ClientGfxResources.PlayPreviewItemLoopUsesQueuedControlsAndRestores
AuthoredVisuals`, `ClientGfxResources.PickupApproachRendersAndPublishes
Inventory`). So the swap's blast radius is exactly these three tests — every
other shader, pipeline and pass in the suite behaves identically. The failure mode is geometry
disappearing (`EXPECT_GT(colored[0], 40u)` sees `0`, i.e. an empty quadrant),
and from the `SCOPED_TRACE` values it only happens when
`backface_culling == true` **and** `mixed_bones == true` — front-facing
skinned geometry being culled, which points at winding or at the blended
skin matrix rather than at bindings or resource layout.

Ruled out by direct SPIR-V comparison of all 21 modules, DXC vs Slang (so
these do not need re-checking):

- Descriptor set/binding assignment — identical, all 57 decorated variables.
- Constant- and storage-buffer member offsets — identical everywhere.
- Matrix majorness and stride — `RowMajor` + `MatrixStride 16` in both, for
  cbuffers, for the `ModelSurfaceInstance` storage buffer, and for the bare
  `StructuredBuffer<float4x4> g_bones`.
- Array strides — every CPU-visible stride matches (`g_bones` 64,
  `PltPaletteLayer` 180240); Slang's extra `180224` is its own wrapper around
  the inner `uint[45056]` member, not a layout difference.
- Vertex input locations and the VS/PS stage interface — both assign by
  semantic index (position 0, normal 1, texcoord 2, tangent 3; TEXCOORD0..6
  → locations 0..6).
- SPIR-V version and `discard` lowering — both 1.6 +
  `OpDemoteToHelperInvocation` once `-profile spirv_1_6` is used.
- A global Y-flip or winding inversion — no `OpFNegate` in either, and
  `raster.frontFace` is a fixed `VK_FRONT_FACE_COUNTER_CLOCKWISE` on the C++
  side (`vulkan_context.cpp:762`).
- The bone-blend arithmetic — DXC's 4 `OpMatrixTimesScalar` versus Slang's 18
  `OpVectorTimesScalar` is row-wise decomposition of the same operation; both
  keep 12 `OpFAdd` and 8 `OpVectorTimesMatrix`.

Vulkan validation is active in these tests and reports no errors, only two
performance warnings (an unconsumed `view_depth` output at location 6, and a
vertex attribute at location 3 not consumed by one shader) — both consistent
with Slang eliminating dead I/O that DXC kept.

All of that structural checking was necessary to rule things out but none of
it found the cause; see "Resolved regression" above for what did.

## Cross-cutting risks

| Risk | Why it matters | Mitigation already in this plan |
| --- | --- | --- |
| ~~Slang assigns SPIR-V bindings differently from DXC~~ | — | **Retired by Finding 2.** Slang honors `register(xN, spaceM)` identically; verified on all 21 entry points with and without the attributes. Phase A kept as documentation, not repair |
| ~~Slang tarballs may not ship embeddable libs~~ | — | **Retired by Finding 1.** Headers, `libslang.so`, and `slangConfig.cmake` all present |
| `SV_InstanceID` means different things to the two compilers | DXC includes `firstInstance`, Slang does not; the renderer's instanced-draw protocol depends on DXC's reading. Breaks pipeline creation (`DrawParameters` unavailable), then silently reads wrong instance rows | **The real blocker (Finding 4).** Fix by moving row selection to `StorageSpan::offset` and drawing with `firstInstance = 0` — behaviour-preserving under DXC, so it lands and is verified before the swap |
| Slang declares `DrawParameters` for any `SV_InstanceID` use | Needs `VkPhysicalDeviceVulkan11Features::shaderDrawParameters`, which is absent from the backend's feature chain entirely; pipeline creation for three vertex shaders fails without it | Add a `features_11` node in `vulkan_core.cpp:219-258` with a support check beside the existing indirect-draw guard — core in Vulkan 1.1, MoltenVK-supported |
| Slang's default profile changes `discard` semantics | `-profile sm_6_0` yields SPIR-V 1.3 + `OpKill` instead of 1.6 + `OpDemoteToHelperInvocation`, changing quad derivatives at alpha-cutout edges in four live pixel shaders | **Use `-profile spirv_1_6`** (Finding 3); verified to match DXC's version and termination opcode on all 21 entry points |
| Slang's Metal backend is younger than SPIRV-Cross's MSL backend | SPIRV-Cross's MSL path has years of MoltenVK mileage behind it; Slang's is newer | Irrelevant until Metal Phase 1 (MoltenVK uses SPIRV-Cross internally regardless), and the fallback is SPIRV-Cross consuming *Slang-generated* SPIR-V — still no DXC |
| Compute shaders and bindless `space1` are the likely divergence sites | `forward_plus_cull.cs.hlsl` and `render_compute_smoke.cs.hlsl` plus the 12 `space1` texture/sampler declarations are the least conventional HLSL in the set | Called out explicitly in Phase B as where to look first, with a named upstream issue as the expected bug shape |
| ~~Skinned geometry vanishes under Slang~~ | — | **Resolved.** Root cause was `SessionDesc::defaultMatrixLayoutMode`: Slang's C++ API defaults to row-major while HLSL/DXC are column-major. Set `SLANG_MATRIX_LAYOUT_COLUMN_MAJOR` |
| Slang's C++ API and its own `slangc` CLI have different defaults | Offline CLI comparisons can look perfectly clean while the in-engine API path is wrong — this cost a long hunt through bindings, offsets and strides that were never the problem | Validate shader-toolchain changes through the engine and the visual corpus, not only through `slangc` on the command line |
| No golden-image harness exists | Visual regressions from a codegen change could pass a green test suite | Acknowledged; manual side-by-side against the pre-swap Linux build, consistent with `rendering-performance-overhaul.md`'s stance on not inventing measurement frameworks for one-off needs |
| Doing this during the Metal port instead of before it | A wrong pixel becomes ambiguous between Metal backend and shader codegen | The entire sequencing rationale; this plan is a prerequisite to the Metal plan's Phase 0, not a parallel track |

## What this plan deliberately does not do

- It does not commit to calendar time. Phase A and C are small and
  mechanical; Phase B's size is dominated by one unknown (the Slang
  embedding API's fit against `ShaderProvider`) that is cheap to resolve
  first.
- It does not touch rendered output, pass structure, or the `nw::gfx`
  public API. If any phase requires a `gfx.hpp` change, that is a signal
  something was mis-scoped.
- It does not improve the shaders. Permutation handling, material
  polymorphism and the duplicated PBR vertex variants are all left exactly
  as they are, for a later authoring pass that has nothing to do with
  which compiler runs.
