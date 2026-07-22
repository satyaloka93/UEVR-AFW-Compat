---
type: game-profile
title: Silent Hill f (SHf) — local Native candidate, profile and limitations
description: SHf now has a published experimental UE5.7/OpenXR compatibility source and alpha.2 profile with reliable injection, bounded owned resources and both-eye Native Stereo; AFW and automatic DLSS repair remain limited.
tags:
- silent-hill-f
- shf
- ue5.7
- openxr
- psvr2
- native-stereo
- d3d12
- vrs
- dlss
- afw
timestamp: '2026-07-22T10:48:00+09:00'
---

# Identity and proven baseline

- Executable: `SHf-Win64-Shipping.exe`
- Engine family: UE5.7
- Renderer/runtime: D3D12 + OpenXR; the proven local era used PSVR2 through
  SteamVR/OpenXR.
- Historical rendering baseline: Native Stereo in Joey Hodge's UEVR lineage.
- Current status: the narrow PureDark beta.4 compatibility source now injects
  reliably, renders both eyes in Native Stereo, keeps resource allocation
  bounded, and permits a manual Native → AFW transition.
- Publication status: source commit `cc0c43f9` publishes the runtime-tested
  candidate. Prerelease alpha.2 includes its backend and a clean SHf profile;
  alpha.1 remains unsupported for SHf.
- Recommended mode: Native Stereo. AFW is technically functional but is not
  recommended because its small additional GPU saving comes with hand/weapon
  distortion and no CPU-cadence improvement.
- Runtime-tested local candidate hashes:
  - `UEVRBackend.dll`: `5be7b51883590cb95e07f7c92e1fdd864d560e3df7a0b5819774531dd42cf9f9`
  - `UEVRBackend.pdb`: `1692ed761d0e63c3da8ac3e9c92fb235ea3987c5b9f726637de64cba99243620`
  - PureDark beta.4 `PDAFWPlugin.dll`: `76bbc4d7a5370ba0d6a1a50de6b81d45223e42b764d2d7c79f0c6c48a59f6b64`
- Historical working checkpoint tags:
  - `57shf54` at `f10ec09a` — UE5.7 OpenXR resolution/frame/input correction.
  - `shf57` at `b8bb816e` — SHf stable UI/scene-target baseline.

The Joey-derived baseline bring-up is captured in
[/fixes/shf-ue57-openxr-bootstrap.md](../fixes/shf-ue57-openxr-bootstrap.md).
It documents both the historical source lineage and the bounded rules retained
by the local port; those candidate changes are not yet delivered by the public
PureDark AFW branch. It was not one injection fix: validated/cached UE5.7
discovery, OpenXR startup ownership, fail-soft viewport handling, bounded
CVar/D3D retries, and stable owned render resources all had to work together.

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

# Injection and resource fix set

The published candidate narrowly ports the tagged `shf57` bring-up and
later SHf recovery mechanisms into the PureDark beta.4 destination. The
historical distinctions below remain intentional because the later source and
the current candidate have not been preserved as clean committed checkpoints.

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
- Reuse a compatible UI texture instead of rebuilding it every frame. The
  candidate adds a four-attempt per-resolution allocation circuit breaker and
  500 ms D3D setup retry throttling. A validated run produced one UI allocation,
  327 stable-reuse reports, five bounded D3D setup passes, no circuit-breaker
  activation, and no `Failed to get back buffer` loop.
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
  message hook proves the injection is still alive, use direct RHI pose enqueue
  instead of the fragile generic RHI command-hook path, and retain an explicit
  right-eye composition fallback while SHf's resources settle.

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

# Injection procedure

1. Use a clean SHf profile and a fresh `SHf-Win64-Shipping.exe` process; inject
   early rather than attaching after the title has initialized its renderer.
2. Start with Native Stereo. Do not persist AFW as the startup method.
3. Let OpenXR, the final `2632x2684` per-eye target, scene copies and profile Lua
   stabilize before opening graphics settings or changing rendering modes.
4. Keep warning-level logging for performance runs. In one diagnostic capture,
   hot texture-hook `info` messages accounted for about 96% of a 327,787-line
   log.
5. If AFW is tested, select it manually only after gameplay begins and restart
   the game to return to Native.

The first PureDark bootstrap candidate violated these rules internally: it
created 1,289 UI textures and performed 1,290 D3D12 texture setups in about 52
seconds, exhausted VRAM, flattened textures and left the Native right eye black.
That candidate was reverted publicly. Stable reuse, bounded allocation and
owned scene resources are therefore safety requirements, not optional tuning.

# Profile-side Lua correction

SHf pawn classes omit some reflected APawn fields expected by generic profile
libraries, including `bUseControllerRotationPitch`. The deployed
`scripts/libs/input.lua` was changed to `pcall`-guard optional pawn/controller
rotation and movement-bool reads/writes. This prevents missing optional
properties from becoming per-frame LuaVR exceptions.

# Recommended profile

The stable launch shape is Native/OpenXR with AFW stored but inactive:

```ini
FrameworkConfig_LogLevel=3
Frontend_RequestedRuntime=openxr_loader.dll
VR_RenderingMethod=0
VR_NativeStereoFix=true
VR_NativeStereoFixSamePass=true
VR_SynchronizationMode=2
VR_ExtremeCompatibilityMode=false
VR_RecreateTexturesOnReset=true
VR_GhostingFix=false
VR_GhostingFixBootstrapViewStates=false
VR_AFW_FramewarpMode=2
VR_AFW_FixMovingObjectBrightnessFlickering=false
VR_AFW_FixObjectMotionRange=3.000000
VR_AFW_IgnoreMotionThreshold=2.500000
VR_AFW_UltraResponsive=true
```

