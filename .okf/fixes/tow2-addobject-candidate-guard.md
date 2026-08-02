---
type: fix
title: TOW2 dynamic AddObject candidate validation
description: TOW2 changes the UObject pointer argument layout between AddObject calls; validate every candidate and its class hierarchy against FUObjectArray before calling add_new_object, otherwise skip the call.
tags:
- tow2
- uobject
- uesdk
- crash
- validation
timestamp: '2026-08-02T18:17:40+09:00'
resource: src/mods/UObjectHook.cpp
---

# Symptom

TOW2 reached the title prompt after the view-extension analyzer fix, then the
render path stopped. A running-process minidump showed the **GameThread** inside
`UObjectHook::add_new_object()` while traversing
`sdk::UStruct::get_super_struct()`.

The original AddObject hook made a one-time RCX/RDX layout choice. TOW2 changes
that argument layout between calls, so the chosen register later contained an
executable-data pointer rather than a `UObject`. Treating it as a reflected
object poisoned class-hierarchy traversal.

# Fix

Executable-scoped to TOW2 in `src/mods/UObjectHook.cpp`:

1. Re-evaluate RCX, RDX, R8, R9 and the captured stack slots **on every call**.
2. Accept a candidate only when its UObject index resolves back to the same
   pointer in `FUObjectArray`.
3. Validate the candidate's class and each traversed superclass against the
   same array before dereferencing further.
4. If no candidate passes, call the original function but skip
   `add_new_object()` processing.
5. Keep Avowed's existing guarded behavior separate; do not enable broad
   incremental `FUObjectArray` scanning as a fallback.

The warning below is expected and means the guard rejected an unsafe call:

```text
[UObjectHook] Skipping AddObject call with no validated FUObjectArray-backed candidate
```

# Evidence

- In-process dump: `tow2_press_any_key_hang_20260719_182836.dmp`
- SHA-256: `7267d7df42e7909c21f834b4e232cc294c14c36225c85d92c13f6b267e6ab1da`
- Analysis bundle:
  `<evidence-root>/puredark-tow2-injection-only-20260719/press-any-key-hang-analysis`
- After the per-call guard, TOW2 reached gameplay and sustained AFW while
  continuing to skip unsafe AddObject calls.

# Principle

This is the UObject equivalent of
[render-target discovery hardening](render-target-validation-hardening.md):
validate identity and hierarchy against an authoritative owner before use;
skipping incomplete hook bookkeeping is safer than admitting an unverified
pointer.

A later 6DoF investigation exposed the safe tradeoff: rejecting ambiguous
AddObject calls can miss a valid late-created Steam weapon. Do not weaken this
guard. Use the separate
[explicit component-enrollment path](tow2-explicit-component-enrollment.md),
which has caller intent and exact component identity.

# Related

- [TOW2 view-extension analyzer timing](tow2-view-extension-analyzer-threshold.md)
- [TOW2 explicit component enrollment](tow2-explicit-component-enrollment.md)
- [Stable 6DoF profile creation](../playbooks/basic-6dof-setup.md)
- [In-process hang dumps](../playbooks/in-process-hang-dump.md)
- [The Outer Worlds 2 state](../games/outer-worlds-2.md)

# Citations

- `<private-worklog-root>/PUREDARK_AFW_TOW2_WORKLOG.md`
- `<evidence-root>/puredark-tow2-injection-only-20260719/press-any-key-hang-analysis/cdb_all_threads.txt`
