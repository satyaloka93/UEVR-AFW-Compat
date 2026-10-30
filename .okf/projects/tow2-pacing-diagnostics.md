---
type: project
title: TOW2 frame cadence and AFW pose-association diagnostics
description: Bounded application timing, AFW matrix/output provenance and sampled GPU timestamps on original scheduling, without menu options or per-frame waits.
tags: [tow2, uevr, afw, openxr, diagnostics, pacing]
timestamp: '2026-09-27'
---

# Status — diagnostics retired

User authorized removing temporary instrumentation after the version2 AFW test.
Clean Release built and deployed September27 13:09:42 to
`C:/Users/gthom/Downloads/UEVR-AFW-JOEY`; DLL/PDB hashes verified, game processes
absent, prior installed pair backed up. Current DLL SHA256:
`bc04ac6c8beb3dd8a38eb35d938d398ccc430b76b9221a3391d6230bc1b9f28c`.

Removed active pacing collector/locks, pose and matrix provenance tags, AFW GPU
queries/signals/output counters, older Native copy GPU/CPU timers, engine/Present
CPU stage timers, and recurring successful AFW/copy/Present/MV trace messages.
Removed their helper headers and test project from active source; archived rather
than leaving disabled settings or unused options. Original frame scheduling,
AFW processing, busy-copy guards, Native timestamp repair, CVars and menu remain.
Normal error/warning/startup logs and basic OpenXR submission summaries remain;
this is not a claim that all logging or timing overhead is zero. Cheeky's separate
addon diagnostics were not modified.

Archive: `build/retired-pacing-diagnostics-20260927`, containing source-before,
retired headers/tests, build logs, API-call-count preservation check and deployment
record. Installed backup: `installed-backup-20260927-130941` beneath that archive.
Scoped diff check and Release passed; initial build caught a leftover fragment
of the removed heartbeat conditional, corrected before successful rebuild.
Existing narrowing warnings remain. No performance improvement or instrumentation
cost is quantified without live comparison. No commit/publication.

All measurement designs, test results and diagnostic hashes below are historical;
the current backend does not produce these temporary diagnostic records.

# First live finding

September27 PID33920: settled AFW2421submissions/50.03s =48.39/s, mostly44.96/s
with brief66.76/s and55.53/s. All4815recorded run submissions accepted, zero
begin/time-invalid/discard errors. Copy CPU waits tiny, no busy-copy skips or
recovery retries; xrWaitFrame dominates measured CPU calls at9.37ms/frame.
This excludes observed copy-fence CPU blocking, not general GPU bottlenecks.
One91.55ms submission gap has only~15.86ms in measured calls; most of that stall
lies outside those calls, origin unknown.

Pose timestamp averages20.51ms behind submitted display time (~22.27ms during
half-rate sections). Exact selected AFW matrix frame IDs match render IDs;
common matrix CPU age~47ms is not headset latency. Retained submit pose IDs
are typically render+1; separate counters and frame_state prediction updates
mean this is not proof of a wrong pose. Saved frame-delay compensation=0.
Source audit below explains the counter offset but does not establish complete
per-image pose correctness. Do not choose matrixslot0 or relabel poses blindly.

SteamVR whole-session Native+AFW GPU13.89ms exceeds90Hz11.11ms budget; cannot
attribute that aggregate specifically to AFW. Evidence and measured stage means:
OFXR workspace `build/run-analysis-20260927-tow2-pacing`. No code/deployment or
profile changes during analysis. No global percentiles inferred from per-window
p95/p99; diagnostics are application-side, not measured headset frame delivery.

# Pose-to-image source audit

The viewport hook assigns `g_frame_count = internal_frame_count` and then calls
`update_hmd_state(true, internal_frame_count + 1)` before rendering. Matrix
provenance uses `g_frame_count`; pose storage uses the incremented count. This
explains the observed matrix=N/pose=N+1 pattern without requiring stale poses.
The view-family hook subsequently aligns the runtime counter with the engine
frame count, and the render-thread enqueue uses that count plus compensation
(saved zero). This is a source explanation, not a new live assertion of every
thread interleaving.

HMD render transforms read the current pipeline view-space location. Pose
acquisition stores both the stage eye views and head location using the same
requested XR time. Submission selects the enqueued pipeline state and uses its
stored stage eye poses. Meanwhile synchronize_frame can replace predicted
frame timing without replacing those poses. Thus the observed 20–22ms
located/submitted timestamp delta alone does not justify moving poses or
changing display timestamps.

