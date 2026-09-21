---
type: project
title: Foveated DLSS 5 NR — ground truth, failure patterns, and the ordered plan
description: What is actually verified about the foveal region in UEVR, which of it works today, the specific ways the work has been failing (settings that never changed, diagnostics that never fired, defaults moved under the user), and a plan where each step has one decisive test.
tags:
- dlss
- neural-rendering
- foveated-rendering
- strategy
- stereo
timestamp: '2026-09-04T10:45:00+09:00'
resource: src/mods/DlssNeuralRendering.cpp
---

Written after the user said: *"I keep testing with the same results... perhaps iterate on a plan
before making me continue testing with same results!"* That is a fair charge and this document
exists to make it stop happening.

Companion to [Foveated DLSS 5 Neural Rendering](../fixes/dlss5-foveated-neural-rendering.md) and
[ReShade host object model](../fixes/reshade-host-object-model.md).

# 1. Ground truth — verified, with the evidence

| fact | how it was established |
|---|---|
| The signed snippet's caller check is beaten by a **tail-JMP** thunk | zero `0xBAD00002` refusals after; a `CALL` reintroduces them |
| NGX parameter vtable **slot 1 = resource**, **slot 6 = float** | wrapping slot 1 as float destroyed every resource pointer (title hang) |
| Region must be written to **Color, Depth, MVec, Output** consistently | mismatched Color/Output is rejected **silently** |
| `DLSSNR.Output` is persistent and blitted back **whole** | periphery froze; band copies fixed it |
| `DLSSNR.ControlMask` is a **suppression** mask, not a weight | bright centre removed all NR; inverted restored it |
| NR costs ~4.5 ms of a ~21 ms frame | measured; foveation ceiling ≈ 19% |
| A 0.35 region saves 2.6–3.5 ms | 16.80 ms → 12.80 ms measured in TOW2 |
| TOW2 is **not** alternate-frame; `is_left_eye()` is meaningless here | `RenderingMethod 0`; both eyes evaluate within one frame |
| CheekyFoveatedDLSS **initialises fully** under our host | `AddonInit returned true`, events 72 + 74 subscribed |
| ...and makes TOW2 **~19× slower**, then wedges the render thread | analyzer storm 1 s without it vs 19 s with it; presents stop |
| ...but it **works fully IN VR under UEVR** with a real ReShade proxied as `d3d12.dll` | GTA SA Definitive Edition, 2026-09-04: red border + foveated DLSS-SR |
| It hooks the **`_nvngx.dll` dispatcher**, after waiting for it to stabilise | its own log: `scans=3/3; direct hooks allowed` |
| Its IAT / early-loader path is **off in the run that works** | `Early executable interception active=no` |
| UEVR **rewrites `config.txt` from memory on exit** | edits made mid-session were reverted at 10:08:50 |

# 2. What works today

* Hosting `renodx-dlss5.addon64` **4.1.5** with no ReShade present.
* The tail-JMP detour on `EvaluateFeature`, re-armed across device reset.
* Rewriting the subrect quadruple on all four planes at native resolution.
* Band-only periphery seeding.
* Settings persistence, versioned ImGui tables, the real ReShade object model.

# 3. How this work has been failing

Not the hard problems — the loop around them.

**A setting was changed that could never take effect.** `ConvergencePct` was moved from `0.04` to
`0.0` *in the code default* while `config.txt` already contained the key. Existing keys win, so the
default was inert. Then `config.txt` was edited directly — but UEVR rewrites it from memory on exit,
so that was reverted too. **Three runs were spent testing a value that never moved**, and the user
was told each time that the fix was in.

**A diagnostic was shipped that could not fire.** The upscaling plane dump was gated on
`g_box_applies < 8`; that counter is in the thousands long before anyone toggles upscaling. It
printed nothing, and its silence was nearly read as evidence.

**Defaults the user had already settled were moved.** The preset was switched 1 → 2 mid-investigation,
changing the shape under test while a *different* variable was being examined.

**Conclusions were stated from inference rather than logs.** "Game-thread deadlock" was called from
`Pre-engine tick` appearing once — a line logged once at startup. It proved nothing, and a real lock
bug was fixed on the strength of it that turned out not to be the reported fault.

