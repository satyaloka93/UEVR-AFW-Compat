# Silent Hill f profile notes

Executable: `SHf-Win64-Shipping.exe`

## Required startup state

This profile intentionally starts in Native Stereo:

```ini
FrameworkConfig_LogLevel=3
Frontend_RequestedRuntime=openxr_loader.dll
VR_RenderingMethod=0
VR_NativeStereoFix=true
VR_NativeStereoFixSamePass=true
VR_GhostingFix=false
VR_GhostingFixBootstrapViewStates=false
VR_AFW_FramewarpMode=2
VR_AFW_FixMovingObjectBrightnessFlickering=false
```

Use a fresh process and inject early. Wait for both eyes and the final VR target
to stabilize before opening graphics settings.

## DLSS workaround

SHf can retain a stale DLSS quality fraction after the VR target changes. The
safe workaround is to change DLSS away from and back to Performance in SHf's own
graphics menu after VR has stabilized. No automatic DLSS reset helper is
included: direct CVar automation did not improve the issue, and reflected
settings automation froze the game.

NVIDIA Inspector profile L plus an Ultra Performance override improved measured
GPU time, but the exact one-third input fraction was not telemetry-confirmed.

## AFW limitation

AFW is optional and manual only. Native to Previous Frame AFW works after
entering gameplay, but hand/weapon distortion remains and the application is
usually CPU-limited near 40–41 FPS. Do not switch AFW back to Native in-process;
PDAFW has no teardown API and the right eye can remain black. Restart the game
to leave AFW.

The existing `afw-beta4-compat-v0.1.0-alpha.1` binary does not contain the SHf
backend fix. Use alpha.2 or a later build from source commit `cc0c43f9`.
