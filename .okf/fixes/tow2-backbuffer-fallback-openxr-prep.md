---
type: fix
title: Real-backbuffer fallback + early OpenXR frame prep (TOW2 D3D12 path)
description: While the fake-stereo render target is unavailable (startup/title), copy from the real D3D12 swapchain backbuffer; synchronize OpenXR, publish poses, and begin the frame before the copy; use non-blocking validated fence waits.
tags:
- tow2
- d3d12
- openxr
- present
timestamp: '2026-07-20T00:00:00Z'
resource: src/mods/vr/D3D12Component.cpp
---

# Problem

During TOW2 startup/title rendering the fake-stereo render target doesn't exist
yet, and the OpenXR frame state wasn't prepared before the D3D12 copy path —
producing a dead Present path and no headset output.

# Fix (executable-scoped to TOW2)

- **Fallback:** when the fake stereo render target is missing, source the copy
  from the **real D3D12 swapchain backbuffer**.
- **Early OpenXR prep before the copy path:** synchronize frame → update/publish
  poses → begin the OpenXR frame.
- **Non-blocking D3D12 fence handling:** validate fence wait results before
  resetting command lists; never block Present on a wait that can't complete.
- **Telemetry:** throttled OpenXR `end_frame` result logging and a
  once-per-second Present heartbeat — cheap permanent instrumentation that
  makes future stalls diagnosable
  ([/playbooks/log-signature-triage.md](../playbooks/log-signature-triage.md)).
- **Rehook suppression:** suppress destructive D3D rehook churn while the
  window-message hook is still intact (rehooks were tearing down a live
  swapchain path).

# Reuse guidance

Any game that shows headset output dying at title/startup while the flat window
still runs is a candidate for this trio: backbuffer fallback, early XR frame
prep, rehook suppression. See [/games/outer-worlds-2.md](../games/outer-worlds-2.md).

# Citations

- `TOW2_UEVR_FIX_SUMMARY.md`, `TOW2_UEVR_WORKLOG.md` stages 5–6
