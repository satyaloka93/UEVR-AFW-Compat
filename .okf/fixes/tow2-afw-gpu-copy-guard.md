---
type: fix
title: TOW2 AFW GPU copy and resource-state guard
description: Track the unresolved TOW2 AFW GPU crash, initial guards, and upstream hook reconciliation while preserving working motion-controller enrollment.
resource: src/mods/VR.cpp
tags:
- tow2
- afw
- d3d12
- gpu-crash
- resource-state
timestamp: '2026-09-19T09:10:00+09:00'
---

# Evidence

The 2026-09-19 TOW2 crash reports are Unreal `GPUCrash` reports whose DRED
breadcrumbs end in `CopyResource`. The preceding runtime log reaches AFW frame
warp evaluation, so this is a GPU-resource submission failure, not evidence
that the TOW2 attachment script caused the crash.

# Narrow hardening

`VR.cpp` now initializes all NGX resource outputs to null, guards `AddRef`,
requires a live D3D12 renderer and both depth/motion-vector inputs before the
AFW copy path, rejects non-transition and split barrier records, and refreshes
the source resource's observed `StateAfter` before copying. The copy hook uses a
thread-local recursion guard. This is not complete resource-state tracking or
cross-thread synchronization. No dimension/format/sample-layout guard was
actually included in that build; the earlier documentation overstated it.

The user confirmed 6DoF restored but AFW still crashed. The 09:12:26 report
`08F92EE74D98E2D1034621BBF1005CF0` is another GPUCrash with an unfinished
CopyResource breadcrumb. That does not identify a particular source resource
or establish that a state mismatch caused the crash. The proposed global
resource-state map was interrupted before application and is not deployed.

# Upstream reconciliation candidate

Reviewed upstream AFW through `fc8de102`. Applied to the authoritative publish
tree, preserving existing local changes and the TOW2 enrollment implementation:

- `29d34c33`: implementation detours for ResourceBarrier/ClearDepthStencilView,
  expanded velocity detection for ALL_SHADER_RESOURCE transitions, and DLSS
  continuity counter. Retained local AFW-only guards. Failure paths close the
  plugin command list and log the hook error. Successful setup logs
  `[AFW upstream] 29d34c33 implementation hooks installed`.
- `e5587035`: load NGX and prefer an NGX-exporting dxgi/winmm OptiScaler proxy.
- `c40eaab8`: kananlib revision `49fdf2d3` fixes scanner cycling/hangs.
- Already present: Y motion-vector scale correction, Native/2D transition
  guarding, and the UESDK FMalloc case-variant correction.
- Consistent with `fc8de102`: no camera origin-offset subtraction is present.

Not imported: new shader-rate or CorrectMotionVectors parameter fields, CAS UI,
or unrelated broad engine merges. New parameter fields require a matched
runtime checkpoint; the installed official beta.4 runtime and header are kept
together. This is a compatible upstream-fix candidate, not full beta.6/latest
feature parity. The prior Ignore Motion Threshold claim requires a distinction:
the existing threshold reaches EvaluateFrameWarp, but the new upstream
CorrectMotionVectors threshold field is absent.

MSVC Release build passed and the DLL/PDB pair was deployed with matching
source/destination hashes. Prior pair retained with backup stamp
`20260919-092038`. SHA-256 checkpoint:

- Backend: `1bc5f7562e47e6f4bec0378b3bdf6807efe956a37d1f3baa50aae3e87eed5fa3`
- PDB: `b9911d6ce9620367fd1a49dcb2b70e30f6687af861069c9ed4d8e18ccbc449df`
- Unchanged beta.4 runtime: `76bbc4d7a5370ba0d6a1a50de6b81d45223e42b764d2d7c79f0c6c48a59f6b64`

The candidate still requires an in-game Native-to-AFW test. Neither source
review nor compilation proves the GPU crash resolved.

# Upstream sources

- https://github.com/PureDark/UEVR/commit/29d34c33
- https://github.com/PureDark/UEVR/commit/e5587035
- https://github.com/PureDark/UEVR/commit/c40eaab8
- https://github.com/PureDark/UEVR/commit/fc8de102

# Deployment correction

