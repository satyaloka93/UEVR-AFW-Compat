# Silent Hill f profile notes

Executable: `SHf-Win64-Shipping.exe`

## Maintained working profile

This directory is the sanitized repository snapshot of the currently working
rebuilt first-person/6DoF profile. It supersedes the old alpha.2 package in this
source tree.

The working profile deliberately removes top-level `main.lua` and `hands.lua`
and loads:

- `00_settle.lua` — pawn-stability gate across startup/loading;
- `91_button_swap.lua` — standalone dodge-button mapping;
- `92_core_init.lua` — pawn, attachment, input and IK initialization plus the
  weapon-grip callback that provides the closing-hand 6DoF pose;
- `shf.lua`, `melee.lua` and `examine.lua`.

The package also retains its active data/Lua libraries, UObjectHook camera and
hidden-component state, the helper DLL, and the disabled calibrated native
weapon-attachment rollback. Runtime logs, SDK cache, ImGui state, generated
callback state, backups, parked scripts and editor metadata are excluded.

Base profile lineage: [letmein-vr/SHF_UEVR](https://github.com/letmein-vr/SHF_UEVR/).
The settle/minimal-core rebuild and compatibility hardening are documented in
the repository OKF.

## Required working state

The retained checkpoint starts directly in Previous Frame AFW and does not use
Native Stereo Fix:

```ini
FrameworkConfig_LogLevel=3
Frontend_RequestedRuntime=openxr_loader.dll
VR_RenderingMethod=3
VR_NativeStereoFix=false
VR_NativeStereoFixSamePass=false
VR_GhostingFix=true
VR_GhostingFixBootstrapViewStates=true
VR_AFW_FramewarpMode=2
VR_AFW_FixMovingObjectBrightnessFlickering=false
```

Use a fresh process and inject early. Native Stereo Fix produces an incorrect
image in SHf's current profile. Restart the process rather than switching out
of and back into AFW.

## DLSS workaround

SHf can retain a stale DLSS quality fraction after the VR target changes. The
safe workaround is to change DLSS away from and back to Performance in SHf's own
graphics menu after VR has stabilized. No automatic DLSS reset helper is
included: direct CVar automation did not improve the issue, and reflected
settings automation froze the game.

NVIDIA Inspector profile L plus an Ultra Performance override improved measured
GPU time, but the exact one-third input fraction was not telemetry-confirmed.

## AFW limitation

The rebuilt profile's retained checkpoint uses cold-start Previous Frame AFW.
Do not switch AFW back to Native in-process; PDAFW has no teardown API and the
right eye can remain black. Restart the game to change rendering modes.

The physical-melee script is preserved because it is part of the working
profile, but its saved JSON calibration is not currently applied after removal
of `main.lua`; firearms remain untested. Do not add back the old monolithic
scripts to recover their configuration UI.

The existing `afw-beta4-compat-v0.1.0-alpha.1` binary does not contain the SHf
backend fix. Alpha.2 contains the initial SHf backend foundation. Alpha.4
publishes the current f37 plus SceneView fail-closed backend and this maintained
profile as an experimental prerelease; repeated SHf and broader cross-game
regression remain open.
