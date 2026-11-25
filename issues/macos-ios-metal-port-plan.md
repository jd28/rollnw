# macOS / iPadOS port and nw::gfx Metal backend

Status: **Phase 1 complete on macOS Apple Silicon** (native Metal 3 backend in
`lib/nw/gfx/backends/metal/`, Slang MSL pipeline, `macos-renderer-dev` preset,
`mudl` with `imgui_impl_metal.mm`, and `rollnw-client` with native RmlUi MSL
shaders — verified under `MTL_DEBUG_LAYER=1` via `RenderGfx*`, `Render*:ClientGfx*`,
`mudl`, and live module rendering in `rollnw-client` on Apple M5).
**Amended 2026-11-25: Phase 0 (MoltenVK bring-up) has been skipped and removed.**
Because the Slang shader toolchain migration has already landed on `main`
(providing pinned `macos-aarch64` prebuilts and native MSL output), the port
went straight to the native Metal backend with no MoltenVK dependency at any stage.

Landed on `main`, but grounded
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

- `nw::gfx` (`lib/nw/gfx/gfx.hpp`) is a backend-agnostic handle/desc API —
  `Core`/`Context`/`Buffer`/`Texture`/`RenderTarget`/`Shader`/`Pipeline`/
  `CommandList`, with no backend types in the public header. Two backends now
  implement this contract: `lib/nw/gfx/backends/vulkan/` (Linux/Windows,
  `native_vulkan.hpp`) and `lib/nw/gfx/backends/metal/` (macOS Apple Silicon,
  `native_metal.hpp`).
- The API is already bindless-leaning (descriptor-buffer-backed sampled
  textures, `BindlessTextureIndex`, `uses_bindless_sampled_textures`). This
  maps cleanly onto Metal argument buffers (Tier 2, Apple7/A13+ and all
  M-series) — no redesign needed, just a different implementation of the same
  contract.
- Shaders are authored once in HLSL (27 files in `lib/nw/render/shaders/`;
  `bin/shaders/` is a gitignored build copy) and compiled at runtime by
  **Slang** (`2026.22.1`, pinned per-platform prebuilts including
  `macos-aarch64`) via `lib/nw/render/shader_provider.cpp`.
- **The descriptor binding contract is explicit in both HLSL and C++.**
  Shaders carry explicit `[[vk::binding(binding, set)]]` attributes alongside
  HLSL `register()` declarations. Slang maps `space0` bindings `1..7` directly
  to Metal `[[buffer(1..7)]]` (leaving `buffer(0)` free for vertex buffers),
  and a shared `bindless_textures.inc.hlsl` header exposes the `space1`
  bindless texture/sampler table as a Tier-2 `ParameterBlock` at `[[buffer(8)]]`
  on Metal while leaving the Vulkan SPIR-V `(set=1, binding=2/3)` layout
  unchanged.
- `README.md` and `CMakePresets.json` now include macOS Apple Silicon renderer
  presets (`macos-renderer-dev`, `macos-renderer-dev-debug`) building
  `rollnw-client` and `mudl` via the native Metal backend (`rollnw-server`
  remains gated on the `server` branch).
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
- MoltenVK in any form. Apple platforms target Metal natively through
  `lib/nw/gfx/backends/metal/` with no Vulkan-over-Metal translation layer.
- Windows/Linux regressions. Every phase must keep existing CI green.

## Sequencing rationale

With the Slang shader toolchain migration already landed on `main`, the port
proceeds directly to the native Metal backend in Phase 1 (skipping any
intermediate Vulkan-over-Metal bring-up). The existing headless
`render_gfx_*`/`render_*` gtest suite and the Linux/Vulkan machine serve as
the correctness oracle against the same HLSL sources.

### Phase 1 — native Metal backend in nw::gfx

Goal: `backends/metal/` implements `gfx.hpp` natively on Metal;
`render_gfx_*`/`render_*` tests pass against it; visual output matches the
Vulkan backend on the same scenes to the extent the two APIs'
rounding/behavior allow.

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
- Pipelines: `MTLRenderPipelineState`/`MTLComputePipelineState`. Note the
  Vulkan backend does **not** reflect binding info — it uses the hardcoded
  set/binding table described in Current state, so the Metal backend needs
  its own explicit argument-buffer index mapping derived from the same
  contract, not a reflection pass. (The original text here claimed
  `SPIRV-Reflect` metadata was "already produced for the Vulkan path";
  SPIRV-Reflect is linked but unused.) Once the shader sources carry
  explicit `[[vk::binding]]` attributes from the migration plan's Phase A,
  that contract is readable in one place instead of two.
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

**Shader pipeline decision — Slang, superseding the earlier SPIRV-Cross
recommendation (revised 2026-11-22):**

The original decision here was DXC -> SPIR-V -> SPIRV-Cross -> MSL, chosen
because it reused the SPIR-V already produced for Vulkan. Two findings
changed it:

- DXC cannot be obtained for Apple Silicon at all (see Current state), so
  "reuse the existing compiler" was never actually available on this
  platform.
