---
type: fix
title: TOW2 explicit dynamic-component enrollment for UObjectHook 6DoF
description: Enroll only an explicitly requested, fully validated TOW2 scene component when guarded AddObject discovery missed a late-created weapon mesh, allowing tick_attachments to drive Steam weapon translation without weakening global UObject discovery.
resource: src/mods/UObjectHook.cpp
tags:
- tow2
- 6dof
- uobjecthook
- motion-controllers
- object-lifetime
- profile
timestamp: '2026-08-02T19:12:00+09:00'
---

# Symptom

The TOW2 Steam profile could resolve and attach the correct first-person weapon
mesh, and raw right-controller position changed normally, but the weapon still
behaved like 3DoF. The decisive runtime state was:

```text
controllers_active=true
hook_disabled=false
UObjectHook.exists(component)=false
```

`get_or_add_motion_controller_state(component)` could return an attachment
state even though the component was absent from UObjectHook's tracked-object
set. `tick_attachments()` deliberately skips such a component, so a valid state
object or saved JSON file is not proof that the transform will ever be applied.

The dynamic Steam weapon mesh is created after UObjectHook's initial snapshot.
TOW2's separately required [guarded AddObject discovery](tow2-addobject-candidate-guard.md)
rejects ambiguous AddObject arguments to prevent title hangs, and therefore can
miss this late component.

# Rejected profile-only explanations

These tests did not repair translation because none addressed enrollment:

- detaching the mesh from its parent made swapped weapons disappear;
- changing `permanent=true` to non-permanent caused temporary detachment and
  firing restored the visual, but did not establish reliable 6DoF;
- increasing the controller inactivity timer could not help while
  `exists(component)` remained false;
- aim method and pawn-control-rotation settings were not the attachment gate.

Keep the mesh parented and use `permanent=true` for this profile.

# Narrow backend fix

`get_or_add_motion_controller_state()` now calls
`ensure_tow2_motion_controller_component_tracked(component)` before looking up
or creating state. The helper does nothing unless all of these conditions hold:

1. the executable is exactly the TOW2 shipping executable;
2. a caller explicitly requested state for this scene component;
3. execution is on the game thread;
4. the component has a module-backed UObject vtable;
5. its internal index resolves to the exact same pointer in `FUObjectArray`;
6. every class/superclass object passes the same checks and reaches a null root
   within a bounded depth;
7. exact array membership still holds immediately before `add_new_object()`.

Only then is the existing enrollment path called. The backend source is
committed as `217162d7`. Successful runtime runs emit:

```text
[TOW2] Enrolled explicit motion-controller component missed by guarded AddObject: ...
```

Afterward the same dynamic weapon reports `exists=true`, and controller-driven
translation works, including newly swapped weapon instances.

# Why this scope matters

Do **not** loosen AddObject scanning globally. TOW2 varies the AddObject argument
layout between calls; accepting a speculative register value previously sent
the game thread through a fake class hierarchy. Explicit enrollment is safer
because intent and pointer identity are both known. Other executables and the
guarded AddObject path remain unchanged.

# Profile half of the fix

The backend only makes the object eligible. The TOW2 profile still must:

```text
UObjectHook_EnabledAtStartup=true
VR_MotionControlsInactivityTimer=9999.000000
```

A minimal Lua script follows the pawn-owned `FPVMesh.AttachChildren` chain at a
low polling rate, selects the intended `SkeletalMeshComponent`, and calls:

```lua
local state = UEVR_UObjectHook.get_or_add_motion_controller_state(mesh)
state:set_hand(1)
state:set_location_offset(Vector3f.new(x, y, z))
state:set_rotation_offset(Vector3f.new(pitch, yaw, roll))
state:set_permanent(true)
```

It reacts only when the component address changes, performs no global object
scan, and preserves the original parent. The high inactivity timer is a
separate lifetime requirement: UEVR refreshes controller activity from actions,
not physical pose movement. With the default 30 seconds, tracking stopped at
that boundary and a trigger click restored it. That timer test became meaningful
only after explicit enrollment fixed `exists(component)`.

# Validation and publication status

Runtime validation has established explicit enrollment, `exists=true`, working
weapon translation, swapped-weapon enrollment, and survival beyond the former
30-second inactivity boundary. Source commit `217162d7` contains the backend
fix; profile commit `69d3a7b0` contains the independently installable TOW2 6DoF
overlay under `profiles/TheOuterWorlds2-Win64-Shipping-6DoF-overlay/`.

The alpha.3 release backend predates the enrollment fix and cannot be paired
with the overlay. Do not silently replace alpha.3's established 3DoF asset or
advertise this as an alpha.3-compatible profile. Package it for the next
prerelease with a backend built from `217162d7` or later. Longer TOW2 sessions
and Avowed/SHf cross-game backend regression remain required before publishing
that binary release.

Use the [basic 6DoF playbook](../playbooks/basic-6dof-setup.md) to reproduce the
gate diagnosis, and the [TOW2 game state](../games/outer-worlds-2.md) for the
renderer/AFW constraints that remain independent of attachment.
