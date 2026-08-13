# AFW beta.4 compatibility v0.1.0-alpha.4

Alpha.4 is an experimental prerelease that publishes the currently maintained
TOW2/SHf bug fixes and the complete rebuilt SHf first-person/6DoF profile. It is
not a stable UEVR distribution.

## Backend changes since alpha.3

### TOW2 allocator discovery / memory growth

The alpha.3 UESDK cache baseline scanned lowercase `binned2`, while TOW2 exposes
case-sensitive `Binned2`. A failing run logged `Failed to find GMalloc` and was
stopped near 30 GB system RAM while still growing.

Alpha.4 carries the narrow upstream-equivalent allocator-name case correction.
The candidate resolved `Binned2` at vtable index 24 and Malloc/Realloc/Free at
5/7/9, then sustained approximately 35 minutes of AFW without observed
continuing growth, OOM, GPU crash or device loss. The automatic private-byte
monitor timed out before that successful run, so no quantitative memory curve
is claimed.

### SHf SceneView fail-closed startup guard

Intermittent SHf starts crashed in `sceneview_constructor()` after failed
`FSceneViewFamily` discovery accepted guessed render-target/scene offsets
`0x8/0x10`. Alpha.4 validates the live family, views array, render target and
scene-interface vtables before publishing the source-confirmed
`0x8/0x30/0x38` layout. If validation is not yet possible, scene-dependent
remapping is skipped and a later constructor retries naturally.

The candidate completed an approximately 35-minute SHf run and additional
user-reported starts without the historical constructor crash. Neither decisive
guard signature appeared in retained logs, so this remains a prerelease rather
than proof that every transient path has been exercised.

### Lua exit-time lifetime fix

A later SHf gameplay run generated an exit-time backend dump after OpenXR focus
loss. Matching-PDB symbolization resolved the exception to
`ScriptContext::log()` line 94: the context destructor routed a diagnostic
through a plugin API function table that was already unsafe during process
teardown. Alpha.4 removes only that destructor diagnostic call; normal runtime
Lua logging remains intact. A clean-exit retest of the release build is still
recommended.

### TOW2 explicit 6DoF enrollment

Alpha.4 is the first backend release after commit `217162d7`. It can explicitly
enroll TOW2's late Steam weapon component after exact FUObjectArray, hierarchy,
thread and module-vtable validation. This enables the separately packaged TOW2
6DoF overlay; the alpha.3 backend predates enrollment and is incompatible with
that overlay.

## Maintained SHf first-person/6DoF profile

The profile archive now contains the complete sanitized working
`SHf-Win64-Shipping/` profile rather than the stale alpha.2 files or alpha.3's
partial overlay. Its base lineage is
[letmein-vr/SHF_UEVR](https://github.com/letmein-vr/SHF_UEVR/), with the local
settle/minimal-core rebuild and compatibility hardening documented in OKF.

Key maintained files:

- `00_settle.lua` — pawn stability gate across startup and loading;
- `91_button_swap.lua` — standalone dodge mapping;
- `92_core_init.lua` — pawn/attachment/input/IK initialization and the
  weapon-grip callback that closes the hand around the weapon;
- active `shf.lua`, `melee.lua`, `examine.lua`, Lua/data dependencies and
  UObjectHook camera/property state;
- `90_weapon_attach.lua.disabled` — calibrated native-attachment rollback.

The crash-prone top-level `main.lua` and redundant top-level `hands.lua` are
removed. Install into a **clean** profile directory so those deleted files
cannot survive a merge.

Required startup state:

```ini
Frontend_RequestedRuntime=openxr_loader.dll
UObjectHook_EnabledAtStartup=true
VR_RenderingMethod=3
VR_AFW_FramewarpMode=2
VR_NativeStereoFix=false
VR_NativeStereoFixSamePass=false
VR_GhostingFix=true
VR_GhostingFixBootstrapViewStates=true
VR_AFW_FixMovingObjectBrightnessFlickering=false
```

Start from a fresh process directly in Previous Frame AFW. Do not change AFW to
Native and back in-process; PDAFW has no teardown API. If DLSS is stale after VR
stabilizes, change it away from and back to Performance in SHf's own menu.
Physical melee remains an unfinished candidate and firearms remain untested.

## Other maintained-game rules

- **TOW2:** start Native, enter gameplay, then switch once to Previous Frame
  AFW. Restart to leave AFW. Preserve the `_3dof` base; apply the included 6DoF
  directory as documented rather than replacing it blindly.
- **Avowed:** the release does not resume the rejected hand/bone experiments.
  Preserve the known-good dynamic Lua/stale-component architecture.
- **Silent Hill 2:** Native-only with Native Stereo Fix enabled for its current
  profile. Prior AFW device hangs and the NVIDIA `0x139` bugcheck remain valid.
- Moving-object brightness correction remains disabled for maintained profiles.
- PureDark CAS is not included.

## Archives

| Archive | Contents |
|---|---|
| `UEVR-AFW-Compat-v0.1.0-alpha.4.zip` | Committed `UEVRBackend.dll`, loaders, `UEVRPluginNullifier.dll`, and the tested amended beta.4-derived `PDAFWPlugin.dll` |
| `UEVR-AFW-Compat-v0.1.0-alpha.4-profiles.zip` | Maintained Avowed and SHf profiles plus the TOW2 6DoF overlay |
| `UEVR-AFW-Compat-v0.1.0-alpha.4-symbols.zip` | Matching `UEVRBackend.pdb` |
| `RELEASE_SHA256SUMS.txt` | SHA-256 manifest for every release archive and packaged runtime artifact |

The injector is not included; use the official UEVR frontend. Runtime logs,
caches, dumps, saves, personal paths and backup files are excluded.

## Dependency checkpoint

Alpha.4 pins `satyaloka93/UESDK` commit `7610b016`, consisting of the maintained
cache baseline plus narrow FMalloc case handling and SceneView validated-offset
publication. The backend, header and amended beta.4-derived PDAFW runtime form
one tested checkpoint; do not hot-swap only `PDAFWPlugin.dll`.

Report problems with the executable name, exact hashes, clean profile config,
sanitized log, reproduction steps and matching dump/PDB where available.
