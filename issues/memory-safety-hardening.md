# Memory Safety & Foundation Hardening

## Data

This plan comes out of a full-codebase C++ review (2026-07-05). The real
problem is not the architecture — the VM, `ScriptHeap`/GC, object model, and
GFF parser are sound. The problem is a concentrated cluster of memory-safety
and correctness defects in two tiers:

1. **Untrusted-input binary parsers.** These read `.key`, `.erf`, `.mod`,
   `.hak`, `.tlk`, `.plt`, `.dds`, and `.mdl` bytes that come from third-party
   modules, haks, override folders, and the server vault. The bytes are
   attacker-controllable in any multiplayer / shared-content scenario. Several
   parsers trust file-supplied offsets, lengths, and counts without validating
   them against the buffer.

2. **Low-level containers and allocators.** Everything sits on these, yet they
   carry less test coverage and less care than the VM built on top of them. A
   silent `reserve` no-op and an allocator that under-allocates on its fallback
   path are the kind of foundational defect that produces confusing failures
   far from the root cause.

The distribution is encouraging: every defect is *local* (one parser, one
allocator method), not structural. The `serialization/Gff.cpp` parser already
demonstrates the correct pattern — overflow-checked multiplications, alignment
checks, per-table bounds re-validation — so most fixes are "make this parser
look like that one," not new design.

### Input shapes that matter

- KEY file: `FileTable.name_size` is a `uint16_t` (0–65535) read from the file
  and used as a `read()` length into a fixed 256-byte stack buffer.
- ERF/BIF element: `offset` and `size` are `uint32_t`; `offset + size` is
  compared to file size in 32-bit arithmetic (wraps).
- PLT: `width`/`height` are `uint32_t`; the pixel-count validation is done in
  32-bit (`2 * width * height`) and can overflow to a small value.
- MDL (binary): a tree of nodes whose child offsets, data offsets, and vertex/
  face/controller counts are all `uint32_t` read from the file and fed
  straight into `memcpy` / `resize` / recursive descent with no validation.
- Bioware DDS: `width`/`height`/`colors` from a header drive an unchecked
  `malloc(4 * width * height)`.

### Out-of-range policy (decide once, apply everywhere)

For all parser fixes the policy is **reject and fail the load**: on any
offset/length/count that does not fit the buffer, log an error and return
`false` / an invalid object. Never clamp-and-continue for binary formats — a
truncated read produces garbage geometry/strings that is worse than a clean
failure. Text formats (`2da`, `nss` source) already fail cleanly via the
sentinel-returning lexers and are out of scope.

## Framing

- **Why solve it:** these parsers are the process's outermost attack surface.
  The StaticKey defect is a straight stack-buffer overflow; the MDL parser is
  a family of out-of-bounds reads plus unbounded recursion. In a game that
  loads community haks and connects to servers, "malformed content crashes or
  compromises the client" is a shipping blocker.
- **Limit / plan B:** if a parser cannot be economically hardened in place, the
  fallback is to gate it behind the fuzzing harness and treat unfixed parsers
  as trusted-input-only (documented, not silently assumed).
- **Cost:** the parser fixes are mechanical (route reads through a checked
  reader). The allocator fixes are small and local. The largest single cost is
  the MDL binary parser refactor. Total is days, not weeks, and adds no
  hot-path overhead — the checks live in load paths, not the VM inner loop.

## Priority 0 — Exploitable memory safety (fix before shipping to untrusted content)

### P0.1 — StaticKey stack buffer overflow — FIXED
`lib/nw/resources/StaticKey.cpp:231-235`. `char buffer[256]` then
`file.read(buffer, it.name_size)` where `name_size` is a `uint16_t` from the
file. `CHECK_OFFSET` only validates against file size, never the buffer.
- **Fix:** clamp the read to the buffer (`std::min<size_t>(name_size,
  sizeof(buffer) - 1)`) and reject entries whose `name_size >= sizeof(buffer)`
  as corrupt. Null-terminate before constructing the `String`.
- **Verify:** unit test with a crafted KEY whose `name_size` is 256; expects a
  clean load failure, no overflow.

### P0.2 — MemoryPool fallback heap overflow — FIXED
`lib/nw/util/memory.cpp:270-294`. When `total_size > max_size_` the fallback
does `malloc(size)` but the code writes a `MemoryHeader` before the user
pointer and hands back `size` usable bytes — it needs the full
`total_size = size + alignment-1 + sizeof(MemoryHeader)`.
- **Fix:** `original = malloc(total_size);` on the fallback path.
- **Verify:** regression test forces the fallback (size just over `max_size_`,
  alignment 64) and writes the full returned span.

