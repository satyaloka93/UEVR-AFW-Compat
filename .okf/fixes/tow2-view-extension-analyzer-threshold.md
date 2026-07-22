---
type: fix
title: TOW2 view-extension analyzer timing threshold
description: PureDark's generic 50-confirmation view-extension detector races TOW2's title transition; a TOW2-only threshold of 40 preserves the offset and frame-delta checks but remains timing-sensitive across launches.
tags:
- tow2
- stereo-hook
- analyzer
- startup
- timing-race
timestamp: '2026-07-20T18:54:40+09:00'
resource: src/mods/vr/FFakeStereoRenderingHook.cpp
---

# Problem

PureDark's fake-stereo analyzer normally requires 50 matching samples before it
installs `BeginRenderViewFamily`. TOW2's title path can stop issuing the needed
callbacks first. Early captures repeatedly reached 48 stable A3 confirmations
at frame-count offset `0x94`, then Presents stopped and the title remained on
"waiting".

# Narrow correction

For TOW2 only, require 40 confirmations instead of 50 while retaining all other
validation:

- monotonic frame-count progression;
- the same candidate offset in A2 and A3;
- frame delta no greater than 3;
- all other games keep the generic threshold of 50.

Expected success lines:

```text
Found final frame count offset at 94 after 40 confirmations
Done setting up BeginRenderViewFamily hook!
```

# Current confidence and limitation

The threshold has completed successfully in multiple runs, including the
beta.4 zero-ghosting run. It is **not yet a deterministic title fix**. An exact
known-good binary/configuration later advanced only one title frame and froze;
a subsequent diagnostic-layout build completed all 40 samples and entered the
game. Another failed log showed A2 continuing while the lower-frequency A3
candidates did not reach the required count.

Treat this as a timing race, not a configuration or AFW-startup failure. Do not
lower the threshold further without a fresh dump/log proving which candidate
stopped and preserving the existing identity checks. Keep the one-shot
[in-process hang dump](../playbooks/in-process-hang-dump.md) available until
several clean launches establish reliability.

# Distinguish the second title failure

Completing analyzer discovery does not protect against unsafe UObject
construction hooks. A separate running dump identified TOW2's changing
AddObject argument layout; see
[dynamic AddObject candidate validation](tow2-addobject-candidate-guard.md).
Both fixes are required.

# Related

- [The Outer Worlds 2 state](../games/outer-worlds-2.md)
- [PureDark AFW integration](../projects/puredark-afw-integration.md)

# Citations

- `log.puredark_native_press_any_key_after_40_20260719_181440.txt`
- `<evidence-root>/puredark-beta4-tow2-zero-ghosting-success-20260720/profile/log.txt`
- `<private-worklog-root>/PUREDARK_AFW_TOW2_WORKLOG.md`
