---
type: game-profile
title: The Outer Worlds 2 — working VR state
description: TOW2 reaches gameplay through guarded Native/OpenXR startup, can switch to Previous Frame AFW with zero observed ghosting, has true late-weapon 6DoF through explicit enrollment, and now provides a fixed controller-ray UEVR menu while preserving gameplay aim.
tags:
- tow2
- outer-worlds-2
- native-stereo
- openxr
- afw
- 6dof
- uobjecthook
- motion-controllers
- menu
- mouse-emulation
- psvr2
- adaptive-triggers
- haptics
- melee
timestamp: '2026-08-14T09:55:00+09:00'
---

# Status

Playable in Native Stereo and, after reaching gameplay, in PureDark AFW. The
2026-07-20 beta.4 run produced smooth **Previous Frame Warping with no visible
weapon or moving-object ghosting**. Committed backend source and a packaged
profile overlay also restore controller-driven weapon translation for dynamic
Steam weapon meshes.
The profile-local framework capture now also matches SHf's gold-standard menu
behavior: the menu remains fixed while the right-controller ray moves the mouse
in both axes, then controller gameplay aim returns after close.
Startup is still timing-sensitive: keep the in-process hang diagnostic until
repeated clean launches establish that the title analyzer is reliable. The
6DoF source is committed as `217162d7` and its overlay as `69d3a7b0`;
a matching new release binary remains held until longer TOW2 and cross-game
regression tests complete.

**Memory-leak correction:** an unpatched run reached approximately 30 GB system
RAM in under ten minutes while logging `[FMalloc::get] Failed to find GMalloc`.
TOW2 contains the case-sensitive `Binned2` marker that the maintained UESDK
branch did not scan. The narrow Praydog UESDK `f37f61c` candidate resolved
`Binned2` plus Malloc/Realloc/Free, then ran AFW for approximately 35 minutes
without observed continuing memory growth or OOM/GPU/device errors. Alpha.4
publishes this correction as an experimental prerelease; broader regression is
still required before stable promotion. See [TOW2 FMalloc discovery and memory growth](../fixes/tow2-fmalloc-memory-leak.md).

# Experimental PSVR2 per-gun haptics

A default-off prototype now mirrors the Cyberpunk gameplay-profile architecture:
TOW2 Lua classifies the equipped gun and publishes semantic profile/shot events,
a minimal UEVR plugin relays them over localhost, and a generalized
PSVR2Toolkit bridge exclusively owns Sense adaptive triggers and grip PCM.

The initial shot signal is native XInput rumble correlated with held R2. After
a prior-backend OpenXR-only baseline, the v1 candidate was staged in the
experimental TOW2 deployment and the user confirmed adaptive triggers work
across tested pistols and rifles. Runtime logs also prove correct live profile
selection and four calibrated Pulse Hammer downstrokes.

The next candidate routed those exact native downstroke episodes through
Toolkit grip PCM with a 0.45-0.85 speed-scaled swing. Runtime confirmed the
feedback was present, but the user reported late sound/haptics and unreliable
sensible target impact. The log contained 30 swings and 29 inferred impacts at
a median 153 ms after attack, including non-contact-like attempts. Native
rumble is therefore delayed attack/animation feedback, not contact evidence;
the rumble impact path was removed. A follow-up tracked-weapon sweep was also
runtime-rejected: 22 attempted early edges were only 2 ms median ahead of native
commit, there were zero contacts/impacts, failed tip resolution caused 5,804
fallback logs in 74 seconds, no haptics were felt, and native sound/damage still
arrived roughly 1–2 seconds after motion. The candidate was rolled back to the
prior impact-disabled baseline. Native timing requires a safe TOW2 combat
endpoint, not generic `ApplyDamage`, which would bypass game combat rules and
risk a second hit when the delayed native trace arrives. Current endpoint
discovery is observation-only and bounded to an explicit candidate-function
allowlist on the current pawn/weapon. The sole-actuator rule remains unchanged
and the `_3dof` anchor remains untouched.
See [TOW2 PSVR2 per-gun haptics](../fixes/tow2-psvr2-per-gun-haptics.md),
the [CyberpunkVR Port design reference](../projects/cyberpunk-vr-port.md), and
the [PSVR2Toolkit actuator reference](../projects/psvr2toolkit.md).

