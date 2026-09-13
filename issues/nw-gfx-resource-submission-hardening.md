# nw::gfx Resource Submission and Lifetime Hardening

Status: open.

Tier: 2. This touches public resource contracts, upload submission, GPU
completion, and backend ownership.

## Problem

The steady render path is already compact enough on the measured development
workload, but resource creation and lifetime management still perform work per
texture and expose contracts that are either broader or less explicit than the
Vulkan backend implements.

The input is a batch of decoded texture mip payloads, resource-destruction
requests, and per-frame draw packets. The required output is resident textures,
valid command buffers, and reclamation only after the last GPU use. The current
texture path transforms each upload independently: allocate staging resources,
record one command buffer, submit, wait, and destroy the temporary resources.
The current destruction path instead requires callers to prove safety with a
whole-device wait or an undocumented stronger policy.

Do not replace the working frame resource-binding model as part of this issue.
First remove repeated upload work, make failure behavior deterministic, and
make completion/lifetime boundaries explicit. Revisit draw-resource encoding
only if a representative larger workload measures it as material CPU cost.

## Platform and Constraints

- The backend targets Vulkan 1.3 and currently submits to one graphics queue.
- `nw::gfx` has exactly two frame slots, one command buffer and one fence per
  slot (`lib/nw/gfx/gfx.hpp:155`,
  `lib/nw/gfx/backends/vulkan/vulkan_internal.hpp:173`).
- VMA already owns Vulkan memory selection and allocation. Extend that path;
  do not add a second allocator.
- Existing consumers use opaque generational handles and free `nw::gfx`
  functions. Preserve that API style and the repository's explicit `bool` or
  invalid-handle failure results.
- Texture upload inputs are decoded byte spans whose owners may release or
  reuse the CPU memory after the upload call returns. Any asynchronous path
  must copy those bytes into `nw::gfx`-owned staging storage before returning.
- The common renderer path is single-threaded today. This issue does not add
  concurrent command recording, a transfer queue, or multi-device support.

ASSUMPTION: one active `Context` per `Core` is the supported runtime shape —
affects whether global resource pools can be constrained rather than moved.
Verify this against every context creation site before implementation.

ASSUMPTION: texture uploads occur at load/preview boundaries rather than in the
steady per-frame path — affects whether one synchronous wait per upload batch
is acceptable. Record upload timestamps from a representative client session
before choosing asynchronous submission.

## Observed Data (2026-09-12)

### Steady frame

Linux headless Vulkan on the development workstation, with an AMD Ryzen AI 9
HX 370 CPU, 1280x720 output, two warmup frames, and ten measured frames:

```text
mudl area-benchmark start \
    --module ./tests/test_data/user/modules/DockerDemo.mod \
    --user ./tests/test_data/user \
    --frames 10 --warmup 2 --camera fit
```

The measured frames reported:

- 580 draws per frame: 407 local-shadow, 163 opaque, and 10 particle draws.
- 581 resource binds and zero resource-bind skips.
- 1,151 uniform allocations and 777,376 uniform bytes per frame.
- 64,224 descriptor-allocation bytes per frame.
- 7 Vulkan pipeline binds and 574 cached pipeline-bind skips.
- Zero descriptor allocation failures, resource-bind failures, or dropped
  draws.
- 0.148 ms average recorded CPU renderer time, with a 0.190 ms maximum.

This is a small functional workload, not a stress test. The timer is the
renderer's recorded CPU interval, not end-to-end frame latency. The exact GPU
and driver identity were not captured, so the result does not support a
cross-device performance claim. It does show that a frame binding redesign is
not justified by this sample.

The common model draw copies two constant records into the per-frame uniform
ring, encodes one resource binding, and emits one indexed draw per surface
(`lib/nw/render/model_renderer.cpp:322`). Prepared surfaces are validated into
a contiguous index list first, but submission still loops over the valid
surface indices (`lib/nw/render/model_renderer.cpp:560`).

### Texture uploads

- The public API has three singular upload entry points
  (`lib/nw/gfx/gfx.hpp:354`).
- Each base-level upload creates one staging buffer, command pool, command
  buffer, and fence; submits once; waits for that fence; and destroys all four
  temporary objects (`lib/nw/gfx/backends/vulkan/vulkan_context.cpp:1016`).
- The mip-chain upload duplicates that machinery
  (`lib/nw/gfx/backends/vulkan/vulkan_context.cpp:1237`).
- Actual upload callers exist in the common model backend, model asset loader,
  NWN render asset cache, mudl runtime, and client UI renderer.
- No first-party upload scheduler, completion-point abstraction, or deferred
  destruction queue exists under `lib/nw/gfx` or `lib/nw/render`.

The missing data is the distribution of upload count, total bytes, largest
payload, and wall time per asset-load transaction. Capture those values before
selecting a persistent staging capacity or claiming a load-time improvement.

