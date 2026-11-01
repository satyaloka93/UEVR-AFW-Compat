---
okf_version: '0.1'
---

# UEVR Game-Fix Knowledge Bundle

Distilled knowledge from the local UEVR fork work: per-game working states,
the fixes that produced them, and the playbooks for analysing new problems.

* [Getting started](getting-started.md) — bundle map and reading order.

## Games

* [Avowed](games/avowed.md) — working 6DoF/AFW state and crafting attachment-lifetime guard.
* [The Outer Worlds 2](games/outer-worlds-2.md) — safe Native startup, beta.4 Previous Frame AFW zero-ghosting checkpoint, committed Steam weapon enrollment, packaged 6DoF overlay, runtime-confirmed PSVR2 adaptive triggers, and an experimental melee grip-haptics candidate.
* [Hogwarts Legacy](games/hogwarts-legacy.md) — transition-cooldown crash fix.
* [Silent Hill f](games/silent-hill-f.md) — alpha.4 publishes the maintained rebuilt first-person/6DoF profile and the f37 plus fail-closed SceneView backend; decisive guard-path and broader cross-game validation remain open.
* [Silent Hill 2](games/silent-hill-2.md) — forced-Native, Native-Stereo-Fix-on renderer checkpoint plus a separately preserved full plugin/IK first-person profile; AFW remains unsafe.

## Fixes

* [Fix index](fixes/index.md) — 24 reusable engine/runtime/profile fixes, including SH2 Native-first startup, SHf bootstrap/SceneView hardening, Lua shutdown lifetime protection, stale attachments, TOW2 title/enrollment/FMalloc/PSVR2-haptics work, and AFW beta.4 guards.

## Playbooks

* [Stable 6DoF profile creation](playbooks/basic-6dof-setup.md) — native attachment, dynamic enrollment, Lua/IK selection and cross-game lifetime validation.
* [Adding PSVR2 haptics to a new game](playbooks/psvr2-haptics-integration.md) — reuse the bridge/relay/protocol; write a game Lua producer that classifies weapons, detects fire (incl. hold-to-charge), and delivers felt rhythm effects; tune with identity logging and an in-game editor.
* [UObject creation hook discovery](playbooks/uobject-creation-hook-discovery.md) — static method and tool to find the real object-creation hooks when UEVR's AddObject heuristic misses.
* [Staged fix methodology](playbooks/staged-fix-methodology.md) — the core loop.
* [Log-signature triage](playbooks/log-signature-triage.md) — symptom → fix table.
* [Crash dump analysis](playbooks/crash-dump-analysis.md) — minidump symbolization.
* [In-process hang dump](playbooks/in-process-hang-dump.md) — elevated live-stall capture.
* [RenderDoc capture](playbooks/renderdoc-capture.md) — GPU-frame diagnosis.
* [Checkpoint and recovery](playbooks/checkpoint-and-recovery.md) — pin working states by hash.
* [OKF maintenance](playbooks/okf-maintenance.md) — consumption/maintenance audit rules; canonical concepts stay current rather than becoming worklogs.

## Projects (ongoing)

* [PureDark AFW integration](projects/puredark-afw-integration.md) — beta.4 game-compatibility checkpoints, including the shipped SH2/Avowed/TOW2 unified branch, exact hashes and remaining validation.
* [CyberpunkVR Port reference](projects/cyberpunk-vr-port.md) — source knowledge for semantic weapon effects, melee swing/impact feedback and the sole-actuator rule.
* [PSVR2Toolkit reference](projects/psvr2toolkit.md) — native Sense actuator API and dual-purpose Cyberpunk/TOW2 haptics bridge.

## Decisions

* [Narrow port scope](decisions/narrow-port-scope.md) — cross-fork porting rules.

* [SteamVR runtime queue experiment](projects/steamvr-runtime-queue.md) — removed from source and installed backend; historical results and rollback.

* [SteamVR frame-wait overlap experiment](projects/steamvr-frame-wait.md) — retired and removed from deployed backend after worse motion smoothness; evidence and rollback.

* [TOW2 pacing diagnostics](projects/tow2-pacing-diagnostics.md) — deployed per-frame cadence, CPU stages and AFW matrix/pose provenance on original scheduling.
