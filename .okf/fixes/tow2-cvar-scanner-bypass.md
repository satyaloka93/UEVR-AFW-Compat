---
type: fix
title: TOW2 CVar scanner/freeze bypass (the decisive playability fix)
description: Post-update TOW2 stalls at title splash because UEVR's reflected CVar scanner and direct CVar reads/writes in VR::update_hmd_state() touch fragile addresses; bypassing both for TOW2 made the game playable. Safe CVars go through console exec instead.
tags:
- tow2
- cvar
- stall
timestamp: '2026-07-20T00:00:00Z'
resource: src/mods/vr/CVarManager.cpp
---

# Symptom

Title splash renders, then the game sits "waiting" forever. No crash, no dump —
a stall. See [/playbooks/log-signature-triage.md](/playbooks/log-signature-triage.md)
for how this was distinguished from a crash.

# Fix

Executable-scoped to TOW2:

- Bypass the CVarManager reflected scanner/freeze path entirely.
- Bypass direct CVar reads/writes in `VR::update_hmd_state()` — **this was the
  fix that ended the title-splash-then-waiting stall.**
- Keep `user_script.txt` working through UE console exec (`UEngine::Exec`) so
  safe profile CVars (like the VSM override) still apply.

# Caveats discovered later (AFW port)

- On the PureDark AFW baseline the TOW2 executable could not resolve
  `UEngine::Exec`; the required override moved to the game's `Engine.ini`.
- Third-party CVar tools are equally dangerous here: UserScriptExpress's
  reflected scanner produced `Prevented corruption of memory` warnings and was
  disabled during diagnosis.

# Principle

When a game stalls at title with UEVR attached, suspect **anything that scans
or pokes reflected engine memory during startup** before suspecting the render
pipeline. Related: [/games/outer-worlds-2.md](/games/outer-worlds-2.md).

# Citations

- `TOW2_UEVR_FIX_SUMMARY.md`, `TOW2_UEVR_WORKLOG.md` stages 4+
- `PUREDARK_AFW_TOW2_WORKLOG.md`
