---
type: fix
title: UObject browser array entry validation
description: Dump-proven TOW2 name formatting crash from unvalidated object-array entries; guarded traversal candidate and interface stride correction.
timestamp: 2026-09-20
tags: [tow2, crash, uobject, diagnostics]
---

# Evidence

TOW2 PID 8288, September 20 09:46:40: GameThread access violation reading
0x1c. Matching deployed backend/PDB resolve the stack through
`sdk::FName::to_string`, `UObjectHook::ui_handle_array_property`, property/
object traversal, `draw_main`, and `engine_tick_hook`. No CVar function occurs
on this stack. The user was browsing UObject Hook objects. Do not confuse this
with the earlier 09:40 crash or old title-hang dump.

Evidence is archived locally under
`diagnostics/uobject-browser-20260920-094640`, including the game crash files,
UEVR log and matching pre-fix backend/PDB. Backend SHA256:
`1429d13e39e4f11b0cba3cbb21c84aad1d2e787603886985fd7a544eb3ad67c4`.

The object-array UI formatted `obj->get_class()->get_fname()` and the object's
name before calling the guarded object UI. Null/invalid entries could reach
engine name conversion. The dump identifies this path, not the precise array
property or a proven GC race.

# Correction candidate

`src/mods/UObjectHook.cpp` copies a non-owning array header with
ReadProcessMemory, checks signed count/capacity and pointer arithmetic, reads
each object pointer safely, and requires hook membership plus live FUObjectArray
index identity for both object and class before formatting names. The follow-up
below removes hook membership as a requirement for read-only browsing. This is not
a general guarantee against concurrent engine object destruction.

Interface arrays now stride two pointers rather than one, using only the object
pointer. Epic documents the distinct object/interface pointers in
[FScriptInterface](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/CoreUObject/FScriptInterface).
This source defect was found alongside the crash; the dump does not prove it
was the triggering array type.

Object/interface arrays use 128-entry pages and per-index UI IDs, bounding
name formatting per expanded array without losing access to later entries.
Checks run only in the browser, not normal gameplay. Struct arrays retain their
existing UI traversal with validated header and arithmetic.

# Validation and scope

Windows Release backend build passes. `scripts/test-uobject-array.cpp` passes
with GCC C++17 and warnings-as-errors: empty/invalid headers, bounds, overflow,
ordinary object stride, interface stride and null slots. These are layout tests,
not an engine-GC simulation. First user retest: no crash, but most objects appear
invalid. This is not a successful usability result.

The September 20 11:18 follow-up removes AddObject-hook membership as a browser
requirement: lack of enrollment does not establish invalid engine identity.
Object and class must still pass readable-header/index/live-slot identity
checks. Root object UI uses the same guard as array traversal. Rejection labels
now distinguish null, unreadable, out-of-range index and registry mismatch.
It does not change enrollment or 6DoF ownership. A chunk-size discovery fallback
and skipped AddObject candidates occur in this run; neither proves a wrong
chunk size or explains every rejected entry. Runtime retest remains necessary.
Follow-up deployment is recorded with [cross-game CVar controls](cvar-script-controls-shf.md).

Deployed from the authoritative AFW-PUBLISH tree at 10:46:19 to the shared
`C:\Users\gthom\Downloads\UEVR-AFW-JOEY` injector folder; DLL and PDB hashes
were checked after copying. Backend SHA256:
`b1bb222b9630e139bfdc427f2d8bb2fe47ff2d528037002448fe983288062f83`.
Both previous files remain recoverable with suffix
`.pre-native-openxr-20260920-104619.bak`.

The [validated CVar/image correction](tow2-validated-cvar-access.md) is preserved;
no Cheeky, AFW, profile or 6DoF changes are part of this fix. Performance benefit
in ordinary gameplay is not claimed.
