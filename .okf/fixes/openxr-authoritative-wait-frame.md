---
type: fix
title: Authoritative xrWaitFrame display time (stale-submit fix)
description: Capture an immutable wait_frame_state immediately after successful xrWaitFrame and submit xrEndFrame with that authoritative display time; prevents XR_ERROR_TIME_INVALID / XR_FRAME_DISCARDED loops caused by speculative render-thread frame-state advancement.
tags:
- openxr
- timing
- frame-submission
timestamp: '2026-07-20T00:00:00Z'
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
- Applied Avowed-scoped on the AFW branch; the equivalent stale-submit-time
  fallback fix lives in the regular backend.

# Impact

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
