# AFW beta.4 compatibility v0.1.0-alpha.2

This experimental prerelease adds the runtime-tested Silent Hill f UE5.7/OpenXR
bootstrap source from commit `cc0c43f9` and portable Avowed/SHf profile assets.
It remains a prerelease, not a stable UEVR distribution.

## Game status

| Game | Status |
|---|---|
| Avowed | Working experimental AFW/6DoF integration. The profile bundle includes the hardened Lua layer; repeated crafting/loadout validation remains open. |
| The Outer Worlds 2 | Working experimental Native startup followed by beta.4 Previous Frame AFW. Title timing and repeated 2D/inventory transitions remain open. |
| Silent Hill f | Working experimental early injection and both-eye Native Stereo with bounded UI/scene resources. AFW is manual, distorts hands/weapons and is not recommended over Native. |
| Silent Hill 2 / other games | No compatibility claim unless explicitly recorded in OKF. |

## Silent Hill f rules

Install the included SHf profile cleanly, inject early into a fresh process and
start Native:

```text
FrameworkConfig_LogLevel=3
VR_RenderingMethod=0
VR_NativeStereoFix=true
VR_NativeStereoFixSamePass=true
VR_GhostingFix=false
VR_GhostingFixBootstrapViewStates=false
VR_AFW_FramewarpMode=2
VR_AFW_FixMovingObjectBrightnessFlickering=false
```

After VR stabilizes, change DLSS away from and back to Performance in SHf's own
graphics menu. The profile intentionally contains no automatic reset helper:
direct CVar automation did not improve the issue, while reflected settings
automation froze the game.

Native to Previous Frame AFW can be selected manually after gameplay begins,
but the additional saving is small after DLSS is corrected and hand/weapon
distortion remains. Restart the game to leave AFW; PDAFW has no teardown API and
AFW to Native can leave the right eye black.

## Included profiles

The separate profiles archive contains:

- `Avowed-Win64-Shipping/` — current AFW/OpenXR config, hardened
  `Avowed6dof.lua`, active Lua dependencies, data and controller bindings;
- `SHf-Win64-Shipping/` — Native-start config, hardened SHf Lua profile, active
  data and optional TArray helper.

Back up `%APPDATA%\UnrealVRMod\<game>` and install the matching directory
cleanly. Runtime logs, caches, dumps, saves, MCP plugins and backup files are not
included. The Avowed data preserves optional `CHEAT_*` gameplay settings; review
them before use.

## Backend package

The main archive contains:

- `UEVRBackend.dll` built from the alpha.2 tag;
- matching `openvr_api.dll`, `openxr_loader.dll` and
  `UEVRPluginNullifier.dll`;
- official PureDark beta.4 `PDAFWPlugin.dll`;
- package notes and SHA-256 manifest.

The matching backend PDB and portable profiles are attached separately. The
UEVR injector is not included; use the official UEVR frontend.

The backend callers, header and official PDAFW runtime form one beta.4 ABI
checkpoint. Do not hot-swap only `PDAFWPlugin.dll`. Building requires authorized
access to gated `PureDark/UESDK` revision `9034a857`.

## Reporting problems

Provide the executable name, exact backend/runtime hashes, clean profile config,
sanitized `log.txt`, reproduction steps and a matching dump when available.
Remove usernames, private paths, tokens, saves and unrelated personal data.
