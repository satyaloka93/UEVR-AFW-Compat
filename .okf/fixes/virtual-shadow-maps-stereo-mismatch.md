---
type: fix
title: Virtual Shadow Maps cause per-eye shadow/background mismatch
description: In UE5 native stereo, Virtual Shadow Maps render inconsistently between eyes; r.Shadow.Virtual.Enable=0 fixes the mismatch. r.Lumen.VSM and r.HZBOcclusion are innocent and can stay enabled.
tags:
- ue5
- shadows
- vsm
- cvar
- stereo
timestamp: '2026-07-20T00:00:00Z'
---

# Symptom

In native stereo, shadows and background elements differ between the left and
right eye — a visual "eye mismatch" rather than a crash. Observed in The Outer
Worlds 2 ([/games/outer-worlds-2.md](/games/outer-worlds-2.md)).

# Root cause and fix

Isolated by CVar bisection to **Virtual Shadow Maps**:

```text
r.Shadow.Virtual.Enable 0
```

Explicitly cleared of blame (can be re-enabled without reproducing the issue):

- `r.Lumen.VSM 1`
- `r.HZBOcclusion 1`

# Delivery mechanisms, in preference order

1. `user_script.txt` via UE console exec — normal path.
2. Game `Engine.ini` — fallback when the executable can't resolve
   `UEngine::Exec` (AFW-branch TOW2; INI backed up first).

Do **not** ship broad INI shadow/culling "diagnostic" settings — they worsened
stereo visuals in TOW2 and were reverted.

# Reuse guidance

Any UE5 title with per-eye shadow/lighting mismatch in native stereo: try
`r.Shadow.Virtual.Enable=0` first, and bisect one CVar at a time before
blaming the stereo pipeline.

# Citations

- `TOW2_UEVR_FIX_SUMMARY.md` — "Resolved visual issue"