# Safe startup state

The 2026-09-19 deployment correction and AFW GPU-copy guard are recorded in
[TOW2 AFW GPU copy and resource-state guard](../fixes/tow2-afw-gpu-copy-guard.md).

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
See [the beta.4 motion-vector correction](../fixes/afw-beta4-motion-vector-scale.md).

# Required injection/stereo fixes

- TOW2 CVar scanner/freeze paths bypassed —
  [CVar scanner bypass](../fixes/tow2-cvar-scanner-bypass.md).
- Real-backbuffer D3D12 fallback + early OpenXR sync/pose/begin —
  [backbuffer and XR prep](../fixes/tow2-backbuffer-fallback-openxr-prep.md).
- TOW2-only fake-stereo confirmation threshold —
  [view-extension analyzer timing](../fixes/tow2-view-extension-analyzer-threshold.md).
- Dynamic per-call UObject candidate/class/superclass validation —
  [AddObject candidate guard](../fixes/tow2-addobject-candidate-guard.md).
- PureDark's invasive XR null-dereference VEH installs only when Native Stereo
  Fix is enabled. TOW2 requires it disabled.
- Destructive D3D rehook is suppressed while the window-message hook remains
  intact; DXGI factory/Present telemetry remains executable-scoped.

# Steam weapon 6DoF: backend and profile are both required

The active profile resolved the correct dynamic first-person weapon and raw
right-controller translation was valid, but `UObjectHook.exists(component)` was
false. TOW2 creates Steam weapon components after the initial UObject snapshot;
the title-stability AddObject guard correctly refuses ambiguous candidates and
can therefore miss the late mesh. `get_or_add_motion_controller_state()` still
returned state, but `tick_attachments()` skipped the untracked component.

Backend commit `217162d7` adds a TOW2-only explicit-enrollment path before
state lookup. It accepts only a caller-requested component on the game thread
after module-vtable, exact `FUObjectArray` identity and bounded class-hierarchy
validation. It does not broaden AddObject scanning or affect other executables.
Runtime logs changed from `exists=false` to:

```text
[TOW2] Enrolled explicit motion-controller component missed by guarded AddObject: ...
```

and weapon translation worked across swapped component instances. Full design,
rejected alternatives and publication status:
[TOW2 explicit component enrollment](../fixes/tow2-explicit-component-enrollment.md).

The profile half is deliberately small:

```text
UObjectHook_EnabledAtStartup=true
VR_MotionControlsInactivityTimer=9999.000000
```

A low-rate Lua callback follows the pawn-owned `FPVMesh.AttachChildren` chain,
attaches only a newly observed `SkeletalMeshComponent`, preserves its parent,
and applies right hand plus calibrated offsets with `permanent=true`. Detaching
the parent and using non-permanent state were rejected because swapped weapons
vanished or temporarily stopped tracking. The long inactivity timeout prevents
a separate exact-30-second dropout: physical pose movement does not refresh
UEVR's action-based controller-activity timestamp, while a trigger click does.

Use the [stable 6DoF profile playbook](../playbooks/basic-6dof-setup.md) to
separate pose, identity, state, enrollment, tick and lifetime gates.

# Fixed UEVR framework menu and controller-ray mouse

The validated profile configuration is:

```ini
UI_Framework_FollowView=false
UI_Framework_MouseEmulation=true
UI_Framework_Distance=1.750000
UI_Framework_Size=2.000000
VR_AimMethod=2
```

