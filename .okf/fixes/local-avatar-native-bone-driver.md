---
type: investigation
title: Avowed local-avatar resolver and parked native bone-driver experiment
description: Separate reusable avatar/proxy/lifetime diagnostics from an uncommitted native hand-bone writer that has no retained successful-apply evidence and is not present in the maintained AFW backend.
tags:
- uobjecthook
- avatar
- bones
- avowed
- experimental
timestamp: '2026-08-04T07:54:00+09:00'
---

# Corrected status

This is **not an upstream or production fix**. The implementation was developed
as uncommitted local work in a separate standard-UEVR tree and compiled into one
local backend. It is absent from the maintained AFW backend. The known-good
Avowed `_mine` profile does not enable `UObjectHook_LocalAvatarNativeBoneDriver`
and retains:

```text
USE_OLD_OBJ_HOOK_METHOD=1
DIRECT_WEAPON_DRIVE=0
NATIVE_BONE_DRIVER_OWNS_WEAPONS=0
```

No retained runtime log contains the success signature
`NativeBoneDriver applied(raw)`. Historical notes saying it "may" have driven
hands are not proof. Do not describe the native writer as stable, active or the
source of the known-good profile's 6DoF.

# Reusable part

The experiment contains useful diagnostic architecture:

- identify the actual gameplay avatar rather than trusting
  `AcknowledgedPawn` unconditionally;
- distinguish gameplay actors from inventory/menu preview proxies;
- track actor, mesh and component addresses across loading and loadout changes;
- validate UObject and scene-component pointers before virtual dispatch;
- clear stale caches and delay reacquisition after context changes.

These ideas can be implemented independently of direct bone writes. In
particular, an avatar resolver may eventually be extracted as a small,
reviewable feature after raw-pose and component identity evidence requires it.
It must not be assumed to exist in the AFW backend today.

# Parked experimental part

The uncommitted driver attempted:

- startup-only late animation-hook installation;
- transform-array layout validation;
- left/right hand-bone or socket resolution;
- raw component-space hand transform writes;
- runtime blocking when validation failed.

This route was parked because ordinary successful hand application was never
retained as proven evidence. Similar diagnostics in TOW2 found a transform
array but failed to resolve hand-bone indices. Native C++ weapon ownership was
also rejected separately; it must not be re-enabled as part of hand testing.

# Current Avowed evidence and decision path

Completed read-only gates:

- raw HMD/left/right poses contain independent positional translation;
- API local pawn and `AcknowledgedPawn` identify the same gameplay actor;
- `FirstPersonMesh` is `FirstPersonSkelMesh` and `Mesh` is `CharacterMesh0`;
- both meshes, the pawn and controller report `UObjectHook.exists=false`;
- bounded `FirstPersonMesh.AttachChildren` inspection found weapon utility rigs,
  bounds objects and three tracked generated poseable meshes, but no separate
  ordinary left/right hand scene components with existing attachment state.

The first `_mine`-derived Native/2D run was black except for the framework
although scene-capture creation completed. Its raw-pose evidence remains valid,
but it is rejected as a visual baseline. A second pass changed only 2D screen
distance/size to the previously visible safe values and remained black. The
probe and diagnostic branch are parked, and the active profile has been
restored byte-for-byte from the untouched `_mine` anchor.

If this investigation is ever resumed:

1. Confirm whether any stable property chain exposes separate existing
   arm/glove components; do not attach the entire first-person skeletal root to
   one controller.
2. If a suitable scene component exists, resolve its enrollment failure before
   changing hand behavior. State creation alone cannot bypass `exists=false`.
3. If hands exist only as bones in `FirstPersonSkelMesh`, reconsider only a
   **small, Avowed-only, default-off** bone/IK experiment. Do not port the former
   driver wholesale and do not give it weapon ownership.
4. Keep `_mine` weapon/loadout ownership and melee/block behavior unchanged
   until ordinary persistent hands survive loading, inventory, crafting,
   swaps and death/load.

This is one branch of the [stable 6DoF profile playbook](../playbooks/basic-6dof-setup.md),
not a prerequisite for TOW2, SHf or SH2.

# Relationships

- Game state: [Avowed](../games/avowed.md)
- Lifetime protection: [Avowed stale attachment guard](avowed-stale-attachment-guard.md)
- Diagnostic order: [Stable 6DoF profiles](../playbooks/basic-6dof-setup.md)

# Citations

- `AVOWED_UEVR_UPDATE.md` — implementation history and conservative/default-off status
- `AVOWED_CHECKPOINT_STATE.md` — known-good dynamic Lua ownership settings
- `AGENT_PROJECT_MESSAGES.md` — failed/parked native diagnostics and later TOW2 bone-resolution failure
