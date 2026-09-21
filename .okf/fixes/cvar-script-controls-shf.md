---
type: fix
title: Cross-game user script controls and SHf validated CVars
description: Explicit script bypass/application across games, bounded numeric script processing and readback-checked SHf stereo CVar candidate.
timestamp: 2026-09-20
tags: [cvar, silent-hill-f, tow2, performance, diagnostics]
---

# Evidence

TOW2 September 20 10:50–10:58 test: user reports no browser crash but mostly
invalid objects and worse perceived performance. Explicit script application
at 10:57:56.138–58.106 sets 22 variables, skips seven and coincides with a
1971.706 ms game-thread pre-work maximum. Log intervals around EarlyZPass and
VSM writes are about 780 and 1171 ms respectively; these are not isolated setter
measurements. HZBOcclusion is set to 1, then the frozen menu restores 0.

Delivery averages 44.97 accepted submissions/s over six windows ending
10:57:28–54, versus 38.39/s over eight ending 10:58:03–38 (stall window excluded).
The last two recover to 44.91/s. Cheeky was off during script application.
Earlier nearby off/on/off windows average 40.67/39.19/34.15/s; these uncontrolled
intervals do not establish a causal Cheeky regression.

SHf's 11:01 log repeatedly fails CVar virtual-function resolution. Its saved
standard CVar file requests VSM=0 but its script comments that setting out.
Neither proves a successful engine write. Current saved config is Native
rendering (0), Native Stereo Fix=true and Same Pass=true, unlike the historical
AFW profile in [SHf notes](../games/silent-hill-f.md). Do not silently overwrite
the user's current rendering mode with that older configuration.

# Implemented behavior

## Detected performance-menu extension (deployed; live test pending)

User retest found the long list stuck under thumbstick input. The subsequent
[framework right-stick scroll fix](../playbooks/framework-menu-controller-pointer.md)
was deployed at 18:33:07; headless input/layout regressions pass, headset retest
pending. Performance CVar values are unchanged by that fix.

`CVars > Performance controls (detected in this game)` adds a 20-entry catalog
in `src/mods/vr/PerformanceCVars.hpp`: Lumen reflections/GI enable, GI probe
spacing/ray resolution, reflection downsampling, SSR quality, volumetric fog and
its XY/Z grid controls, conventional shadow quality/resolution/cascades/distance,
contact/distance-field shadows, view distance, detail mode and grass/foliage
density. Existing resolution/VSM/AO/post-process controls remain below.

Optional controls are registered for all games, using only bounded registry and
interface validation (no missing-variable legacy fallback). Discovery begins
after five seconds, one optional entry per tick; steady polling is 250 ms.
Absent variables are hidden and counted. Present but unsupported variables are
listed as unavailable. Failed registry discovery is not reported as absence.
Registered does not imply the feature is active in a scene. New entries do not
apply defaults; existing saved overrides are honored. Sliders keep a draft and
commit on release, clamp edits to UI ranges, then use verified game-thread writes
and existing per-profile persistence. Hover displays exact CVar and trade-off.

Catalog uniqueness/range/type tests and both CVar interface-layout fixtures pass;
Windows Release build passes. September 20 18:14 deployment was refused because
TheOuterWorlds2-Win64-Shipping was running. Subsequent user-authorized deployment
succeeded at 18:15:47 to the shared injector folder, with DLL/PDB hash verification.
Backend SHA256: `e675a8872bcb773a7d72e5afdd81f90677bdac4086505af8466d29ee18f336cc`.
PDB SHA256: `ce797aa97b67d4621d5109eebdc45d097accf658a16249f9110ea20d13d82ede`.
Previous files have `.pre-native-openxr-20260920-181547.bak` suffixes;
evidence is in `diagnostics/native-openxr-20260920-181547`. Runtime presence,
interaction and effect still require game validation. This extension does not
add the separately discussed cross-game performance timing or timed A/B runner.