Stage-space framework placement fixed the OpenXR view-space/stage-space ray
mismatch, but was not sufficient by itself. TOW2's right-controller gameplay
aim continued changing view/rotation offset while pointing horizontally, which
made the stage-space menu appear to move. The first Lua candidate called only
`vr.set_aim_allowed(false)` while the framework was drawing; runtime telemetry
proved it captured and released correctly, but OverlayComponent restored aim
later in the same pre-ImGui frame.

The successful profile-local script temporarily selects game aim (method `0`)
for the entire framework session and also marks aim disallowed. It stores the
actual prior method (`2` in TOW2), holds capture through the close frame,
restores the prior method/allowed state, and calls `vr.save_config()` after
restoration because the framework performs its own close-time save before Lua
observes closure.

Runtime validation completed three open/close cycles with horizontal, vertical
and diagonal mouse movement, a stationary framework, no Lua exceptions, and
matching telemetry:

```text
[tow2menu] captured framework aim; method=2 allowed=true
[tow2menu] released framework aim; method=2 allowed=true
```

After exit, `config.txt` still contained `VR_AimMethod=2`. No backend, AFW,
attachment, melee, button or game-menu behavior changed. The packaged scripts
and required config fragment are in profile commit `69d3a7b0`. See
[Fixed UEVR framework menu with a controller-ray mouse](../playbooks/framework-menu-controller-pointer.md)
for the reusable configuration, Lua lifecycle and validation checklist.

# Performance baseline with letmein profile and OFXR (2026-11-01)

User-reported best feel so far: letmein-vr TOW2 profile (2026-11-01 import), UEVR
`afw-beta4-game-compat` with [real object creation hooks](../fixes/guarded-addobject-object-recovery.md)
(2 hooks, 0 recovery-scan adoptions), OFXR V557 2X. Session 18:46:28-18:52:08.

**UEVR / profile:** `VR_RenderingMethod=0` Native Stereo, `VR_NativeStereoFix=false`,
`OpenXR_ResolutionScale=1.0`. Validated CVars (all `verified=true`):

| Setting | Value |
|---|---|
| `sg.ShadingQuality` | 1 (game menu does not expose shading; file had 3/Epic) |
| `sg.GlobalIlluminationQuality` | 1 |
| `sg.ReflectionQuality` | 0 |
| `r.Shadow.Virtual.Enable` | 0 (stereo requirement) |
| `r.TranslucentLightingVolume` | 0 |
| `r.AmbientOcclusionLevels`, `r.DefaultFeature.AmbientOcclusion` | 0 |
| `r.LightCulling.Quality` | 0 |
| `r.DepthOfFieldQuality`, `r.MotionBlurQuality`, `r.SceneColorFringeQuality` | 0 |
| `r.Upscale.Quality` / `r.Upscale.Softness` | 1 / 0 |
| `r.SubsurfaceScattering`, `r.TemporalAA.Upsampling`, `r.VolumetricCloud` | 1 |
| `r.Tonemapper.Sharpen` / `r.TonemapperGamma` | 1.094 / 1.813 |

The `sg.*` entries come from the new Performance CVars "Scalability groups" section;
`sg.ShadowQuality` is deliberately absent (re-applying it can re-enable VSM).

**Game settings** (`GameUserSettings.ini`): DLSS Ultra Performance, `Resolution3D=66`,
3200x2000 window, Reflex on, `VisualMode=Beauty`, Effects 1, PostProcess 1, View
distance 1, Textures 2, Foliage 2, Shadows 2, AA 2, Landscape 3, hardware RT off
(`bHardwareRayTracedShadows=True` stored but `bHardwareRayTracing=False`).

**OFXR:** NVIDIA medium, 50% flow, independent eye queues on, HUD panel FG off,
**Prefer FPS over latency (`deep_pipeline=1`) ON** (flight-log `700 a=2 b=1`, `951 c=1`).
No swapchain/end-frame errors in TOW2 (24,896 `app_end_frame` results 0), unlike the
Cyberpunk swapchain issue that keeps it off there.

