---
type: fix
title: Foveated DLSS 5 Neural Rendering — beating the caller check, and the NGX facts it needed
description: A tail-jump thunk lets a detour rewrite DLSSNR's subrects without tripping the signed runtime's return-address check, cutting frame time 24%. Carries the measured NGX parameter vtable order, the ControlMask suppression semantics, the mirrored per-eye offset, and the eye-parity trap that made one eye look unlit.
tags:
- dlss
- neural-rendering
- foveated-rendering
- renodx
- ngx
- hooking
- d3d12
- stereo
timestamp: '2026-09-02T10:30:00+09:00'
resource: src/mods/DlssNeuralRendering.cpp
---

Builds on [DLSS 5 Neural Rendering addon host](dlss5-neural-rendering-addon-host.md), which
established that UEVR can host `renodx-dlss5.addon64` with no ReShade present. That left NR running
at full per-eye resolution and unplayably expensive. This is how it was confined to a box.

**Measured result: 16.80 ms → 12.80 ms per frame, a 24% saving**, at fraction 0.35 in The Outer
Worlds 2. Also reaches NR in GTA San Andreas — The Definitive Edition (`-dx12`): a second addon
build, a second engine version, no code changes.

# 1. The signed runtime's caller check is defeatable

The CyberpunkVR port recorded this as closed: `g_nrDiagState` is initialised to `-4` because
detouring `nvngx_dlssnr!NVSDK_NGX_D3D12_EvaluateFeature` made feature 18 return `0xBAD00002`, "even
a read-only trampoline". **That conclusion is wrong, and this is the correction.**

The check reads the return address **at function entry**. An inline hook is entered by `JMP`, so at
that instant the caller's return address is still the addon's. What breaks it is *calling* the
trampoline — a `CALL` pushes our module's address over it.

```text
08:04:41.678  inline feature 18 evaluation succeeded (count=1)   <- 5 ms before the detour
08:04:41.683  EvaluateFeature detoured  (trampoline reached by CALL)
08:04:41.807  feature 18 evaluate failed with 0xbad00002
```

So never call the original. A hand-assembled thunk saves the four argument registers, calls a
modifier, restores them, and **`JMP`s** to the trampoline:

```asm
sub  rsp, 0x48                  ; keeps 16-byte alignment across the call
mov  [rsp+0x20..0x38], rcx/rdx/r8/r9
mov  rdx, r8                    ; rcx is already the command list, r8 the parameter block
mov  rax, <modifier>  ; call rax
mov  rcx/rdx/r8/r9, [rsp+0x20..0x38]
add  rsp, 0x48
mov  rax, <trampoline> ; jmp rax     ; JMP, not CALL -- the entire point
```

Zero refusals afterwards. The cost is the return value, recovered free by reading the addon's own
log lines through `ReShadeLogMessage`. Install with safetyhook **`StartDisabled`**, patch the
trampoline address into the thunk, mark it `PAGE_EXECUTE_READ`, then enable — NR evaluates on the
render thread while installation runs on the present thread.

**The detour does not survive a device reset.** The addon detaches and re-attaches its NGX hooks,
and the snippet may reload. Evidence: the last box application is one second *before*
`destroy_device`, while NR keeps creating features for minutes after. Re-arm on reset, and clear
recorded resource pointers at the same time — they belong to the destroyed device.

# 2. The NGX parameter vtable is NOT the documented order

The most expensive fact here; it cost a title-screen hang. `NVSDK_NGX_Parameter` documents slots 0-7
as `Set(ULL, float, double, unsigned, int, ID3D11Resource*, ID3D12Resource*, void*)`.

| slot | documented | measured |
|---|---|---|
| 1 | `Set(name, float)` | **resource** — `DLSSNR.Color`, `.Depth`, `.MVec`, `.Output` |
| 3 | `Set(name, unsigned)` | unsigned ✔ |
| 6 | `Set(name, ID3D12Resource*)` | **float** — `Intensity`, the strengths, the scales |
| 11 | `Get(name, unsigned*)` | unsigned ✔ |

Slots 1 and 6 are reversed while 3 and 11 match — which is exactly what makes the documented order
look confirmed. Wrapping slot 1 as taking a `float` makes the forwarder read XMM2 and call the
original **without R8 set**, destroying every resource pointer the addon passes. The fingerprint is
slot 6, where every recorded "pointer" was the identical `0x7ff8368d5b50`: R8 garbage read where the
real float sat in XMM2.

**Wrap only slots whose type is proven.** Float and double take their value in XMM2, so one generic
forwarder corrupts precisely those two. Replacing slot 1 alone is sufficient and safe — the snippet
reads through the untouched `Get` slots.

# 3. Resource parameters cannot be read back, only intercepted

```text
probe finished: 0 resource name(s) found; control ColorSubrectWidth ok=true value=3060
```

Seventeen candidate names across `Get` slots 13/14/15 returned `0xBAD00010`
(`FAIL_UnsupportedParameter`) while a control read on the same object in the same call succeeded.
`DLSSNR.Color` and `DLSSNR.Output` are **log format strings** (`Color=%p subrect=(%u,%u %ux%u)`), not
keys. The only reliable source is what the addon passes to `Set` slot 1.

# 4. Confining the model to a box

