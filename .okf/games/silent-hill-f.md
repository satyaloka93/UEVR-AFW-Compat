---
type: game-profile
title: Silent Hill f (SHf) — rebuilt AFW profile, compatibility state and limitations
description: SHf's maintained rebuilt first-person profile uses cold-start Previous Frame AFW with Native Stereo Fix off; alpha.4 publishes the f37 plus fail-closed SceneView backend as an experimental prerelease with decisive guard-path and broader cross-game validation still open.
tags:
- silent-hill-f
- shf
- ue5.7-lineage
- ue5.4.2
- openxr
- psvr2
- native-stereo
- d3d12
- vrs
- dlss
- afw
timestamp: '2026-08-12T13:33:00+09:00'
---

# Identity and proven baseline

- Executable: `SHf-Win64-Shipping.exe`
- Engine family: crash metadata reports UE 5.4.2 (`5.4.2-0+++NOCEDev`).
  “UE5.7” remains only a historical branch/lineage label.
- Renderer/runtime: D3D12 + OpenXR; the proven local era used PSVR2 through
  SteamVR/OpenXR.
- Historical rendering baseline: Native Stereo in Joey Hodge's UEVR lineage.
  The current first-person profile does **not** use Native Stereo Fix.
- Current status: the rebuilt first-person profile starts directly in Previous
  Frame AFW with bounded resources, Ghosting Fix/Bootstrap enabled and
  moving-object brightness correction disabled. Native Stereo Fix makes its
  image incorrect and must remain off.
- Alpha.4 backend status: the f37 plus SHf SceneView fail-closed backend
  completed one approximately 35-minute stable run after PSVR2 runtime
  recovery. A later approximately 16-minute gameplay run also remained free of
  the old SceneView crash, but exposed a separately symbolized Lua
  `ScriptContext` destructor logging AV during process exit; that invalid
  shutdown call is now removed. Neither SceneView guard signature appeared.
- Profile publication status: `profiles/SHf-Win64-Shipping/` now maintains a
  sanitized snapshot of the active rebuilt first-person/6DoF profile, including
  its settle/core/IK wiring and UObjectHook camera state. The old alpha.2 release
  archive remains historical and must not be merged over this package.
- Backend publication status: alpha.4 publishes the matching f37 plus
  SceneView-guard backend as an experimental prerelease. Alpha.1 remains
  unsupported for SHf.
- Alpha.4 release hashes:
  - `UEVRBackend.dll`: `1e502f87bb64484b676dd91c25f52ff74d161c6311ee904bd796e7df2f769593`
  - `UEVRBackend.pdb`: `1d5d7214d351e3a5ebfbcb21d8d15ad743ddbcec0aab2029a1a736fc8e6b8aca`
  - amended beta.4-derived `PDAFWPlugin.dll`: `b129118ba239e0c9fd7b0c803dab0199242af7142c7b9541e656e2f3eaca8ff9`
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

# 2026-07-31: load crash was PROFILE-caused, not the game update (superseding the first analysis)

The initial 2026-07-30 analysis blamed the 2026-07-23 Steam patch for breaking
the bootstrap. That was **wrong** and is corrected here (user-confirmed):

- **No-profile injection works on the patched exe** — on both the AFW-Compat
  backend and the user's main-fork build (3rd person, and AFW gave a HUGE
  performance boost — the older "AFW not recommended for SHf" guidance in this
  document is obsolete).
