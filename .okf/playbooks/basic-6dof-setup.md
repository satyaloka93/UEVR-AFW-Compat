---
type: playbook
title: Basic 6DoF — camera attach, hand/weapon attachment, and the rules that keep it stable
description: How to give a third-person UE game first-person 6DoF in UEVR, native-first via UObjectHook and only then via Lua; includes the motion-controller API, the Lua sandbox constraints, and the object-lifetime rules that separate a stable profile from one that crashes on every level load.
tags:
- 6dof
- uobjecthook
- motion-controllers
- lua
- profile
- process
timestamp: '2026-08-01T12:00:00+09:00'
---

# Scope

Getting a game into first person with the camera and a weapon/hand tracked by
the motion controllers. Not covered: gesture systems, melee swing detection,
IK body rigs — those sit on top of this.

# Order of attack: native before Lua

**Always try UObjectHook's UI first.** It is native C++, persists as profile
state, needs no scripting, and cannot participate in Lua object-lifetime bugs.
Reach for Lua only when the native path provably cannot express what you need.

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

## 3. Required for shared profiles

`UObjectHook_EnabledAtStartup=true`. UObjectHook is **off** by default, so a
profile you hand to someone else silently does nothing without this. This is
the single most common "the profile doesn't work" cause.

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
state:set_permanant(false)                     -- NOTE: misspelled in the binding
```

Offsets **must** be real usertypes. Passing a plain Lua table raises
`Invalid type for set_location_offset`, and if the call is inside a `pcall` it
fails silently forever. `set_permanant` is genuinely misspelled — spelling it
correctly is a no-op.

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
4. **`UObjectHook.exists()` is necessary but not sufficient.** It confirms the
   UObject is still tracked; it says nothing about internal pointers, and there
   is still a window between the check and the dereference.
5. **Throttle.** Attachment and visibility work belongs at a few Hz, driven by
   change detection, never every frame.
6. **`pcall` everything that touches a game object**, and make diagnostics
   incapable of breaking the game.
7. **Re-assert visibility after reloads.** Recreated weapons may not get the
   attachment system's init pass; a cheap idempotent
   `SetVisibility`/`SetHiddenInGame`/`SetRenderInMainPass` watchdog fixes
   invisible-weapon-after-reload.

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
