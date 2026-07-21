---
type: game-profile
title: Silent Hill f (SHf) — baseline bring-up and known state
description: SHf was brought up in Joey Hodge's baseline UEVR lineage through a staged UE5.7/OpenXR bootstrap and stable owned resources; that Native Stereo work is historical reference only because the current PureDark AFW compatibility release does not yet run SHf.
tags:
- silent-hill-f
- shf
- ue5.7
- openxr
- psvr2
- native-stereo
- d3d12
- vrs
timestamp: '2026-07-20T00:00:00Z'
---

# Identity and proven baseline

- Executable: `SHf-Win64-Shipping.exe`
- Engine family: UE5.7
- Renderer/runtime: D3D12 + OpenXR; the proven local era used PSVR2 through
  SteamVR/OpenXR.
- Known rendering baseline: Native Stereo in Joey Hodge's UEVR lineage, not
  PureDark AFW.
- Current AFW compatibility status: **not working**; SHf is not supported by
  this release yet.
- Historical working checkpoint tags:
  - `57shf54` at `f10ec09a` — UE5.7 OpenXR resolution/frame/input correction.
  - `shf57` at `b8bb816e` — SHf stable UI/scene-target baseline.

The Joey-derived baseline bring-up is captured in
[/fixes/shf-ue57-openxr-bootstrap.md](/fixes/shf-ue57-openxr-bootstrap.md).
It documents source lineage to port, not functionality already delivered by the
current PureDark AFW branch. It was not one injection fix: validated/cached UE5.7 discovery, OpenXR startup
ownership, fail-soft viewport handling, bounded CVar/D3D retries, and stable
owned render resources all had to work together.

Do not confuse SHf with **Silent Hill 2** (`SHProto-Win64-Shipping.exe`), which
has a different profile and AFW history.

# Why baseline UEVR originally failed

SHf exposed several UE5.7 timing and lifetime differences at injection:

- view-extension and Slate/UI discovery could run before scene-family offsets
  and render-thread state were valid;
- the normal fake-stereo scene target and `FRHITexture2D` vtable appeared late;
- unsafe LocalPlayer/PostInit or `FViewportInfo` guesses could crash startup;
- OpenXR could enter READY with partial tracked bits and leave an early frame
  open before a valid D3D12 submit existed;
- the engine rotated external scene render-target objects while UEVR retained
  descriptors to the old object;
- CVar polling, discovery logging and immediate retry loops amplified the
  startup race.

The visible result varied between failure to inject, indefinite waiting,
repeated D3D12 setup/UI recreation, a null scene target, or a wedged OpenXR
frame loop.

# Baseline fix set

This combines the tagged `shf57` bring-up with later SHf recovery mechanisms
retained in the current baseline integration tree. The distinctions below are
intentional because the later source was not preserved as a clean checkpoint.

## UE5.7 startup and discovery

- Cache SceneViewExtension indices/frame-count offsets only after validating
  module ownership, index bounds and plausible offsets.
- Delay dedicated UI creation until PreRenderViewFamily and
  FSceneViewFamily-offset discovery are complete.
- Track UI creation generations and own the UTexture reference so stale
  asynchronous completion cannot overwrite the current target.
- If UE5.7 PostInitProperties or viewport-provider layout is not proven, skip
  it rather than calling through a guessed pointer.
- Apply bounded D3D12 initialization retry backoff and avoid flushing every
  discovery `info` line synchronously.
- Load CVar files once and update/freeze only the values requiring work.

## OpenXR startup ownership

- Synchronize the first frame when the session reaches READY and permit the
  frame loop in READY/SYNCHRONIZED/VISIBLE/FOCUSED states.
- Accept startup poses when position/orientation are valid and tracking has
  begun, even if every tracked bit is not yet asserted.
- Reject invalid eye projections without destroying the last valid matrices.
- Close an empty startup frame if no valid poses/scene submission are ready;
  recover stuck `frame_began` and call-order failures instead of preserving a
  permanently open frame.
- Recreate renderer textures and swapchains when OpenXR resolution changes.

## SHf render-target lifetime

- Force the FSceneViewport separate-RT flags from `UGameViewportClient::Draw`
  only after validating the vtable and candidate bool bytes.
- Reuse a compatible UI texture instead of rebuilding it every frame. This
  reduced thousands of renderer reinitializations to one UI creation, two
  expected D3D12 setup passes, and periodic reuse telemetry.
- **Tagged `shf57`:** copy SHf's volatile external scene RT into the
  UEVR-owned committed resource `SHf Stable Scene Copy`; rebuild only when its
  descriptor changes and suppress unsafe mirror/2D use until that copy exists.
- **Retained follow-up:** apply the same ownership rule to volatile scene
  capture with `SHf Stable Scene Capture Copy`, then composite the real
  backbuffer plus stable scene capture into the OpenXR double-wide target.
- **Retained follow-up:** permit render-thread scene-capture bootstrap when the
  normal `FRHITexture2D` vtable is unavailable, then publish the vtable from the
  valid created target.
- **Retained follow-up:** suppress redundant D3D rehooks while the Windows
  message hook proves the injection is still alive, and use direct RHI pose
  enqueue instead of the fragile generic RHI command-hook path.

Preserve the follow-up mechanisms when integrating AFW/OpenXR work, but do not
attribute them to tag `shf57`; they still lack an isolated source checkpoint.

# Healthy baseline signatures

```text
[OpenXR] Session is READY; synchronizing first frame
[OpenXR] Got first valid poses ...
Found final frame count offset ...
Done setting up BeginRenderViewFamily hook!
[SHf] Scene capture texture created via render-thread bootstrap!
[SHf][D3D12] Creating owned stable scene copy ...
[SHf][D3D12] Copied volatile external RT into owned stable scene texture
[SHf] Reusing stable UI texture ...
```