### P0.2b — GC incremental major use-after-free — FIXED (commit f74aafe)
The incremental major collector scanned VM registers/stack roots once at
`start_major_gc()` and swept immediately in `finish_major_gc()` with no root
re-scan. With a Dijkstra insertion barrier, a white object reachable only
through a root that changed mid-mark (loaded into a register, then its last
heap edge overwritten) was never shaded and got collected while still live.
Fixed by a stop-the-world final root re-scan before sweep, plus a debug-only
idempotence assert. Regression test
`SmallsGCTest.RootReachableObjectSurvivesIncrementalMajorGC` reproduces the
collection of a still-referenced object (fails without the re-scan). Full
analysis is in the commit message.

### P0.3 — MDL binary parser: unbounded offsets and counts — FIXED
`lib/nw/model/MdlBinaryParser.cpp` (systemic). Reachable from any model file
via `Mdl::Mdl` (`Mdl.cpp:377`, dispatched on `bytes[0] == 0`). Representative
sites:
- `:57` `memcpy(&header, data(), 12)` with no `size() >= 12` guard.
- `:152-155` `resize(controller_data.length)` + `memcpy(length*4)` with
  file-supplied length.
- `:202-221` vertex/tvert/normal/face copies sized by file `vertex_count` /
  `faces.length`.
- `:139-142`, `:397-405` child node offsets read from file and passed back
  into `parse_node` recursively — unbounded offset **and** unbounded recursion.
- `:226-236` `s_ctx.verts[i]` indexed for `i < vertex_count` even when `verts`
  was never resized (absent vertex pointer path).
- **Fix:** `BinaryParser` now has one checked byte reader and overflow-safe
  range helpers. Every binary parser copy routes through it. File-controlled
  pointer arrays, raw mesh payload offsets, controller arrays, animation event
  arrays, child pointer arrays, and node headers are validated before resize or
  read. Node recursion is bounded by the declared node count and an absolute
  cap. Meshes with a nonzero `vertex_count` but no vertex payload are rejected
  before vertex indexing.
- **Verify:** regression tests cover a 1-byte binary MDL, a self-recursive
  child pointer, and a mesh that declares vertices without a payload. The
  existing binary parse and skin tests still cover known-good data.

### P0.4 — MDL skin weights read/stored twice — FIXED
`lib/nw/model/MdlBinaryParser.cpp:339-352`. Two consecutive identical loops
both read `float[4]` per vertex and push into `s_ctx.weights`, over-reading the
weight block by `vertex_count*16` bytes and doubling the stored entries.
- **Fix:** the duplicate weight read was removed. Bones and weights now read
  through the checked raw-array path, one row per vertex.
- **Verify:** the existing known-good skinmesh tests still validate expected
  bone and weight values; malformed offsets are covered by the P0.3 checked
  reader path.

### P0.5 — ERF/BIF/StaticErf offset+size 32-bit overflow — FIXED
`lib/nw/resources/Erf.cpp:492`, `StaticKey.cpp:83` (Bif::demand),
`StaticErf.cpp:211-233`. `offset + size` (both `uint32_t`) is compared to file
size in 32-bit and can wrap past a valid bound, then `seekg`/`read` runs OOB.
`StaticErf::read` additionally validates nothing beyond `seekg` success.
- **Fix:** promote to `size_t`/`int64` before adding
  (`size_t(offset) + size > size_t(fsize_)`). Add the same explicit bound to
  `StaticErf::read` before `resize`/`read`.
- **Verify:** unit tests with crafted ERF/BIF elements whose `offset + size`
  wraps; expect a logged error and empty result.

### P0.6 — PLT overflow + OOB layer index + null palette deref — FIXED
`lib/nw/formats/Plt.cpp:21`, `decode_plt_color:40-52`.
- Size check `24 + (2 * width_ * height_)` is 32-bit; overflow validates a huge
  image against a tiny buffer, then `pixels()[y*width_+x]` reads OOB.
- `colors.data[pixel.layer]` indexes a `std::array<uint8_t,10>` with a
  file-supplied `layer` (no clamp to `plt_layer_size`).
- `palette_texture(pixel.layer)` can return `nullptr`
  (`ResourceManager.cpp:428`) but line 50 dereferences it unconditionally.
