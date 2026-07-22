---
type: fix
title: UESDK render-target discovery hardening (no blind vtable-index fallbacks)
description: FRenderTarget/FTextureRenderTargetResource discovery must validate candidate vtable offsets (gamma + render-target-texture indices together) and refuse plausible-but-unverified candidates; the old blind index-1 fallback crashed patched Avowed.
tags:
- uesdk
- render-target
- scene-capture
- validation
timestamp: '2026-07-20T18:54:40+09:00'
resource: dependencies/submodules/UESDK/src/sdk/FRenderTarget.cpp
---

# Problem

The UESDK's `FRenderTarget`/`FTextureRenderTargetResource` discovery used
"close enough" assumptions — notably a blind fallback of `GetRenderTargetTexture`
to vtable index `1`. Patched Avowed shifted layouts enough that these false
positives caused scene-capture/native-stereo crashes.

# Fix

In `dependencies/submodules/UESDK/src/sdk/FRenderTarget.{cpp,hpp}` and
`FTextureRenderTargetResource.cpp`:

- **Removed** the blind index-1 `GetRenderTargetTexture` fallback.
- Test candidate render-target vtable offsets and validate **both**
  `GetDisplayGamma` and the render-target-texture index before accepting.
- Refuse to fall back to the first plausible candidate when validation fails —
  fail cleanly and let the caller disable scene capture instead
  ([/fixes/native-stereo-safe-activation.md](native-stereo-safe-activation.md)).
- Added `FRenderTarget::reset_offsets()` and an accessor for the discovered
  `GetRenderTargetTexture` index.

# Principle

Same as the [dynamic AddObject guard](tow2-addobject-candidate-guard.md)
on the AFW branch: **a candidate pointer/offset must be positively validated
before use; skipping is safer than guessing.** This shows up in three places
now — render-target vtables, UObject AddObject candidates, and scene-capture
rebuilds.

# Citations

- `AVOWED_UEVR_UPDATE.md` §1.3–1.5
- UESDK checkpoint `04dd06e` "Harden Avowed render target/resource discovery"
