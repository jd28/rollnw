# macOS / iPadOS port and nw::gfx Metal backend

Status: planning, no implementation started. Landed on `main`, but grounded
against the in-progress `server` branch: every `nw::gfx`/`tools/client` fact
below is true on both, but `tools/server` and the GameNetworkingSockets
build (`ROLLNW_BUILD_SERVER`, `cmake/GameNetworkingSockets.cmake`) exist only
on `server` and are not yet on `main` as of this writing. Phase 2's macOS
*server* work (not the client/Metal work, which does not depend on it) is
therefore gated on that branch merging first — see the note inline below.
Driven by new Apple Silicon hardware (M5 MacBook Air) becoming available for
this work. The existing Linux/Vulkan machine remains in use throughout as
the correctness oracle; this is an addition, not a replacement.

## Current state (grounded in code, not assumption)

- `nw::gfx` (`lib/nw/gfx/gfx.hpp`) is already a backend-agnostic handle/desc
  API — `Core`/`Context`/`Buffer`/`Texture`/`RenderTarget`/`Shader`/`Pipeline`/
  `CommandList`, no Vulkan types in the public header. The only backend today
  is `lib/nw/gfx/backends/vulkan/` (`vulkan_context.cpp`, ~3100 lines). Backend
  escape hatches are isolated in `native_vulkan.hpp`. This shape is favorable:
  a second backend is "implement the same ~30 free functions," not "redesign
  the API."
- The API is already bindless-leaning (descriptor-buffer-backed sampled
  textures, `BindlessTextureIndex`, `uses_bindless_sampled_textures`). This
  maps cleanly onto Metal argument buffers (Tier 2, Apple7/A13+ and all
  M-series) — no redesign needed, just a different implementation of the same
  contract.
- Shaders are authored once in HLSL (`bin/shaders/*.hlsl`, `lib/nw/render/
  shader_provider.cpp`) and compiled via DXC (`libdxcompiler.so`, loaded
  through `dxcapi.h` when `NW_RENDER_HAS_DXC`) straight to SPIR-V. `glslang`
  is vendored with its HLSL frontend explicitly disabled
  (`ENABLE_HLSL OFF`) — it exists only to supply `SPIRV` plumbing, DXC is the
  real compiler. `SPIRV-Reflect` is already vendored for binding reflection.
- `VK_USE_PLATFORM_MACOS_MVK` / `VK_USE_PLATFORM_METAL_EXT` are already in
  `lib/nw/gfx/CMakeLists.txt` and `backends/vulkan/CMakeLists.txt` for `APPLE`
  — someone anticipated MoltenVK, but MoltenVK itself is not vendored or
  referenced anywhere else. Nothing runs on Apple today.
- `README.md` states renderer tools "currently only run on Linux and
  Windows." `CMakePresets.json` has a library-only `macos-dev` preset (no
  renderer) — macOS has never built `rollnw-client`/`mudl`/`rollnw-server`.
- SDL3 is vendored with full Cocoa, UIKit, and Metal backends already present
  (`external/SDL/src/video/cocoa`, `.../uikit`, `SDL_metal.h`,
  `SDL_uikitmetalview.m`). Windowing/input portability is mostly already paid
  for by the SDL3 dependency choice.
- Networking (`tools/server`, `ROLLNW_BUILD_SERVER`) is GameNetworkingSockets
  1.6.1, built via `cmake/GameNetworkingSockets.cmake` as an `ExternalProject`
  with `USE_CRYPTO=OpenSSL`. That cmake file only special-cases `Linux` and
  `WIN32`; Apple falls through untested — but GNS's own vendored
  `CMakeLists.txt` (`external/GameNetworkingSockets-1.6.1/CMakeLists.txt`)
  has real, maintained Apple support: `Darwin` (macOS) is treated as a
  normal desktop target, and iOS/tvOS/watchOS/visionOS get a dedicated
  `GNS_APPLE_EMBEDDED` mode that forces a static lib or `.framework` build
  (app-bundle signing rules) and defines `IOS` via `TargetConditionals.h`.
  So this is a gap in *our* wrapper cmake, not a gap in GNS's platform
  support — meaningfully lower risk than "porting an unsupported library,"
  though the iOS embedded-build path should still be exercised, not assumed
  identical to the desktop path we already use.
