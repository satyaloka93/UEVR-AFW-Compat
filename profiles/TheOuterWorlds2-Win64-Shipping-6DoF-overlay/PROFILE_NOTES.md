# The Outer Worlds 2 — Steam 6DoF profile overlay

This overlay upgrades the TOW2 3DoF profile distributed with
`afw-beta4-compat-v0.1.0-alpha.3`. It adds true right-controller weapon
translation, optional physical downstroke melee, and a fixed UEVR framework
controller-ray mouse.

## Required backend

This profile requires UEVR-AFW-Compat source commit `217162d7` or later. The
alpha.3 backend does **not** contain explicit late-component enrollment, so
installing these scripts with the old backend leaves the weapon at 3DoF.

The backend correction is executable-scoped to
`TheOuterWorlds2-Win64-Shipping.exe`. It enrolls only a scene component that a
caller explicitly requests after game-thread, module-vtable, exact
`FUObjectArray` identity and bounded class-hierarchy validation. It does not
broaden global AddObject scanning.

## Installation

1. Back up `%APPDATA%\UnrealVRMod\TheOuterWorlds2-Win64-Shipping`.
2. Begin with the alpha.3 TOW2 profile or another known-good copy. Do not modify
   the preserved `_3dof` restore profile.
3. Copy this overlay's `scripts/` files into the active profile's `scripts/`
   directory.
4. Apply every assignment in `REQUIRED_CONFIG.txt` to the active `config.txt`.
   Do not replace the complete config with `REQUIRED_CONFIG.txt`.
5. Keep `userscriptex.dll` disabled and start a fresh game process.

The overlay intentionally contains no runtime logs, caches, dumps, saves,
UObjectHook address-derived state, backup scripts, disabled diagnostics or
third-party base-profile libraries.

## Rendering lifecycle

Native-first remains the safe launch policy:

```ini
VR_RenderingMethod=0
VR_GhostingFix=false
VR_GhostingFixBootstrapViewStates=false
VR_AFW_FramewarpMode=2
VR_AFW_FixMovingObjectBrightnessFlickering=false
```

After gameplay and weapon tracking are stable, Previous Frame AFW can be
selected manually. Do not switch AFW back to Native in the same process;
PDAFW has no teardown API. Restart the game to leave AFW.

The alpha.3 TOW2 profile shipped its already-active proven AFW state. This
overlay does not force either rendering mode; preserve one isolated renderer
variable per test.

## Included scripts

### `93_tow2_attach.lua`

Follows `pawn.FPVMesh.AttachChildren` without scanning all UObjects, detects a
new skeletal weapon component, and requests a right-hand motion-controller
state with calibrated offsets and `permanent=true`. The backend enrollment fix
turns the formerly skipped `exists=false` component into a tracked object.
Expected first-equip/swap telemetry:

```text
[TOW2] Enrolled explicit motion-controller component missed by guarded AddObject: ...
[tow2attach] ATTACHED ... hand=1
```

Do not detach the component from its game-owned parent. Detachment and
non-permanent attachment were runtime-rejected.

### `05_tow2_melee_swing.lua`

Optional physical melee that preserves the native attack system:

- manual right trigger remains unchanged;
- only a weapon whose gameplay category contains `Melee` is eligible;
- a calibrated committed OpenXR negative-Y downstroke pulses native right
  trigger once;
- the game remains responsible for animation, stamina, cooldown, collision,
  perks, damage and hit detection;
- ranged weapons are blocked;
- menus/conversations suppress swings;
- right grip immediately cancels and suppresses synthetic melee so grenades
  keep their dedicated control.

The in-framework `TOW2 Melee Swing` panel can disable the gesture while leaving
manual attacks intact.

### `06_tow2_framework_menu_aim_capture.lua`

Produces the SHf gold-standard UEVR menu behavior: the stage-space framework
stays fixed and the right-controller ray moves the mouse horizontally,
vertically and diagonally. TOW2 normally uses right-controller aim method `2`,
so the script temporarily selects game aim method `0` only while the framework
is drawing, then restores and saves the captured prior method after close.

A simple `set_aim_allowed(false)` candidate was insufficient because
OverlayComponent restored aim later in the same pre-ImGui frame.

## Runtime validation completed

- late first-person weapon enrollment changed `exists=false` to `exists=true`;
- visible weapon translation followed the right controller;
- swapped weapon components enrolled and attached;
- action-based 30-second tracking dropout was removed by the long inactivity
  timeout;
- manual melee and calibrated physical downstroke independently invoked native
  attacks;
- ranged weapon and right-grip suppression worked;
- three UEVR framework open/close cycles kept the menu stationary, restored
  method `2`, preserved it in `config.txt`, and produced no Lua exceptions;
- Previous Frame AFW continued submitting successfully during the menu test.

Longer inventory, death/load, level-transition and cross-game backend regression
remain required before promoting a new binary release beyond experimental
prerelease status.
