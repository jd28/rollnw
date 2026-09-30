# Texture upload completion during large area loads

Status: open investigation; split from
[resource submission hardening](closed/nw-gfx-resource-submission-hardening.md).

## Observed input and cost

The 2026-09-30 host comparison used the AMD Radeon 890M (RADV STRIX1), Mesa
26.2.2-arch1.1, Release builds, Vulkan validation, one warmup process and five
paired measured processes. The read-only `pvp_area_2` input has 512 tiles.
Each load uploads 1,035 textures and 677,414,400 payload bytes. The largest
temporary staging allocation is 1,048,576 bytes. After hardening, median area
load time is 1,071.855 ms, upload CPU time is 275.345 ms, and upload fence waits
sum to 116.858 ms. These are aggregate load costs, not a single 117 ms wait.

Most model/cache callers decode and consume one image at a time; their decoded
payloads do not coexist. The fixed fallback and environment texture groups now
use real batches. Keeping all decoded area pixels alive just to form a larger
batch would add roughly the payload sum in memory and violates the present
lifetime constraint. Measurements and input hashes are in the parent issue's
evidence directory.

The separate client area/preview/UI lifecycle test leaves frames in flight.
Its seven scene-replacement idle waits total 0.389 ms, maximum 0.192 ms.
This does not justify replacing immediate destruction with a general deferred
reclamation system. The unresolved opportunity is overlapping CPU decode with
queued texture transfers during the large load.

## Required evidence before implementation

Capture decode intervals, staging copies, queue submission/completion, and the
first frame that consumes each upload, on this same input and a typical smaller
area. Establish whether enough independent CPU work overlaps GPU transfer to
reduce end-to-end load latency. The existing wait time includes required GPU
work; removing a CPU wait does not remove that work. Reject this direction if
the complete load does not improve at a measured, bounded memory cost.

## Candidate completion and retirement contract

If that measurement supports queued uploads, keep one calling thread and one
graphics queue. Caller-owned pixels must be copied into gfx-owned staging
before returning. Each submitted batch receives a monotonically increasing
completion index and owns its staging ranges/fence until completion; indices
address contiguous pending-batch records. A bounded staging budget applies
back-pressure by waiting for the oldest incomplete batch, never overwriting
in-flight bytes. Select the budget from the measured occupancy distribution.

Publish newly created texture bindings only with a defined first-use ordering:
their upload barriers must precede consuming draws on the same queue. Do not
overwrite descriptors or textures referenced by previously submitted or still
recording frames. Replacements carry the last graphics-use completion index;
retired texture/allocation records are reclaimed only after both their upload
and last-use submissions complete. Unsubmitted command references must be
resolved before retirement. A failed validation/allocation leaves existing
resources unchanged; errors after submission retain the fatal Vulkan policy.
Shutdown drains pending submissions before freeing staging, textures, or core.

The cost would be O(pending batches + retired resources) metadata, an explicit
bounded staging allocation, completion polling, back-pressure and drain states.
It is paid on the desktop CPU/Vulkan queue. Existing frame fences are the first
completion mechanism to evaluate; a timeline semaphore is not a requirement.
No transfer queue, background decoder, cache redesign, or renderer binding
rewrite is authorized by this follow-up alone.

## Done

Either record evidence rejecting queued completion, or implement the selected
contract with matched load/memory results, upload ordering and failure tests,
frame-in-flight replacement/shutdown tests, validation, and pixel comparisons.
Until then the synchronous batch uploader remains the supported contract.