Rewrite the subrect quadruple on **Color, Depth, MVec and Output**, applying the same *fractional*
region computed from each plane's own dimensions — the guides run at a third of colour resolution
(`1019x1039` against `3060x3120`). The snippet rejects the evaluation **silently** if Color and
Output disagree (`Invalid Color/Output rect configuration`). Round dimensions down to a multiple of
eight.

The addon's single-slot cache is undisturbed: it checks its cache and sets parameters *before*
calling evaluate, so it always sees full-size values.

# 5. Per-eye placement: mirrored offset, and the parity trap

**Do not derive the offset from projection matrices.** OpenXR-Toolkit's fixed-foveated path uses a
hardcoded mirrored horizontal offset, and that is the right model:

```cpp
float xOffset = m_gazeOffset[2].x + 0.04f;
m_gazeLocation[0] = gaze[0] + XrVector2f{ xOffset, ... };   // left  +
m_gazeLocation[1] = gaze[1] + XrVector2f{-xOffset, ... };   // right -
```

**4% of eye width, opposite signs per eye**, confirmed correct here. Expressing it in pixels invited
a test at 21 px against a 2544 px eye — 0.8%, five times too small to align anything. Keep it a
fraction. A projection-derived variant was built and removed: it produced the same value for both
eyes and merely slid everything sideways.

**The parity trap.** Eye identity came from a counter reset on every present. It never advanced past
zero, so every evaluation was treated as the same eye and the offset was applied with the *same*
sign to both — one eye correctly placed, the other displaced backwards. The symptom was one eye
looking almost unlit, which read convincingly as "NR only reaches one eye" and was written up as
such. **Turning foveation off and seeing both eyes match disproved it in one minute.** Derive parity
from a counter that flips on **every evaluation**: correct whether the addon evaluates once per
frame alternating eyes, or twice per frame.

# 6. The periphery: bands only, never the box

`DLSSNR.Output` is persistent and blitted back **whole**, so a box leaves the outside showing
whatever NR left there earlier — a frozen border, not an un-denoised one. Seed it from colour before
NR runs.

Copy **only the four bands outside the box**, with `CopyTextureRegion`. Copying the whole resource
means that if the addon evaluates both eyes and composites afterwards, the second copy wipes the
first eye's neural rendering. Band copies cannot overwrite NR's output and move far less memory.

D3D12 cannot be asked a resource's current state, so `UNORDERED_ACCESS` is an assumption — what NGX
asks for and what the addon's inline resources are created as. It holds in both games tested.

# 7. `DLSSNR.ControlMask` is a SUPPRESSION mask

Undocumented, unused by the addon, and worth recording precisely:

| ramp | centre | edge | observed |
|---|---|---|---|
| bright centre | 1.0 | 0.0 | **all** neural rendering removed |
| inverted | 0.0 | 1.0 | box returns, edge softened |

So the mask suppresses where it is bright. A feather is therefore **clear in the middle, opaque at
the box edge**. Leaving `DLSSNR.UseAutoMask` enabled alongside a supplied mask tanks frame rate —
NR computes its automatic mask as well. Turn it off while supplying one.

The mask gates the **effect, not the compute**: cost is unchanged inside the box.

# 8. Diagnostic discipline, learned the hard way

* **Silent early returns cost two runs.** A function with four unlogged exits made "the copy did not
  help" indistinguishable from "the copy never ran". Every gate announces itself.
* **Sampling period must not alias the eye.** Logging every 600th evaluation, with evaluations
  alternating between eyes, sampled one eye forever. Use an odd period.
* **One-shot reporting freezes a transient as the verdict.** The Set recorder only populates from the
  frame *after* installation, so the first attempt legitimately finds nothing.
* **Read back what you write.** `apply 15000: wrote (992,1016) 1064x1088 -- read back OK` is what
  proved the subrect path healthy and moved the investigation downstream.
* **Frame-rate impressions across runs are not a measurement.** Comparisons were made across
  different render targets (`3060x3120` vs `3060x2160`) and box sizes. Bucket present-to-present time
  by the toggle, inside one run, and exclude frames where NR is not evaluating.

# What transfers to the CyberpunkVR port

The [CyberpunkVR Port](https://github.com/satyaloka93/cyberpunk-vr-port) reaches exactly one eye, permanently, because MAIN and
VRCAM render into separate resources and the addon's cache holds one slot.

* **The caller check is beatable.** That bundle's "permanently disabled (state -4)" conclusion should
  be revised: the census is reachable through a tail-jump thunk. This is proven, not conjecture.
* **Resources are interceptable at `Set` slot 1**, so the port can see and *substitute* the pointers
  the addon passes.
* **Therefore a proxy-resource scheme is worth trying.** Substitute one persistent resource for both
  views so the cache sees a single stable key, copy the real eye's content in before evaluate, and
  copy the result back at the start of the next evaluation. Each eye's result lands one evaluation
  later. **Untested** — but it attacks the single-slot cache directly rather than working around it,
  which nothing previously could.

# Open

* The box edge is still faintly visible; feather width is being tuned.
* Whether the proxy-resource scheme above actually defeats the single-slot cache.

# Citations

[1] [RenoDX](https://github.com/clshortfuse/renodx) — framework only; this addon's source is not public.
[2] [OpenXR-Toolkit `vrs.cpp`](https://github.com/mbucchia/OpenXR-Toolkit) — source of the mirrored 4% offset and the elliptical two-ring pattern.
[3] NVIDIA NGX `nvsdk_ngx_params.h` — the documented vtable order that section 2 contradicts.