### Contract and ownership findings

- `RenderTargetDesc` exposes four color attachments, mip levels, and layers
  (`lib/nw/gfx/gfx.hpp:360`). The Vulkan backend accepts only `color[0]`, mip
  zero, and layer zero, and explicitly rejects the rest
  (`lib/nw/gfx/backends/vulkan/vulkan_context.cpp:172`). No current call site
  populates `color[1]` through `color[3]`.
- Invalid pipeline, vertex-buffer, and index-buffer binds return without
  invalidating previously cached state
  (`lib/nw/gfx/backends/vulkan/vulkan_context.cpp:1633`). A later draw can
  therefore observe stale state. Resource binding already has the stronger
  behavior: it marks the command invalid and increments failure/drop counters.
- Buffer, shader, and pipeline pools are process-global
  (`lib/nw/gfx/backends/vulkan/vulkan_internal.hpp:127`), although each payload
  records its owning core. Texture and render-target pools are context-owned.
- `destroy_*` releases backend resources immediately. The public contract
  requires `wait_idle` or an external completion policy
  (`lib/nw/gfx/gfx.hpp:158`).

## Patterns & Conventions Found

- Per-frame transient data uses context-owned, persistently mapped linear rings
  and explicit high-water/failure counters. Reuse this access pattern for
  staging data rather than creating an unrelated allocator.
- `CommandStats` and `mudl area-benchmark` already provide the reporting path
  for command, uniform, descriptor, and dropped-draw evidence. Extend those
  records for upload batches and waits.
- Backend errors use `SDL_LogError`, invalid handles, or `false`; command
  resource failures additionally poison the pending draw. Match that behavior.
- The renderer prepares batches in contiguous vectors and then submits their
  valid indices. Texture upload should accept a batch directly; singular calls
  may remain only as compatibility wrappers over a batch of one.
- VMA is the existing memory allocation dependency. Volk exposes timeline
  semaphore functions, but the project has no existing timeline policy to
  reuse.

## Architecture Decision

Implement one context-owned upload path whose primary input is a contiguous
batch of texture upload records. Start with one synchronous submission per
batch, reusing VMA and the graphics queue. This removes per-texture command and
wait machinery without adding asynchronous lifetime states before load traces
show that they are required.

At the same time, make existing public/backend contracts truthful:

1. Reduce the public render-target color contract to the single attachment the
   backend and all current callers use. Keep mip zero and layer zero implicit.
2. Make an invalid pipeline, vertex-buffer, index-buffer, or resource bind
   poison the pending draw, increment a failure counter, and produce a
   validation-build diagnostic. Never continue with stale state.
3. Audit the one-context-per-core assumption. If it is true, enforce it at
   context creation and document the global-pool lifetime. Do not refactor pool
   ownership for unsupported multi-context behavior.
4. Measure whole-device waits and resource replacement. Add scoped completion
   points and deferred reclamation only if the trace shows `wait_idle` on a
   live path or an actual unsafe lifetime boundary.

The accepted trade-off is that the first batch implementation still waits
once. Its state machine is allocate, copy, record all, submit once, wait once,
release. Moving to persistent asynchronous staging would add ring wrap,
in-flight range tracking, completion values, back-pressure, and shutdown
states. Those costs require measured need.

The descriptor-buffer draw path, pipeline resource flags, shader resource
contract, queue count, and frames-in-flight count remain unchanged.

## Component Design

- Component: texture upload batch protocol
  - File path: `lib/nw/gfx/gfx.hpp`
  - Responsibilities: describe one texture and its ordered mip payloads; accept
    a contiguous batch; report success or reject the complete batch.
  - Dependencies/reuse: `Handle<Texture>`, `TextureMipData`, `std::span`, and
    existing texture format/extent metadata.
  - Interfaces: add a plural `upload_textures` entry point. Keep existing
    singular functions temporarily as batch-of-one wrappers while call sites
    migrate.
  - Ownership/lifetime model: inputs are non-owning for the duration of the
    call. The synchronous implementation completes GPU reads before returning.
  - Error model: prevalidate every record before allocating or submitting. An
    invalid handle, mip count, extent, byte size, null payload, overflow, or
    unsupported format rejects the entire batch and leaves every texture in
    its pre-call state.

- Component: Vulkan batch uploader
  - File path: `lib/nw/gfx/backends/vulkan/vulkan_context.cpp`
  - Responsibilities: prefix-sum aligned payload sizes, allocate and map one
    staging buffer, copy input bytes linearly, record all image transitions and
    copies in one command buffer, submit once, wait once, then release staging
    and command resources.
  - Dependencies/reuse: VMA, existing format-size and image-transition helpers,
    the context graphics queue, and current upload validation.
  - Interfaces: backend implementation of `upload_textures`; no new public
    Vulkan type.
  - Ownership/lifetime model: the context owns all temporary Vulkan objects for
    the duration of the call. Texture handles remain owned by their caller.
  - Error model: reject before submission when possible. After submission,
    propagate fence/queue failures through the established fatal Vulkan-error
    policy; do not publish partially transitioned texture state as success.

