---
type: fix
title: TOW2 FMalloc discovery failure and unbounded system-memory growth
description: TOW2 exposes an uppercase Binned2 allocator marker that the maintained case-sensitive UESDK scanner misses; the runtime then cannot free engine-owned temporary arrays, matching Praydog's narrow FMalloc memory-leak fix.
resource: dependencies/submodules/UESDK/src/sdk/FMalloc.cpp
tags:
- tow2
- memory-leak
- uesdk
- fmalloc
- uobjecthook
- afw
timestamp: '2026-08-08T19:35:00+09:00'
---

# Symptom

A TOW2 Native-to-Previous-Frame-AFW run grew to approximately 30 GB of system
RAM in under ten minutes and was stopped while memory was still rising. The
runtime log contained the decisive discovery failure:

```text
[FMalloc::get] Finding GMalloc...
[FMalloc::get] Failed to find GMalloc
```

The same run initialized frame warping exactly once and then reused three
command-list slots. It did not repeatedly call `InitFrameWarp`, so an AFW
resource-reinitialization loop is not the leading explanation. PureDark CAS was
absent and is not implicated.

# Upstream correlation

Praydog UEVR commit `74b76bc` is titled `Deps: Update UESDK (FMalloc/Memory leak
fix)` and advances UESDK from `cfa7516` to `f37f61c`. The UESDK commit changes
only `FMalloc.cpp`: it adds canonical-case allocator names such as `Binned2`
and then appends lowercase variants for the case-sensitive string scanner.

TOW2's executable contains both the `Binned2` marker and FMallocBinned2
diagnostic strings. The maintained custom UESDK branch is based before
`f37f61c`; its scanner includes lowercase `binned2` but not uppercase
`Binned2`, and the runtime confirms that discovery fails.

When `FMalloc::get()` is unavailable, UESDK `TArray` destruction cannot call the
game allocator's `free()`. Repeated engine-returned temporary arrays can
therefore accumulate. UObjectHook and profile-driven reflection make TOW2 a
credible high-rate trigger.

# Narrow candidate

Only UESDK commit `f37f61c` was applied onto the maintained cache branch; no
other current UESDK or Praydog changes were merged. The patch applies cleanly
to the pinned `FMalloc.cpp`, and a separately identified Release backend built
successfully with provenance `70f6e508+local-uesdk-f37f61c`. It is deployed
only to the experimental directory with matching symbols; PDAFW, profiles and
the stable deployment were not replaced. Candidate hashes are:

```text
UEVRBackend.dll  ca825f277df9486cd3b482ccd0ff5ee187f59300ad165e1c6a9564ba8289732b
UEVRBackend.pdb  c0e2d5d37dbcc3e6c64481b9aa7be55b5bc92bb2addc60a77a1ba821b1f5ccf9
```

The fresh-process A/B must:

1. verify startup changes from `Failed to find GMalloc` to `Found GMalloc
   "Binned2"` plus valid Malloc/Realloc/Free indices;
2. remain Native for a fixed interval and record process private bytes;
3. switch Native to Previous Frame AFW once and record the same metric;
4. stop immediately if memory growth remains unbounded;
5. regression-test TOW2 6DoF/enrollment and Avowed/SHf before replacing the
   maintained backend.

# Runtime validation

The candidate passed its first sustained TOW2 run:

- `Found GMalloc "Binned2"` at vtable index 24;
- Malloc, Realloc and Free resolved at indices 5, 7 and 9;
- no `Failed to find GMalloc` or FMalloc failure remained;
- AFW initialized exactly once and ran for approximately 35 minutes;
- no OOM, GPU crash, device-removed/hung or Lua-error signature occurred;
- the user observed no continuing memory leak.

The unpatched run had reached approximately 30 GB and was still growing in less
than ten minutes. The patched run therefore remained healthy for roughly four
times the prior failure interval. Automatic private-byte telemetry had timed
out before this later test, so a future measured run would improve the record,
but the allocator signatures, duration and observed result are sufficient to
call the TOW2 leak fixed by this candidate. Alpha.4 publishes the correction as
an experimental prerelease. Broader cross-game regression remains required
before stable promotion.

SHf does not show a deterministic f37 regression, but it still blocks clean
promotion because its pre-existing `FSceneViewFamily` startup race remains
intermittent. Two candidate runs crashed before `FMalloc::get()` executed; a
third run with authoritative f37 provenance and the retained cold-AFW profile
ran for about 213 seconds without a crash. The successful run did not fall
through to the fatal guessed scene-interface offset `0x10`. This isolates the
failures from allocator behavior while leaving the SceneView race unresolved.
The intended pre-f37 A/B did not occur because the successful run loaded the
main f37 deployment. The later combined f37 plus SHf SceneView-guard backend
also completed an approximately 35-minute SHf run without allocator, GPU or
runtime-watchdog failure. That is useful broad regression evidence, but neither
SceneView guard signature appeared, so the intermittent constructor path still
needs decisive validation.

After the run, the active TOW2 profile was reset to Native startup with both
Ghosting Fix toggles off. Keep both off for the current known-good TOW2 startup
and switch only Native to Previous Frame AFW after gameplay. Any Ghosting Fix
toggle test must be a separately isolated variable.

# Publication

UESDK commit `62471af` carries the case-variant correction on the maintained
cache branch; parent backend commit `b64dacc8` advances the submodule and ships
it in alpha.4. The release remains a prerelease because measured private-byte
telemetry and broader cross-game regression are still incomplete.

# Relationships

- Game state: [The Outer Worlds 2](../games/outer-worlds-2.md)
- 6DoF enrollment: [TOW2 explicit component enrollment](tow2-explicit-component-enrollment.md)
- Porting policy: [Narrow port scope](../decisions/narrow-port-scope.md)