- **Fix:** compute the size check in `uint64_t` and reject on mismatch;
  bounds-check `pixel.layer < plt_layer_size` (reject/skip pixel otherwise);
  null-check `img` before `img->valid()`.
- **Verify:** unit tests cover overflow dimensions and an out-of-range layer
  byte; the implementation now null-checks palette texture lookup before
  dereference.

### P0.7 — Image (Bioware DDS / PLT→Image) unchecked malloc on file dims — FIXED
`lib/nw/formats/Image.cpp:73, 299, 343`. `malloc(4ull * width_ * height_)`
with file-controlled dimensions, no upper bound and no null check; the next
lines write through the (possibly null) pointer.
- **Fix:** cap `width_ * height_` against a sane maximum (reject larger) and
  check every `malloc` result before use.
- **Verify:** regression test rejects an oversized Bioware DDS before
  allocation.

## Priority 1 — Correctness defects in foundations

### P1.1 — ChunkVector::reserve is a no-op — FIXED
`lib/nw/util/ChunkVector.hpp:96-102`. After the `n <= allocated_` early return,
the loop `while (n < allocated_)` is immediately false, so no block is ever
allocated. Used by `ObjectPool` and handle pools; the reserve intent is
silently lost (correctness is preserved by lazy `push_back` growth).
- **Fix:** `while (allocated_ < n) alloc_block();`
- **Verify:** unit test asserting `capacity() >= n` after `reserve(n)`.

### P1.2 — MemoryArena::reset semantics — FIXED
`lib/nw/util/memory.cpp:99-103`. Documented "free all blocks except the first"
but only resets block 0. Blocks 1..N are neither freed nor reused: after reset,
`allocate_block` appends *new* blocks and orphans the grown ones until the
destructor, and `used()`/`capacity()` report stale totals. Defeats the
reset-and-reuse contract the TLS scratch arena (`kernel/Memory.cpp`) depends
on.
- **Fix:** on reset, either free blocks > 0 (keep only block 0) or zero every
  block's `used` and reuse them in order. Pick one; document it.
- **Verify:** reset the arena after over-filling block 0; assert extra blocks
  are freed and `used()` returns to 0.

### P1.3 — ByteArray::read_at off-by-one — FIXED
`lib/nw/util/ByteArray.cpp:32-38`. `if (offset + sz >= size()) return false;`
rejects a read that ends exactly at the buffer end (should be `>`), and
`offset + sz` can wrap for pathological inputs. Direction is safe (rejects
valid reads, never allows OOB) but it silently drops legitimate
end-of-buffer field reads in GFF/PLT.
- **Fix:** `if (offset > size() || sz > size() - offset) return false;`
- **Verify:** unit test reads the final byte and a zero-length end offset.

## Priority 2 — Robustness & consistency (post-hardening polish)

### P2.1 — FixedVector error-policy inconsistency — FIXED
`lib/nw/util/FixedVector.hpp:188-221`. `insert(pos, const T&)` uses `CHECK_F`
(abort) on overflow while `insert(pos, T&&)` throws `std::out_of_range`. Same
class, same precondition, two failure modes.
- **Fix:** `insert(pos, T&&)` now uses the same `CHECK_F` capacity policy as
  the rest of `FixedVector`.

### P2.2 — MDL binary vs text parser error handling — FIXED
`lib/nw/model/Mdl.cpp:381-388`. The text path is wrapped in `try/catch`; the
binary path is not. Moot once P0.3 lands, but align the two.
- **Fix:** binary parser construction/parsing is now wrapped the same way as
  the text parser and leaves the model invalid on exceptions.

### P2.3 — Non-reentrant localtime — FIXED
`lib/nw/resources/Erf.cpp:187`. `*localtime(&tt)` in `save_as`; racy under
concurrent saves. Low impact.
- **Fix:** `save_as` now uses `localtime_r` / `localtime_s` through a local
  `util/platform` helper.

### P2.5 — Register-count truncation at the 256-register boundary — FIXED
`lib/nw/smalls/AstCompiler.cpp:35` (`high_water_mark()` does
`static_cast<uint8_t>(max_reg)`). `max_reg` is a `uint16_t` that becomes 256 the
moment register index 255 is allocated, so a function that uses all 256
registers stores `register_count = 0`. This is caught downstream — the
`BytecodeVerifier` rejects every instruction (`check_reg` against `reg_count=0`)
— so it is safe, but the diagnostic is inverted at the boundary: a 256-register
function produces a confusing "register out of range r0 (reg_count=0)" verifier
error, while a 257-register function throws the clean "exceeded 256 registers"
compiler message. Clamp/detect the 256 case in the allocator and surface the
same clean diagnostic.
- **Fix:** `RegisterAllocator::high_water_mark()` now rejects the unencodable
  256-register count before bytecode metadata is written.
