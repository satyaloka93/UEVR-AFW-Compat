---
type: game-profile
title: Hogwarts Legacy — crash fix state
description: TaskGraph access-violation crashes traced to native-stereo re-enable churn after load/world transitions; fixed with a Hogwarts-only post-transition cooldown. Direct-pose fallback is rejected (breaks right-eye stereo).
tags:
- hogwarts-legacy
- native-stereo
- crash
- openvr
timestamp: '2026-07-20T00:00:00Z'
---

# Status

Stable with the Hogwarts-only transition cooldown patch —
[/fixes/hogwarts-transition-cooldown.md](/fixes/hogwarts-transition-cooldown.md).

# The crash pattern (worked example of the triage method)

- Symptom: `EXCEPTION_ACCESS_VIOLATION` on `TaskGraphThreadNP 5`, faulting in
  `HogwartsLegacy.exe` — *not* in `UEVRBackend.dll`.
- Log showed the real story: repeated `FFakeStereoRenderingHook` null-deref
  handler activity + `Previous instruction does not use the same register as
  the dereference` + late D3D12 rehook attempts.
- Dump symbolization ([/playbooks/crash-dump-analysis.md](/playbooks/crash-dump-analysis.md))
  put `safetyhook::MidHook::create` and `memcpy` (hook patching) on the stack —
  the framework was mid-memory-patch when the game crashed.
- Root behavioural cause: the game cycled
  `Load transition detected; enabling passthrough` →
  `Safe point reached after 60 stable frames; enabling native stereo fix`
  on every load/world transition, re-arming the fragile path each time.
- Suspect elimination: `wandpos.dll` plugin was cleared because a comparable
  crash occurred with it removed, and validation later passed with it restored.

# Validated config (during native-stereo testing)

```text
Frontend_RequestedRuntime=openvr_api.dll
VR_RenderingMethod=0
VR_NativeStereoFix=true
VR_DelayNativeStereoFix=true
VR_NativeStereoFixSamePass=false
VR_NativeStereoFix_SkipFrameDecrement=true
```

Note: the conservative config **alone** did not stop the crashes — the
source-level cooldown was required.

# Rejected: direct-pose-enqueue fallback

Disabling the RHI command vtable-hook path on instability and falling back to
direct pose enqueue reduced hook-poisoning risk but **broke in-world stereo,
especially the right eye**. Reverted. Hogwarts native stereo must keep its hook
path. Non-fatal `Command still hooked on a later frame (...) forcing immediate
reset` warnings persist and are tolerated.

# Citations

- `Hogwarts_Project.md` (full investigation, dump offsets, validation runs)
