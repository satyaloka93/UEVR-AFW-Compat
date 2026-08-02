---
type: overview
title: UEVR game-fix knowledge — start here
description: Map of this bundle — per-game working states, the engine fixes that made them work, and the playbooks for diagnosing new problems.
tags:
- getting-started
- uevr
timestamp: '2026-08-02T18:17:40+09:00'
---

# What this bundle is

Portable, sanitized knowledge distilled from baseline UEVR stabilization and
the PureDark AFW compatibility effort: which fixes worked per game, which
experiments were rejected and why, and the repeatable methods used to analyse
crashes, stalls, stereo problems, and frame-warp artifacts. Private workstation
paths and raw evidence are intentionally represented by placeholders rather
than published.

# Reading order

1. **Game states** — what works today and the exact config for it:
   - [/games/avowed.md](games/avowed.md)
   - [/games/outer-worlds-2.md](games/outer-worlds-2.md)
   - [/games/hogwarts-legacy.md](games/hogwarts-legacy.md)
   - [/games/silent-hill-f.md](games/silent-hill-f.md)
   - [/games/silent-hill-2.md](games/silent-hill-2.md)
2. **Fixes** — the individual engine/runtime corrections, each reusable when a
   new game shows the same symptom: see [/fixes/index.md](fixes/index.md).
3. **Playbooks** — how to analyse a new problem:
   - [/playbooks/basic-6dof-setup.md](playbooks/basic-6dof-setup.md) — build and stabilize camera/weapon/hand 6DoF, including late-component enrollment
   - [/playbooks/staged-fix-methodology.md](playbooks/staged-fix-methodology.md) — the core loop
   - [/playbooks/log-signature-triage.md](playbooks/log-signature-triage.md) — read the log first
   - [/playbooks/crash-dump-analysis.md](playbooks/crash-dump-analysis.md) — when it crashes
   - [/playbooks/in-process-hang-dump.md](playbooks/in-process-hang-dump.md) — when it freezes and external dump capture is denied
   - [/playbooks/renderdoc-capture.md](playbooks/renderdoc-capture.md) — when it renders wrong
   - [/playbooks/checkpoint-and-recovery.md](playbooks/checkpoint-and-recovery.md) — before you change anything
   - [/playbooks/okf-maintenance.md](playbooks/okf-maintenance.md) — how to consume/amend this bundle without turning it into a worklog
4. **Decisions** — standing rules:
   - [/decisions/narrow-port-scope.md](decisions/narrow-port-scope.md)

# The three recurring failure domains

Almost every game problem so far landed in one of these:

| Domain | Typical symptom | Start with |
|---|---|---|
| Stereo hook discovery | fails to hook, one eye black, crash at startup | [/fixes/stereo-vtable-scan-widening.md](fixes/stereo-vtable-scan-widening.md), [/fixes/render-target-validation-hardening.md](fixes/render-target-validation-hardening.md) |
| Native stereo stability | crash after load/world transition, right-eye corruption | [/fixes/native-stereo-safe-activation.md](fixes/native-stereo-safe-activation.md) |
| Present/OpenXR pipeline | title-screen stall, frozen frame counter, `XR_ERROR_TIME_INVALID` | [/fixes/tow2-backbuffer-fallback-openxr-prep.md](fixes/tow2-backbuffer-fallback-openxr-prep.md), [/fixes/openxr-authoritative-wait-frame.md](fixes/openxr-authoritative-wait-frame.md) |
| AFW cold-start ownership | saved AFW profile creates missing/double-wide swapchain hybrid | [/fixes/sh2-shproto-afw-cold-start-native-force.md](fixes/sh2-shproto-afw-cold-start-native-force.md) |
| UE5.7 injection/bootstrap | repeated UI/render-target setup, null FRHI vtable, OpenXR frame opens before a valid submit | [/fixes/shf-ue57-openxr-bootstrap.md](fixes/shf-ue57-openxr-bootstrap.md) |
| UObject discovery | title/game-thread freeze, unsafe AddObject candidates | [/fixes/tow2-addobject-candidate-guard.md](fixes/tow2-addobject-candidate-guard.md), [/playbooks/in-process-hang-dump.md](playbooks/in-process-hang-dump.md) |
| 6DoF attachment lifetime | raw pose works but weapon remains 3DoF, swaps lose attachment, reload causes stale-pointer crashes | [/playbooks/basic-6dof-setup.md](playbooks/basic-6dof-setup.md), [/fixes/tow2-explicit-component-enrollment.md](fixes/tow2-explicit-component-enrollment.md), [/fixes/avowed-stale-attachment-guard.md](fixes/avowed-stale-attachment-guard.md) |
| AFW reconstruction | weapons/moving objects trail despite stable eye ownership | [/fixes/afw-beta4-motion-vector-scale.md](fixes/afw-beta4-motion-vector-scale.md) |

# Citations

- `TOW2_UEVR_FIX_SUMMARY.md`, `TOW2_UEVR_WORKLOG.md` (repo root)
- `AVOWED_UEVR_UPDATE.md`, `AVOWED_UEVR_UPDATE_SHORT.md`, `AVOWED_DEBUG_LOG.md`, `AVOWED_CHECKPOINT_STATE.md`
- `Hogwarts_Project.md`
- Private investigation worklogs `PUREDARK_AFW_AVOWED_WORKLOG.md` and `PUREDARK_AFW_TOW2_WORKLOG.md`; public repository file `RENDERDOC_CAPTURE_GUIDE.md`
