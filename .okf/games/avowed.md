---
type: game-profile
title: Avowed — working VR state
description: Patched Avowed's known-good 6DoF state uses Native Stereo and a heavily extended dynamic Lua profile; current hand work must prove raw positional tracking and existing-component identity before revisiting the parked native-bone experiment.
tags:
- avowed
- native-stereo
- openvr
- openxr
- psvr2
timestamp: '2026-08-04T07:54:00+09:00'
---

# Status

Playable in 6DoF VR. The game's patch broke UEVR's original stereo discovery;
recovery took four layers, in order of importance:

1. **Stereo recovery** — [/fixes/stereo-vtable-scan-widening.md](../fixes/stereo-vtable-scan-widening.md)
   plus [/fixes/native-stereo-safe-activation.md](../fixes/native-stereo-safe-activation.md)
   and [/fixes/render-target-validation-hardening.md](../fixes/render-target-validation-hardening.md).
2. **Hands / weapons** — the known-good path is the dynamic Lua profile plus stale-component protection. The separate [local-avatar/native-bone investigation](../fixes/local-avatar-native-bone-driver.md) is parked and was not the source of the known-good profile's 6DoF.
3. **Controller input** — [/fixes/openvr-analog-trigger-fallback.md](../fixes/openvr-analog-trigger-fallback.md)
   and, on the later OpenXR path, [/fixes/psvr2-triangle-dpad.md](../fixes/psvr2-triangle-dpad.md).
4. **Game-specific Lua layer** — `%APPDATA%/UnrealVRMod/Avowed-Win64-Shipping/scripts/Avowed6dof.lua`
   (grew 2404 → 4622 lines: weapon attach/loadout recovery, slot property-scan
   fallback, aim-hand lock, debounce/cooldown, perf counters).

OpenXR performance additionally depends on [/fixes/openxr-authoritative-wait-frame.md](../fixes/openxr-authoritative-wait-frame.md).

# Known-good profile (OpenXR/PSVR2 checkpoint, 2026-04-23)

`config.txt`:
```text
Frontend_RequestedRuntime=openxr_loader.dll
VR_ControllersAllowed=true
VR_DPadShifting=true
VR_DPadShiftingMethod=1
VR_DelayNativeStereoFix=false
VR_NativeStereoFix=true
VR_NativeStereoFixSamePass=true
VR_SnapTurn=false
```

`data/avowed.txt` keys: `remap_lb_button=0`, `USE_OLD_OBJ_HOOK_METHOD=1`,
`DIRECT_WEAPON_DRIVE=0`, `NATIVE_BONE_DRIVER_OWNS_WEAPONS=0`, `USE_ATTACHED_WIDGETS=0`.

The exact known-good AppData files are pinned by sha256 — see
[/playbooks/checkpoint-and-recovery.md](../playbooks/checkpoint-and-recovery.md).
**Recovery rule:** restore the hashed known-good `Avowed6dof.lua`
(`f2b7df6d…`), *not* `Avowed6dof.lua.copy` — they are not equivalent.

# Earlier known-good variant (OpenVR era)

`Frontend_RequestedRuntime=openvr_api.dll`, `UObjectHook_EnabledAtStartup=true`,
`VR_AimMethod=2`, `VR_AsynchronousScan=false`, `VR_DelayNativeStereoFix=true`,
`VR_NativeStereoFix=true`, `VR_NativeStereoFixSamePass=true`,
`VR_NativeStereoFix_SkipFrameDecrement=true`, `VR_RecreateTexturesOnReset=false`.

# Conservative-by-default settings that matter

- The native bone driver exists only as uncommitted code in a separate local
  standard-backend tree; it is absent from the maintained AFW backend and has
  no retained successful-apply evidence. The `_mine` profile does not enable
  it and keeps `NATIVE_BONE_DRIVER_OWNS_WEAPONS=0`.
- Trusted ownership remains `USE_OLD_OBJ_HOOK_METHOD=1`,
  `DIRECT_WEAPON_DRIVE=0`, with the dynamic Lua layer handling replacement and
  loadout recovery.
- `VR_AsynchronousScan=false` and `VR_RecreateTexturesOnReset=false` were part
  of the stability recipe on the OpenVR profile.

# 6DoF architecture and reusable lessons

Avowed is not a static "attach the acknowledged pawn's weapon" profile. Game
updates separated the visible local avatar from the simple
`AcknowledgedPawn` assumption, and inventory/crafting contexts can expose proxy
actors or replace weapon components. The known-good shape therefore combines:

1. a game-specific dynamic Lua layer that follows first-person weapon/loadout
   changes and clears stale references;
2. conservative fallback attachment (`NATIVE_BONE_DRIVER_OWNS_WEAPONS=0`);
3. an Avowed-scoped module-backed-vtable guard that removes stale attachment
   state before virtual dispatch.

A local-avatar resolver was explored in a separate uncommitted tree, but it is
not present in this AFW backend and must not be listed as a current dependency.
The present hand investigation first records raw controller translation and
`FirstPersonMesh`/child identity from property chains. Only evidence that
`AcknowledgedPawn` is the wrong gameplay actor would justify extracting a
small resolver.