AFW receives source and opposite-eye destination matrices from the same selected
matrix-history pair. The D3D12 path copies the rendered eye from the backbuffer
and the generated opposite eye from the opposite-eye AFW framebuffer. No
confirmed routing defect was found in this audit. However, current diagnostics
do not verify the plugin's returned output texture identity or GPU completion;
the binary plugin's actual image contents are not proven by these CPU tags.
Neither source inspection nor these logs establish complete pose correctness.

Audit decision: no speculative pose correction or profile change. Original
scheduling remains installed. The subsequently authorized version2 diagnostic
build targets AFW output identity and GPU completion; repeating the same
CPU-counter test would not resolve those questions.

Source anchors: FFakeStereoRenderingHook.cpp viewport_client_draw and
pre-render view-family hooks; OpenXR.cpp update_poses, synchronize_frame,
get_submit_state, end_frame; VR.cpp update_camera_data; D3D12Component.cpp
EvaluateFrameWarp and AFR eye copies.

# Version2 live result

September27 12:56–12:58, TOW2 PID19120, SteamVR90Hz, Cheeky off. All4409
reported AFW outputs match the opposite-eye texture selected for copying; zero
missing/mismatch/timer failures. This confirms identity, not image/pose correctness.
Settled GPU windows contain133 samples: weighted mean1.189ms, max2.892ms.
Settled delivery2261/50.05s=45.17/s, mostly44.95/s; no rejected frames or CPU
copy-busy/recovery events. Window p95 intervals23.496–24.192ms, one64.04ms max.
The hitch contains~15.75ms in measured CPU calls; its remaining cause is unknown.
Sparse GPU samples cannot exclude unsampled GPU spikes.

Early higher-rate windows and a frame-counter reset are excluded from settled
results. Three startup/transition time-invalid discards, none after settling.
SteamVR whole-session GPU11.876ms exceeds90Hz11.11ms budget, consistent with
missed full-rate cadence but not proof of a sole cause or an AFW-only measurement.
No demonstrated optimization or quantified instrumentation overhead. No new
pose/timing correction warranted by these results. Evidence archive in OFXR:
`build/run-analysis-20260927-tow2-afw-gpu` (raw logs, parsed data, metrics, summary).

# Measurements

`[TOW2 pacing] enabled=true version=2 original_scheduling=true afw_gpu_sample_stride=17` marks current activation.
Window reporting piggybacks existing five-second delivery logs and teardown.
Up to1024 records retained per window; excess records counted as overflow rather
than allocating unbounded memory. No per-frame file output, new worker thread, queue wait or scheduling change.
Version2 adds sampled GPU timestamps and completion signals as described below.

- `[TOW2 pacing]`: individual accepted xrEndFrame return intervals, exact
  nearest-rank p95/p99/max of retained samples, mean adjacent-interval change,
  busy-copy/recovery counts and unknown pose associations. Native/AFW groups
  separate; first interval after mode/session change excluded. Failed attempts
  do not become successful deliveries; the next accepted interval spans the gap.
- `[TOW2 pacing CPU]`: per-submitted-frame sums of wait, begin, image acquisition,
  image wait, command-context wait/poll, copy command execution call, image
  release, AFW EvaluateFrameWarp call, and end-frame CPU time. Retries measured.
  UI/eye/depth copy stages contribute to the same frame. These are CPU durations,
  not GPU work; GPU copy completion is observed via existing context waits and
  busy-context skips, not new synchronization.
- `[TOW2 pacing pose]`: submitted display time minus the actual successful
  stage xrLocateViews timestamp stored alongside that pipeline pose. Samples
  require the pose frame ID to match the submit-state frame ID; unknowns counted.
  This is an XR-time difference, not elapsed CPU age or motion-to-photon latency.
- `[TOW2 pacing warp camera]`: CPU age of the exact matrix-history slot selected
  for AFW and render-minus-matrix frame-ID difference. Tags move with the actual
  matrix history entries and selected camera data. These can expose stale
  associations; nonzero offsets alone do not establish a bug.
- `[TOW2 pacing frame]`: three worst intervals plus the latest record each
  window, with render/submit/pose/matrix/camera frame IDs, eye, selected matrix
  slot, waited/submitted/located XR timestamps, refresh period, return/entry
  intervals, copy counts, stage times, and actual AFW source/destination camera
  positions in meters. Position values are game camera space, not OpenXR stage
  coordinates. The matrix producer's CPU stamp is not a fresh xrLocateViews
  measurement of the warp pose. Diagnostics reveal provenance rather than
  assuming the two pose paths share a timestamp or coordinate system.