The first 6DoF regression was deployment, not profile configuration: the
deployed backend came from the older `UEVR` tree and lacked the publish tree's
`ensure_tow2_motion_controller_component_tracked` explicit enrollment. The
authoritative backend is now rebuilt and deployed from `UEVR-AFW-PUBLISH`, with
the prior DLL/PDB retained as timestamped backups. The official beta.4 AFW
runtime remains the existing hash-verified runtime.

# Test gate

## Final-eye Cheeky SR border candidate (2026-09-19)

The publish backend now consumes the optional `CheekyAFWBorderV1` addon export
on final acquired OpenXR AFR eye images. It draws the projected contour after
the copy restores RENDER_TARGET state, before execution/release, using the
existing RTV. No AFW history/input/output framebuffer is modified. Eye identity
comes from the destination swapchain, not inferred DLSS evaluation parity.
The source addon verifies source projection and matching final image size;
unavailable/stale geometry is not drawn. CPU clipping/shape tests and Release
build passed; actual headset behavior remains unverified. Ghosting is not
claimed fixed. Only SR diagnostics on OpenXR use this path.

Candidate backend SHA-256:
`d370eb1649edb87d83ddaef503ba351b2c55dc04bbdedf8274f3e1218e397368`.
Matching Cheeky addon:
`42a81b9aa56d37d19c79e47111eac00b408c62b3ad31e37a51ebf03b91271500`.
Initial deployment aborted because a game was running. After closure, the
backend/PDB and both games' addons were deployed and hash-verified against
the tested checkpoint. Adjacent backups use suffix
`.pre-final-border-20260919-183537.bak`. The official PDAFW runtime and all
profiles remain unchanged. Headset validation is pending. Runtime signature:
`[Cheeky AFW border] final OpenXR image`.

## Ghosting follow-up

The user reported no crash with copy-layout validation but worse ghosting.
The run confirmed accepted copies (depth format 19, motion format 34) and
stable separate eye-state ownership. Motion format 34 is already R16G16_FLOAT;
the hardcoded-format hypothesis therefore does not explain this run. Early
depth layout rejections instead showed 1340x1364 versus 2684x1364. This is
evidence that layout rejection was relevant, not proof of complete crash repair.

Source review found CorrectMotionVectors writing into the NGX motion input
before the original DLSS evaluation. The next candidate first seeds the private
AFW motion target with original vectors, then applies correction to that target.
This preserves untouched regions if correction is partial and leaves DLSS's
input intact. Logs identify corrected versus original-vector paths under
`[AFW MV isolation]`. Visual improvement remains pending runtime validation.

## Copy-layout candidate after upstream test failed

The upstream candidate also crashed (`DF03829B4BD4F5486670B996BF96408E`).
A 165 MB live dump, symbolized CPU thread report, logs, configuration and the
earlier automatic hang dump are preserved under
`diagnostics/tow2-afw-upstream-live-20260919`. The correct backend was loaded;
AFW completed CPU evaluations before GPU progress stopped. DRED again contains
an unfinished CopyResource. The saved CPU stacks are post-fault waits, not an
attribution of the faulty GPU operation.

Source review found a concrete layout defect: AFW motion-vector targets were
always R16G16_FLOAT although the NGX input format was not constrained to that
format. Reallocation checked dimensions only. The candidate now allocates the
observed motion-vector format and recreates depth/MV targets on format change.
NGX input copies require matching dimensions, format, array size, mip count,
sample count/quality and resource dimension, and reject self-copy. Diagnostics
log accepted source formats and rejected source/destination layouts. The shared
predicate has compile-time tests for each incompatible field.

This fixes an invalid-copy possibility; the failing resource's actual format
was not recovered from the GPU dump, so crash causality remains unproven.
Resource-state/lifetime/queue synchronization issues remain possible. No new
global state-tracking cache is added. Upstream fixes and TOW2 enrollment remain.

Start TOW2 Native with the 6DoF overlay, verify `[tow2attach] ATTACHED` and
physical weapon translation, then transition Native → AFW once in gameplay.
Do not use AFW → Native in the same process. A repeat GPU crash requires the
new crash report and matching runtime log; absence of a crash alone does not
prove frame-warp quality.
