---
type: fix
title: Guarded AddObject object recovery (Avowed/TOW2)
description: The guarded AddObject hook skipped nearly all runtime objects on Avowed/TOW2, breaking Lua attachment profiles; recover them from FUObjectArray with slot-index liveness and a budgeted scan.
timestamp: 2026-10-30
tags: [avowed, tow2, uobjecthook, lua, attachments, performance, crash]
---

# Symptom

letmein-vr Avowed/TOW2 profiles (uevrlib: `attachments.lua`, `controllers.lua`,
hands) worked on letmein's fork but not here: weapons/attachments missing, hands
not found. Logs repeated `Skipping AddObject call with no validated
FUObjectArray-backed candidate`.

# Cause

Avowed/TOW2 change `UObjectBase::AddObject` argument layouts between callsites,
so `is_guarded_uobjecthook_game()` validates RET/RCX/RDX/R8/R9/stack1-4 and skips
calls with no candidate. It skipped almost everything: the recovery scan later
adopted ~475k (Avowed) / ~580k (TOW2) objects per session. uevrlib calls
`UEVR_UObjectHook.exists()`, `to_uobject`, `get_objects_by_class` and
`get_first_object_by_class`; every skipped object looked invalid. letmein's fork
falls back to FUObjectArray scanning (4096-8192/tick), which is also why it is
slower.

Avowed's destructor hook is also unreliable (`Found destructor ... but it's the
wrong one, falling back to branch start`), so tracked objects were not always
removed: `get_objects_by_class` then returned freed memory and Lua threw
`caught (...) exception` every tick.

# Fix (UEVR `d50573ae`, guarded games only)

- `try_adopt_untracked_object`: plugin `exists()` (also used by Lua `exists` /
  `to_uobject`) and `get_or_add_motion_controller_state` adopt a missed object on
  the game thread after FUObjectArray membership + class-hierarchy validation.
  Replaces the TOW2-only `ensure_tow2_motion_controller_component_tracked`.
- `refresh_untracked_objects_from_uobject_array` each pre-engine tick: 2048
  fresh (appended) slots, resumable so level-load bursts are not left to the
  sweep; 1024-slot background sweep for reused free-list slots; 0.4 ms adoption
  budget measured separately from collection.
- Slot-index liveness: `MetaObject::index` + `m_slot_owners`. An entry is dead
  once its slot no longer holds it (`is_tracked_object_stale_unsafe`, never
  dereferences the object); `exists_unsafe` and `get_objects_by_class` filter
  dead entries and the sweep prunes them.
- Cost reductions that mattered: skip the hash lookup when the slot still holds
  its recorded owner; check vtables against the cached exe range before
  `get_module_within` (its `GetModuleHandleEx` fallback takes the loader lock);
  stop the hierarchy walk at the first tracked class; resolve `full_name` lazily
  (UI only).
- SEH-guarded name read: an object adopted mid-construction during level
  streaming faulted in `FName::to_string`.
- Native Stereo null-deref VEH (`FFakeStereoRenderingHook.cpp`) now has a mutex
  and re-entry guard; its unsynchronized static set crashed under concurrent
  access violations (two WER dumps, fault at the handler itself).

# Evidence

Avowed 2026-10-30: attachments and hands work with the letmein profile. Scan log
line `adopted X/Y candidates (collect N us, adopt M us; ... fresh cursor A/B)`;
steady state 30-200 us/tick, startup backlog cleared in ~40-100 s. Before the
cost fixes, collection alone blew the budget (exactly 15 adoptions/tick), hands
took minutes and the game lost ~5 fps (45 -> 40), which with OFXR shallow
pipeline raised never-shown frames from 3% to 16%. Remaining Avowed limit was
GPU (Native Stereo, ~10.6 ms of 11.1 ms); lowering view distance/shadows gave
42 fps game / 84.6 presented / 4.2% never shown.

# Open

The real fix is identifying Avowed's correct AddObject argument and destructor;
the scan would then be unnecessary. Debug UI shows `Adopted (on demand / scan)`
and `Stale entries pruned` counters.
