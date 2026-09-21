---
type: diagnostic
title: Native OpenXR GPU copy and CPU pacing attribution
description: Sample final-copy GPU work without added waits and correlate it with fresh Cheeky timing records.
timestamp: 2026-09-19
tags: [openxr, d3d12, performance, diagnostics]
---

The [timestamp guard](openxr-authoritative-wait-frame.md) accepted 6,036/6,036
recorded Native submissions in the first live run, but both late Cheeky-on and
off sections stayed near 45 submissions/s. Whole-run GPU 12.774 ms exceeded
the 90 Hz budget. Instrument cost before assuming further submission faults.

## Measurements

`[OpenXR copy timing]` in UEVR log reports separate swapchain IDs:
GPU mean/max and sample count, plus CPU acquire, image-wait and existing
command-context-wait means. A sample encloses the complete final-copy command
list including barriers and any UI/draw callbacks; it is **not total UEVR GPU
time**, engine rendering time, or queue latency. No-sample mean is -1, not 0.
Normal-success CPU means are per completed copy; recovery/skip intervals are
not controlled benchmark samples. Timer failure count is cumulative per
swapchain lifetime. Approximate five-second windows reset on swapchain rebuild.

`CopyGpuTimer.hpp` samples every eighth eligible per-image copy, reuses two
timestamp queries and a 16-byte readback, and polls the existing submission
fence before reading. No additional Signal/Wait, flush, or blocking readback.
Query resources live with TextureContext and are reset after commands retire.
Initialization failures disable that timer rather than retrying every frame.
Instrumentation applies only when `is_using_afr()` is false; AFW/AFR/Sequential
and OpenVR are not modified. Existing runtime waits remain in place.

Cheeky's maintained first-path addon now logs `[Cheeky cost]` every five
seconds and on observed SR/NR enable transitions, including menu toggles.
Reports contain its existing native/foveated/peripheral GPU means with
published sample counts and age, plus submission/completion/failure counters.
These are per sampled evaluation (often an eye), **not stereo-frame totals**.
Means may be old after toggling: require advancing sample counts and fresh age.
Do not sum possibly nested scopes or subtract them from a whole-session
SteamVR average to invent a precise game-only cost.

## Verification and scope

Windows Release backend and addon builds pass. Standalone hardware test
`scripts/test-copy-gpu-timer.vcxproj` verifies a positive timestamp span over
a 16 MiB copy, sample cadence, pending-slot exclusion and one-shot collection.
The test waits explicitly; production does not. In-game timing availability
and overhead still require validation. No graphics/profile settings changed.

Deployment helper `scripts/deploy-native-costs.ps1` backs up and hash-checks the
shared UEVR backend/PDB and TOW2 addon. It archives previous UEVR, Cheeky,
ReShade and compositor logs. It does not deploy an AFW runtime or change SHf's
addon, ReShade configuration, eye tracking, resolution, or 6DoF.

Deployed and destination-hash verified at `20260919-193838` (addon backup
stamp `193839`). Backend DLL SHA256
`0bc16a870c9b9929e08fed921827538658a556fe6688ada909123ffc5b841ca8`, PDB
`319f7b21ac0bc28b8d263fb80774c25b5f64fb9a5f87e13f33ca359948412d8c`,
addon `e0e69edea5fd650112befe006f5041dd08fa62cbdb45b6910192008ef63a373f`.
Prior logs are in `diagnostics/native-openxr-20260919-193838/`; prior binaries
are adjacent timestamped backups. Cheeky focused `--cost-diagnostics`,
`--afw-core`, `--d3d12-history`, `--d3d12-composite`, and `--present-lock`
passed. The no-argument full suite stalled without output and was stopped;
this matches an existing documented issue but is not a full-suite pass.

## Citations

## First instrumented game result (2026-09-19 19:40–19:42)

TOW2 PID 36740: 4,976/4,976 recorded submissions accepted, four repairs,
zero time-invalid/begin-error/discarded counters. GPU copy timers report zero
failures. Cheeky logs establish enabled=0 initially, enabled=1 at 19:41:20.893,
then enabled=0 at 19:42:03.653; NR remained off.

Settled off reporting windows ending 19:40:45–19:41:10 average 44.97 accepted
submissions/s; settled on windows ending 19:41:35–19:42:00 average 44.99.
This excludes startup and toggle-associated dips, not all scene differences.
Per-copy sample-weighted means in similar intervals:

| Scope | Off | On |
| --- | --- | --- |
| Stereo image, swapchain 0 DOUBLE_WIDE | 0.068 ms | 0.063 ms |
| UI, swapchain 4 UI | 0.152 ms | 0.150 ms |

Do not reverse these swapchain labels. CPU image-acquisition waits were
roughly 0.02–0.06 ms; context reuse waits roughly 0.01–0.015 ms. The final
copy path is not a multi-millisecond bottleneck in this run. This does not
profile all UEVR hooks, scene rendering, or earlier intermediary copies.

Fresh Cheeky measurements show native SR usually ~1.26 ms per sampled
evaluation (with larger samples), foveated SR ~0.20 ms, and peripheral
~0.136 ms typically, sometimes ~0.24 ms. The latter are separate reported
scopes, not a measured complete addon total. They demonstrate cheaper
measured processing, not an equivalent reduction in stereo frame latency.

Whole-run SteamVR application CPU 12.253 ms / GPU 14.586 ms, compositor GPU
0.753 ms; 5,555/10,971 presents reprojected (50.63%). These mixed-state
averages cannot yield per-toggle GPU totals. Persistent ~45/s at a 90 Hz
target is consistent with workload/runtime cadence limits; not proof that
the runtime is unnecessarily imposing a cap. Existing VERY_LATE sync mode
remains unchanged. Next inspect game/render-thread scheduling and runtime
cadence, not optimize the already-small final-copy span or delete required
xrWaitFrame pacing. No binaries/configs changed during this assessment.

## Sources

- [D3D12 query resolution](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-resolvequerydata)
- [Queue timestamp frequency](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12commandqueue-gettimestampfrequency)