A few early `Failed to get back buffer (D3D12)` retries are not decisive; they
must stop once the scene target is bootstrapped. Continuous null-vtable,
backbuffer, renderer-setup, `frame_began`, or rehook lines mean baseline
initialization did not complete.

# Profile-side Lua correction

SHf pawn classes omit some reflected APawn fields expected by generic profile
libraries, including `bUseControllerRotationPitch`. The deployed
`scripts/libs/input.lua` was changed to `pcall`-guard optional pawn/controller
rotation and movement-bool reads/writes. This prevents missing optional
properties from becoming per-frame LuaVR exceptions.

# Baseline profile essentials

The current profile still records the intended Native/OpenXR shape:

```ini
Frontend_RequestedRuntime=openxr_loader.dll
VR_RenderingMethod=0
VR_NativeStereoFix=true
VR_NativeStereoFixSamePass=true
VR_SynchronizationMode=2
VR_ExtremeCompatibilityMode=false
VR_RecreateTexturesOnReset=true
VR_GhostingFix=false
VR_GhostingFixBootstrapViewStates=false
```

This is **not** a complete hash-pinned known-good profile: the live directory
was subsequently used for VRS and AFW experiments, and its latest `log.txt` was
overwritten by a failed PureDark run. Reconstruct the baseline from the
`shf57` code lineage plus a clean Native profile; do not treat the current
profile hash as authoritative.

# Cinematic 2D handling

After the injection/render baseline worked, commits `258f5983` and `529d1644`
added backend detection for SHf's mono cinematics and routed them through the
2D-screen path. This was intentionally moved out of profile scripts so scene
transitions follow the renderer's actual state.

# Performance and VRS follow-up

Later baseline testing reported roughly 25 ms frametime with Lumen at a
6800x3468 double-wide scene RT after stable UI reuse and scene-copy fixes.

SHf was also the first testbed for experimental injected VRS
(`FoveatedRendering.cpp`, `VRSInjector.cpp`, default off):

- PSVR2/OpenXR eye tracking and tier-1/tier-2 VRS were active;
- artifacts were visible and fpsVR showed no measured saving;
- eye-tracked mode rebuilt the shading-rate image every frame, while fixed
  foveation held `sri_updates=1`;
- publishing was throttled to about 30 Hz/meaningful gaze movement, viewport
  binding was restricted to matching OM render targets, and 4x4 was disabled by
  default;
- the corrected VRS path was not subsequently validated, so VRS remains off.

# AFW and later integration status

The current PureDark AFW compatibility release **does not yet work with SHf**.
The working code described above came from Joey Hodge's baseline UEVR lineage
and has not been successfully reconciled with this AFW branch.

Backups named `config.txt.pre_afw_emergency_rollback_20260711_1850` and
`config.txt.pre_afw_disable_20260711_1900` record an SHf AFW rollback. The
2026-07-19 PureDark integration failure repeatedly reports a null
`FRHITexture2D` vtable and `Failed to get back buffer`, never reaches the SHf
scene-capture bootstrap, and then loses the OpenXR session. Preserve the Joey
Native baseline and PureDark AFW as separate deployments until the required SHf
bootstrap/resource work is deliberately ported and validated.

## Unvalidated AFW bootstrap candidate

The branch now has a narrow SHf-only candidate intended to break the observed
startup deadlock without claiming success:

- force the validated SHf FSceneViewport separate-RT bytes;
- permit scene-capture creation before the normal fake-stereo
  `FRHITexture2D` vtable exists;
- validate and publish the scene-capture texture's module-backed vtable from the
  render thread;
- immediately publish the validated scene-capture target instead of waiting on
  SHf's blocked startup RHI/game-thread handoff;
- use the real D3D12 backbuffer temporarily while the fake-stereo target is
  absent; and
- suppress destructive D3D rehooks while the Windows message hook is intact.

Expected progress telemetry includes `[SHf] Bootstrapped FRHITexture2D vtable
from scene capture` and `[SHf] Scene capture texture created via render-thread
bootstrap!`. This candidate is not part of `v0.1.0-alpha.1` and remains
unsupported until a new runtime log proves setup reaches valid stereo/AFW.

# Remaining recovery gaps

- No SHA-256 manifest exists for the original known-good SHf backend, PDB,
  loader and complete profile.
- The retained vtable-bootstrap, scene-capture-copy, composition, rehook and
  direct-pose follow-ups were not isolated as a clean source commit.
- The `config.pre_waiting_diag_20260706_192114.txt` bring-up incident lacks its
  matching baseline log/binary checkpoint.
- Re-test Native Stereo from a clean profile and preserve backend/PDB/config/
  scripts/log hashes before changing SHf again.

# Citations

- Git history and tags: common UE5.7 base through `6e4f0c5e`; parallel
  checkpoints `44d613cd` → `f10ec09a` (`57shf54`) and `835dbccb` →
  `c199a185` → `7bbbbf3a` → `b8bb816e` (`shf57`); cinematic follow-ups
  `258f5983`, `529d1644`
- Code: `src/Framework.cpp`, `src/mods/VR.cpp`,
  `src/mods/vr/CVarManager.cpp`, `src/mods/vr/D3D12Component.cpp`,
  `src/mods/vr/FFakeStereoRenderingHook.cpp`,
  `src/mods/vr/runtimes/OpenXR.cpp`
- `TOW2_UEVR_WORKLOG.md` lines ~1699–1738 (2026-07-08/09 follow-up)
- Profile: `%APPDATA%\UnrealVRMod\SHf-Win64-Shipping\`
