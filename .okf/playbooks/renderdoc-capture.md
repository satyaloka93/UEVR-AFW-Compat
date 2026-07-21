---
type: playbook
title: RenderDoc GPU frame capture via the UEVR-AFW-JOEY launcher
description: Capture real (including stereo VR) GPU frames from UE games using the embedded RenderDoc system in the UEVR-AFW-JOEY fork — launch suspended, inject RenderDoc first, trigger captures with a file.
tags:
- renderdoc
- gpu
- capture
- diagnostics
timestamp: '2026-07-20T00:00:00Z'
resource: RENDERDOC_CAPTURE_GUIDE.md
---

# When to use

Visual wrongness that logs can't explain: per-eye differences, ghosting,
wrong render targets, UI projection issues. Works flat or in VR (including the
Meta XR Simulator — no headset needed).

# Quick procedure

```powershell
.\UEVRRenderDocLauncher.exe --exe "C:\...\Game-Win64-Shipping.exe" --wait
.\Capture-RenderDoc.ps1          # writes trigger file, prints .rdc path
```

- The launcher starts the game **suspended**, injects `renderdoc.dll` first
  (clean captures require RenderDoc in before any graphics objects exist),
  then `UEVRBackend.dll`, then resumes.
- Trigger is just a file: `%TEMP%\uevr_renderdoc_capture.req` — scriptable from
  anything.
- Captures land in `%TEMP%\uevr_renderdoc\` by default.

# Gotchas learned

- Point `--exe` at the **real render exe** under `Binaries\Win64`
  (`<Game>-Win64-Shipping.exe` or `UnrealGame-Win64-Shipping.exe` for
  generic-target builds), never the root redirector stub.
- Game won't boot / ready-event timeout (common on modular / D3D12
  Agility-SDK titles): add `--defer-backend-ms 8000` to skip the early D3D12
  prehook.
- VR stays flat: copy `openxr_loader.dll` next to the game exe.
- Stereo + RenderDoc crashes need the `patches/UESDK-StereoStuff-renderdoc.patch`
  fix (UEVR stereo setup must understand RenderDoc-wrapped textures).
- Check `%APPDATA%\UnrealVRMod\<Game>\log.txt` for `RenderDoc`, `prehook`,
  `capture_safe=true` to see how far startup got.

# Citations

- `RENDERDOC_CAPTURE_GUIDE.md` (full guide)