**Measured (flight log, settings changed live in-session; scenes not identical):**

| Period | Game fps | Presented | Never shown | GPU p50/p90 ms |
|---|---|---|---|---|
| Start: Shading 3, GI 2, Refl 1 | 39.6 | 88.8 | 0.5% | 9.3 / 11.6 |
| Shading 1, GI 1 | 42.0 | 89.5 | 0.1% | 9.4 / 11.8 |
| Shading 0, GI 2 | 38.2 | 89.8 | 0.1% | 8.7 / 11.5 |
| Shading 3, GI 3 | 32.0 | 89.8 | 0.0% | 8.7 / 11.4 |
| **Final: Shading 1, GI 1, Refl 0** (76 s) | **37.6** | **89.4** | **0.3%** | **8.7 / 11.5** |

Interpretation: lowering the scalability groups improved game performance in play (user
observation, supported by the data: Shading 3/GI 3 ran 32.0 fps, Shading 1/GI 1 42.0 fps,
i.e. ~7 ms per frame; Shading 1/GI 1 vs the Shading 3/GI 2 start was +2.4 fps). Exact
per-step gains are approximate because settings were changed live while the scene varied.
Separately, the deep pipeline kept presentation at ~89 FPS with near-zero drops at 32-42
game fps by holding/repeating frames (553 `presenter_transition 400` + 3,176 `401` of
29,827 presented, ~12.5%); a shallow pipeline lost 18-28% of frames at the same ~40 game
fps on 17:52/18:26. Both contributed. Earlier shallow-pipeline analysis: heavy areas were a
steady ~2 ms GPU shortfall (frame interval ~24 vs 22.2 ms), not particle hitches; Effects
Low alone did not help, while Shading/GI did.

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

Use [in-process hang dumps](../playbooks/in-process-hang-dump.md) when external
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
- **Inventory/2D transition** — beta.4 adds relevant 2D guards, but this needs a
  deliberate repeated menu test before being called resolved.
- **FMalloc stable promotion** — alpha.4 publishes the narrow correction as a
  prerelease, but it still needs broader cross-game regression before stable
  promotion.

# Related

- [PureDark AFW integration](../projects/puredark-afw-integration.md)
- [Narrow port scope](../decisions/narrow-port-scope.md)
- [Checkpointing and recovery](../playbooks/checkpoint-and-recovery.md)
- [Stable 6DoF profile creation](../playbooks/basic-6dof-setup.md)
- [Fixed framework menu controller pointer](../playbooks/framework-menu-controller-pointer.md)
- [Explicit dynamic-component enrollment](../fixes/tow2-explicit-component-enrollment.md)
- [FMalloc discovery and unbounded memory growth](../fixes/tow2-fmalloc-memory-leak.md)
- [TOW2 PSVR2 per-gun and melee haptics](../fixes/tow2-psvr2-per-gun-haptics.md)
- [CyberpunkVR Port PSVR2 reference](../projects/cyberpunk-vr-port.md)
- [PSVR2Toolkit actuator/bridge reference](../projects/psvr2toolkit.md)

# Citations

- `TOW2_UEVR_FIX_SUMMARY.md`, `TOW2_UEVR_WORKLOG.md`
- `<private-worklog-root>/PUREDARK_AFW_TOW2_WORKLOG.md`
- `<evidence-root>/puredark-beta4-tow2-zero-ghosting-success-20260720/profile/log.txt`
- [Cyberpunk PSVR2 adaptive-trigger/melee runbook](https://github.com/satyaloka93/cyberpunk-vr-port/blob/psvr2-tweaks/.okf/operations/psvr2-adaptive-triggers.md)
- [PSVR2Toolkit bridge project](https://github.com/satyaloka93/PSVR2Toolkit/tree/cyberpunk-dsx-bridge/projects/psvr2_toolkit_dsx_bridge)