- The pipeline required two tools with independently-evolving opinions
  about descriptor layout to agree. The risk list the original decision
  wrote for itself — storage-buffer register mapping, the compute shaders,
  the shared `.inc.hlsl` includes — was a direct consequence of that
  two-tool split, not of anything about MSL.

Slang resolves both: it ships pinned prebuilts for macos-aarch64 (and
linux-aarch64, which DXC also lacks), and it emits SPIR-V and MSL from one
front end with one layout engine, so the two targets agree by
construction. Full reasoning, phasing and acceptance criteria live in
[Shader toolchain migration: DXC to Slang](closed/shader-toolchain-slang-migration.md);
that prerequisite has already landed on `main`.

What remains Phase 1's responsibility is only the Metal half: enabling
Slang's MSL/`metallib` target for the Metal backend and validating it
against this project's real shader set — the compute shaders
(`forward_plus_cull.cs.hlsl`, `render_compute_smoke.cs.hlsl`) and the 12
bindless `space1` texture/sampler declarations first, since those are the
least conventional HLSL in the set. By then it is a target flag on a
compiler already in the build.

**SPIRV-Cross remains the documented fallback.** If Slang's MSL output proves
inadequate for any future shader, SPIRV-Cross consuming *Slang-generated*
SPIR-V keeps the single-source-of-truth property and requires no DXC.

For distribution, split compile-time from runtime:

- macOS dev builds may keep a runtime compiler in the loop for fast
  iteration, matching today's Vulkan dev flow.
- iOS/iPadOS must not invoke a shader compiler at runtime — compile all
  shaders to `.metallib` offline as a build step and bundle them. This
  avoids any ambiguity around shipping a third-party JIT/compiler dylib in
  an App Store build and removes a large, unnecessary cold-start cost on a
  tablet. With one tool serving both targets this is the same offline bake
  step the migration plan's Phase D introduces for SPIR-V, parameterized
  by target, rather than a Metal-only special path.

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

Builds directly on Phase 1's native Metal backend.

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

The largest and least graphics-related phase. Depends on Phase 1 (native Metal
backend) and partially on Phase 2's shader bundling decision.

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
  changes what's permissible (background networking entitlements, the file
  access needed for the game-data transfer story below) and the signing
  story.
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
- **Project-authoring import moves off-device.** Because the iPad never runs
  its own server, the `project_import.cpp` module-import flow (NWN:EE
  install -> editable project JSON/CAF) has nowhere to run on iPad and
  doesn't need to: that happens once, on the server's host (the Mac/Linux
  box from Phase 2). What's left to design is narrower but not zero — the
  project/area *browsing and editing* UI on iPad still needs its own answer
  for anything that currently assumes local filesystem access for
  non-import purposes (screenshots, exported files, `client_preferences.cpp`
  settings storage) under iPadOS's sandboxed file model. This is separate
  from, and smaller than, the raw game-data transfer problem below.

#### Getting NWN game data onto iPadOS

Project-authoring import moving server-side (above) does **not** mean the
iPad client is free of needing local game data. `tools/client`'s own
startup path requires it unconditionally: `client_application.cpp` fails
outright — "failed to find NWN install; set NWN_ROOT and NWN_HOME" — if it
can't find a local NWN:EE data tree, independent of whether any server is
involved, because BioWare/Beamdog's assets are never bundled with rollnw
and never will be. A remote server supplies authoritative object/module
state, but resolving a resref to an actual mesh/texture/2da is a client-side
job today and there is no reason that changes on iPad. This is exactly the
problem OpenMW's iOS port solves for Morrowind's data files, for the same
underlying reason (the game data is the user's own legally-owned copy, not
something the app can ship) — "how do I get my data files onto the
sandboxed device" is a real, previously-solved problem worth copying from,
not a novel one.

Two distinct transfers, different size/frequency profiles:

1. **Base NWN:EE data** (`nwn/data` plus any expansions) — large (likely
   multiple GB), one-time per device.
2. **Per-module hak sets** — smaller individually, but changes per server/
   module the user connects to, and needs rechecking whenever a new server
   is joined.

iOS has no "point at an arbitrary folder on this volume" primitive, so this
needs a deliberately layered set of mechanisms rather than one, roughly in
build-it-first order:

1. **Finder/iTunes file sharing** (`UIFileSharingEnabled` +
   `LSSupportsOpeningDocumentsInPlace` in `Info.plist`) as the primary bulk
   path. Plug the iPad into the Mac — plausibly the same Mac running the
   Phase 2 server — and drag the existing `nwn/data`/hak folders straight
   into the app's Documents from Finder's Files tab. This is nearly free to
   add (two plist keys, no new code) and, importantly, needs **no new
   container/package format**: it preserves the same directory layout
   `ResourceManager` already scans, so the only change on the resource-
   loading side is pointing its root at the app's Documents directory
   instead of `NWN_ROOT` — the scanning code itself is unchanged from
   desktop.