**Presets were repeatedly "fixed" when the preset table was already correct.** The recurring
two-boxes report was attributed to the table, to nasal anchoring, to eye swapping, and to preset
indices, across several rounds — while the actual mechanism (a per-eye shift on a hard edge) sat
unexamined in the centred branch.

# 4. Rules adopted

1. **A knob the user must set is not a fix.** If the correct value is known, encode it so a stale
   config cannot defeat it. `ConvergencePct` has therefore been *deleted*, not re-defaulted.
2. **Never edit `config.txt` to effect a change.** It is overwritten from memory on exit.
3. **Prove a diagnostic can fire** before relying on its silence — check its gate against live
   counter values.
4. **Do not move a variable the user has settled** while testing a different one.
5. **One decisive test per change**, with the discriminating log line named in advance.
6. **Read the log before theorising**, and quote the line rather than the impression.

# 5. Why 50% shows two boxes — and why placement cannot fix it

**Disproved first:** the mirrored ±4% shift was blamed, removed, and the boxes were then placed at
an identical `x=632` in both eyes. The user still saw two boxes. Placement is ruled out by evidence.

**Actual mechanism.** A hard edge drawn in screen space carries zero binocular disparity, so it
fuses at **infinity**. The viewer's eyes are converged on scene content a few metres away, and a
zero-disparity feature seen by eyes converged nearer is perceived **double**. This is ordinary
binocular vision. A non-zero offset does not fix it — it only moves the single depth at which the
edge happens to fuse. **No offset makes a hard screen-space edge fuse at every scene depth.**

**Why the nasal slab does not have the problem.** At 65% the left eye keeps `[0.35, 1.0]` and the
right eye `[0, 0.65]`. Each region runs all the way to its **nasal** edge, so the only boundary in
each eye lies on the **temporal** side — out in the monocular crescent, where the other eye
contributes nothing. No binocular rivalry at the edge means no doubling, while the central overlap
is covered in both eyes, giving one continuous region. This, not convergence, is why the CyberpunkVR
port defaults to preset 2 and why that is the shape remembered as "one stereo-correct box".

**Consequence.** Centre boxes (35%, 50%) cannot be made to fuse by any placement. They require a
**feathered** edge, which presents no localisable feature to double — and that requires the
composite pass in step 4. Until then the slab presets are the only stereo-correct option, and the
centre presets are labelled accordingly.

**Correction to an earlier claim in this document:** the removal of `ConvergencePct` was justified as
*the* fix for the seam. It was not. It remains the right change — a per-eye shift on a hard edge is
harmful and it was an inert setting — but it does not resolve the doubling.

# 6. The plan, in order

Each step names the single log line or observation that settles it.

### Step 1 — use the nasal slab *(shipped, `beca162d`; default and stored config now preset 2)*
Per-eye shift deleted; centre presets labelled as doubling in stereo.
**Settles it:** `region N: nasal left eye (896,0) 1648x2592` and `nasal right eye (0,0) 1648x2592` —
opposite origins, full height — and one continuous region in the headset with no central seam.

### Step 2 — upscaling, `0xbad00005`
Geometry is arithmetically exact (848×864 → 2544×2592 is 3.0×, no rounding), so rects are probably
not the fault. The plane dump now counts upscaled applications from zero and will print 16.
**Settles it:** whether Depth/MVec sit at a third scale, or whether the failure is unrelated to
rects — both refusals landed exactly when the feature was rebuilt for the new resolution.
**Do not change code before reading that dump.**

### Step 3 — eye identity from the NGX feature handle
`view_id = (uintptr_t)handle`; roles assigned first-seen, reassigned LRU. Replaces the parity
counter with a stable per-eye identity. Note honestly: this does **not** retire `Swap eyes` —
Cheeky still ships "Invert stereo eye order" — it makes the toggle stable instead of launch-dependent.

### Step 4 — the composite pass (the real prize)
The only route to an oval region, a working feather, and a live periphery. Their shader:

