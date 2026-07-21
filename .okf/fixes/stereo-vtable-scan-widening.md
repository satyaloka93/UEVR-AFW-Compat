---
type: fix
title: Widen FFakeStereoRendering vtable scan window (100 → 300 bytes)
description: Game patches can move the FFakeStereoRendering constructor's vtable reference beyond UEVR's default 100-byte scan; widening both fallback scans to 300 bytes restored stereo hook discovery for patched Avowed (reference at +0xCC).
tags:
- stereo
- vtable
- hook-discovery
- avowed
timestamp: '2026-07-20T00:00:00Z'
resource: src/mods/vr/FFakeStereoRenderingHook.cpp
---

# Symptom

After a game update, UEVR fails to hook stereo at all: the fake-stereo
constructor/vtable reference is no longer found, so injection either never
reaches a VR render state or falls into broken fallbacks.

# Fix

In `src/mods/vr/FFakeStereoRenderingHook.cpp`, increase the constructor
vtable-reference scan range from **100 to 300 bytes** (both fallback scans).
Patched Avowed's reference sits at `+0xCC`, past the old window.

This was the *first* domino for Avowed recovery — see
[/games/avowed.md](/games/avowed.md). Once discovery worked, the failure mode
moved downstream into native-stereo/scene-capture stability
([/fixes/native-stereo-safe-activation.md](/fixes/native-stereo-safe-activation.md)).

# When to reach for it

Any time a previously-working game stops hooking after a patch. Check the log
for failed fake-stereo discovery before suspecting anything else. The same
widening was ported to the PureDark AFW branch and worked unchanged.

# Citations

- `AVOWED_UEVR_UPDATE.md` §1.1
- `AVOWED_DEBUG_LOG.md` — "Was the vtable scan increase required?" (yes)
- `PUREDARK_AFW_AVOWED_WORKLOG.md` — port item 4
