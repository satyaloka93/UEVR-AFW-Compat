---
type: fix
title: Lua ScriptContext shutdown logging lifetime guard
description: Avoid an exit-time backend access violation by not routing the ScriptContext destructor message through a UEVR plugin API function table that may already have been destroyed.
resource: lua-api/lib/src/ScriptContext.cpp
tags:
- lua
- shutdown
- lifetime
- access-violation
- shf
timestamp: '2026-08-12T14:03:40+09:00'
---

# Symptom

A successful SHf gameplay session reached normal OpenXR focus loss and NVIDIA
shutdown, then UEVR recorded `EXCEPTION_ACCESS_VIOLATION` reading
`0xffffffffffffffff` while the process was exiting. The generated dump placed
the fault at backend RVA `0x4cc187`.

The matching PDB resolves that address to:

```text
uevr::ScriptContext::log(...)
lua-api/lib/src/ScriptContext.cpp:94
```

The call came from `ScriptContext::~ScriptContext()`, whose diagnostic message
used `API::get()->log_info(...)`. During static/process teardown, the plugin API
object can outlive the backend-owned function table it references. The game had
already left focus and SteamVR logged `NvAPI: Skipping NvAPI_Unload`; this was an
exit-lifetime bug, not the SHf SceneView constructor race.

# Correction

Remove only the destructor's call to `ScriptContext::log()`. Normal runtime Lua
logging remains unchanged. The destructor performs no essential cleanup through
that message, and its own existing comments already avoid callback removal
because shutdown ordering is unsafe.

This is narrower than adding exception handling around all Lua logging: a stale
function pointer is not made safe by a null check, and runtime logging should
continue to report real script failures.

# Validation

- The dump and matching PDB provide exact symbol/line attribution.
- The correction compiles as part of the backend/Lua library.
- A clean-exit runtime retest is still recommended after installing the release
  build, but the invalid call is removed rather than masked.

# Relationships

- Current profile/runtime state: [Silent Hill f](../games/silent-hill-f.md)
- Independent startup race: [SHf SceneView fail-closed guard](shf-sceneviewfamily-fail-closed.md)