- `tools/client` is the fused editor+game-runtime product ("rollnw |
  toolset", binary `rollnw-client`), built on RmlUI + `nw::gfx`/`nw::render`,
  entirely keyboard/mouse-driven today (`client_input*.cpp`, `pc_input.hpp`).
  There is no separate "Arclight" project anymore — this *is* it, in this
  repo. The user has confirmed the iPad target is this full toolset,
  touch-adapted, not a trimmed play-only client.
- Existing render tests (`tests/render_gfx_*.cpp`, `render_forward_plus.cpp`,
  etc.) exercise `nw::gfx` directly through gtest, headless-capable
  (`ContextDesc{window=nullptr}`). These are backend-agnostic by construction
  and become the correctness harness for a Metal backend with no rewriting.

## Goal

1. A native Metal backend for `nw::gfx` (`backends/metal/`), implementing the
   same `gfx.hpp` contract as the Vulkan backend.
2. `rollnw-client` and `rollnw-server` building and running on macOS
   (Apple Silicon).
3. `rollnw-client` running on iPadOS as the same full toolset, with a
   touch-adapted input/UI layer. **iPadOS is client-only** — `rollnw-server`
   is not being ported there. An iPad always connects to a server running
   elsewhere (macOS or Linux); see the Phase 3 note on `local_server.cpp`
   below for why this is a real constraint, not just a scoping choice.

Because the toolset *is* the runtime (no separate editor/game split —
`play_preview_view.cpp`/`play_preview_runtime.cpp`/`play_preview_inventory.cpp`
already implement play-mode inside `tools/client`), a touch-adapted toolset
is not editor-only work with a game client bolted on afterward: it is the
iPad gameplay client. Play-mode's input needs (camera control, interacting
with the world, inventory/dialog UI) are a subset of what the full editor
touch redesign already has to solve, not a separate deliverable. That
subset should be usable, and probably validated, before the harder editor
touch interactions (precision drag, multi-step authoring commands) are
finished — it is the natural "smallest slice that proves the touch input
model works end to end" milestone inside Phase 3.

## Non-goals

- Re-litigating the `nw::gfx` API surface. It is a known-good constraint for
  this work; only add to it where a Metal-specific capability genuinely has
  no mapping (expect few, if any).
- MoltenVK as a shipping backend. It is useful as a Phase 0 bring-up aid
  (below) and then should be dropped — the ask is a native Metal backend,
  not a Vulkan-over-Metal shim.
- Windows/Linux regressions. Every phase must keep existing CI green.

## Sequencing rationale

Three workstreams (Metal backend, macOS client/server, iPad client) are
tangled if attempted together — a Metal bug and a macOS build-system bug
look identical from the client's point of view. Phase 0 exists purely to
separate those failure modes: bring up the *entire* non-graphics stack
(CMake, SDL3 Cocoa windowing, DXC on Apple, GameNetworkingSockets on Apple,
RmlUI, asset/resource loading) on macOS using the **existing, unmodified
Vulkan backend via MoltenVK**. Once that runs, any problem in the native
Metal backend work is provably a Metal backend problem, and the Linux
Vulkan build remains available as a frame-for-frame oracle on the same
source shaders.

### Phase 0 — macOS bring-up on MoltenVK (de-risking, not shipping)

Goal: `rollnw-client` and `rollnw-server` build and run on the M5 MacBook Air
using the existing Vulkan backend, unmodified, over MoltenVK.

- Vendor or fetch MoltenVK (it is not currently in `external/`). Add an
  `APPLE` branch to `lib/nw/gfx/backends/vulkan/CMakeLists.txt` linking it,
  keeping the Win32/Wayland/Xlib branches untouched.
- Extend `cmake/GameNetworkingSockets.cmake`'s platform special-casing to
  `CMAKE_SYSTEM_NAME STREQUAL "Darwin"` (compile defs, crypto backend
  selection) — same OpenSSL crypto path already used on Linux should apply.
- Get DXC running on macOS: Microsoft ships `libdxcompiler.dylib` for macOS;
  swap `bin/libdxcompiler.so` fetch/load logic for the Apple case. Confirm
  `-spirv` output is unaffected by host OS (it should be — DXC's SPIR-V
  backend is host-independent).
- New `macos-renderer-dev` CMakePresets entry mirroring `linux-renderer-dev`.
- Acceptance: `rollnw-client` launches, imports a module, renders a test
  area, `rollnw-server` accepts a client connection, and the full
  `render_gfx_*`/`render_*` gtest suite passes headless on macOS.
- Explicitly throwaway: the MoltenVK branch should be removable in one
  commit once Phase 1 lands, so do not let it grow its own feature branches
  or workarounds beyond "make it boot."

