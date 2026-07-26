---
type: fix
title: Silent Hill 2 (SHProto) — unify onto the AFW fork via forced Native cold-start
description: 'SH2 has no game carve-out and rides baseline UEVR''s generic shared render path; the AFW-Compat fork''s hardened UE5.8 UESDK and ungated AFW cold-start rework break it. Solved by dropping the hardened UESDK and forcing Native rendering BEFORE OpenXR swapchain init (runtime AFW switch still works).'
tags:
- silent-hill-2
- sh2
- shproto
- afw
- native-stereo
- uesdk
- ue5.8
- swapchain
- unification
timestamp: '2026-07-26T18:50:00+09:00'
---

# Scope

Making a **single UEVR-AFW-Compat backend** run Silent Hill 2 (`SHProto-Win64-Shipping.exe`)
alongside Avowed and TOW2. Validated on branch `sh2-compat-gating` (local, unpushed;
base commit `a0bc23aa`, fix tip `bc5162e5`). See the game state at
[/games/silent-hill-2.md](/games/silent-hill-2.md) and the fork project at
[/projects/puredark-afw-integration.md](/projects/puredark-afw-integration.md).

# Symptom

On the AFW fork, SH2 either:
- **GPU-hangs / black-screens the whole machine** (TDR, requires reboot, no minidump), or
- boots but the headset shows **"waiting"** while the desktop UEVR menu renders, with the
  log repeating `[VR] Double wide swapchain not created` / `OpenXR: Trying to copy to
  swapchain 0 but it doesn't exist`.

Baseline `./UEVR` and the Joey-Merged lineage (`021d45b7`) run SH2 fine; the plain PureDark
beta.4 bundle runs it fine too.

# Why it diverged from baseline ./UEVR

**SH2 has no per-game carve-out.** Baseline `./UEVR` ships explicit gated fixes only for
Avowed (`is_avowed_executable`), TOW2 (`is_the_outer_worlds2_executable`) and **Silent Hill f**
(`is_silent_hill_f_executable` — a *different* game). There are **zero** `shproto`/SH2
references anywhere in baseline. SH2 is a well-behaved UE5 title that "just works" on the
**generic, shared** stereo/present path.

The AFW-Compat fork diverged from baseline in two ways that only hurt the un-carved-out game:

1. **Hardened UE5.8 UESDK bump** (`c1528b7c`: submodule `491f973a` → `9034a857`). This is a
   3,924-line UE5.8 SDK upgrade (new `StereoStuff`/`IXRTrackingSystemVTables`/`FSceneView`/
   `UObjectArray` layouts + UE5.8 "layout version-gates"). Those gates mis-read SH2's older
   UE build → wrong stereo/vtable offsets → corrupt D3D12 submission → **GPU hang**. It was
   bundled needlessly with a required `PDAFWPlugin.h` beta.4 ABI change; nothing here needs
   UE5.8 (all target games are UE5.1–5.4). `FUObjectItem::get_object()` is just a layout-aware
   accessor over the baseline `object` member, so reverting is safe.

2. **Ungated AFW cold-start rework** (`276300db`, "publish Avowed and TOW2 AFW compatibility").
   A 1,804-line rewrite of the *shared* stereo/D3D12 path — only partially gated to
   Avowed/TOW2. The reworked **AFW cold-start bring-up leans on per-game handling** that
   Avowed/TOW2/SHf supply and SH2 does not. So SH2 cold-starting in AFW hangs VR init, while
   the games with carve-outs are immune. (Runtime *switch* to AFW is fine — only the cold
   AFW bring-up path is broken; the Native bring-up path was untouched.)

Architectural lesson: shared-path changes made for one game must be game-gated, or the game
without a carve-out inherits the breakage. See
[/decisions/narrow-port-scope.md](/decisions/narrow-port-scope.md).

# Solution

1. **Drop the hardened UESDK back to baseline `491f973a`** and revert the
   `FUObjectItem::get_object()` accessor to `->object` in the 3 dependent files
   (`UObjectHook.cpp`, `pluginloader/FUObjectArrayFunctions.cpp`, `uobjecthook/SDKDumper.cpp`).
   Keep the `PDAFWPlugin.h` beta.4 ABI. Fixes the GPU hang; Avowed/TOW2 unaffected
   (they never exercise UE5.8). — commit `a0bc23aa`.

2. **Force SH2 to cold-start in Native, before swapchain init.** SH2 cannot cold-start in
   AFW, so `VR::on_config_load` forces `RenderingMethod = NATIVE_STEREO` for
   `shproto-win64-shipping.exe`. **Critical ordering:** the force must run *immediately after
   the ModValue load loop and BEFORE `initialize_openxr_swapchains()`*. Placed at the end of
   `on_config_load` (the first attempt), the OpenXR swapchains were already built for the
   saved AFW value, and flipping to Native afterward left a broken hybrid
   (`value=Native`, `swapchains=AFR`) → the "Double wide swapchain not created" / "waiting"
   loop. Moving it before swapchain init makes a config with AFW cleanly cold-start Native.
   The runtime switch to AFW still works, and the config reverts to Native on the next launch.
   — commit `bc5162e5` (fixes the mis-placed `509b1e8a`).

**Verified 2026-07-26:** SH2 comes up reliably (confirmed across repeated launches), can be
switched to AFW at runtime, and reverts to Native when the game is quit from AFW.

# Superseded / symptom-chasing attempts (kept for the record)

While the hybrid-swapchain state (from the mis-placed force) was still present, two changes
were made that treated its *symptoms* rather than the cause, by extending SHf/TOW2 carve-outs
to SH2:
- `1cb26273` — added SH2 to `needs_real_backbuffer_fallback()` (real-backbuffer fallback).
- `6656ed52` — extended the SHf `FRHITexture2D` render-thread bootstrap chain to SH2.

Once the force-ordering fix landed, the double-wide swapchain is created normally and neither
path is needed for the confirmed-working case; they are harmless (fire only on null
backbuffer/vtable) and slated to be dropped before the branch is published.

# Citations

- Branch `sh2-compat-gating` commits: `a0bc23aa`, `1cb26273`, `509b1e8a`, `6656ed52`, `bc5162e5`.
- Divergence sources: `c1528b7c` (UESDK bump), `276300db` (AFW compat rework).
- Baseline SH2-working lineage: Joey-Merged `021d45b7` (`RDC-Joey-Merged`).
- Log signatures: `Double wide swapchain not created`, `Failed to determine chunk size`
  (benign), `Failed to find stereo rendering device` (benign 2× fail-then-recover).