- Component: command binding validity
  - File path: `lib/nw/gfx/backends/vulkan/vulkan_internal.hpp` and
    `lib/nw/gfx/backends/vulkan/vulkan_context.cpp`
  - Responsibilities: represent whether the next draw has all required valid
    state and account for each rejected bind/draw.
  - Dependencies/reuse: existing command state cache, `CommandStats`, and
    `resource_bind_failed` behavior.
  - Interfaces: no public API addition; extend command statistics only if the
    existing aggregate failure counter cannot state the cause.
  - Ownership/lifetime model: validity is reset at render-pass begin and stored
    only in the current command list.
  - Error model: invalid input fails loudly in validation output and causes
    deterministic draw rejection in every build.

- Component: truthful render-target contract
  - File path: `lib/nw/gfx/gfx.hpp`, renderer call sites, and Vulkan validation
    helpers.
  - Responsibilities: expose exactly one color texture and one depth texture,
    both at mip zero/layer zero.
  - Dependencies/reuse: existing `RenderTargetAttachment` ownership behavior.
  - Interfaces: remove the unused color array and unused mip/layer fields rather
    than retaining unsupported states.
  - Ownership/lifetime model: unchanged; the render target owns its attachment
    handles until that policy is separately reconsidered with real sharing
    data.
  - Error model: invalid or missing handles return an invalid render target and
    log the rejected contract.

## Batch Transform Contract

Input layout:

- A contiguous span of upload records.
- Each record contains one valid texture index and a contiguous, base-to-last
  mip span.
- Each mip contains a non-null byte pointer, exact byte count, width, and
  height. Width and height are positive, within the texture descriptor, and
  match the expected mip progression.

Output layout:

- No separate output allocation. On success, every target texture contains the
  supplied mip data and is in its normal shader-readable layout.
- On validation failure, no command is submitted and no target state changes.
- Duplicate texture handles in one batch are rejected; last-write-wins behavior
  would add an unneeded state.

Owner and lifetime:

- The caller owns record and pixel memory until `upload_textures` returns.
- `nw::gfx` owns the temporary packed staging bytes and Vulkan submission
  objects.
- Texture ownership remains unchanged.

Cost on the current platform:

- Validation and prefix sizing: O(textures + mips).
- CPU copy and GPU transfer: O(total payload bytes), which is required work.
- Temporary host-visible GPU allocation: the aligned sum of batch payload
  bytes, plus one offset/region record per mip.
- Vulkan setup/submission/wait: one of each per batch instead of one per
  texture. The performance effect is a hypothesis until before/after load
  traces are recorded.

Access pattern and branches:

- CPU reads source payloads sequentially and writes the packed staging mapping
  linearly.
- Validation is a separate pass, keeping error branches out of the copy and
  command-recording passes.
- Command recording traverses the same upload/mip order linearly.

## Implementation Map

- Modify `lib/nw/gfx/gfx.hpp`: narrow the render-target contract, define the
  flat upload-batch records, add the plural upload entry point, and document
  exact failure/lifetime behavior.
- Modify `lib/nw/gfx/backends/vulkan/vulkan_context.cpp`: consolidate duplicated
  pixel/mip upload machinery into the batch transform; poison invalid command
  state; simplify render-target validation to the supported shape.
- Modify `lib/nw/gfx/backends/vulkan/vulkan_internal.hpp`: add only the command
  validity/counters required by deterministic draw rejection.
- Modify upload call sites under `lib/nw/render`, `tools/mudl`, and
  `tools/client`: form real load-transaction batches where decoded payload
  lifetimes already overlap. Do not retain decoded pixels merely to make a
  larger artificial batch.
- Modify `tools/mudl/viewer_runtime.cpp`: report upload batch count, texture
  count, byte count, staging high-water bytes, submission count, wait count,
  and CPU wall time.
- Modify or add focused gfx/render tests using the existing test organization;
  no new test framework.

## Data Flow

```text
decoded texture/mip spans
    -> validate all handles, formats, extents, sizes, duplicates, and sums
    -> compute aligned offsets once
    -> allocate/map one VMA staging buffer
    -> copy bytes linearly
    -> record transitions and copies linearly in one command buffer
    -> submit once to the existing graphics queue
    -> wait once
    -> publish shader-readable texture layouts
    -> destroy temporary staging/command resources
```

Failure before submission leaves all texture states unchanged. A Vulkan error
after submission follows the backend's existing fatal error policy because the
GPU-visible state can no longer be rolled back safely.