### Phase 1 — native Metal backend in nw::gfx

Goal: `backends/metal/` implements `gfx.hpp` with no Vulkan or MoltenVK in
the loop; `render_gfx_*`/`render_*` tests pass against it unmodified; visual
output matches the Vulkan backend on the same scenes to the extent the two
APIs' rounding/behavior allow.

Structure, mirroring the Vulkan backend's shape:

- `metal_context.mm` (Objective-C++; Metal has no pure-C++ binding) —
  `MTLDevice`/`MTLCommandQueue` setup, swapchain via `CAMetalLayer` (SDL3
  exposes this through `SDL_Metal_CreateView`), per-frame command buffer and
  the `kFramesInFlight`-deep resource lifetime discipline the Vulkan backend
  already follows.
- Buffers/textures: direct `MTLBuffer`/`MTLTexture`, `MTLResourceOptions`
  `storageModeShared` vs `storageModePrivate` mapped from `BufferDesc
  ::cpu_visible` — a closer match to Vulkan's `cpu_visible` than most APIs,
  since Apple Silicon unified memory makes "shared" cheap.
  (map_buffer/unmap_buffer become close to no-ops for shared storage.)
- Pipelines: `MTLRenderPipelineState`/`MTLComputePipelineState` built from
  reflected binding info (same `SPIRV-Reflect` metadata already produced for
  the Vulkan path, since both backends compile from the same SPIR-V — see
  shader pipeline below).
- Bindless textures: Metal argument buffers, Tier 2
  (`MTLArgumentBuffersTier2`, available on every Apple Silicon GPU this
  project will target). `get_bindless_texture_index` becomes an index into
  an argument-buffer texture array exactly as it is an index into a
  descriptor-buffer-backed array today — this is the one piece of the
  existing bindless design that was clearly chosen with a path like this in
  mind, confirm it holds up once implemented rather than assume it.
- Render targets / load-store ops: `MTLRenderPassDescriptor`, straightforward
  mapping from `RenderLoadOp`/`RenderTargetDesc`.
