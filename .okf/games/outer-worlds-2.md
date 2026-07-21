---
type: game-profile
title: The Outer Worlds 2 — working VR state
description: TOW2 reaches gameplay through a guarded Native/OpenXR startup and can switch to PureDark beta.4 Previous Frame AFW with zero observed ghosting; AddObject and analyzer timing guards remain essential.
tags:
- tow2
- outer-worlds-2
- native-stereo
- openxr
- afw
timestamp: '2026-07-20T18:54:40+09:00'
---

# Status

Playable in Native Stereo and, after reaching gameplay, in PureDark AFW. The
2026-07-20 beta.4 run produced smooth **Previous Frame Warping with no visible
weapon or moving-object ghosting**. Startup is still timing-sensitive: keep the
in-process hang diagnostic until repeated clean launches establish that the
title analyzer is reliable.

# Safe startup state

TOW2 must start conservatively; AFW remains an ordinary runtime-selectable
option rather than a restart-gated fork behavior:

```text
VR_RenderingMethod=0
VR_NativeStereoFix=false
VR_AFW_FramewarpMode=2
VR_GhostingFix=false
VR_GhostingFixBootstrapViewStates=false
VR_AFW_FixMovingObjectBrightnessFlickering=false
```

After reaching gameplay, select AFW and enable Ghosting Fix + Bootstrap. Do not
switch **AFW → Native** in-process; that direction can leave a black right eye.
Native → AFW has worked. Restart to leave AFW.

# Proven beta.4 AFW state

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
VR_2DScreenMode=false
```

The successful log recorded:

- `Found final frame count offset at 94 after 40 confirmations`;
- one `Before InitFrameWarp` / `After InitFrameWarp` pair;
- continuous OpenXR `XR_SUCCESS` submissions;
- no Present failures and no hang dump;
- sustained gameplay with unsafe AddObject calls rejected rather than
  traversed.

Exact checkpoint:
`<evidence-root>/puredark-beta4-tow2-zero-ghosting-success-20260720`.
See [the beta.4 motion-vector correction](/fixes/afw-beta4-motion-vector-scale.md).

# Required injection/stereo fixes

- TOW2 CVar scanner/freeze paths bypassed —
  [CVar scanner bypass](/fixes/tow2-cvar-scanner-bypass.md).
- Real-backbuffer D3D12 fallback + early OpenXR sync/pose/begin —
  [backbuffer and XR prep](/fixes/tow2-backbuffer-fallback-openxr-prep.md).
- TOW2-only fake-stereo confirmation threshold —
  [view-extension analyzer timing](/fixes/tow2-view-extension-analyzer-threshold.md).
- Dynamic per-call UObject candidate/class/superclass validation —
  [AddObject candidate guard](/fixes/tow2-addobject-candidate-guard.md).
- PureDark's invasive XR null-dereference VEH installs only when Native Stereo
  Fix is enabled. TOW2 requires it disabled.
- Destructive D3D rehook is suppressed while the window-message hook remains
  intact; DXGI factory/Present telemetry remains executable-scoped.

# Profile and visual requirements

The reflected CVar path is unsafe. `userscriptex.dll` remains reversibly
disabled as `userscriptex.dll.disabled_puredark_afw_20260718`. On the AFW
baseline, `UEngine::Exec` could not be resolved reliably, so the required VSM
override is in the game's `Engine.ini`:

```text
r.Shadow.Virtual.Enable=0
```

The historical regular-backend `user_script.txt` also used:

```text
r.Shadow.Virtual.Enable 0
r.Lumen.VSM 1
```

Only disabling `r.Shadow.Virtual.Enable` proved necessary for the stereo shadow
mismatch.

# Title-stall history

There were two independent failures with the same visible "waiting" symptom:

1. **Analyzer race:** the generic detector required 50 samples while TOW2's
   title transition stopped callbacks first. Forty is more reliable but still
   timing-sensitive.
2. **UObject traversal:** a running minidump showed GameThread inside
   `UObjectHook::add_new_object()` because TOW2 changes the AddObject argument
   layout between calls. Per-call authoritative validation fixed this path.

Use [in-process hang dumps](/playbooks/in-process-hang-dump.md) when external
ProcDump is denied. Do not infer AFW caused a title freeze unless
`InitFrameWarp` actually appears before the stall.

# Rejected or unresolved experiments

- **Synchronized Sequential** — not the required rendering path.
- **Native Stereo Fix** — scene-capture/FRenderTarget discovery blocked the
  title and crashed TOW2.
- **Primary-pass stereo metadata override** — title-wait/one-eye-black
  regression.
- **Broad baseline backend A/B** — imported restart-gated AFW, VRS UI and
  unrelated artifact paths; immediately rolled back.
- **Broad reflected scanner restoration** — unsafe memory warnings and title
  stalls.
- **TOW2 controller-ray menu changes** — framework aim capture and trigger
  pulse experiments did not establish a reliable fix and were rolled back.
  Right-controller UEVR-menu selection remains an open validation item.
- **Inventory/2D transition** — beta.4 adds relevant 2D guards, but this needs a
  deliberate repeated menu test before being called resolved.

# Related

- [PureDark AFW integration](/projects/puredark-afw-integration.md)
- [Narrow port scope](/decisions/narrow-port-scope.md)
- [Checkpointing and recovery](/playbooks/checkpoint-and-recovery.md)

# Citations

- `TOW2_UEVR_FIX_SUMMARY.md`, `TOW2_UEVR_WORKLOG.md`
- `<private-worklog-root>/PUREDARK_AFW_TOW2_WORKLOG.md`
- `<evidence-root>/puredark-beta4-tow2-zero-ghosting-success-20260720/profile/log.txt`
