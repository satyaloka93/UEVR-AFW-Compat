---
okf_version: '0.1'
---

# UEVR Game-Fix Knowledge Bundle

Distilled knowledge from the local UEVR fork work: per-game working states,
the fixes that produced them, and the playbooks for analysing new problems.

* [Getting started](getting-started.md) — bundle map and reading order.

## Games

* [Avowed](games/avowed.md) — working 6DoF/AFW state and crafting attachment-lifetime guard.
* [The Outer Worlds 2](games/outer-worlds-2.md) — safe Native startup and beta.4 Previous Frame AFW zero-ghosting checkpoint.
* [Hogwarts Legacy](games/hogwarts-legacy.md) — transition-cooldown crash fix.
* [Silent Hill f](games/silent-hill-f.md) — baseline UE5.7/OpenXR injection bootstrap, Native Stereo and stable UI/scene-copy lineage; AFW remains unvalidated.

## Fixes

* [Fix index](fixes/index.md) — 16 reusable engine/runtime fixes, including SHf UE5.7 bootstrap, stale attachment, TOW2 title/UObject, and AFW beta.4 guards.

## Playbooks

* [Staged fix methodology](playbooks/staged-fix-methodology.md) — the core loop.
* [Log-signature triage](playbooks/log-signature-triage.md) — symptom → fix table.
* [Crash dump analysis](playbooks/crash-dump-analysis.md) — minidump symbolization.
* [In-process hang dump](playbooks/in-process-hang-dump.md) — elevated live-stall capture.
* [RenderDoc capture](playbooks/renderdoc-capture.md) — GPU-frame diagnosis.
* [Checkpoint and recovery](playbooks/checkpoint-and-recovery.md) — pin working states by hash.
* [OKF maintenance](playbooks/okf-maintenance.md) — consumption/maintenance audit rules; canonical concepts stay current rather than becoming worklogs.

## Projects (ongoing)

* [PureDark AFW integration](projects/puredark-afw-integration.md) — beta.4 zero-ghosting result, exact hashes and remaining validation.

## Decisions

* [Narrow port scope](decisions/narrow-port-scope.md) — cross-fork porting rules.