- **Verify:** focused allocator test covers the boundary.

### P2.4 — Misleading `// private:` comments — FIXED
`lib/nw/util/memory.hpp:165, 207`, `HandlePool.hpp`, etc. Public data members
labeled `// private:`. Either enforce access control or document why the
members are intentionally open.
- **Fix:** removed the misleading labels without changing public layout/API.

## Priority 3 — Assurance (raise the floor so the class can't return)

The specific fixes close the known holes; this is what stops them reopening and
what would move the codebase from "known holes closed" to "trusted with hostile
input."

- **Fuzzing in CI.** libFuzzer targets under ASan+UBSan for every format
  parser: `Gff`, `Erf`, `StaticErf`, `StaticKey`/`Bif`, `Tlk`, `Plt`, `Image`
  (dds + bioware), `Mdl` (binary + text), `TwoDA`. The `fuzz/` directory
  already exists — extend it. This is the single highest-leverage item: it
  turns each P0 from "fixed once" into "can't regress."
- **Allocator/container tests under sanitizers.** Coverage for `MemoryPool`
  (all bucket + fallback paths), `MemoryArena` (reset/rewind), `ChunkVector`,
  `FixedVector`, `HandlePool` proportional to how load-bearing they are.
- **Audit status of the formerly-sampled subsystems (updated 2026-07-06).**
  The compiler middle-ends, the GC state machine, and the renderer have now had
  a first-class pass:
  - *Smalls compiler + verifier + VM:* sound. Bytecode is compiler-generated
    in-process and never deserialized from untrusted bytes, so the
    `BytecodeVerifier` is a defense-in-depth net, not a security boundary. Every
    op the verifier delegates (CALLNATIVE/CALLINTR index, SUMGETPAYLOAD variant)
    is independently bounds-checked by the VM at runtime. Register allocation
    throws on overflow rather than truncating, including the 256-register
    boundary case. No memory-safety defect found.
  - *GC:* the write barrier is complete (every heap pointer store — fields,
    tuples, sums, array elements, map keys *and* values — routes through
    `write_barrier`, which correctly records old→young edges in the remembered
    set). The one real defect is P0.2b (incremental major collection lacks a
    root re-scan). Fixing that closes the last correctness gap.
  - *Renderer (`render/` + `gfx/`):* the sampled critical paths are correct —
    frame-in-flight fence sync, GPU buffer sizing from actual container sizes,
    and bounds-checked/validated index processing in `model_loader`. Not
    exhaustively line-audited at 36k lines, but no defect surfaced in the
    high-risk paths.
  With P0.2b addressed, "structurally sound" is a direct finding for these
  subsystems rather than an extrapolation; only the exhaustive line-by-line of
  the renderer remains as residual uncertainty.

## P2.6 — Rare flakiness in the GC test suite (investigated 2026-07-06)
`tests/smalls_gc.cpp` `FixedArrayRootsSurviveMinorGC` and `AllocationChurn`
failed together once during a full `SmallsGCTest.*` run and have not recurred in
~80 subsequent runs (45 plain + 5 full-suite ASan + 30 suspect-only ASan + shuffle
across 8 seeds + 25 under compile load). Investigation findings:
- **ASan does NOT rule out a GC-level use-after-free here.** `ScriptHeap`
  sub-allocates script objects out of its own `mmap`'d region via
  `OffsetAllocator`; ASan only instruments `malloc`/`free`/stack/globals and
  cannot see frees inside that arena. A dangling `HeapPtr` whose slot the
  OffsetAllocator immediately reuses reads a live-but-wrong object with no ASan
  report. So the clean ASan runs (`build-asan`) prove there is no UAF in
  *malloc-tracked* memory, but they do **not** prove the script heap is
  UAF-free. The only current detector for a script-object UAF is the value
  assertions in these tests — which is exactly what flaked. This must be closed
  before the flake can be called benign (see P2.7).
- The failure was a *wrong computed value* (or missing object), and it occurred
  with the P0.2b fix already in place, so if it is a real UAF it is a different
  path than the incremental-major root gap already fixed (e.g. minor-cycle
  ordering, or a hash-seed-dependent edge).
