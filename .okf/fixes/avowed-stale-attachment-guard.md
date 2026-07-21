---
type: fix
title: Avowed stale motion-controller attachment guard
description: Crafting can replace an attached scene component before UObjectHook removes it; validate that its vtable and first virtual function remain module-backed before ProcessEvent, detach stale state, and revalidate deferred restores.
tags:
- avowed
- uobject
- motion-controller
- attachment
- crafting
- crash
timestamp: '2026-07-20T19:41:50+09:00'
resource: src/mods/UObjectHook.cpp
---

# Proven failure

Avowed crashed at a crafting table with `c0000005` in UEVRBackend on GameThread.
The matching PDB resolved the stack to:

```text
sdk::UObjectBase::process_event
sdk::USceneComponent::get_world_location
UObjectHook::tick_attachments
UObjectHook::on_pre_calculate_stereo_view_offset
FFakeStereoRenderingHook::calculate_stereo_view_offset
```

`UObjectHook::exists()` still found the attached component in its membership
set, but the component's first pointer was `0x1fb16889070`, a heap address rather
than a module-backed UObject vtable. `process_event` attempted virtual index 77
through that stale table and faulted. This is consistent with crafting replacing
or destroying an equipped-item component before destructor bookkeeping removes
its attachment state.

This was not a PDAFW evaluation, OpenXR submission, GPU, or Lua-performance
failure.

# Guard

Avowed-scoped in `UObjectHook::tick_attachments`:

1. Before attachment virtual dispatch, require both the UObject vtable and its
   first virtual function to belong to loaded modules.
2. If validation fails, remove the matching component/state pair from
   `m_motion_controller_attached_components` and skip all ProcessEvent calls.
3. Apply the same validation in the deferred transform-restoration callback.
4. Do not alter TOW2 or other games; the current Lua attachment/reprobe path may
   attach the replacement component normally.

Expected protective telemetry when the stale window occurs:

```text
[Avowed] Detaching stale motion-controller component before virtual dispatch
```

# Validation status

The immediate crafting-table retest completed without a crash and without Lua
performance spikes. That run did **not** emit the stale-detach line, so it proves
the candidate build did not regress the tested workflow but does not prove the
same stale window occurred. Repeat crafting and loadout transitions remain
required before calling the race eliminated.

Candidate artifacts:

- Backend SHA-256: `f36a03d520a4e7ff2242fc90826277bda5d1e530fd2803605f46293823761c54`
- PDB SHA-256: `580fed6818b1927e3d23e74e4b9a3606ff9b4399c9e3fd2c904f1da49abc340f`
- Crash evidence:
  `<evidence-root>/avowed-crafting-crash-beta4-20260720`
- Candidate/retest evidence:
  `<evidence-root>/avowed-crafting-stale-attachment-guard-candidate-20260720`

# Related

- [Avowed working state](/games/avowed.md)
- [TOW2 AddObject validation](/fixes/tow2-addobject-candidate-guard.md)
- [Windows minidump analysis](/playbooks/crash-dump-analysis.md)

# Citations

- `avowed-crafting-crash-beta4-20260720/cdb_analysis.txt`
- `avowed-crafting-crash-beta4-20260720/profile/crash.dmp`
- `avowed-crafting-stale-attachment-guard-candidate-20260720/profile/log.crafting_retest_no_crash.txt`