This is the context/lifetime branch of the
[stable 6DoF profile playbook](../playbooks/basic-6dof-setup.md). It contrasts
with TOW2's late-component enrollment issue: Avowed can retain a destroyed or
proxy component, while TOW2 had the right live component but
`UObjectHook.exists(component)=false`.

# AFW variant

An AFW/frame-warp-capable port of this working state exists on the PureDark
branch — see [/projects/puredark-afw-integration.md](../projects/puredark-afw-integration.md).

Alpha.2 includes a sanitized `profiles/Avowed-Win64-Shipping/` snapshot with the
current AFW/OpenXR config, hardened `Avowed6dof.lua`, active Lua dependencies,
data and controller bindings. Install it into a clean profile directory rather
than merging old scripts. Its tested `data/avowed.txt` retains optional
`CHEAT_*` regeneration settings, which users should review before playing.

# Crafting-table attachment crash

A beta.4 AFW regression test crashed on GameThread while
`UObjectHook::tick_attachments()` called `get_world_location()` on an attached
scene component whose vtable had already become a heap pointer. The dump proves
this was stale attachment lifetime/virtual dispatch, not PDAFW, OpenXR, GPU, or
Lua performance. An Avowed-scoped module-backed-vtable guard now detaches stale
state before ProcessEvent and revalidates deferred restores — see
[/fixes/avowed-stale-attachment-guard.md](../fixes/avowed-stale-attachment-guard.md).

One immediate crafting retest completed without a crash, but did not emit the
protective stale-detach telemetry; repeat crafting/loadout transitions remain
part of validation.

# 2026-07-31: startup discovery race — fixed by SDK discovery cache

With zero on-disk changes (game exe, driver, SteamVR, profile all unchanged),
Avowed abruptly went from launching reliably to failing every launch with
three different crash/hang signatures, all inside UEVR's per-launch offset
discovery. Root cause was a latent startup race in baseline discovery (the
scanner disassembles vtable entries UEVR has already hooked), not any
regression. Fixed by porting the minimal two-commit SDK discovery cache onto
the pinned baseline UESDK; the profile's existing `cache/sdk_discovery.json`
now short-circuits the fragile scans. Full analysis, the rejected third cache
commit (per-frame rescan performance regression), and the shrunken-menu
`no_tag` ImGui fix: [SDK discovery cache port](../fixes/sdk-discovery-cache-port.md).
Validated backend: `54c5f9d3…`, performance confirmed back to normal.

# 2026-08-04: hands investigation reset

The collaborator camera/controller/generated-poseable-hand route was rejected:
UEVR camera ownership corrected world height, but hands remained rotation-only
and disappeared after loading; an AFW-start candidate also produced a genuine
GPU page fault/device hang. Avowed must remain Native/2D-safe during this work.

An isolated `_mine` copy then ran with `VR_RenderingMethod=0`,
`VR_2DScreenMode=true`, native ownership off, and a read-only low-frequency
probe. The image was black except for the UEVR overlay, so that profile is not a
usable visual startup baseline. It did not crash, and it conclusively answered
the lower gates:

- both controller poses contained independent positional translation relative
  to the HMD; rotation-only appearance is downstream of pose publication;
- API local pawn and `AcknowledgedPawn` were the same gameplay actor;
- `FirstPersonMesh` resolved `FirstPersonSkelMesh`, while `Mesh` resolved
  `CharacterMesh0`;
- `UObjectHook.exists` was false for both meshes and the pawn/controller;
- three generated poseable children were tracked, but no ordinary existing
  left/right hand scene components or motion-controller states were found.

The probe is now parked. A follow-up changed only the inherited 2D game-screen
geometry from `UI_Distance=10`/`UI_Size=6.5` to the previously visible safe
`2`/`2` values; it remained black except for the UEVR overlay. The diagnostic
branch was stopped at the user's request and the active profile was restored as
an exact copy of the untouched `_mine` anchor. No hand behavior was layered
onto either black visual baseline.

# Remaining known issues

- Persistent existing-avatar hands are not yet proven on the current game/runtime.
- Perf hit during 2H-weapon / loadout-recovery windows.
- Occasional temporary right-weapon *misclassification* (not attachment loss)
  after some loadout/spell events.
- Confirm the stale-attachment guard across repeated crafting, weapon
  replacement and loadout transitions.

# Relationships

- Playbook: [Stable 6DoF profile creation](../playbooks/basic-6dof-setup.md)
- Parked research: [Local-avatar resolver and native bone-driver experiment](../fixes/local-avatar-native-bone-driver.md)
- Lifetime fix: [Avowed stale attachment guard](../fixes/avowed-stale-attachment-guard.md)
- Project: [PureDark AFW integration](../projects/puredark-afw-integration.md)

# Citations

- `AVOWED_UEVR_UPDATE.md`, `AVOWED_UEVR_UPDATE_SHORT.md` (full ranked change list)
- `AVOWED_CHECKPOINT_STATE.md` (hashes + checkpoint commits)
- `AVOWED_DEBUG_LOG.md`, `AVOWED6DOF_FULL_SWEEP.md` (iteration history, Lua review)
