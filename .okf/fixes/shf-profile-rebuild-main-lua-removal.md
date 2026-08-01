---
type: fix
title: SHf profile rebuild — replacing main.lua with minimal gameplay wiring
description: The inherited SHf first-person profile crashed repeatedly on the post-update exe; the fix was to delete main.lua (a monolith that pulled the crashy library stack in only to build developer/config panels) and re-implement its ~80 lines of real gameplay wiring, plus a settle gate and reload hardening, in three standalone scripts.
tags:
- silent-hill-f
- shf
- lua
- profile
- uobjecthook
- attachments
- crash
timestamp: '2026-08-01T11:30:00+09:00'
---

# Symptom

The inherited first-person profile (`SHf-Win64-Shipping-LETMEIN`, preserved
unmodified) crashed constantly on the 2026-07-23 patched exe — at load, on
save/reload, on examine interactions, on weapon re-equip. It also crashed a
third party's AFW build, so it was never backend-specific. A bare profile ran
flawlessly at ~90 fps in AFW, proving the game and backend were fine.

# What the crashes actually were

UE's own crash dumps (`%LOCALAPPDATA%\SHf\Saved\Crashes\`) were the only usable
evidence — UE's handler catches the AV, so UEVR and WER never produced dumps.
Stacks consistently showed
`engine_tick_hook → PluginLoader::on_pre_engine_tick → ScriptContext::on_pre_engine_tick → Lua → game code`,
faulting in **game** code. UE's captured register context was unreliable
(small integers where object pointers were required), so individual call sites
could not be identified from dumps alone.

Two concrete crash causes were isolated and fixed directly:

1. **Full GUObjectArray walks from tick context.** `shf.lua`'s lantern search
   used `find_first_of` → `get_first_object_by_class`. It crashed during
   level-load churn *and* in settled gameplay on lantern re-equip. Disabled
   outright (cosmetic loss: lantern cutscene visibility enforcement).
2. **Stale cross-level references.** After a save/reload, `melee` still held
   the previous level's IK body mesh; its swing detection derives hand velocity
   from that mesh, so destroyed-object reads produced wild deltas that
   registered as attacks from thumbstick input. Cleared on pawn change.

# The fix: delete main.lua

`main.lua` (332 lines) hard-required `montage`, `uevr_dev`, `reticule`, `ui`,
`configui`, `hands` and more — almost entirely to construct developer/config
panels which it then explicitly hid. Bisection showed `shf` + `melee` alone was
stable while adding `main.lua`'s stack crashed. Because the requires are
unconditional, no feature of it could be loaded selectively.

It was replaced by three scripts (load order matters: LuaLoader uses NTFS
upcased order, where digits precede letters — a `_` prefix sorts LAST):

| Script | Role |
|---|---|
| `00_settle.lua` | Settle gate + pawn tracking; publishes `_G.SHF_SETTLED` and `_G.SHF_PAWN_ADDR` |
| `91_button_swap.lua` | X↔B (dodge) remap, lifted from `main.lua`; no requires, no object access |
| `92_core_init.lua` | The real gameplay wiring, ~80 lines (see below) |

`92_core_init.lua` keeps: `pawn`/`attachments`/`input`/`ik` init, the IK-mesh
callback feeding `shf` and `melee`, and the attachments grip callback returning
`weapon.WeaponMesh` + IK mesh + `"hand_r"`. **That grip callback is what makes
the hand close around the weapon** — it drives a per-weapon grip pose. It drops:
`montage.init`, `uevrDev.init`, `hands.enableConfigurationTool`, and all
`configui` panels (melee keeps its saved `data/shf_melee_config.json` values;
only the in-game sliders are lost).

# The settle gate

Crashes clustered ~0.2 s after a pawn appeared. The gate suppresses script tick
work until the pawn has been stable for 4 s, re-closing whenever the pawn
address changes (covering level loads, save loads and respawns).

**Critical implementation note:** `uevr.sdk.callbacks` is a **C++ usertype**.
Assigning to its fields *reports success* but reads still return the original
binding — so an earlier gate implemented by wrapping registrations was inert for
many test cycles while appearing to work. The working gate publishes a global
flag that each callback checks as its first statement, applied by direct file
edit to `shf.lua`, `melee.lua`, and `uevr_utils.lua`'s `preEngineTick`/
`postEngineTick` dispatch.

# UEVR Lua sandbox constraints (learned the hard way)

- `io.open` rejects absolute paths and `..`; relative paths resolve under
  `<profile>/data/`.
- The `debug` library is not exposed.
- Lua 5.4 rejects `string.format` widths wider than two digits (`%-100s`).
- `print()` does not reach `log.txt` at this profile's log level;
  `uevr.params.functions.log_warn` does.
- Motion-controller offsets require real usertypes — `Vector3f.new(x,y,z)`;
  plain Lua tables raise `Invalid type for set_location_offset`. A `Vector3f`
  passed as a rotation is interpreted as euler degrees.

# Rejected approaches

- **Module stubbing** (`package.loaded[name] = noop_table`) as a bisection
  tool: live code (`interaction`/`montage` → `libs/ui`) then received `nil`
  where objects were expected, and its introduction lines up exactly with a new
  crash-17-seconds-after-launch pattern. Park whole top-level scripts instead.
- **Native UObjectHook weapon attachment** (`get_or_add_motion_controller_state`):
  works and is simpler, but cannot reproduce the closing-hand grip pose, so the
  hand stays open around the weapon. Kept as
  `90_weapon_attach.lua.disabled` with all 14 weapons' calibration baked in.
- **Per-dispatch breadcrumb instrumentation**: useful once (it identified the
  dispatch hub and proved the inert gate), but even disabled it built
  `tostring(entry.func)` plus three strings per callback per tick. Removed.

# Result

No crashes across extended play, scene transitions, weapon swaps, and
save/exit/reload cycles. In AFW at 80% resolution the framerate averages ~72
but swings widely (lows ~45, highs ~89) — a bare profile holds ~90. The gap and
the instability are the inherent cost of IK solving, roomscale camera writes and
attachment updates every frame, not leftover debug code; the swing also tracks
the game's dynamic resolution scaler hunting under variable script load.
Frame-pacing work is open, not attempted.

# Known gaps / open work

- **Firearms are untested.** All 14 calibration entries and the grip wiring are
  melee weapons; SHf gun handling has not been exercised at all. Note that
  `libs/reticule` was among the libraries dropped with `main.lua` — aiming
  feedback may need it (or a standalone replacement) before guns are usable.
  Re-adding it means the same care: it must not reintroduce the crashy stack.
- **Frame pacing.** 45–89 fps swings; a fixed resolution scale instead of the
  game's dynamic scaler is the first thing to try.
- `hands.lua` remains parked; hands work without it (IK arrives via `shf.lua`).

See [Silent Hill f](../games/silent-hill-f.md) and
[basic 6DoF setup](../playbooks/basic-6dof-setup.md).