## Build Sequence

- Phase 1: make contracts truthful
  - [ ] Verify and enforce the one-context-per-core constraint, or record the
        concrete call site that disproves it before changing pool ownership.
  - [ ] Reduce `RenderTargetDesc` to the shape all current callers and the
        backend implement.
  - [ ] Make every invalid command bind poison the pending draw and add tests
        proving stale bindings cannot be consumed.
- Phase 2: observe upload transactions
  - [ ] Add upload counters and wall-time reporting without changing behavior.
  - [ ] Capture representative area load, area switch, object preview, and UI
        texture traces, including count and byte distributions.
  - [ ] Define the natural batch boundaries from overlapping decoded-payload
        lifetimes in those traces.
- Phase 3: implement the synchronous batch transform
  - [ ] Add and test `upload_textures` validation as a separate pass.
  - [ ] Replace duplicated upload implementations with one packed staging
        allocation and one recorded submission per batch.
  - [ ] Migrate real batch-capable callers; retain singular wrappers only for
        genuinely isolated uploads.
  - [ ] Record before/after load wall time, allocations, submissions, waits,
        uploaded bytes, validation output, and rendered screenshots.
- Phase 4: completion decision gate
  - [ ] Record every `wait_idle` call during a representative authoring session
        with its reason and CPU duration.
  - [ ] If scoped upload waits or resource replacement materially stall that
        trace, file a separate issue with the observed completion/lifetime data
        and an explicit retirement protocol.
  - [ ] Otherwise retain synchronous batch completion and immediate destruction;
        do not add timeline or deferred-reclamation states.

## Simplification Pass

- Do not redesign frame binding: the available benchmark does not show a
  material cost.
- Do upload validation once before allocation rather than checking error cases
  through the copy loop.
- Do one allocation, command recording, submission, and wait per natural batch
  instead of per texture.
- Constrain render targets to the one color mip-zero/layer-zero case actually
  used instead of carrying unsupported array states.
- Constrain core/context cardinality if the observed application already has
  one; do not pay for multi-device ownership machinery without a consumer.
- Do not add a transfer queue, worker thread, persistent staging ring, timeline
  semaphore, deferred delete queue, generalized barrier API, new allocator, or
  shader resource protocol in this issue.

## Critical Details

### Testing

- Batch sizes zero, one, and many.
- Mixed RGBA8 and RGBA16F uploads, full generated mip chains, and provided mip
  chains.
- Invalid handle, duplicate handle, null pixels, wrong byte size, invalid mip
  count, invalid extent progression, and total-size overflow.
- Failure before submission leaves recorded texture layouts unchanged.
- Invalid pipeline/vertex/index/resource binds reject following draws and
  increment counters instead of using previous state.
- Render-target creation accepts the supported single-color/depth cases and has
  no public representation for the removed unsupported cases.
- Existing headless area rendering and screenshots remain validation-clean.

### Performance and memory

Report before/after values; do not state an improvement from source inspection.
The required evidence is load wall time, upload CPU time, texture and mip count,
bytes copied, staging allocation count/bytes, command-pool and command-buffer
creation count, queue submission count, fence-wait count/time, and peak memory.
The expected direction is fewer Vulkan setup operations and waits per load
transaction. Peak temporary staging memory may increase to the batch byte sum;
that cost must be reported.

### Concurrency and synchronization

The first implementation remains on the calling thread and existing graphics
queue. Input payloads need no lifetime beyond the return. The batch is a single
queue submission, so its image transitions and copies preserve input order.
No new cross-thread or cross-queue ownership exists.

### Robustness and style

Use checked addition/alignment for total staging sizes. Reject a batch before
allocating when any size overflows `VkDeviceSize` or addressable host memory.
Keep Vulkan implementation details behind `nw::gfx`, preserve snake_case API
naming and adjacent error/logging conventions, and run the prevailing
clang-format on changed C++ files.

## Done

Done means:

- The public render-target shape matches implemented and used behavior.
- Invalid command bindings cannot leak prior cached state into a draw.
- Natural texture load transactions use the plural batch transform; the common
  batch performs one staging allocation, command recording, queue submission,
  and wait.
- Batch ownership, lifetime, valid ranges, and failure behavior are documented
  and tested.
- Before/after load and memory measurements are recorded for the same real
  inputs, with Vulkan validation output and visual evidence.
- Frame command statistics do not regress on the existing benchmark.
- Completion/timeline work is either justified by an observed stall and split
  into a data-backed issue, or explicitly rejected for lack of evidence.

Evidence against this direction would be traces showing that decoded payload
lifetimes never overlap enough to form batches, or that upload setup/wait time
is negligible relative to required byte transfer. In that case, keep the
singular upload path and close the performance portion without adding scheduler
state; retain the correctness and contract fixes.
