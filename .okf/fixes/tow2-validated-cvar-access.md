---
type: fix
title: TOW2 validated CVar registry and extended interface access
description: Restore numeric menu access without legacy caller chasing, module-sized emulation or raw-data writes; verify the VSM stereo correction by readback.
timestamp: 2026-09-20
tags: [tow2, cvar, stereo, performance, diagnostics]
---

# Evidence and scope

September 20 live run (PID 8288): user confirms visible CVars and corrected
per-eye image problems. At 09:43:52 the log verifies VSM requested=0, before=1,
readback=0. Not every entry is supported: the boolean interface for
r.AllowOcclusionQueries is rejected and three requested variable names are absent.
First-entry discovery costs about 1.51 seconds once at startup, not per frame.
Active-game intervals vary around 31–45 submissions/s; later settled windows
reach 45/s. Menu/browser CPU cost also rises during browsing. This is not a
controlled performance regression measurement. The subsequent crash is
[independently localized to array name formatting](uobject-browser-array-validation.md),
not CVar scanning.

The September 19 TOW2 log reports `Failed to find UEngine::Exec function`.
The old [CVar bypass](tow2-cvar-scanner-bypass.md) therefore did not provide a
working backend `user_script.txt` fallback. That file still contains
`r.Shadow.Virtual.Enable 0`, the previously isolated
[per-eye shadow correction](virtual-shadow-maps-stereo-mismatch.md).
Its separate Lua loader uses PlayerController commands without verifying values.

Offline disassembly of the installed TOW2 executable (SHA256
`55ac756281828d00d93194ac80f99d13233415aff69360554d982a899f17c444`)
establishes the integer CVar interface at preferred-image VA `0x147e551a0`:

| Slot | Function |
| --- | --- |
| 15 | null AsConsoleCommand |
| 16 | Release: null-this guard, dispatch through vtable[0] |
| 17 | string setter |
| 18 | additional setter, **not GetInt** |
| 19 | GetBool |
| 20 | GetInt |
| 21 | GetFloat |

The old one-setter assumption selects slot 18 as GetInt. This is an ABI
compatibility defect; it does not independently prove the cause of every
historical stall. Additionally, legacy `is_vfunc_pattern` fallbacks instantiate
ShemuContext with a module-sized internal buffer per attempt. The new strict
path never invokes that matcher or emulator.

Registry discovery uses string anchors and bounded instruction walks, not
hardcoded game addresses or `find_function_start_with_call`. The first installed
`r.DumpingMovie` reference is in a 109-instruction function; its manager global
references are within the 192-instruction discovery bound. Candidate registry
layout is confirmed with bounded copied entries and three independent names.
Static addresses above are evidence only, not runtime constants.

# Candidate behavior

The original TOW2-only/explicit-only behavior below is superseded by
[cross-game script controls and SHf access](cvar-script-controls-shf.md): default
bypass, opt-in automatic application, one-line-per-tick scripts, no-op detection
and frozen-menu conflict reporting. The first TOW2 live menu/VSM test succeeded;
SHf access and latest script/browser follow-ups still require live validation.

TOW2 only: defer discovery until five seconds of engine ticks, resolve one menu
entry per tick, then refresh/freeze every 250 ms. Standard and formerly raw-data
menu entries both use verified interfaces. UI reads atomic snapshots, and menu
setters are queued to the game thread. Failed layouts/reads/readbacks disable
that entry for the session; no legacy scan or raw-data fallback occurs.

Release is checked by bounded register-provenance inspection; getters are
located after a boolean-return signature, accounting for the extra setter.
Set uses UE5.5+ console priority `0x0E000000`, preserves float round-trip
precision, and logs requested/before/readback. An explicit saved menu choice
takes precedence over the default VSM=0 correction. A checked/frozen box is
not success evidence: require the readback log and visual validation.

Broad `user_script.txt` execution is **explicit-only** via the CVar menu button,
accepting numeric variable assignments rather than arbitrary commands.
Dump All CVars dumps validated registry names without invoking help/type/value
methods. Arbitrary homebrew console dispatch remains disabled. The old direct
`VR::update_hmd_state` CVar bypass remains: this candidate does not silently
reactivate automatic HZB/instance-culling writes on the render path.

`scripts/deploy-validated-cvars.ps1` archives the profile, disables the separate
automatic `cvar_loader.lua` by recoverable backup and replacement with a no-op
compatibility module, then uses the existing backup/hash-checked DLL+PDB
deployment helper. Failure restores the Lua loader.
It leaves Cheeky, 6DoF scripts, resolution, ReShade and game settings unchanged.

# Performance measurements

TOW2 `[UEVR CPU pipeline]` five-second per-thread summaries measure mean/max:

- EngineTick: UEVR pre-work (jobs, UI, pre callbacks), original tick including
  its internal waits/hooks, UEVR post callbacks.
- Present: pre callback, desktop Present-or-skip, post callback including XR
  preparation/pacing. Interval is between observed calls on that thread.

These are CPU wall times, not GPU busy times or unique displayed FPS. They do
not individually isolate render-thread/RHI work, and averages are not
frame-correlated traces. No new waits or scheduling changes were introduced.
The later September 19 OFXR run is a separate user-confirmed experiment and
must not be pooled with the earlier standard UEVR A/B baseline.

# Verification

**Deployment correction:** The first deployment renamed `cvar_loader.lua`
without preserving its module name. `TheOuterWorlds2.lua:3` requires that name,
so the main gameplay profile aborted at startup with `module not found`.
Restored the installed module as a no-op returning an empty table, matching the
former loader's return contract without registering callbacks or applying CVars.
The deployment helper now installs/hash-verifies that compatibility module;
the original automatic loader remains backed up. Restart the game after this
correction so the interrupted profile initializes from a clean Lua state.

Deployed and destination-hash verified at `20260920-093701` to
`C:\Users\gthom\Downloads\UEVR-AFW-JOEY`. DLL SHA256
`1429d13e39e4f11b0cba3cbb21c84aad1d2e787603886985fd7a544eb3ad67c4`,
PDB `d29655a29c3b66ae08745cb60f4c3c176e20a53790e778c515527bda264fb9c1`.
Previous DLL/PDB have `.pre-native-openxr-20260920-093701.bak` suffixes.
The old Lua loader is preserved as
`cvar_loader.lua.disabled-validated-cvars-20260920-093701`; profile copies are
in `diagnostics/validated-cvars-20260920-093701/`, predeployment logs in
`diagnostics/native-openxr-20260920-093701/`. No game test yet.

Windows Release build passes. `test-console-registry.vcxproj` passes valid
snapshot and malformed/unreadable pointer, array, count and string rejection.
`uevr_cvar_interface_test` passes the actual SDK resolver against test-owned
executable stubs: Release 16, setters 17/18, getters 20/21; slot 18 traps if
miscalled. Integer Set/Get and float Get pass, and missing Release is rejected.
These are focused tests, not evidence that the live registry or visual fix has
already worked. In-game startup, menu values, VSM readback and visual/performance
validation remain required.

# Citations

- [UE5.5 console-variable flags and priorities](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Core/HAL/EConsoleVariableFlags?application_version=5.5)
- [Prior Native cost baseline](native-openxr-cost-attribution.md)