- **Not a timing artifact.** `GCConfig::minor_step_time_budget_us` defaults to 0,
  so the collector uses no wall-clock deadlines in its control flow
  (`high_resolution_clock` feeds stats only). GC scheduling is a deterministic
  function of allocation count and byte thresholds
  (`incremental_work_budget = 100`, `major_threshold_percent = 0.8`).
- **Not cross-test leakage.** `Services::shutdown()` destroys and recreates the
  `Runtime` (and its GC + `ScriptHeap`) per test, so no GC phase leaks across
  tests.
- **Most likely cause:** per-process hash-container iteration-order
  randomization (abseil) interacting with GC marking / remembered-set order —
  a benign assertion sensitivity or a very rare ordering edge, not corruption.
- **Recommended actions (not yet done — could not reproduce to verify a fix):**
  1. Add an **AddressSanitizer CI lane** for the test suite. This is the real
     safety net: it makes any genuine GC UAF a deterministic, located failure
     regardless of the timing/order flake, and the `build-asan` config already
     works.
  2. Optionally make the two tests self-loop (repeat the allocate/collect/verify
     inner sequence N times within one test) to raise signal, and force a
     deterministic `collect_full()` at the assertion point rather than relying
     on allocation-triggered scheduling.
  Left open because a de-flake cannot be verified without a reproduction. The
  ASan blind spot is now closed (P2.7 landed), so a genuine script-object UAF
  in these tests would show as a deterministic ASan report rather than a
  wrong-value flake — the flake can be re-investigated under the now-effective
  ASan lane. Note: CI registers tests via `gtest_discover_tests` (each case runs
  in its own process), so the full-suite-single-process trigger seen here is
  unlikely to red CI directly, but a bad-seed single-test process could still
  fail.

## P2.7 — Custom allocators self-instrument for ASan — DONE (commit 50739f6)
Our arenas/pools carve objects out of memory they own (`malloc`/`mmap`), so ASan
was blind to use-after-free / use-after-scope inside them — a dangling `HeapPtr`
or pool pointer whose slot got reused read a live-but-wrong object with no
report. Added `lib/nw/util/asan.hpp` (`nw::asan::poison`/`unpoison`, no-ops
unless ASan is built) and wired it into `ScriptHeap` (poison user region on
free, unpoison on allocate), `MemoryArena` (poison blocks, unpoison handed-out
ranges, re-poison on reset/rewind), `MemoryPool`, and `ObjectPool`. Verified
with a positive control (reading a freed `ScriptHeap` slot trips ASan at the
exact line) and false-positive-clean across the compiler/GC/heap/VM suites. The
existing ASan lane is now effective for the whole engine.

Consequence: the now-effective ASan lane surfaces the inventory/equips UAF below
and closes the P2.6 blind spot (that flake can be re-investigated under ASan).

## Inventory/equips item-ownership use-after-free — tracked in the object overhaul
The P2.7 instrumentation surfaced a real use-after-free: `Inventory`/`Equips`
hold raw `Item*` (a `Variant<Resref, Item*>`), and `ObjectManager::destroy()`
frees an item without unlinking it, so teardown can read a freed item
(reproduces deterministically under ASan in
`SmallsEngineIntegration.CoreCreatureEquipApisProcessItemProperties`). This is
item-ownership, not a memory-subsystem defect: the fix — owners hold
version-checked handles, not raw pointers — is a declared target of the
object→propset conversion ("equipment/inventory own item handles"). It is
tracked there as the "inventory/equips own item handles" slice in
`object-propset-overhaul.md`, not here. Until it lands, the ASan lane will flag
this pre-existing UAF.

## Remaining Work

- Extend the fuzzing harness beyond the binary MDL target to the rest of the
  binary format parsers listed in P3.
- Add broader sanitizer coverage for allocator/container combinations not
  covered by the focused regression tests.
- Keep the inventory/equips item-ownership UAF tracked in
  `object-propset-overhaul.md`; it is outside this memory-subsystem issue.

## Done criteria

- Every P0 has a focused regression test. The binary MDL parser also has a
  libFuzzer target wired into the existing fuzz smoke lane; the remaining
  format fuzzers are tracked as P3 assurance work.
- P1 defects have positive unit tests asserting the corrected behavior
  (`reserve` grows, `reset` reuses/frees, `read_at` accepts end-of-buffer).
- No parser reachable from `resman().demand()` reads outside its buffer on any
  input in the fuzz corpus.
- Out-of-range behavior for every binary parser is explicit (reject + log) and
  matches the policy stated above.
