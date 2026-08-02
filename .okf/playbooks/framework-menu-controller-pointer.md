---
type: playbook
title: Fixed UEVR framework menu with a controller-ray mouse
description: Configure an OpenXR profile so the UEVR framework stays fixed in stage space, the right-controller ray moves the mouse in both axes, and controller-driven gameplay aim is suspended only while the framework is open.
tags:
- uevr
- profile
- openxr
- menu
- controller
- mouse-emulation
- lua
timestamp: '2026-08-02T19:12:00+09:00'
---

# Gold-standard behavior

The UEVR framework quad stays fixed in space. Pointing the right controller
moves the framework mouse horizontally, vertically and diagonally without
rotating the game camera or moving the quad. Closing the framework restores the
profile's normal controller-driven gameplay aim. Silent Hill f demonstrates
this naturally because it uses game aim (`VR_AimMethod=0`); TOW2 validated the
same behavior while preserving right-controller aim during gameplay.

# Base profile configuration

```ini
UI_Framework_FollowView=false
UI_Framework_MouseEmulation=true
UI_Framework_Distance=1.750000
UI_Framework_Size=2.000000
```

`UI_Framework_FollowView=false` is essential under OpenXR. The framework quad
and controller poses then share stage space. With follow-view enabled, the quad
is in view space while controller-ray intersection is calculated from
stage-space poses, producing inconsistent pointer motion.

Distance and size are comfort values, not part of the input fix. Preserve known
good values if a profile already has them.

# Why controller-aim games need an additional capture

UEVR aim methods are:

```text
0 = game
1 = head
2 = right controller
3 = left controller
4 = two-handed right
5 = two-handed left
```

A game using `VR_AimMethod=0`, such as the validated SHf profile, already has
the desired framework behavior. A profile using controller aim, such as TOW2
with `VR_AimMethod=2`, can rotate its view while the controller is trying to
reach the framework. Because the stage-space framework transform follows
UEVR's rotation offset, this looks like the menu itself moving left and right.
Vertical mouse movement may still work, which can misleadingly suggest that
ray intersection is the only problem.

Calling only `vr.set_aim_allowed(false)` from an engine-tick Lua callback is not
enough. `OverlayComponent::update_input_mouse_emulation()` can restore aim later
in the same pre-ImGui frame, and its normal capture is conditional on already
intersecting an ImGui item. The camera can therefore rotate before the pointer
reaches an item.

# Profile-local framework-session capture

For a controller-aim profile, temporarily select game aim for the whole
framework session. Always remember and restore the actual prior method rather
than hardcoding right-controller aim.

```lua
local vr = uevr.params.vr
local funcs = uevr.params.functions
local GAME_AIM = 0

local captured = false
local release_pending = false
local previous_method = GAME_AIM
local previous_allowed = true

local function force_capture()
    vr.set_aim_method(GAME_AIM)
    vr.set_aim_allowed(false)
end

local function update()
    local drawing = funcs.is_drawing_ui() == true

    if drawing then
        release_pending = false
        if not captured then
            previous_allowed = vr.is_aim_allowed()

            -- get_aim_method() reports game aim while aim is temporarily
            -- disabled, so expose the stored method before reading it.
            vr.set_aim_allowed(true)
            previous_method = vr.get_aim_method()
            captured = true
        end
        force_capture()
        return
    end

    if captured and not release_pending then
        -- Hold through the framework's close/config-save frame.
        force_capture()
        release_pending = true
        return
    end

    if captured then
        vr.set_aim_method(previous_method)
        vr.set_aim_allowed(previous_allowed)

        -- The framework saves immediately before Lua observes closure. Save
        -- again so temporary method 0 cannot remain in config.txt.
        vr.save_config()
        captured = false
        release_pending = false
    end
end

uevr.sdk.callbacks.on_pre_engine_tick(function()
    pcall(update)
end)

-- Reassert after OverlayComponent's pre-ImGui input pass.
uevr.sdk.callbacks.on_draw_ui(function()
    if captured then pcall(force_capture) end
end)
```

Use a late numeric filename such as
`<profile>/scripts/06_framework_menu_aim_capture.lua` only after checking the
profile's existing load order. The name is not functionally significant; the
callbacks and restoration lifecycle are.

# Persistence and safety rules

- Do not permanently change a controller-aim profile to method `0`; weapon and
  camera aim would stop following the controller after the menu closes.
- Restore the captured method, not an assumed value, so head, left-controller
  and two-handed profiles remain valid.
- Hold capture through one closed-menu engine tick. This prevents the overlay's
  own intersection release from winning the close frame.
- Call `vr.save_config()` after restoration. UEVR's close-time save occurs
  before Lua observes `is_drawing_ui()==false`.
- Do not continuously move the framework transform from Lua. Stage-space
  configuration plus aim capture fixes the coordinate/input problem without
  moving the user's menu or view.
- Keep this profile-local until cross-game regression proves a generic backend
  policy. Games using method `0` do not need the workaround.

# Validation

Use a fresh process and test:

1. open the UEVR framework;
2. move the pointer horizontally, vertically and diagonally;
3. confirm the framework remains stationary;
4. click controls and scroll;
5. close the framework and confirm normal gameplay aim returns;
6. repeat open/close at least twice;
7. exit and verify `config.txt` still contains the original `VR_AimMethod`.

For TOW2 the validated lifecycle telemetry was method `2` on both capture and
release. Its successful profile combination was stage-space framework, mouse
emulation, and temporary method `0` only while the framework was drawing.

# Related

- Packaged TOW2 6DoF overlay: `profiles/TheOuterWorlds2-Win64-Shipping-6DoF-overlay/`
- [TOW2 working VR state](../games/outer-worlds-2.md)
- [Silent Hill f working VR state](../games/silent-hill-f.md)
- [Stable 6DoF profile creation](basic-6dof-setup.md)
