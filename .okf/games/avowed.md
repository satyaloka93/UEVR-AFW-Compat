---
type: game-profile
title: Avowed — working VR state
description: Patched Avowed works in Native Stereo after vtable-scan widening, safe native-stereo activation, local-avatar work, and a heavily extended Avowed6dof.lua; two known-good profile variants (OpenVR-era and OpenXR/PSVR2 checkpoint).
tags:
- avowed
- native-stereo
- openvr
- openxr
- psvr2
timestamp: '2026-07-20T19:41:50+09:00'
---

# Status

Playable in 6DoF VR. The game's patch broke UEVR's original stereo discovery;
recovery took four layers, in order of importance:

1. **Stereo recovery** — [/fixes/stereo-vtable-scan-widening.md](../fixes/stereo-vtable-scan-widening.md)
   plus [/fixes/native-stereo-safe-activation.md](../fixes/native-stereo-safe-activation.md)
   and [/fixes/render-target-validation-hardening.md](../fixes/render-target-validation-hardening.md).
2. **Local avatar / hands / weapons** — [/fixes/local-avatar-native-bone-driver.md](../fixes/local-avatar-native-bone-driver.md).
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

- Native bone driver exists in C++ but the live profile keeps
  `UObjectHook_LocalAvatarNativeBoneDriver=false` and
  `NATIVE_BONE_DRIVER_OWNS_WEAPONS=0` — fallback attach behaviour is the
  trusted path until native ownership matures.
- `VR_AsynchronousScan=false` and `VR_RecreateTexturesOnReset=false` were part
  of the stability recipe on the OpenVR profile.

# AFW variant

An AFW/frame-warp-capable port of this working state exists on the PureDark
branch — see [/projects/puredark-afw-integration.md](../projects/puredark-afw-integration.md).

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

# Remaining known issues

- Perf hit during 2H-weapon / loadout-recovery windows.
- Occasional temporary right-weapon *misclassification* (not attachment loss)
  after some loadout/spell events.
- Confirm the stale-attachment guard across repeated crafting, weapon
  replacement and loadout transitions.

# Citations

- `AVOWED_UEVR_UPDATE.md`, `AVOWED_UEVR_UPDATE_SHORT.md` (full ranked change list)
- `AVOWED_CHECKPOINT_STATE.md` (hashes + checkpoint commits)
- `AVOWED_DEBUG_LOG.md`, `AVOWED6DOF_FULL_SWEEP.md` (iteration history, Lua review)
