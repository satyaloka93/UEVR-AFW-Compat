---
type: fix
title: Hosting the DLSS 5 Neural Rendering addon inside UEVR
description: UEVR exports the ReShade addon API itself so renodx-dlss5.addon64 runs without ReShade, which cannot coexist with UEVR. Both eyes receive neural rendering because double-wide stereo gives them identical cache keys — the constraint that limits a two-resource VR port to one eye.
tags:
- dlss
- neural-rendering
- renodx
- reshade
- stereo
- ngx
- experimental
timestamp: '2026-08-31T19:30:00+09:00'
resource: src/mods/DlssNeuralRendering.cpp
---

# Why UEVR hosts the addon instead of using ReShade

ReShade and UEVR do not coexist here, by both of its entry points:

* **As a proxy** (`Binaries\Win64\dxgi.dll`) UEVR did not inject into The Outer Worlds 2 at all —
  no `UEVRBackend.dll` in the process and no log written. Renaming the proxy away fixed it on the
  next launch.
* **As the OpenXR API layer** it loads but declines the session: `Skipping OpenXR session because
  it was created without a proxy Direct3D 11 device`. A VR mod creates its own device, so ReShade
  never recognises it, never builds an effect runtime, and therefore never loads any addon.

A RenoDX addon does not link against ReShade. It calls `K32EnumProcessModules`, takes the **first**
module exporting `ReShadeRegisterAddon`, and binds to it. Exporting those ten entry points from
`UEVRBackend.dll` is therefore sufficient to be that host.

`DlssNeuralRendering` is a normal `Mod` with a sidebar entry, `ModToggle` options persisted in the
game profile, `on_present()` for addon event 74, and the addon's own ImGui settings page drawn
inside the UEVR menu — the addon keeps NR Preset, NR Intensity and the codec strengths in an
overlay rather than in config, so calling that page is the only way to reach them.

# The result: both eyes, which a two-resource port cannot achieve

Confirmed in TOW2. This is the outcome the same addon cannot produce in the CyberpunkVR port,
where it reaches one eye permanently — see the [CyberpunkVR Port repository](https://github.com/satyaloka93/cyberpunk-vr-port).

## Why it works here, and it is not the reason first assumed

The addon keeps its inline resource set and its visible codec in **single-slot caches at fixed
addresses**. Its guard compares an incoming `{output resource pointer + five scalar fields}` against
one stored record and rebuilds on any mismatch. Two render targets can therefore only *share* one
set or *thrash* rebuilding it.

The obvious hypothesis — "double-wide gives one contract covering both eyes" — is **wrong**. The
addon captures a per-eye contract:

```text
UEVR stereo target ..... 6120x3120     (double-wide)
NR runs at ............. 3060x3120     (exactly half — one eye)
guides ................. 1019x1039
```

It works because both eyes present the **same key**: same resource, same dimensions, differing only
in subrect origin — and the subrect base is not part of the cache key. Matching keys take the
*share* branch. In a port that renders each eye into its own resource the keys differ, only the
rebuild branch is available, and one eye is the permanent outcome.

**So the requirement is not "double-wide". It is "both eyes must produce an identical cache key".**

# Not thrashing — read the timestamps

Two resource-set creations look like churn and are not. They are one geometry transition:

```text
19:13:11.636  created inline NR resources 3060x2160     pre-stereo (target 3840x2160)
19:13:11.796  RenderTargetSize After: 6120x3120         stereo activates
19:13:12.961  created inline NR resources 3060x3120     re-made once for the new geometry
```

The 11 feature creations are spread over five minutes (19:13, 19:14:40, 19:15:42, 19:15:51,
19:18:12) — menu and scene transitions. Cyberpunk's genuine thrash was 11 creations in seconds.
Zero `(upscaling)` lines: `NREnableUpscaling` was never on, and every create says `(native)`.

# Performance is the real cost, not a defect

```text
TOW2 : 3060x3120 = 9.5 Mpixel, evaluated per eye, per frame
CP2077: 2560x2560 = 6.5 Mpixel, one eye — and that already cost 5-7 ms/frame
```

Roughly three times the work that halved Cyberpunk's frame rate, so the observed unplayability is
the honest price of this resolution rather than something to debug away. The lever is UEVR's own
render-target scale: NR cost falls quadratically with it, and unlike anything in the Cyberpunk
investigation it is a dial we control.

# Setup

| item | location | why |
|---|---|---|
| `renodx-dlss5.addon64` | beside the game exe | the host scans the executable's directory |
| `nvngx_dlssnr.dll` | beside the game exe | the addon requires it **next to itself**, not in the DLSS plugin folder |
| ReShade | absent | see above; a proxy stops UEVR injecting |

Config keys, in the game's UEVR profile:

```ini
DlssNeuralRendering_Enabled=true          ; next launch only -- the addon hooks NGX and must load first
DlssNeuralRendering_DispatchPresent=true
DlssNeuralRendering_DrawAddonUI=true
```

**A matched DLSS Super Resolution version is not required.** TOW2's stock `nvngx_dlss.dll` 310.7
drove NR 310.8 with feature 18 creating and evaluating cleanly and zero
`skipped an NGX evaluation` lines. 310.8 was installed afterwards for SR quality, not to make NR
work. Do **not** replace `nvngx_dlssg.dll`: a mismatched frame-generation snippet produced a
separate repeatable startup-crash family.

# Host implementation notes worth keeping

* **The lock must be recursive.** An addon binds from inside its own `DllMain`, on the thread still
  inside our `LoadLibrary`, so `ReShadeRegisterAddon` re-enters a lock that thread already holds.
  With a plain `std::mutex` MSVC throws `system_error`, the exception unwinds through the addon's
  `DllMain`, and the loader reports `ERROR_DLL_INIT_FAILED` with nothing explaining why.
* **`ReShadeGetImGuiFunctionTable` must never return null** while hosting: `reshade.hpp`'s
  `register_addon` treats null as a hard failure and the addon aborts before installing its NGX
  hooks.
* **Unimplemented ImGui stubs must return zero.** Returning true means "the user changed this", so
  the addon commits and rebuilds its feature every frame.
* **Widget slots were found by logging each stub's first argument** — an ImGui widget's label —
  because counting members of ReShade's table produced a set no settings page would call. The
  measured ones are 80 Separator, 103 TextUnformatted, 104 TextV, 111 Button, 115 Checkbox,
  129 Combo, 144 SliderFloat.
* **The API-18 config signatures include the addon module argument.** Omitting it shifts every
  parameter and yields nonsense such as a key of `RenoDX.DLSS5` with a setting *name* as its value.
* **Exporting these entry points is not neutral.** An addon binds to the first module that exports
  `ReShadeRegisterAddon` and does not fall back if it refuses, so with hosting off the host
  forwards to a real ReShade rather than declining.
* Present, overlay and `init_device` call-ins are each SEH-guarded; a fault disables that path for
  the session instead of taking the frame with it.

# Citations

[1] [RenoDX](https://github.com/clshortfuse/renodx) — framework only; this addon's source is not public.
[2] [RR + DLSS 5 + RenoDX distribution](https://github.com/renodxdlss5/RR-and-DLSS-5-RenoDX-for-the-Games) — binaries and an installer profile table; defines no addon settings.