`VR_AFW_FramewarpMode=2` records Previous Frame mode but does not activate it
while `VR_RenderingMethod=0`. Never automatically enable AFW for SHf. Profile
experiments can re-save Ghosting Fix bootstrap independently; reset both
Ghosting Fix values to `false` before the next Native launch.

The original runtime-tested candidate has exact backend/PDB/runtime hashes, and
source commit `cc0c43f9` plus the alpha.2 profile publish the corresponding
implementation and launch state. Preserve fresh-process backend/PDB/config/
scripts/log manifests when validating the rebuilt release binary.

# DLSS startup and profile behavior

SHf can initialize DLSS before UEVR's final stereo dimensions and quality
fraction have settled. Two distinct stale states were observed:

- an early desktop generation used output `2560x1440` while UEVR was still at
  `1280x1440` per eye; later game-owned recreation produced output
  `2632x2684`;
- with DLSS 3.10.7 profile L, final-size NGX features could exist while their
  input remained full `2632x2684`. Re-selecting Performance in SHf changed the
  input to approximately `1316x1342` without replacing the main handles.

The safe workaround is therefore game-owned: start Native, wait for the final
VR target, then change DLSS away from and back to Performance in SHf's graphics
menu. Do not force-release NGX handles from UEVR.

Profile letters and render scale are separate. Profile L controls a DLSS model
preset; SHf's visible Performance mode produced the normal 50% input scale. A
prior profile-M run produced approximately one-third input, and a later profile
L plus NVIDIA Inspector Ultra Performance override improved the fpsVR mean from
about 19.09 ms GPU/38.03 application FPS to 16.19 ms/40.35 FPS. That latest run
used the passive stable backend, so it is evidence of improvement but **not
proof** that the Inspector override achieved exactly one-third resolution. CPU
time was 23.86 ms, limiting application cadence despite the GPU reduction.

Automatic repair is currently rejected:

- a delayed direct `r.NGX.DLSS.Quality` experiment produced no improvement;
- the delayed reflected `NoceLocalGameUserSettings` experiment froze SHf when
  its game-thread callback ran; evidence does not prove `ApplySettings(false)`
  was reached, and the helper was removed;
- no automatic profile script may be restored until the exact game-owned UI
  callback is identified passively and invoked without reflection traversal.

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

# AFW local status and limitations

The narrow local beta.4 candidate successfully reconciles the required Joey
bootstrap/resource mechanisms with PureDark AFW, but it is not release-ready:

- Native Stereo renders both eyes without runaway allocation.
- Manual Native → Previous Frame AFW works after gameplay begins. A measured
  run reported 40.28 application FPS, 89.90 Hz total cadence, 16.74 ms GPU,
  21.34 ms CPU and zero dropped frames.
- Ghosting Fix established stable eye ownership, but hand/weapon distortions
  remained. Preserved SH2 motion tuning improved them without eliminating them.
- After the DLSS setting is correctly reapplied, AFW removes only another
  roughly 3–4 ms of GPU time. Application cadence remains around 40–41 FPS
  because CPU timing is approximately 21–24 ms.
- AFW → Native remains unsafe because PDAFW exposes no teardown API and can
  leave the right eye black. Restart to leave AFW.
- Brightness-flicker correction stays disabled because it can introduce
  pumping/shimmer.

Consequently Native Stereo plus the manual DLSS reapply is the recommended SHf
configuration. Alpha.2 publishes the working experimental source, backend and
profile, but remains a prerelease until sustained gameplay, transitions and
image quality are reproduced with the released hashes.

# Remaining gaps

- Source and profiles are published, but the rebuilt alpha.2 backend still
  requires runtime reproduction against its released hashes.
- No sustained clean-run checkpoint yet combines the alpha.2 backend, PDB,
  loader, installed profile, game config and long gameplay log.
- DLSS still requires a manual game-menu reapply after startup; profile L at an
  exact Ultra Performance fraction has not been telemetry-confirmed.
- Reflected settings automation is rejected because it froze the game; direct
  NGX release remains prohibited because the game owns those handles.
- AFW hand/weapon distortion, missing teardown and CPU-limited cadence make AFW
  unsuitable as the recommended mode.
- Source commit `cc0c43f9` isolates the retained vtable bootstrap,
  scene-capture copy, composition, rehook and direct-pose follow-ups; their
  cross-game regression surface still requires testing.
- Re-test long Native gameplay, cinematics, save/load and graphics transitions
  before publishing; test actual Silent Hill 2 separately under
  `SHProto-Win64-Shipping.exe`.

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
- Published SHf source: commit `cc0c43f9`
- Runtime evidence placeholders:
  `<evidence-root>/shf-afw-bootstrap-candidate-20260720`,
  `<evidence-root>/shf-stable-resources-candidate2-20260722`,
  `<evidence-root>/shf-dlss-generation-telemetry-candidate4-20260722`, and
  `<evidence-root>/shf-reflected-settings-candidate5-freeze-20260722`
- Profile: `%APPDATA%\UnrealVRMod\SHf-Win64-Shipping\`
