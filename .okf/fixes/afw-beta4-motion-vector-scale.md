---
type: fix
title: PureDark AFW beta.4 motion-vector and 2D-transition corrections
description: Beta.4 corrects Y motion-vector scaling, updates the frame-warp runtime, and suppresses AFW during 2D-screen transitions; in TOW2, Previous Frame Warping then produced a smooth zero-ghosting result.
tags:
- afw
- puredark
- motion-vectors
- ghosting
- d3d12
- tow2
timestamp: '2026-07-20T18:54:40+09:00'
resource: https://github.com/PureDark/UEVR/releases/tag/UEVR_AFW_v1.0-beta.4
---

# Upstream update reviewed

PureDark beta.4 published two user-facing fixes: white-speck removal and crash
protection when changing resolution or toggling 2D-screen mode. The `AFW`
branch/tag remained at `e260ffe8`, while `origin/Joey-Merged` added five small
commits through `7510c210`. The integration did **not** merge that branch.
Instead, it ported only the audited AFW deltas and used the official runtime.

# Ported corrections

## Correct Y motion-vector scaling

The old NGX hook calculated both axes from `mvScale[0]`:

```cpp
vr->mvScale[0] = mvScale[0] * outputDesc.Width / mvDesc.Width;
vr->mvScale[1] = mvScale[0] * outputDesc.Height / mvDesc.Height;
```

Beta.4 commit `7510c210` correctly uses `mvScale[1]` for Y. X and Y may differ
in magnitude or sign; using X for Y makes warped moving geometry trail or move
in the wrong direction.

## Suspend active AFW during 2D mode

Commits `7b08bfec` and `967e55ad` define active AFW as selected **and not in
2D-screen mode**, use synchronized rendering during the transition, and avoid
reinitializing frame warp while 2D is active. This is relevant to games whose
profiles switch menus/cutscenes to UEVR 2D mode.

## Match the beta.4 plugin ABI

`PDAFWPlugin.h` was updated for the new `FrameWarpParams::UseUINT64` field and
the expanded `D3D12RendererAPI::Copy` signature. The UINT64 shader path remains
disabled; it was an upstream debug option, not part of this compatibility port.

# TOW2 result

With the official beta.4 runtime and the narrow source corrections, TOW2 ran in
AFW **Previous Frame Warping** with no visible weapon or moving-object ghosting
and smooth output. The successful runtime settings were:

```text
VR_RenderingMethod=3
VR_AFW_FramewarpMode=2
VR_GhostingFix=true
VR_GhostingFixBootstrapViewStates=true
VR_AFW_FixObjectMotionVector=true
VR_AFW_FixObjectMotionRange=3.000000
VR_AFW_IgnoreMotionThreshold=2.500000
VR_AFW_UltraResponsive=true
VR_AFW_FixMovingObjectBrightnessFlickering=false
```

The log recorded one `InitFrameWarp`, continuous `XR_SUCCESS`, no Present
failures, and no hang dump.

# Causal assessment

The strongest source-level explanation is the corrected Y motion-vector scale,
because Previous Frame mode and the same ghosting options had already reduced
but not eliminated trails before beta.4. The updated closed-source runtime also
changed, so the zero-ghosting result belongs to the **combined beta.4 runtime +
scale correction** unless a future controlled A/B isolates them. The 2D guard
does not affect steady non-2D gameplay but should protect menu transitions.

# Exact checkpoint

- Backend SHA-256: `8d67055a3154c5df1b4322986e6fbf07f976f58cd7223177198b78353d0bd675`
- PDAFW beta.4 SHA-256: `76bbc4d7a5370ba0d6a1a50de6b81d45223e42b764d2d7c79f0c6c48a59f6b64`
- Safety bundle:
  `<evidence-root>/puredark-beta4-tow2-zero-ghosting-success-20260720`
- Pre-update rollback:
  `<evidence-root>/pre-puredark-beta4-port-20260720`

# Related

- [PureDark AFW integration](/projects/puredark-afw-integration.md)
- [The Outer Worlds 2 state](/games/outer-worlds-2.md)
- [Narrow port scope](/decisions/narrow-port-scope.md)

# Citations

[1] [PureDark UEVR AFW beta.4 release](https://github.com/PureDark/UEVR/releases/tag/UEVR_AFW_v1.0-beta.4)
[2] [Y-scale correction commit](https://github.com/PureDark/UEVR/commit/7510c210226ff62cc35d55e355cbcedda5b3414d)
[3] [2D transition correction commit](https://github.com/PureDark/UEVR/commit/7b08bfec85fc007c088bb44c715c82997ef6a5e6)
