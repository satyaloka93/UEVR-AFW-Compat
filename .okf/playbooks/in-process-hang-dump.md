---
type: playbook
title: In-process minidumps for elevated UEVR stalls
description: When an elevated game stalls without crashing and external ProcDump is denied, add a temporary executable-scoped watchdog that calls MiniDumpWriteDump in-process after Presents stop.
tags:
- debugging
- hang
- minidump
- windows
- tow2
timestamp: '2026-07-20T18:54:40+09:00'
---

# When to use

Use this only after logs establish a **stall**, not a crash: Present telemetry
stops while hook-monitor/background logs continue, and no WER or Unreal dump
exists. External `procdump` may fail with `Access is denied (0x80070005)` when
the injected game is elevated.

# Temporary diagnostic pattern

Add a one-shot, executable-scoped path in the existing hook monitor:

1. Track the last successful Present.
2. If no Present occurs for at least five seconds while the window-message hook
   remains intact, capture once rather than rehooking D3D.
3. Dynamically load `dbghelp.dll` and resolve `MiniDumpWriteDump`.
4. Write `MiniDumpNormal | MiniDumpWithThreadInfo |
   MiniDumpWithUnloadedModules` to the game's persistent profile directory.
5. Log start/completion, preserve the exact backend/PDB/dump/log together, and
   remove the diagnostic after the failure is understood.

A dump from a live stall records every thread at the point of deadlock, spin,
or invalid traversal even though no exception generated a crash dump.

# Analysis

Use matching symbols and inspect all threads, not only the debugger-selected
thread:

```powershell
cdb.exe -z tow2_title_hang.dmp `
  -y "srv*C:\symbols*https://msdl.microsoft.com/download/symbols;C:\path\to\matching\pdb" `
  -c "~* kb; q"
```

Then apply the address/PDB rules in
[Windows minidump analysis](crash-dump-analysis.md).

# TOW2 result

The first TOW2 in-process dump identified the GameThread inside
`UObjectHook::add_new_object()` / `sdk::UStruct::get_super_struct()`, leading to
[dynamic AddObject validation](../fixes/tow2-addobject-candidate-guard.md).
External ProcDump had been denied.

A later `tow2_title_hang.dmp` watchdog was reintroduced while investigating an
intermittent analyzer race. The diagnostic-layout build entered gameplay and
therefore emitted no dump. This is important: adding diagnostics changes binary
layout and timing. A successful diagnostic run does **not** prove the watchdog
fixed anything; preserve both binaries and continue testing.

# Safety rules

- Scope by executable and capture only once per process.
- Do not leave full-memory dumps enabled permanently; they can be large and may
  contain sensitive process data.
- Avoid capturing normal long loading/minimized intervals.
- Never symbolize against a merely similar PDB; use SHA-256-pinned artifacts
  from [checkpointing and recovery](checkpoint-and-recovery.md).

# Citations

- `<evidence-root>/puredark-tow2-injection-only-20260719/press-any-key-hang-analysis`
- `src/Framework.cpp`
