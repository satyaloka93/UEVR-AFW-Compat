---
type: playbook
title: Stable 6DoF profiles — camera, dynamic weapons, enrollment, IK and lifetime safety
description: A native-first method for building UEVR 6DoF profiles, diagnosing every attachment gate, escalating safely when late-created components miss UObjectHook enrollment, and choosing among static attachment, dynamic Lua and IK using TOW2, Avowed, Silent Hill 2 and Silent Hill f.
tags:
- 6dof
- uobjecthook
- motion-controllers
- lua
- profile
- process
timestamp: '2026-08-02T19:12:00+09:00'
---

# Scope

Getting a game into first person with a camera and weapon/hand tracked by motion
controllers, then keeping that attachment valid across late object creation,
weapon swaps, inventory proxies, level loads and controller inactivity.

This playbook covers the decision boundary between native saved UObjectHook
state, small dynamic Lua attachment, a narrowly scoped backend enrollment fix,
and a full IK/plugin profile. Gesture thresholds and custom damage remain
separate layers; do not add them until ordinary 6DoF attachment is stable.

# Order of attack: native before Lua

**Always try UObjectHook's UI first.** It is native C++, persists as profile
state, needs no scripting, and cannot participate in Lua object-lifetime bugs.
Reach for Lua only when the native path provably cannot express what you need.
A backend change is the last step, after proving that pose, identity, state and
profile settings are correct but UObjectHook still does not track the object.

## 1. Camera (turns third person into first person)

In-game menu → enable advanced options → UObjectHook → Common Objects →
Acknowledged Pawn → **Attach Camera to** → **Save State**. Use *Attach Camera to
(Relative)* instead if the game is already first-person and you only want to
remove unwanted camera movement.

This writes `<profile>/uobjecthook/camera_state.json` (a `path` plus an
`offset`; the offset is head height above the pawn root, e.g. `z ≈ 68`).

## 2. Weapon / hand components