```hlsl
lerp(max(scaled.x, scaled.y), length(scaled), ShapeRoundness)   // box -> ellipse
1.0 - smoothstep(1.0 - normalized_feather, 1.0, distance)       // the feather
lerp(bilinear, dlss, weight)                                    // periphery is LIVE low-res colour
```

They own the output and blend the model's result over a bilinear upsample of the game's own low-res
colour. That single structure solves the static periphery **and** the seam **and** the rectangle
constraint — all three of the problems closed as impossible under subrect rewriting, because they
are only impossible while we do not own a pass.

**Risk, stated up front:** their transport and resource-cache machinery is the likely home of the
19x slowdown. Adopt the shader and the compositing idea; do **not** adopt `hooks.cpp` or the
D3D11<->D3D12 transport.

# 7. Why CheekyFoveatedDLSS works elsewhere and not under our host

The user ran it in **GTA San Andreas - The Definitive Edition, under UEVR, in VR**, with a real
ReShade renamed `dxgi.dll` -> `d3d12.dll`: red alignment border and foveated DLSS-SR both working
(DLSS-NR untried). Its own log is preserved as the healthy baseline.

**So the addon is not incompatible with UEVR.** It is incompatible with *being hosted by us*. UEVR's
own log for that session contains no `[DLSSNR]` lines at all -- our host is not in the picture; a real
ReShade loads the addon, and both run alongside UEVR without issue. The conclusion that hosting is
required, and the whole ReShade-host effort aimed at this addon, was solving a problem that a renamed
proxy DLL solves outright.

(Method note: the absence of `UEVRBackend.dll` from the game folder was briefly read as "this is a
flat game". It is not evidence of anything -- the backend is loaded from the injector directory. See
[[uevr-live-dll-is-the-injector-copy]]. UEVR was confirmed running by
`AppData/Roaming/UnrealVRMod/SanAndreas/log.txt`, timestamped to the same minute as the addon's own
crop lines.)

**A load-order theory was formed and then disproved by that log within the hour.** The reasoning was
that `install_early_loader_interception()` patches the game EXE's import table and so must run before
the game resolves NGX. The working run says otherwise, in its own words:

```text
Early executable interception active=no
HOOKDBG GetProcAddress IAT interception disabled; Streamline interception remains enabled
Initial executable IAT patch complete patched=no getProcAddress=disabled
Initial direct hook scan complete installed=no
```

The IAT path is **disabled in the configuration that works**. It is not the mechanism.

**What actually carries it** is a deferred, stability-gated direct detour on the NGX *dispatcher*:

```text
NGX runtime first sighting runtime=_nvngx.dll  scans=1/3; deferring direct hooks
NGX runtime stabilized     runtime=_nvngx.dll  scans=3/3; direct hooks allowed
Direct detour installed export=NVSDK_NGX_D3D12_Init target=... detour=...
```

It waits for `_nvngx.dll` to settle across three scans, then MinHooks `NVSDK_NGX_D3D11_Init`,
`NVSDK_NGX_D3D12_Init`, `NVSDK_NGX_D3D12_Shutdown1` and friends on it.

**And that is exactly what is unavailable in the UEVR path.** This bundle already recorded the reason,
in the note beside `install_ngx_probe()`: the addon `LoadLibrary`s the snippet from beside itself and
resolves the entry points by name, and *"there is no `_nvngx.dll` dispatcher in this path to sit
upstream of, which is precisely why the detour has to go on the signed module."* Their design assumes
the dispatcher exists; ours had to beat a caller check on the signed snippet instead. Two different
problems, and their solution does not transfer.

**Status: parked, 2026-09-04**, at the user's direction -- eye-tracked dynamic foveated rendering is
being built elsewhere, which supersedes a static region. The 19x slowdown figure measured under our
host stands as an observation but its cause remains unexplained; do not cite it as a property of the
addon.

# Citations

[1] [CheekyFoveatedDLSS](https://github.com/ClarkCheekyKent/CheekyFoveatedDLSS) — `src/foveation.cpp`,
    `src/d3d12_backend.cpp`, `src/settings.cpp`. Cloned and read, not summarised.
[2] CyberpunkVR port `src/Hooks/Ngx.cpp` @ `9d8e8e6` — `kNrFovealPresets`, `ComputeNrRegion`.