Busy-context release time is measured separately from the context poll.
Window clear preserves last accepted time for cadence continuity. Mode changes
reset cadence adjacency; after overflow metrics describe the retained subset.
Missing matrix data uses frame=-1/age=-1. End-frame success means runtime API
acceptance, not a uniquely displayed headset frame. No pose/render matrices or
frame times are altered. Logging itself has some overhead; inspect periodic
outliers before attributing them entirely to the runtime.

# Version2 AFW GPU/output diagnostics

`[TOW2 AFW GPU]` reports every five seconds and at reset. Each EvaluateFrameWarp
call counts whether its returned color texture matches the opposite-eye texture
UEVR subsequently selects for copying. Null output is counted as unknown/missing,
not as a mismatch. A mismatch records the latest offending render frame. A match
proves pointer association only, not pixel contents or correct warp poses.

A timer samples every17 eligible evaluations (odd stride to cover alternating
eyes), placing timestamps immediately around EvaluateFrameWarp's command-list
work. It uses the plugin renderer's device and direct queue, resolves two query
values, and signals one diagnostic fence after EndCommandList. One sample can
be outstanding at a time; subsequent frames poll without waiting. The existing
copy timer keeps its default stride8; the AFW wrapper requests17 explicitly.

Collected samples retain their original frame and eye. Reports include sampled
GPU mean/max, maximum's frame ID, sample count, pending state and cumulative
failures. This measures the timestamp interval on the supplied command list;
work the binary plugin might dispatch elsewhere is not independently measured.
No percentile or whole-frame GPU estimate is inferred from the sparse samples.

`completion_observed_*` is elapsed CPU time between the post-submit signal and a
later successful poll. Polling cadence contributes to it; it is not the exact
GPU completion timestamp, headset latency or compositor delivery. No GPU clock
calibration or forced stable-power setting is used. Query instrumentation and
sampled signals have some overhead, still unbenchmarked in game.

Resources survive ordinary component reset while pending. Device/queue changes
or unsupported command-list types disable sampling rather than reusing storage
unsafely. Destruction can wait solely to retire outstanding diagnostic resources;
there is no new wait on the frame path. If teardown cannot establish retirement
on a still-live device, the small diagnostic allocation is retained until process
exit rather than freeing potentially referenced GPU memory.

D3D12 timestamp semantics:
[Microsoft timing documentation](https://learn.microsoft.com/en-us/windows/win32/direct3d12/timing).

# Validation and artifacts

GNU C++20 tests and MSVC Release CTest passed: alternating11/33ms intervals,
80ms spike across window reset, failed-submit gaps, Native/AFW boundaries,
camera-tag transfer/reset, empty stats and overflow. Real compositor, head pose
correctness, and instrumentation cost remain live validation items.

Backend VS2022 Release target uevr passed after fixing a private-member access
through the existing public getter. Existing UESDK/narrowing warnings remain.
Scoped whitespace check passed; source scan confirms retired options absent.
Unrelated CVar/haptics/profile/shader work preserved.

Historical version2 DLL SHA256: `6c6ccd2a1ce1df13845dc0e8cd6bc11847bf4ef9f2c93d955d34121c5439aee0`.
Deployment record: `build/afw-gpu-diagnostics-20260927/deployment.json`.
Previous version1 DLL/PDB: `build/afw-gpu-diagnostics-20260927/installed-backup-20260927-094908`.

Version2 Release build and both CTest cases passed. The real D3D12 test enabled
the debug layer with no errors: positive GPU copy timestamps, alternating
eye/frame provenance, a queue held behind a gate proving nonblocking collection,
pending-storage exclusion, reuse and outstanding-query teardown. Initial backend
compile failed on a runtime accessor before its local declaration; corrected to
the existing getter and rebuilt successfully. Existing narrowing warnings remain.
These checks do not replace live AFW plugin validation.
First version1 deployment archive (historical):
`F:/ai/GITHUB/UEVR-AFW-JOEY/UEVR-AFW-PUBLISH/build/pacing-diagnostics-20260927/installed-backup-20260927-091130`.
Deployment record, build log and source backups:
`build/pacing-diagnostics-20260927`.
Test project: `tests/pacing-diagnostics`; Windows output:
`build/pacing-diagnostics-tests/Testing/Temporary/LastTest.log`.

Source: d3d12/AfwGpuTimer.hpp and CopyGpuTimer.hpp; runtimes/PacingDiagnostics.hpp, OpenXR.cpp/.hpp, D3D12Component.cpp/.hpp,
VR.cpp/.hpp and FFakeStereoRenderingHook.cpp under src/mods.

See [retired overlap](steamvr-frame-wait.md) for why higher throughput was
rejected and [TOW2](../games/outer-worlds-2.md) for game context.