UObjectHook → Acknowledged Pawn → Components → select the mesh → **Attach
right**/**Attach left** → **Adjust** (menu closes; physically line the object up
with your controller) → reopen → **Permanent Change** if needed → **Save
State**. Then set Input → Aim Method to the matching controller.

**Permanent Change** stops UEVR resetting the component's transform after
rendering. It is what makes projectiles leave the right place and melee
colliders work — and the docs warn it can launch you out of the map if applied
to something that drives player position. Enable it deliberately, on the weapon
mesh, not on anything the pawn's location depends on.

## 3. Required for native-attachment profiles

`UObjectHook_EnabledAtStartup=true`. UObjectHook is **off** by default, so a
profile based on saved native state or the motion-controller-state API silently
does nothing without this. This is the single most common "the profile doesn't
work" cause. A self-contained plugin/IK profile may intentionally leave it off;
record that architecture rather than copying the setting blindly.

## 4. Prove every attachment gate

Do not diagnose "3DoF" from appearance alone. Check these gates in order and
change only the first failing one:

1. **Raw pose:** `vr.get_pose(right_index, position, rotation)` changes in
   translation as the controller moves.
2. **Target identity:** the component reached through the pawn/property chain
   is the same UObject that renders the visible weapon. Compare addresses and
   full names across all candidate paths.
3. **State:** `get_or_add_motion_controller_state(component)` returns a state
   and receives hand/offset/permanence values.
4. **Enrollment:** `UObjectHook.exists(component)` is true. A state can exist
   while this is false.
5. **Tick eligibility:** UObjectHook is enabled, controllers are active, and
   the component still exists when `tick_attachments()` runs.
6. **Lifetime:** weapon swaps, reloads and level transitions replace addresses;
   the profile detects that change and discards old references.

A diagnostic should report these booleans and addresses at low frequency. Park
it after the question is answered; warning-level logs and per-frame string
building can distort the result.

# When you need Lua instead

Native attachment is static: one component, one saved offset. Use Lua when

- the attached object **changes at runtime** (weapon swaps), or
- you need a **grip pose** so the hand closes around the weapon.

The second one is decisive and easy to get wrong. Attaching a weapon mesh
directly to a motion controller works and is simple, but the hand stays open
around it. A closing hand requires attaching the weapon to a **hand bone socket
on an IK body mesh** and driving a per-weapon pose, which is a rig-level feature,
not a UObjectHook one.

## The motion-controller API

```lua
local state = UEVR_UObjectHook.get_or_add_motion_controller_state(component)
state:set_hand(1)                              -- 0 = left, 1 = right, 2 = HMD
state:set_location_offset(Vector3f.new(x,y,z))
state:set_rotation_offset(Vector3f.new(p,y,r)) -- Vector3f => euler degrees
state:set_permanent(false)
```

Offsets **must** be real usertypes. Passing a plain Lua table raises
`Invalid type for set_location_offset`, and if the call is inside a `pcall` it
fails silently forever.

**Check the binding name against the source, not the docs.** The published UEVR
docs list `set_permanant`; this backend binds `set_permanent` (correct
spelling). Verify with `grep -rn set_perman lua-api/lib/src/ScriptContext.cpp`
before writing scripts against it — forks differ, and a wrong name inside a
`pcall` fails silently.

## A returned state is not proof of enrollment

`get_or_add_motion_controller_state(component)` maintains the attachment-state
map. `tick_attachments()` separately checks whether the component belongs to
UObjectHook's tracked-object set. Therefore this is possible:

```text
state returned = true
UObjectHook.exists(component) = false
visible translation = false
```

This was the TOW2 Steam failure. Its dynamic first-person weapon appeared after
the initial UObject snapshot and was missed by the deliberately guarded
AddObject path. Detachment, non-permanent state, aim settings and a larger
inactivity timeout could not solve an enrollment failure.

The correction in backend commit `217162d7` is intentionally narrow: when
TOW2 explicitly requests a motion-controller state, validate game-thread ownership, module-backed vtables,
exact `FUObjectArray` index-to-pointer identity and a bounded complete class
hierarchy before calling the existing enrollment path. Do not broaden
speculative AddObject scanning globally. See
[TOW2 explicit component enrollment](../fixes/tow2-explicit-component-enrollment.md).
The matching distributable profile half is commit `69d3a7b0` under
`profiles/TheOuterWorlds2-Win64-Shipping-6DoF-overlay/`.

## TOW2 profile pattern after enrollment

The backend only makes the component eligible; the profile remains responsible
for attachment:

- follow `pawn.FPVMesh.AttachChildren` rather than scanning all UObjects;
- poll slowly and act only when the mesh address changes;
- preserve the game's parent; do not call `DetachFromParent`;
- use `permanent=true` for this weapon mesh;
- enable UObjectHook at startup;
- use a long `VR_MotionControlsInactivityTimer` when physical-only motion must
  remain active. UEVR refreshes controller activity from actions, not pose
  movement, so a dropout exactly at the configured timeout that a button press
  immediately repairs is an inactivity gate, not lost tracking.

The successful TOW2 combination is both halves together: explicit enrollment
turns `exists=false` into `exists=true`; the minimal profile then applies the
right-hand state to each newly swapped component. The timer did not help before
enrollment and became necessary after enrollment worked.

# Controller-ray operation of the UEVR framework

A usable 6DoF profile must not make its configuration menu fight controller
aim. Under OpenXR, use a stage-space framework with mouse emulation:

```ini
UI_Framework_FollowView=false
UI_Framework_MouseEmulation=true
```

Profiles using game aim (`VR_AimMethod=0`) naturally produce the SHf
gold-standard behavior: the framework stays fixed while the controller ray
moves the mouse in both axes. A controller-aim profile can still rotate its
camera and therefore its stage transform while pointing. TOW2 solved this
without sacrificing gameplay aim by temporarily selecting game aim for the
whole framework session, then restoring and saving the previous aim method
after close. A Lua `set_aim_allowed(false)` call alone was too early;
OverlayComponent restored it later in the same frame.

Use the exact lifecycle, persistence safeguard and test matrix in
[Fixed UEVR framework menu with a controller-ray mouse](framework-menu-controller-pointer.md).
Do not move the menu transform from Lua, and do not permanently replace a
controller-aim profile's gameplay method with game aim.

# Lua sandbox constraints

- `io.open` rejects absolute paths and `..`; relative paths resolve under
  `<profile>/data/`.
- The `debug` library is **not** exposed.
- Lua 5.4 rejects `string.format` widths wider than two digits (`%-100s`).
- `print()` may not reach `log.txt` depending on the profile's log level;
  `uevr.params.functions.log_warn(...)` always does.
- `uevr.sdk.callbacks` is a **C++ usertype**. Assigning to its fields reports
  success but reads still return the original binding — you cannot wrap or
  intercept callback registration from Lua. Gate inside the callbacks instead.
- Script load order is NTFS upcased-name order: **digits sort before letters**,
  and `_` sorts *after* letters. A `00_` prefix loads first; a `_00_` prefix
  loads last.

# Stability rules (each one earned from a crash)

1. **Never walk the whole object array from tick context.**
   `get_first_object_by_class` / `get_objects_by_class` traverse
   GUObjectArray and will eventually read a torn entry during object churn,
   faulting on garbage. Follow property chains from the pawn instead
   (`pawn.CurrentWeapon.WeaponMesh`), which is O(1) and stays valid.
2. **Gate script work until the pawn is stable.** Crashes cluster within a
   second of pawn creation. Track the pawn's address, suppress tick work for a
   few seconds after it changes, and re-close the gate on every change so level
   loads, save loads and respawns are all covered.
3. **Clear cached references on pawn change — before the gate reopens.**
   A cached mesh or component from the previous level is the classic silent
   killer: it survives as a pointer, reads as garbage, and produces symptoms
   that look like input bugs (phantom attacks from wild velocity deltas) rather
   than crashes.
4. **`UObjectHook.exists()` is necessary but not sufficient.** False means the
   attachment tick will skip the object even if state exists. True confirms the
   UObject is tracked, but says nothing about internal pointers, and there is
   still a window between the check and the dereference.
5. **Throttle.** Attachment and visibility work belongs at a few Hz, driven by
   change detection, never every frame.
6. **`pcall` everything that touches a game object**, and make diagnostics
   incapable of breaking the game.
7. **Re-assert visibility after reloads.** Recreated weapons may not get the
   attachment system's init pass; a cheap idempotent
   `SetVisibility`/`SetHiddenInGame`/`SetRenderInMainPass` watchdog fixes
   invisible-weapon-after-reload.

# Choose the smallest architecture that fits

| Game | 6DoF problem | Smallest working pattern | Reusable lesson |
|---|---|---|---|
| [TOW2](../games/outer-worlds-2.md) | Late Steam weapon components missed guarded discovery | TOW2-only explicit backend enrollment plus a minimal parent-preserving Lua attachment | Separate state creation from object enrollment; never weaken global discovery to fix one late object |
| [Avowed](../games/avowed.md) | Visible avatar can differ from `AcknowledgedPawn`; inventory proxies and loadout churn replace targets | Local-avatar resolver, hardened dynamic Lua, stale-vtable guards; native bone ownership remains off | Resolve gameplay avatar context and treat crafting/loadout as destructive lifetime transitions |
| [Silent Hill 2](../games/silent-hill-2.md) | First-person camera, IK hands, two-hand interaction and melee are a coordinated profile feature | Full profile/plugin stack (`camera.lua`, `main.lua`, `melee.lua`, IK/attachments and game plugin) | Do not force every game through saved UObjectHook state; preserve a validated full profile atomically |
| [Silent Hill f](../games/silent-hill-f.md) | Dynamic grip pose and IK are required, but the inherited monolith crashed during object churn | Minimal standalone wiring, pawn settle gate, property-chain lookups and stale-reference clearing | Keep only the modules needed for gameplay; grip-pose requirements justify IK, not unsafe global scans |

These are alternatives, not a progression that every game must traverse. A
static weapon should stop at native UObjectHook. TOW2 needs a tiny dynamic
script and one scoped backend exception. Avowed needs a game-aware resolver.
Silent Hill 2 and Silent Hill f justify coordinated IK profiles because hand
pose and interaction are part of the requirement.

# Diagnosing crashes

UE catches the access violation itself, so **UEVR and Windows often produce no
dump at all**. The real evidence is in
`%LOCALAPPDATA%\<Game>\Saved\Crashes\<id>\` — `CrashContext.runtime-xml` gives
the error and seconds-since-start, `UEMinidump.dmp` gives the stacks.

A stack of
`engine_tick_hook → PluginLoader::on_pre_engine_tick → ScriptContext::on_pre_engine_tick`
faulting in game code means **a Lua callback called into the game and the game
faulted** — a script problem, not a backend one. Be aware that UE's captured
register context is frequently inconsistent with the faulting instruction, so
do not trust registers to identify the object.

To find *which* callback, instrument the dispatch hub rather than guessing, and
prefer a constant-size record (one line rewritten in place) over logging, which
floods the log and perturbs timing. Remove the instrumentation afterwards — even
disabled, string building per callback per tick is real overhead.

# Bisecting a crashing profile

Park whole top-level scripts and test. **Do not stub modules** via
`package.loaded[name] = <noop table>`: live code then receives `nil` where it
expects objects, which manufactures new failures and invalidates the
experiment. If a monolithic script hard-requires the crashy library set purely
to build config/dev panels, re-implement its gameplay wiring standalone rather
than trying to load it selectively — see
[SHf profile rebuild](../fixes/shf-profile-rebuild-main-lua-removal.md).

Finally: with random-timing crashes, one clean run proves nothing. Judge a
candidate over a session long enough to have failed before, and repeat the
specific transition (save → exit → relaunch → load) that caught it.

# Validation matrix

Before calling a 6DoF profile portable, test all applicable rows:

- fresh process and first equip;
- slow translation with no buttons beyond the inactivity timeout;
- multiple ranged and melee weapon swaps;
- reload, fire/attack animation and holster/unholster;
- inventory, crafting and any preview-pawn context;
- cutscene/conversation and 2D-screen transitions;
- death, save/load, level transition and respawn;
- fixed controller-ray UEVR menu operation in horizontal, vertical and diagonal directions, repeated open/close, restored gameplay aim, and preserved `VR_AimMethod`;
- every supported renderer/runtime mode, without changing modes mid-test;
- at least one maintained game that does **not** use the new backend path.

Record target address changes, enrollment success and only exceptional events.
Remove identity/pose/stage probes from the final profile.

# Related

- [TOW2 explicit dynamic-component enrollment](../fixes/tow2-explicit-component-enrollment.md)
- [Local-avatar resolution and native bone driver](../fixes/local-avatar-native-bone-driver.md)
- [Avowed stale attachment guard](../fixes/avowed-stale-attachment-guard.md)
- [SHf minimal profile rebuild](../fixes/shf-profile-rebuild-main-lua-removal.md)
- [Checkpoint and recovery](checkpoint-and-recovery.md)
- [Staged fix methodology](staged-fix-methodology.md)
