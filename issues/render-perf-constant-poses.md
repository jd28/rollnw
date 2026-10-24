# RP-10: Constant poses

Status: open; implementation provisional. [Parent](rendering-performance-overhaul.md).
Depends on RP-02. [Evidence](render-perf-evidence/README.md).

Current max-scene profile: animation sampling is 18.34% inclusive sampled user
cycles (zero loss; truncated stacks limit attribution, not a timing estimate).
200/472 enabled instances have only empty/single-key channels; city 50/118,
outdoor 12/16. Ozz output was exact across 270 test times and both looping modes.
Multi-key constants can round differently in the reference sampler; keep sampling them.

Extend the existing pose snapshot with one constant-pose flag, classified when
the clip changes or the snapshot invalidates. Finite clock changes may reuse the
pose; existing root/clip/mode/fallback/binding and edit invalidation rules remain.
Cost: 21 net production lines, 8 bytes/instance (13.1 KiB for 1,680); no new buffers.

- [x] Inspect actual clip data and backend/reference output.
- [x] Implement retained classification; build succeeds.
- [x] 208 tests, 58 captures, zero validation errors/warnings; logical fields
  match across 10,800 timed pairs and 87 capture pairs except intended reuse/sync counts.
- [x] Simplification/self-check and compact evidence retained.
- [x] [Controlled attribution](render-perf-evidence/README.md):
  sampler CPU improves in all 120 paired blocks at unchanged layout. Production
  repeat max host 5.748 -> 4.808 ms; city 1.724 -> 1.641 ms, all pairs improve.
- [x] [Presented control](render-perf-evidence/README.md): max host
  5.868 -> 5.307 ms uncapped, 5.732 -> 5.373 ms paced; all 120 sampler block pairs
  improve. Instrumented same-layout comparison, 19,200 logical pairs match.
- [ ] Resolve outdoor tails/full gameplay acceptance with RP-08:
  production repeat 0.894 -> 0.906 ms, p95 1.035 -> 1.620 ms. Fixed-layout probes
  expose fence waits and CPU/preemption variation; full causation remains open.

Full gameplay, moving-camera and frame-tail acceptance remain in RP-06/RP-08.

## 2026-10-09 idle doors

Default scene playback incorrectly looped door hold clips at rate 1. The explicit
door transition path already froze completed holds. Default selection now uses
door ownership to set rate 0/nonlooping; opening/closing and manual clip playback
retain their existing paths. No model-name policy, new cache or retained memory.
This preserves same-time poses rather than broadening multi-key constant detection.

Matched 96×96 local view: original / spawn batching only / batching plus door holds
host medians 17.448 / 16.904 / 12.848 ms; pre-render 4.787 / 4.679 / 0.689 ms.
Updated model rows fall from 2,448 to 36 (2,412 doors), with identical draws and
pixels. At 32×32 they fall from 272 to 4. These are standalone synthetic views,
not full-client acceptance. [Evidence](render-perf-evidence/README.md).

166 particle/animation regressions pass. New tests fail on the original idle-door
setup; corrected tests verify closed/open holds reuse poses, both transitions
advance, and standalone explicit playback still works. Existing tests cover
transformed attachments, joined rows, missing holds and lease restoration.
