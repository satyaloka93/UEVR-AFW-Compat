-- 91_button_swap.lua — X (Reload) <-> B (Dodge) controller remap for SHf.
--
-- Extracted verbatim in behaviour from the profile's main.lua so the dodge
-- binding survives without loading main.lua's library stack (pawn/attachments/
-- montage/configui/uevr_dev/hands), which is where this profile's crashes live.
--
-- Self-contained: no requires, no game-object access, no per-frame allocation.
-- on_xinput_get_state only rewrites the button bitmask, so it cannot touch the
-- stale-object paths that cause the crashes.
--
-- The two _block flags prevent the swap from immediately swapping back within
-- the same poll (the classic A<->B remap feedback loop).

local XINPUT_GAMEPAD_X_BIT = 0x4000
local XINPUT_GAMEPAD_B_BIT = 0x2000

local _blockX = false
local _blockB = false

uevr.sdk.callbacks.on_xinput_get_state(function(retval, user_index, state)
    if state == nil then return end

    if state.Gamepad.wButtons & XINPUT_GAMEPAD_X_BIT ~= 0 and not _blockB then
        _blockX = true
        state.Gamepad.wButtons = state.Gamepad.wButtons & ~XINPUT_GAMEPAD_X_BIT
        state.Gamepad.wButtons = state.Gamepad.wButtons | XINPUT_GAMEPAD_B_BIT
    else
        _blockX = false
    end

    if state.Gamepad.wButtons & XINPUT_GAMEPAD_B_BIT ~= 0 and not _blockX then
        _blockB = true
        state.Gamepad.wButtons = state.Gamepad.wButtons & ~XINPUT_GAMEPAD_B_BIT
        state.Gamepad.wButtons = state.Gamepad.wButtons | XINPUT_GAMEPAD_X_BIT
    else
        _blockB = false
    end
end)

pcall(function() uevr.params.functions.log_warn("[btnswap] loaded (X <-> B)") end)