- **The crash was the first-person profile.** It also crashed a third party's
  AFW build. Root cause, symbolized from UE's own crash dumps
  (`%LOCALAPPDATA%\SHf\Saved\Crashes\` — UE's handler catches the AV, which is
  why UEVR/WER never produced dumps): the profile's `shf.lua` lantern search
  calls `find_first_of` → native `get_first_object_by_class`, a **full
  GUObjectArray walk from tick context during level-load churn** → garbage
  item pointer (FName text "Scen…" read as an object pointer, noncanonical →
  `EXCEPTION_ACCESS_VIOLATION reading 0xffffffffffffffff`).
- **Fix: a 12-second post-level-load settle guard** before the class scan
  (patched into the active profile's `shf.lua`; pristine original preserved in
  `SHf-Win64-Shipping-LETMEIN`). With the guard, the profile injects into
  first person on the AFW-Compat backend.
- The "failing scans" the first analysis blamed (PostInitProperties,
  GetViewportSizeXY, DrawWindow_RenderThread, chunk-size default) appear in
  known-good pre-update logs too — benign noise, not causes. The UESDK-pin
  A/B (identical crash on `491f973a` and `9034a857`) remains valid evidence
  that the SH2 unification did not break SHf.
- **Engine version correction: SHf is UE 5.4.2** (`5.4.2-0+++NOCEDev`, every
  crash context back to 2026-07-07). The "UE5.7" label throughout this
  document came from branch-naming folklore; treat version-specific claims
  below accordingly.

## Working first-person + AFW profile (2026-08-01, rebuilt)

**Superseded the 2026-07-31 notes below.** The profile was rebuilt after further
crashes: `main.lua` is gone entirely, replaced by minimal standalone scripts.
Full rationale, the crash root causes, the UEVR Lua sandbox constraints and the
rejected approaches are in
[SHf profile rebuild](../fixes/shf-profile-rebuild-main-lua-removal.md).

Active scripts in `SHf-Win64-Shipping/scripts/`:

| File | Role |
|---|---|
| `00_settle.lua` | Settle gate (4 s after pawn stable, re-closes on pawn change); publishes `_G.SHF_SETTLED`, `_G.SHF_PAWN_ADDR` |
| `91_button_swap.lua` | X↔B dodge remap (extracted from `main.lua`) |
| `92_core_init.lua` | pawn/attachments/input/ik init, IK-mesh wiring, weapon grip callback, reload hardening |
| `shf.lua` | First person, camera, movement, state gating (lantern scan disabled; ViewTarget guards added) |
| `melee.lua` | Collision-gated physical swing candidate that injects native RB/light or RT/heavy attacks |
| `examine.lua` | Examine-puzzle support |

The 2026-08-02 audit found that parking `main.lua` also removed the only loader
that applied `data/shf_melee_config.json` through `melee.setConfig`; the active
script therefore uses hardcoded thresholds rather than its saved calibration.
It also enumerates enemy objects repeatedly and does not exclude future
firearms. Treat swing attacks as an unfinished candidate, not part of the
validated minimal baseline. See
[SHf profile rebuild](../fixes/shf-profile-rebuild-main-lua-removal.md).

SHf is also the controller-ray framework-menu reference: with
`UI_Framework_FollowView=false`, `UI_Framework_MouseEmulation=true` and its
normal `VR_AimMethod=0`, the menu remains fixed while the right-controller ray
moves the mouse cleanly in both axes. It needs no temporary aim workaround;
controller-aim profiles can reproduce this behavior with the lifecycle in
[Fixed UEVR framework menu with a controller-ray mouse](../playbooks/framework-menu-controller-pointer.md).

Parked: `main.lua`, `hands.lua`. Disabled: `90_weapon_attach.lua.disabled` (a
working native-UObjectHook attach alternative that cannot produce the
closing-hand grip pose). Pristine original profile preserved as
`SHf-Win64-Shipping-LETMEIN` — do not modify.

Config: `VR_RenderingMethod=3` (cold-start AFW), `VR_AFW_FramewarpMode=2`,
`VR_PassDepthToRuntime=false`, `VR_LoadBlueprintCode=false`,
`UObjectHook_EnabledAtStartup=true`, `smoothTurnSpeed=60`.

Validated: no crashes across extended play, scene transitions, weapon swaps and
save/exit/reload. ~72 fps avg in AFW at 80% resolution (45–89 range) versus ~90
for a bare profile. Frame pacing is open work.

## Working first-person + AFW profile (2026-07-31, validated)

After iterative isolation, SHf now runs the rebuilt first-person profile in
**cold-start AFW at a solid 90 fps** (couple-of-minutes session, lantern/knife
switching clean) on the AFW-Compat backend `54c5f9d3…`. The profile
(`SHf-Win64-Shipping`, pristine original preserved as
`SHf-Win64-Shipping-LETMEIN`) is LETMEIN's config/scripts/data plus:

1. `scripts/00_settle.lua` — loads FIRST and (a) wraps
   `on_pre/post_engine_tick` registrations so all script callbacks stay
   dormant until the local pawn is stable for 10 s (re-gates on pawn loss, so
   every load screen is protected), and (b) **drops all stereo-view-offset
   Lua callback registrations** — under AFW, scripts running through that
   dispatch corrupt the heap (`ntdll` allocator AVs moments after scripts
   activate; suspected transient position/rotation buffers on the AFW path;
   Native-only sessions could re-enable them). Costs: melee attack camera pin
   and decoupled-pitch-class camera features are disabled.
   **NTFS load-order trap:** LuaLoader loads scripts via `directory_iterator`
   = NTFS upcased-name order, where `_` sorts AFTER letters — an earlier
   `_00_settle.lua` silently loaded LAST and gated nothing. Digit prefixes
   (`00_`) genuinely load first.
2. `scripts/shf.lua` — lantern `find_first_of` (full GUObjectArray walk)
   **disabled entirely**: it crashed even in settled gameplay on lantern
   re-equip (not just load churn). Cosmetic cost: lantern cutscene visibility
   enforcement no-ops.
3. `config.txt` — `VR_RenderingMethod=3` (cold-start AFW),
   `VR_AFW_FramewarpMode=2` (mode 3/Combined has the crash rap sheet),
   `VR_PassDepthToRuntime=false`; `data/input_parameters.json`
   `smoothTurnSpeed` 106 → 60 (also adjustable via the scripts' in-game
   "Smooth Turn Speed" slider — stock UEVR only exposes snap turn).

Open items: AFW lifecycle remains unsafe with this profile. Start directly in
the desired mode from a fresh process; the retained first-person checkpoint
uses cold-start Previous Frame AFW. Restart to change modes, and never re-enter
AFW in the same process. DLSS may still need the
manual in-game quality reapply after injection. Still held back:
`uevr_mcp.dll`, OpenVR binding JSONs (unneeded under OpenXR). Backend
follow-up: fix or gate the AFW-path Lua stereo-view dispatch so profiles keep
camera features under AFW.

# 2026-08-07: intermittent SceneView race on the FMalloc candidate

Two SHf runs on the TOW2 `f37f61c` FMalloc candidate crashed with
`EXCEPTION_ACCESS_VIOLATION` reading `0xffffffffffffffff` in
`FFakeStereoRenderingHook::sceneview_constructor()` line 3173:

- the immediately preceding discovery failed to prove `FSceneViewFamily`
  offsets and guessed scene-interface offset `0x10`;
- no `FMalloc::get()` line appeared, so allocator discovery did not cause it;
- the same function, line and exception address exist in an older pre-f37
  transition/re-entry crash, proving the underlying SceneView race predates
  f37.

One of those runs initialized with Native Stereo Fix and Same Pass enabled. The
image was wrong, those controls were disabled in the UI, and selecting AFW then
crashed. That run is invalid as an A/B because disabling a hook control does not
undo hooks already installed in that process.

A subsequent fresh cold-AFW run did **not** crash. Its log authoritatively
identified the f37 candidate (`70f6e508+local-uesdk-f37f61c`), despite an
intended old-backend control deployment. It ran for about 213 seconds with the
retained profile state: mode 2, Native Stereo Fix/Same Pass off,
Ghosting/Bootstrap on and moving-object brightness correction off. Discovery
again had early difficulty, but did not fall through to the fatal guessed scene
offset.

Therefore f37 does not deterministically regress SHf and its allocator logic is
not implicated. SHf still blocks clean promotion because the pre-existing,
timing-sensitive SceneView discovery race can kill otherwise identical starts.
The exact pre-f37 A/B was not performed; an isolated control deployment remains
available but must never be used for TOW2 because it lacks f37.

A narrow uncommitted candidate now applies the existing validate-or-skip rule at
this exact call site: on SHf only it publishes the source-confirmed
`views=0x8/render-target=0x30/scene=0x38` layout after live range, array and
module-vtable validation; otherwise it skips scene-dependent remapping and lets
a later constructor retry. It does not enable the rejected Avowed per-frame
rescan behavior. See [SHf FSceneViewFamily validated layout and fail-closed constructor guard](../fixes/shf-sceneviewfamily-fail-closed.md).

# 2026-08-12: gameplay pass and exit-time Lua lifetime correction

The current working profile/backend completed approximately 16 minutes of SHf
gameplay without the old SceneView constructor crash. At process exit, after
OpenXR lost focus and NVIDIA shutdown began, UEVR generated `crash.dmp` for an
access violation in the backend. Matching-PDB symbolization resolved it to
`ScriptContext::log()` line 94, called by the `ScriptContext` destructor after
the plugin API function table was no longer safe.

The narrow correction removes only that destructor diagnostic call. It is
independent of SHf scene-family discovery and documented in the
[Lua ScriptContext shutdown logging guard](../fixes/lua-scriptcontext-shutdown-log.md).
The release build should receive a clean-exit retest.

# 2026-08-08: first stable combined-candidate run

After a separate PSVR2/SteamVR watchdog failure was recovered, the same f37 plus
SceneView-guard candidate ran the retained cold-AFW profile for approximately
35 minutes without a new UE crash report, allocator failure, GPU/device-hung
signature or PSVR2 watchdog. SteamVR later exited gracefully.

Neither `[SHf] Published validated FSceneViewFamily layout ...` nor the
transient fail-closed warning appeared. The run therefore validates broad SHf
stability but does not prove that the new guard intercepted the formerly fatal
constructor timing. Keep repeating fresh cold starts until a guard signature is
captured; then regression-test Avowed, TOW2 and SH2 before promotion.

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

# Current injection procedure

1. Use the retained rebuilt SHf profile and a fresh
   `SHf-Win64-Shipping.exe` process; inject early rather than attaching after
   the title has initialized its renderer.
2. Start directly in Previous Frame AFW (`VR_RenderingMethod=3`, mode `2`) with
   Native Stereo Fix and Same Pass both off. Do not change rendering modes in
   that process.
3. Let OpenXR, the final per-eye target, scene copies and profile Lua stabilize
   before opening graphics settings. If DLSS is stale, change it away from and
   back to Performance in the game menu.
4. Keep warning-level logging for performance runs. In one diagnostic capture,
   hot texture-hook `info` messages accounted for about 96% of a 327,787-line
   log.
5. Restart the process to change rendering modes. PDAFW has no teardown API;
   never switch AFW → Native → AFW in-process.

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

# Current rebuilt-profile configuration

The retained checkpoint starts directly in Previous Frame AFW:

```ini
FrameworkConfig_LogLevel=3
Frontend_RequestedRuntime=openxr_loader.dll
VR_RenderingMethod=3
VR_NativeStereoFix=false
VR_NativeStereoFixSamePass=false
VR_GhostingFix=true
VR_GhostingFixBootstrapViewStates=true
VR_AFW_FramewarpMode=2
VR_AFW_FixMovingObjectBrightnessFlickering=false
```

Native Stereo Fix/Same Pass must remain off for this profile. Install the
maintained source-tree profile into a clean directory; do not merge the old
alpha.2 release profile over it because removed top-level scripts can survive.
Preserve fresh-process backend/PDB/config/scripts/log manifests when validating
any rebuilt release binary.

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

The current rebuilt profile uses cold-start Previous Frame AFW rather than the
older Native-first alpha.2 procedure:

- Ghosting Fix plus Bootstrap and object motion vectors recovered stable eye
  ownership; moving-object brightness correction remains off.
- AFW can synthesize toward the headset cadence, but the application remains
  CPU-limited and residual hand/weapon distortion is still possible.
- The game-owned DLSS quality reapply remains the only safe workaround.
- AFW → Native remains unsafe because PDAFW exposes no teardown API and can
  leave the right eye black. Restart to change modes.
- The combined f37 plus SceneView-guard candidate completed one approximately
  35-minute stable run, but did not emit either decisive guard signature.

The alpha.2 release archive remains a historical prerelease checkpoint. The
maintained source-tree SHf profile now supersedes its profile contents for any
future package.

# Remaining gaps

- Clean-exit retest the release build to confirm the Lua destructor no longer
  produces an exit-time dump.
- Repeat fresh cold-AFW starts until the validated-layout or transient
  fail-closed signature proves the guarded constructor path was exercised.
- Regression-test the combined f37 plus SceneView candidate in Avowed, TOW2 and
  SH2 before release promotion.
- DLSS still requires a manual game-menu reapply after startup; profile L at an
  exact Ultra Performance fraction has not been telemetry-confirmed.
- Reflected settings automation is rejected because it froze the game; direct
  NGX release remains prohibited because the game owns those handles.
- Residual AFW hand/weapon distortion, missing teardown and CPU-limited cadence
  remain open limitations.
- Test loading, menus, weapon swaps, gameplay and clean exit across repeated
  SHf starts; test actual Silent Hill 2 separately under
  `SHProto-Win64-Shipping.exe`.

# Relationships

- 6DoF method: [Stable 6DoF profile creation](../playbooks/basic-6dof-setup.md)
- Profile fix: [Minimal SHf profile rebuild](../fixes/shf-profile-rebuild-main-lua-removal.md)
- Renderer fix: [SHf OpenXR bootstrap](../fixes/shf-ue57-openxr-bootstrap.md)
- Shutdown fix: [Lua ScriptContext lifetime guard](../fixes/lua-scriptcontext-shutdown-log.md)
- Contrast: [Silent Hill 2](silent-hill-2.md)

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
- Maintained package: `profiles/SHf-Win64-Shipping/`
- Runtime install: `%APPDATA%\UnrealVRMod\SHf-Win64-Shipping\`