2. **AirDrop / Share Sheet "Open in rollnw-client"** for single files (one
   hak, a small pre-zipped module pack) — register a custom document type
   (UTI) so dropping a file via AirDrop, Mail, or another app's share sheet
   routes it straight into the app through `application(_:open:)`. This is
   the literal "AirDrop, like OpenMW" mechanism the question was about —
   good for small, occasional transfers, not a realistic way to move a
   multi-GB base install.
3. **Files app document picker** (`UIDocumentPickerViewController` +
   security-scoped bookmarks) — the Apple-blessed way to point at a folder
   already sitting in iCloud Drive or a third-party cloud provider without
   copying it. Useful once data is already staged somewhere cloud-synced,
   but doesn't solve getting it there in the first place — complements (1)
   and (2), doesn't replace them.
4. **Stretch: an in-app LAN HTTP(S) upload server**, foreground-only (iOS
   suspends background sockets), so transfer can happen wirelessly from any
   machine on the same Wi-Fi without a cable — the same pattern several iOS
   file-manager/emulator apps use to avoid requiring one. Real added
   complexity (a small server plus a minimal upload page, and it should
   require an on-screen PIN so other devices on the same network can't push
   files uninvited). Worth building only if cable-based Finder sharing
   proves too much friction in practice — not a day-one requirement, and
   unrelated to the GameNetworkingSockets game-protocol work above (it's a
   plain file upload, nothing to do with the game server connection).

Recommend shipping (1) first — it covers the large bulk-transfer case for
near-zero implementation cost and requires no new resource-loading code.
Add (2) next for convenience on small files. Treat (3) and (4) as later
polish once the core transfer story already works, not blockers for a
first working iPad build.

Worth scoping into Phase 3 regardless of which mechanisms ship: a
connect-time **data manifest check** — the server reports which haks/
version hashes a module expects, the client diffs that against its local
data tree and tells the user exactly what's missing *before* anything fails
deep inside resource loading. On desktop a missing hak is at least visible
in a terminal/log; on a touch device with no console, "here's what to go
get and where" is the difference between a usable error and a silent pink
missing-texture box with no explanation.

Given the size of this phase, treat it as its own planning pass once
Phases 1-2 land and the open questions above have answers — this document
intentionally does not sequence Phase 3's internal work further, to avoid
planning against unknowns that are cheap to resolve first and expensive to
guess wrong.

## Cross-cutting risks

| Risk | Why it matters | Mitigation already in this plan |
| --- | --- | --- |
| Metal indirect-draw model mismatch | `cmd_draw_indexed_indirect*` is load-bearing for GPU-driven culling work already in `nw::render` | Called out explicitly in Phase 1 as the highest-uncertainty item; interim scope cut allowed if flagged |
| Shader compiler unobtainable on Apple Silicon | DXC ships no macOS or arm64 build and has no Homebrew formula | Resolved — [the Slang migration](closed/shader-toolchain-slang-migration.md) landed on `main` with pinned `macos-aarch64` prebuilts |
| Shader compiler change renumbers SPIR-V bindings | `vulkan_context.cpp` hardcodes every set/binding literal and agrees with the shaders only via DXC's default register mapping; a silent renumber breaks all pipelines with no compile error | Migration plan pins bindings with explicit `[[vk::binding]]` before any compiler swap, and diffs the decorations as the first acceptance gate |
| Slang's MSL backend is younger than SPIRV-Cross's | Metal codegen quality must be proven on this shader set | Validated all 21 shaders + shadow filter test on Apple M5 Metal; SPIRV-Cross consuming Slang SPIR-V remains the documented fallback if a future shader hits a codegen gap |
| GNS's iOS embedded-build mode (client role only — iPad never hosts) is unexercised by this project | Lower risk than an unsupported platform, but still unverified in practice | Named as an early spike with a fallback path, not assumed to work untested |
| iPad client has no reachable server to use without Phase 2 | iPad is client-only (`local_server.cpp`'s subprocess model doesn't apply there); the client is inert without a `rollnw-server` running elsewhere | Noted as a soft prerequisite in Phase 3, not just a graphics dependency |
| iPad has no local game data and no way to point at an arbitrary folder | `client_application.cpp` won't even start without a resolvable NWN:EE data tree; this holds regardless of server location | Layered transfer plan in Phase 3 (Finder file sharing first, AirDrop/UTI second, Files picker + LAN upload later); no new asset package format needed |
| One-laptop verification | Phase 1 acceptance currently depends on manual runs on a single M5 Air | Phase 2 adds macOS CI specifically to turn this into a durable guarantee |
| iPadOS sandboxing vs the desktop import/project model | `project_import.cpp`'s filesystem assumptions don't hold | Named as an open question to resolve before Phase 3 work starts, not mid-phase |

## What this plan deliberately does not do

- It does not commit to calendar time. Each phase's acceptance criteria are
  concrete (tests pass, binary runs, CI is green); size them as Phase 1
  progresses and the toolchain's actual friction is known, rather than
  guessing now.
- It does not touch Windows/Linux Vulkan code paths. The Metal backend is
  additive (`backends/metal/`), same as Vulkan's `backends/vulkan/`.
