-- TOW2 OpenXR framework-menu aim capture, candidate 2.
--
-- SHf's gold-standard fixed framework pointer uses VR_AimMethod=0 (game aim),
-- while TOW2 needs VR_AimMethod=2 (right-controller aim) during gameplay.
-- Merely calling set_aim_allowed(false) is insufficient: OverlayComponent's
-- pre-ImGui mouse-emulation pass can restore aim later in the same frame.
-- Temporarily use game aim for the whole framework session, then restore the
-- exact previous method/allowed state and save it after the framework's own
-- close-time config save. No quad transform, game-menu, button, or attachment
-- changes.

local vr = uevr.params.vr
local funcs = uevr.params.functions
local TAG = "[tow2menu] "
local GAME_AIM = 0

local captured = false
local previous_aim_method = 2
local previous_aim_allowed = true
local release_pending = false

local function safe(fn)
    local ok, value = pcall(fn)
    if ok then return value end
    return nil
end

local function log(message)
    pcall(function() funcs.log_warn(TAG .. tostring(message)) end)
end

local function force_framework_capture()
    -- The stored method is the important gate: OverlayComponent may set
    -- aim_allowed=true after this callback, but method 0 still cannot rotate the
    -- camera from the right-controller pose.
    pcall(function() vr.set_aim_method(GAME_AIM) end)
    pcall(function() vr.set_aim_allowed(false) end)
end

local function restore_gameplay_aim()
    pcall(function() vr.set_aim_method(previous_aim_method) end)
    pcall(function() vr.set_aim_allowed(previous_aim_allowed) end)

    -- Framework saves config immediately before Lua observes that it closed.
    -- Save once more after restoration so a temporary method 0 never persists.
    pcall(function() vr.save_config() end)
end

local function update_capture()
    local drawing = safe(function() return funcs.is_drawing_ui() end) == true

    if drawing then
        release_pending = false
        if not captured then
            previous_aim_allowed = safe(function() return vr.is_aim_allowed() end) ~= false

            -- get_aim_method() reports game aim whenever aim is temporarily
            -- disabled, so briefly expose the stored method before reading it.
            pcall(function() vr.set_aim_allowed(true) end)
            local old_method = safe(function() return vr.get_aim_method() end)
            if type(old_method) == "number" then
                previous_aim_method = old_method
            end

            captured = true
            log("captured framework aim; method=" .. tostring(previous_aim_method)
                .. " allowed=" .. tostring(previous_aim_allowed))
        end
        force_framework_capture()
        return
    end

    if captured and not release_pending then
        -- Hold capture through the framework's close/config-save frame.
        force_framework_capture()
        release_pending = true
        return
    end

    if captured and release_pending then
        restore_gameplay_aim()
        captured = false
        release_pending = false
        log("released framework aim; method=" .. tostring(previous_aim_method)
            .. " allowed=" .. tostring(previous_aim_allowed))
    end
end

uevr.sdk.callbacks.on_pre_engine_tick(function(engine, delta)
    update_capture()
end)

-- Reassert the stored method after OverlayComponent's pre-ImGui input pass.
uevr.sdk.callbacks.on_draw_ui(function()
    if captured then force_framework_capture() end
end)

log("loaded; framework uses temporary game aim")
