---
type: fix
title: Authoritative xrWaitFrame display time (stale-submit fix)
description: Preserve authoritative wait-frame timing; Avowed override plus a Native Stereo stale-time guard and bounded submission diagnostics for TOW2 and other games.
tags:
- openxr
- timing
- frame-submission
timestamp: '2026-09-19T00:00:00Z'
resource: src/mods/vr/runtimes/OpenXR.cpp
---

# Symptom

Recurring `XR_ERROR_TIME_INVALID`, `XR_FRAME_DISCARDED`, or `xrEndFrame failed`
loops; poor/erratic OpenXR frame pacing (Avowed OpenXR performance before the
fix was substantially worse).

# Fix

- Capture an immutable `wait_frame_state` **immediately after** a successful
  `xrWaitFrame`.
- Use that authoritative `predictedDisplayTime` for `xrEndFrame` submission
  instead of any speculatively advanced render-thread frame state.
- The unconditional override remains Avowed-scoped. The equivalent
  stale-submit-time fallback fix also lives in the regular backend.

## Native Stereo extension (2026-09-19)

The authoritative `UEVR-AFW-JOEY/UEVR-AFW-PUBLISH` tree now clamps a Native
Stereo candidate timestamp to the latest immutable wait prediction **only if
the candidate is older**. Equal/future predictions remain unchanged. The
rendered image's pipelined poses/FOV remain unchanged; re-locating poses would
misdescribe the existing image. The candidate and wait snapshot are read with
the existing synchronization/assignment locks. This is an engine integration
policy, not a claim that the OpenXR specification mandates latest-wait time
for every pipelined renderer.

`is_using_afr()` excludes AFW, AFR, Synchronized Sequential and extreme
compatibility mode from this new guard. Avowed retains its previous override
in every mode. No renderer/profile, 6DoF, Cheeky, resolution, CVar or AFW
algorithm changes are part of this patch. OpenVR is unchanged.

`OpenXRSubmissionPolicy.hpp` contains the tested selection policy;
`scripts/test-openxr-submission.cpp` checks missing/stale/equal/future times,
invalid wait state, Avowed and non-Native preservation, and 10,000 mode-switch
sequences via compile-time assertions. Full Windows/MSVC Release build passed.
First Native runtime validation below confirms clean submissions, not an FPS uplift.

Deployed DLL/PDB from this tree to `C:\Users\gthom\Downloads\UEVR-AFW-JOEY`
at local timestamp `20260919-192156`, with no Shipping game processes running.
Both destination hashes matched the build outputs:

- DLL SHA256 `7803f89fd28ba115e2cc9a743b4b0d6a474d9151dbf6e3550951450d8f87c748`
- PDB SHA256 `cd20ffa972b4a746b30b6af88b0ee94b686a8e97899fb72278be7babe6acd8e9`

Previous DLL/PDB remain beside the deployed files with suffix
`.pre-native-openxr-20260919-192156.bak`. Baseline TOW2 and SteamVR logs are
preserved in `diagnostics/native-openxr-20260919-192156/`. Deployment helper:
`scripts/deploy-native-openxr.ps1`. The AFW plugin was not replaced.

## Evidence and diagnostics

TOW2 PID 34364, 2026-09-19 18:52:10–19:00:07 local log time:

- 2,398 actual `xrEndFrame failed: XR_ERROR_TIME_INVALID` calls, 2,392 of them
  during 18:57:45.290–18:58:39.365. The 2,452 raw token matches also include
  heartbeat summaries and are **not** the failure-call count.
- Submitted times were typically 44.49 ms behind the **mutable** frame state;
  the old log did not record the immutable wait prediction, so the stale
  pipeline explanation and its repair still need runtime confirmation.
- Main failure burst falls outside the logged AFW-active intervals.
- SteamVR: 42,822 compositor presents, 19,661 reprojected (45.9%); application
  CPU 12.356 ms, GPU 13.478 ms, compositor GPU 0.606 ms, target 90 Hz. This is
  a mixed-mode session, not isolated UEVR overhead or a controlled A/B. CPU and
  GPU timings overlap and must not be added.

New `[OpenXR delivery]` summaries every approximately five seconds (and at
runtime destruction) count attempts, accepted calls, Native attempts,
timestamp repairs, time-invalid results, begin errors, and discarded frames.
Only the first begin/end failure in a reporting interval emits a detailed
error. `XR_FRAME_DISCARDED` is counted as a successful begin, not logged as
an API failure. Positive success statuses use `XR_SUCCEEDED`.

`accepted_per_s` is accepted **API submissions**, including possible empty
layer submissions, not unique images displayed, AFW-generated FPS, or a
"real feel" FPS measure. `wait_cpu_ms` includes intentional runtime pacing;
`end_cpu_ms` is CPU API duration, not GPU execution time. Counters reset per
summary; sum them to evaluate the run. Abnormal termination can lose the final
partial interval. No GPU synchronization or extra frame wait is introduced.

Validation target: Native Stereo with AFW off, unchanged scene/settings;
check repair counts against accepted/failed calls and SteamVR application
timings/reprojection. Do not infer FPS uplift from policy tests alone.

# Impact

## First Native validation: 2026-09-19 19:23–19:25

TOW2 PID 28440: 25 completed reporting windows contain 6,036 attempts,
all accepted and all Native, eight timestamp repairs, zero time-invalid,
begin-error or discarded results. The last partial interval was not emitted
before shutdown; these are recorded-window counts, not an exact session total.
The guard executed successfully; one clean shorter run does not establish
that the previous intermittent failure has been permanently eliminated.

User reports Cheeky initially on, then off. Foveated processing records continue
through 19:24:51.380 and then stop; saved ReShade.ini has Enabled=0. There is no
explicit timestamped toggle event, so treat the boundary as inferred. Late
active windows covering approximately 19:24:24–49 average 44.88 accepted
submissions/s; inferred-off windows covering 19:24:54–19:25:29 average 44.63/s.
No meaningful submission-rate benefit is demonstrated either way. Earlier
69–82/s windows cannot be attributed to the toggle and are not comparable
gameplay evidence.

SteamVR whole-run CPU 10.818 ms, GPU 12.774 ms, 5,145/11,571 compositor
presents reprojected (44.46%). Prior mixed-mode run CPU 12.356/GPU 13.478 ms
and 45.9% reprojection are not a controlled before/after comparison. The
90 Hz GPU budget remains unmet on average. OpenXR end-call CPU time averages
0.713 ms across recorded submissions; wait-call time includes runtime pacing,
not proven waste. Next attribution target is application/GPU workload and
runtime cadence, not another assumption that API submission loss explains
the remaining roughly 45/s plateau.

This was the single biggest OpenXR performance improvement for Avowed
([/games/avowed.md](../games/avowed.md)) and was one of the first things ported
to the PureDark AFW branch.

# Reuse guidance

If the log shows time-invalid/discarded frame errors, check whether end-frame
is using a display time that `xrWaitFrame` actually predicted, before touching
swapchain or pipeline code.

# Citations

- `AVOWED_CHECKPOINT_STATE.md` — "OpenXR stale submit-time fallback fix"
- `PUREDARK_AFW_AVOWED_WORKLOG.md` — port item 1
- [OpenXR xrEndFrame](https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrEndFrame.html) — prediction-derived display time, pipelining, and recoverable-error handling.
- [OpenXR xrBeginFrame](https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrBeginFrame.html) — discarded-frame success status.
