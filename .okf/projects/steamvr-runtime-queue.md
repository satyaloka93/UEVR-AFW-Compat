---
type: project
title: SteamVR separate runtime queue experiment
description: Opt-in TOW2 D3D12 OpenXR queue isolation, menu activation, GPU synchronization and validation limits.
tags: [openxr, steamvr, d3d12, tow2, performance]
timestamp: '2026-09-26T00:00:00+09:00'
---

# Status

**Retired from active source September27 at user request.** Separate queue
helper, fences/handoffs, menu/config registration and tests removed; archived in
`build/retired-runtime-queue-20260927`. Rebuilt successfully and deployed September27 08:44 after user clarified
authorization. Installed backend no longer contains the option or queue code;
the false profile key is inert. Frame-wait overlap was subsequently removed and deployed at08:54 after a
motion-smoothness regression; neither experiment remains active. The following implementation/usage record is historical.

# Usage

Installed backend and matching PDB:
`C:/Users/gthom/Downloads/UEVR-AFW-JOEY/UEVRBackend.dll`.
Start TOW2 via the existing injector. In the UEVR VR menu beside **Reinitialize
Runtime**, enable **Separate SteamVR queue (TOW2 experiment)**, then click
**Reinitialize Runtime**. The session label must read **Separate**. Selection
is stored as `VR_SteamVRRuntimeQueue`, but the session uses the prior choice until
runtime recreation. Disable and reinitialize to return to the game's queue.
Use OFXR off for this AFW experiment. Test the existing AFW off/on pattern; the
prior baseline is already archived in the OFXR workspace.

Log signature: `[SteamVR runtime queue] requested=1 active=1 runtime=SteamVR ...`.
Wrong runtime/game or failed initialization logs active=0 and keeps original
binding. Queue handoff failure prevents further image writes/submission and
requests runtime reinitialization; it does not drop required fences.

# Implementation

An independent normal-priority DIRECT queue is supplied to the OpenXR session.
All existing game, AFW and eye/UI copy command lists stay on the original game
queue. After successful xrWaitSwapchainImage, a runtime signal/game wait orders
runtime image work before game writes. Before every release (including static
UI and busy-copy skip paths), a game signal/runtime wait orders writes before
runtime consumption. Two session-owned monotonic fences separate the directions.
No per-frame CPU fence wait is added. Existing copy-context fences remain.
Runtime work is drained at swapchain teardown; the runtime queue survives
swapchain recreation and is replaced only at session reinitialization after
old-session destruction. Explicit handoffs cover Native and AFR/AFW copies.

Source: `src/mods/vr/d3d12/OpenXRRuntimeQueue.hpp`, D3D12Component.cpp/.hpp,
VR.cpp/.hpp. Source backups under `build/steamvr-runtime-queue-backup/src`.

# Validation

VS2022 x64 Release target `uevr` built successfully. Focused D3D12 tests passed
on WARP and the hardware adapter. Tests deliberately block each queue and prove
the other cannot bypass acquisition/release ordering, then run 256 bidirectional
handoffs, drain, reset and recreate. They also verify inactive and setup-failure
behavior. The September27 live run confirms SteamVR queue activation across runtime recreation. Image correctness is not assessed by these tests; a performance benefit remains unproven.

Test project: `tests/steamvr-runtime-queue`. Build directory:
`build/steamvr-runtime-queue-tests`. Run CTest Release for WARP and
`Release/runtime_queue_tests.exe --hardware` for the hardware adapter.
Build/test logs: `build/steamvr-runtime-queue-backup/validation`.

Candidate DLL SHA256:
`7dbd050f3c36233b77b42ae7b3c08671ca0573e2340b92cbb83a14a64aa7b187`.
Previous installed DLL SHA256:
`5b11894e1870ce52723af42e60dad41c0db9612b181f80d55953595b847211ff`.
The installed candidate DLL/PDB were compared with the build outputs after copy.
Full rollback: close the game and restore DLL plus PDB from
`build/steamvr-runtime-queue-backup/installed` into the injector directory.

See [TOW2](../games/outer-worlds-2.md) and
[narrow port scope](../decisions/narrow-port-scope.md).

# First live result

September27 TOW2 PID19640: game queue active at07:59:39; separate queue active
at08:01:50 following reinitialization, no handoff errors. After Cheeky disabled,
native submissions were40.04/s before and39.91/s after the queue toggle. AFW on
with separate queue produced48.23/s (~21% over its native sample). No AFW-on
sample with game queue exists in this run, so an AFW regression cannot be isolated.
Actual per-eye target3400x3468 is61.5% more pixels than September26's2676x2728.
SteamVR reported game GPU17.191ms before recreation and16.163ms after, both
over90Hz budget. Do not label yesterday-to-today throughput differences a queue
regression. Experiment should not be promoted on this result; menu Off plus
reinitialize remains rollback. Live default/profile choice was not changed in
analysis. Evidence is archived in the OFXR workspace at
`build/run-analysis-20260927-tow2-queue`.

See the separate [frame-wait overlap experiment](steamvr-frame-wait.md) for the next scheduling candidate. The installed backend removes the queue option entirely.