- Indirect draws (`cmd_draw_indexed_indirect`, `..._indirect_count`): Metal's
  `ICB` (indirect command buffers) is the closest analog but has a materially
  different programming model (encoded ahead of time, not a GPU-buffer draw
  argument struct read at draw time like Vulkan/D3D indirect). This is the
  single highest-uncertainty item in the backend — budget real investigation
  time here, don't assume a thin mapping. If ICB proves too disruptive short
  term, a CPU-side readback-free fallback (encode the indirect draws as a
  compute pre-pass writing to an argument buffer, or drop to per-draw encode
  for the Metal backend's first cut) is an acceptable interim scope cut, but
  must be called out explicitly in the backend's status, not silently
  degraded.
- Native escape hatch: `native_metal.hpp` mirroring `native_vulkan.hpp`
  (`get_native_metal_core/frame/buffer/texture`, exposing `id<MTLDevice>`,
  `id<MTLCommandBuffer>`, etc.) for any caller (RmlUI render interface,
  debug tooling) that needs to reach the raw API, matching the existing
  contract's intent.
- GPU timers (`cmd_begin_gpu_timer`): `MTLCounterSampleBuffer`, a different
  API shape from Vulkan timestamp queries but equivalent capability.

**Shader pipeline decision — recommend SPIRV-Cross, not Apple's Metal Shader
Converter:**

Both DXC→SPIR-V→SPIRV-Cross→MSL and DXC→DXIL→Apple's `metal-shaderconverter`
are viable. Recommend SPIRV-Cross:

- It consumes the exact SPIR-V already produced today for the Vulkan
  backend — zero divergence in the shader source-of-truth, one compile
  step added after the existing one, no new DXC output mode to validate.
- `metal-shaderconverter` consumes DXIL, a separate DXC output target from
  the SPIR-V already in use, and is a newer/less-adopted tool outside
  Apple's own game-porting-toolkit context; it buys nothing here since
  SPIR-V is already the project's cross-compilation pivot format.
- SPIRV-Cross's MSL backend explicitly supports argument buffers
  (`msl_argument_buffers`) for the bindless path and ordinary binding/set
  decorations for everything else — the existing Vulkan backend uses normal
  SPIR-V binding decorations (descriptor buffers are an API-side resource
  supply mechanism, not a different shader interface), so no shader-side
  rework is implied.
- Known SPIRV-Cross risk surface to validate early, specifically against
  this project's shaders: `RWStructuredBuffer`/storage-buffer register
  mapping (t2/t3/t5/t6/t7 convention in `gfx.hpp`'s comments), the compute
  shaders (`forward_plus_cull.cs.hlsl`, `render_compute_smoke.cs.hlsl`), and
  `.inc.hlsl` shared includes that assume HLSL/SPIR-V binding conventions.

For distribution, split compile-time from runtime:

- macOS dev builds may keep DXC in the loop at build/first-run time (fast
  iteration, matches today's Vulkan dev flow).
- iOS/iPadOS must not depend on runtime DXC or SPIRV-Cross invocation —
  compile all shaders to `.metallib` offline as a build step and bundle
  them. This avoids any ambiguity around shipping a third-party JIT/compiler
  dylib in an App Store build and removes a large, unnecessary cold-start
  cost on a tablet.

Acceptance for Phase 1: `ROLLNW_BUILD_RENDERER` on macOS with the Metal
backend selected builds `rollnw-client`/`mudl`, the full `render_gfx_*`/
`render_*`/`render_forward_plus`/`render_particles`/`render_shadow_culling`
suite passes headless, and a manual side-by-side of the same imported module
area on Linux/Vulkan vs macOS/Metal shows no correctness divergence (this
project has no golden-image harness today — visual comparison here is
manual; do not invent a new automated image-diff framework for this alone
unless a specific regression demands it, consistent with how
`rendering-performance-overhaul.md` treats new measurement frameworks).

### Phase 2 — macOS client/server productionization

Builds on Phase 1's backend, not Phase 0's MoltenVK bring-up (which is
retired once Phase 1 lands).

- Code signing / notarization for `rollnw-client` and `rollnw-server`
  (`codesign`, `notarytool`), since both are distributed outside the Mac App
  Store at this stage — Gatekeeper will block an unsigned/unnotarized binary
  by default on a fresh machine.
- `GameNetworkingSockets` on macOS: confirm the OpenSSL crypto backend
  builds against macOS's OpenSSL (not the deprecated system
  LibreSSL/Secure Transport shim) — likely needs the same vendored/brew
  OpenSSL path the project already requires for the Linux server build,
  pinned the same way.
- Add a macOS CI leg (GitHub Actions ships Apple Silicon runners) running
  the same `render_gfx_*` suite, so the Metal backend has a continuously
  verified oracle instead of depending on manual runs on one laptop. This is
  the point where "runs on my Mac" becomes a durable guarantee rather than a
  one-time fact.
- Packaging: reuse whatever `scripts/ci/validate_tool_packages.cmake` /
  `prepare_release.cmake` already do for Linux/Windows, extended for a macOS
  artifact (`.app` bundle or plain binary + `bin/shaders`/`bin/stdlib`
  alongside, matching the existing install layout).

### Phase 3 — iPadOS client, full toolset, touch-adapted

The largest and least graphics-related phase. Depends on Phase 1 (Metal
backend; MoltenVK is explicitly out of scope for iOS — Apple's own platform,
no reason to carry a translation layer) and partially on Phase 2's shader
bundling decision.

**Resolved: iPadOS is client-only, always networked to a server running
elsewhere.** `rollnw-server` is not being ported to iPadOS. This matters
beyond scope-trimming: today's desktop single-player/solo-editing flow
(`tools/client/local_server.cpp`) works by spawning `rollnw-server` as a
*child process* via `SDL_Process` and talking to it over loopback
GameNetworkingSockets — iOS does not allow an app to spawn arbitrary
subprocesses, and that path is explicitly out of scope here anyway. So the
iPad client has no "local server" mode at all: it needs a `rollnw-server`
already running and reachable on the network (most likely the same M5 Mac,
or a Linux box, from Phase 2) for any use, including solo play. This makes
Phase 2's macOS server work a practical (if not strict build-order)
prerequisite for the iPad client being useful for anything beyond
connecting to someone else's already-running server.

Open questions that materially change scope and should be settled before
deep investment (listed here rather than guessed at):

- **Minimum supported iPad generation.** Determines whether Tier-2 argument
  buffers (A13/Apple7+) and available unified memory for texture/model
  residency are guaranteed floors or per-device fallbacks. Pick a floor (e.g.
  "M-series iPad only," or "A14+") rather than discovering the constraint
  from a crash report.
- **Distribution channel.** App Store vs TestFlight vs sideloaded/enterprise
  changes what's permissible (background networking entitlements, file
  access for NWN module import — iPadOS's sandboxed file model is a real
  departure from `tools/client`'s free-form filesystem project/import flow
  in `project_import.cpp`/`client_preferences.cpp`) and the signing story.
- **GameNetworkingSockets on iOS, client role only.** GNS itself has real
  upstream iOS support (its `GNS_APPLE_EMBEDDED` CMake mode — see Current
  state above), and iPadOS never hosts (above), so this only needs GNS's
  *connecting-client* code path exercised on iOS/arm64 through that
  embedded build mode (forced static lib or `.framework`, `ENABLE_ICE=OFF`
  already set project-wide, OpenSSL built for the iOS SDK/arch). Lower risk
  than initially assumed, but still unexercised by this project specifically
  and worth an early spike before relying on it — if OpenSSL-for-iOS or the
  static build proves awkward in practice, the fallback is a lighter
  custom client-only transport for iPad — flag that decision point now
  rather than discovering it mid-Phase-3.
- **Touch input/UI model for an editor, not a game.** `tools/client`'s input
  stack (`client_input.cpp`, `pc_input.hpp`, `viewport_pointer_drag.hpp`,
  drag-and-drop in `inventory_drag.cpp`/`project_resource_drag.cpp`) assumes
  a mouse (hover, right-click, precise single-pixel drag) and a keyboard
  (shortcuts throughout `client_application_keys.cpp`). This is a genuine
  UX redesign, not a input-event-remapping exercise: hover has no touch
  equivalent, precision dragging on a 11-13" tablet is materially different
  from a desktop mouse, and keyboard-shortcut-driven commands
  (`command_bus.hpp`, `command_view.hpp`) need an on-screen or
  external-keyboard-optional path. RmlUI itself can take touch events
  (SDL3 reports them), but the *interaction design* built on top is 100%
  new work, not a backend concern at all.
- **Module import mostly moves off-device, but isn't fully eliminated.**
  Because the iPad never runs its own server, the `NWN_ROOT`/`NWN_HOME` +
  `project_import.cpp` module-import flow against a local NWN:EE install
  has nowhere to run on iPad and doesn't need to: importing happens once,
  on the server's host (the Mac/Linux box from Phase 2). What's left to
  design is narrower but not zero — the project/area *browsing and editing*
  UI on iPad still needs its own answer for anything that currently assumes
  local filesystem access for non-import purposes (screenshots, exported
  files, `client_preferences.cpp` settings storage) under iPadOS's sandboxed
  file model.

Given the size of this phase, treat it as its own planning pass once
Phases 1-2 land and the open questions above have answers — this document
intentionally does not sequence Phase 3's internal work further, to avoid
planning against unknowns that are cheap to resolve first and expensive to
guess wrong.

## Cross-cutting risks

| Risk | Why it matters | Mitigation already in this plan |
| --- | --- | --- |
| Metal indirect-draw model mismatch | `cmd_draw_indexed_indirect*` is load-bearing for GPU-driven culling work already in `nw::render` | Called out explicitly in Phase 1 as the highest-uncertainty item; interim scope cut allowed if flagged |
| SPIRV-Cross MSL gaps on this project's actual shaders | Storage buffer register convention, compute shaders, shared `.inc.hlsl` | Validate against the real shader set early in Phase 1, not a synthetic test shader |
| GNS's iOS embedded-build mode (client role only — iPad never hosts) is unexercised by this project | Lower risk than an unsupported platform, but still unverified in practice | Named as an early spike with a fallback path, not assumed to work untested |
| iPad client has no reachable server to use without Phase 2 | iPad is client-only (`local_server.cpp`'s subprocess model doesn't apply there); the client is inert without a `rollnw-server` running elsewhere | Noted as a soft prerequisite in Phase 3, not just a graphics dependency |
| One-laptop verification | Phase 0/1 acceptance currently depends on manual runs on a single M5 Air | Phase 2 adds macOS CI specifically to turn this into a durable guarantee |
| iPadOS sandboxing vs the desktop import/project model | `project_import.cpp`'s filesystem assumptions don't hold | Named as an open question to resolve before Phase 3 work starts, not mid-phase |

## What this plan deliberately does not do

- It does not commit to calendar time. Each phase's acceptance criteria are
  concrete (tests pass, binary runs, CI is green); size them once Phase 0 is
  underway and the toolchain's actual friction is known, rather than
  guessing now.
- It does not touch Windows/Linux Vulkan code paths. The Metal backend is
  additive (`backends/metal/`), same as Vulkan's `backends/vulkan/`.