Controls informed by Epic's [Lumen performance guide](https://dev.epicgames.com/documentation/unreal-engine/lumen-performance-guide-for-unreal-engine)
and [fog/shadow performance guidance](https://dev.epicgames.com/documentation/unreal-engine/in-camera-vfx-best-practices-in-unreal-engine);
the shipped game's detected registry remains authoritative for availability.

Every game's CVars panel now exposes:

- `Automatically apply user_script.txt on profile load` — default off; stored
  per profile as `CVar_AutoApplyUserScript` by Save Config.
- `Apply user_script.txt once` — queues the file on the game thread.
- `Bypass / cancel pending script` — disables automatic application and clears
  queued work. **Not an undo** of previous CVar changes; restart for baseline.
- Pending-line count. One script entry is processed per engine tick.

These controls govern UEVR's loader only, not independent Lua or Engine.ini
overrides. Other games retain legacy Exec dispatch, logged without readback.

TOW2 and SHf use bounded validated registry/interface access for both standard
and formerly raw-data menu entries. SHf also skips the legacy render-path CVar
writes/queries, including automatic uncap/culling operations. Discovery starts
after five seconds; unverified entries are unavailable rather than retried via
the old scanner. The strict resolver supports single- and extra-setter layouts.

For these two games scripts accept numeric values only, skip values already
equal to readback, and skip conflicting frozen-menu values with an explanation.
Logs include before/request/readback, verification, no-op status and per-entry
elapsed time. Splitting the batch does not prevent a costly individual engine
setter. Frozen-menu settings retain priority; scripts do not silently unfreeze
them. Write/readback evidence survives SHf's warning-level logging.

VSM=0 remains the narrow default unless an explicit saved menu value overrides
it. Proven for TOW2; **candidate only for SHf**. Broad script bypass does not
disable the independent saved/frozen menu settings. SHf first test: fresh launch,
auto-script off, do not Apply once; inspect VSM readback and per-eye lighting.
Preserve other rendering settings for this isolation.

# Verification and deployment

First SHf live result, September 20 12:10:58 launch: user reports eye mismatch
appears gone. At 12:11:12.027 the log records VSM requested=0, before=1,
readback=0, verified=true. Eight frozen menu entries report verified readbacks;
AmbientOcclusionLevels (-1 to 2), SubsurfaceScattering (1 to 0), and
Upscale.Quality (2 to 3) also change, so this is not an isolated VSM bisection.
Saved CVar_AutoApplyUserScript=false and no script-entry diagnostics are present.
No validated read/write/readback failures appear. One interface layout remains
unsupported and unavailable. Startup D3D12/scanner and profile JSON errors still
exist; the last recorded line is at 12:11:12.180. Warning-level logging provides
no steady-state FPS evidence or proof of clean shutdown. This validates the
visible improvement plus working CVar access, not all CVars or performance.

Windows Release backend passes. Executable interface fixture passes both
single-setter and extended-setter layouts, int/float access and missing-Release
rejection. Portable C++17 tests pass script no-op/frozen-conflict policy and array
bounds/interface stride. First SHf visual/readback confirmation is recorded above.

Deployed September 20 11:18:49 from authoritative AFW-PUBLISH to
`C:\Users\gthom\Downloads\UEVR-AFW-JOEY`. Backend SHA256:
`672c9f8a589fff5a62dec2d16837e9ea62242e1f638ecc6e835079b00804afaf`.
PDB SHA256:
`4799931234ab31c7ccaa4b08d4b4a66514bbaffaf676cb29cfd9ed6b21ce13cc`.
Backups: `.pre-native-openxr-20260920-111849.bak` beside installed originals.
Both games' logs and SHf settings/script archived under
`diagnostics/native-openxr-20260920-111849`. Neither profile, user_script file,
Cheeky addon nor AFW runtime was changed.

See [original validated CVar repair](tow2-validated-cvar-access.md) and
[browser correction](uobject-browser-array-validation.md).
