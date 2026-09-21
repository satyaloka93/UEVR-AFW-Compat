---
type: fix
title: Hosting a newer RenoDX addon — the ImGui table, the ReShade object model, and where it stalled
description: What the UEVR host had to implement for renodx-dlss5 4.7 (versioned ImGui function tables, the real api_object/device_object vtable layouts, register-safe stubs), and the one unresolved blocker — its workset pool never recycles because the queue it tracks is not the one the game submits on.
tags:
- reshade
- addon-host
- imgui
- dlss
- neural-rendering
- d3d12
timestamp: '2026-09-02T19:40:00+09:00'
resource: src/mods/DlssNeuralRendering.cpp
---

Extends [DLSS 5 Neural Rendering addon host](dlss5-neural-rendering-addon-host.md). Addon **4.1.5**
works fully. Addon **4.7** (HDR fix, `NRToggleKey`/`NRScreenshotKey`, tone controls) draws its whole
settings page after the work below, but **neural rendering never contributes**, so 4.1.5 is the
installed build. 4.7 is retained beside it as `renodx-dlss5.addon64.v47`.

Everything here is host-side and benefits any addon version. None of it touches the foveation work
in [Foveated DLSS 5 Neural Rendering](dlss5-foveated-neural-rendering.md).

# 1. The ImGui function table is versioned, and the version was being ignored

ReShade ships **one table layout per ImGui version** — `imgui_function_table_18600`, `_18971`,
`_19000`, `_19040`, `_19250` — and the addon names the one it wants in
`ReShadeGetImGuiFunctionTable(version)`. The host recorded that argument from the first version and
never acted on it, returning one fixed table regardless.

4.7 asks for **19250**, which has **421 members**.

**PARSE THE HEADER; DO NOT READ A SUMMARY OF IT.** Two attempts used indices from a summarised
reading and both were wrong in the same fatal way — placing a void function where a struct-returning
one lives:

| taken from summary | slot | actually is |
|---|---|---|
| `Separator` | 96 | `PushID3` |
| `SliderFloat` | 186 | `TreePop` |
| `GetKeyName` | 405 | `ImDrawList_PathArcToFast` |

Those "fixes" broke a table that had been correct, and cost two runs. The layout originally measured
by logging widget labels **is** 19250:

```text
Separator 80   TextUnformatted 103   TextV 104   Button 111
Checkbox 115   Combo 129 / 130       SliderFloat 144
GetContentRegionAvail 72   PushID 94-97   PopID 98   GetKeyName 324
```

# 2. Stubs must zero BOTH return registers

A C++ stub returning `uint64_t` sets RAX and leaves XMM0 holding whatever the last call left there.
ImGui's layout queries return `ImVec2` **in XMM0**, so those members handed the addon garbage
dimensions. No C++ signature can zero both, so each unimplemented slot gets a hand-assembled thunk:

```asm
mov   rax, &g_last_imgui_slot   ; records which member was entered
mov   dword ptr [rax], <slot>
xor   eax, eax                  ; ints/bools -> 0, false = "unchanged"
xorps xmm0, xmm0                ; floats/ImVec2 -> 0, (0,0)
ret
```

Recording the slot matters as much as the zeroing: a shared anonymous stub is safe but leaves a
fault unattributable, which cost two more runs.

# 3. 4.7 draws a custom toggle widget

Not a checkbox. The call sequence, decoded against the parsed table:

```text
 12  GetWindowDrawList          <- returning 0 here is what crashed the page
113  InvisibleButton            the hit area
287  IsItemHovered   288 IsItemActive
 70  GetCursorScreenPos    92 GetFrameHeight    66 GetColorU32
389  ImDrawList_AddCircleFilled   404 ImDrawList_PathArcTo
```

It asks for the window's draw list and draws the knob into it. Thirty-two members are now forwarded
to the real ImGui, including nine `ImDrawList_*` calls, each null-guarded.

# 4. The ReShade object model, from the published headers

The host presented device, queue, swapchain and effect_runtime objects whose **every** vtable slot
returned the same native pointer. That satisfied 4.1.5, which the disassembly showed never calls a
method on them. 4.7 does. Parsed layouts:

```text
api_object      0 get_native   1 get_private_data   2 set_private_data
device_object   : api_object, then 3 get_device
device          : api_object, then 3 get_api, 4 check_capability, ...
command_queue   : device_object, then 4 get_type, 5 wait_idle, ... 12 get_timestamp_frequency
swapchain       : device_object, then 4 get_back_buffer, 5 count, 6 current index
effect_runtime  : device_object, then 4 get_back_buffer, ...
```

`get_device()` is **slot 3**, and the uniform fakes returned the *queue* there — so 4.7 treated a raw
`ID3D12CommandQueue` as a `reshade::api::device`. Also implemented: real storage for
`get_private_data`/`set_private_data`, since addons attach per-object state through them and garbage
is worse than nothing. `get_api` reports `d3d12` (`0xc000`), `get_type` reports `graphics` (`0x1`).

# 5. Where it stalls: the workset pool never recycles

With the object model in place the pool initialises, which it never did before:

```text
GPU-safe NR workset pool active: up to 4 scratch generations; exact queue fences
recycle only completed submissions
... ~7 seconds later ...
NR workset pool exhausted; preserving game output for this evaluation
```

"Preserving game output" means NR evaluates and contributes nothing — the failure is silent and
looks like healthy rendering. The addon's own strings describe the mechanism:

> `native D3D12 queue submission tracker installed for GPU-safe NR workset recycling`
> `failed to install native D3D12 queue submission tracker; NR pool will fail closed when all worksets are busy`

Neither line appears. The pool starts, all four scratch generations go busy, and nothing retires.

**Leading hypothesis, untested:** it recycles by watching `ExecuteCommandLists` on the queue the host
exposes, and that is UEVR's captured queue — `D3D12Hook::get_command_queue()` — which need not be the
one TOW2 submits its render work on. A tracker watching a queue that never sees those submissions
would behave exactly like this.

Next step if resumed: identify the queue the NR command lists are actually executed on and expose
that as the ReShade `command_queue`. The command-list vtable interception already proven for the
foveation work is a reasonable place to observe it.

# Also worth knowing about 4.7

* `EnableHooks` defaults to **2** — NGX hooks only, Streamline modules left unpatched. `1` adds
  Streamline hooks and the addon itself warns it is a contested patch site. Not the cause here: NGX
  capture worked and feature 18 was created.
* Hotkeys are live from load — **F6** toggles NR, **F5** takes a screenshot pair — independent of the
  settings page.
* Every setting is served from the host's own store, so a title can be tuned by editing
  `reshade-addon-config.ini` and relaunching even when the addon's page cannot be drawn.
* The host now also exports `ReShadeGetBasePath`, which 4.7 requires and 4.1.5 does not.

# Citations

[1] [ReShade](https://github.com/crosire/reshade) — `include/reshade_api.hpp`,
    `include/reshade_api_device.hpp`, `source/imgui_function_table_19250.hpp`. Parsed, not summarised.
