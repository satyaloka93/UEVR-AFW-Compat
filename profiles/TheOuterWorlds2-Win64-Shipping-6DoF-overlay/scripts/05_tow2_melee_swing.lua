-- TOW2 optional physical swing -> native melee attack.
-- This does not implement damage or collision. A qualified right-hand swing
-- briefly pulses the same XInput right trigger used by manual native attacks.

local api = uevr.api
local vr = uevr.params.vr
local funcs = uevr.params.functions

local TAG = "[tow2swing] "
local SETTINGS_FILE = "tow2_melee_swing.txt"
local GRIP_ACTION_PATH = "/actions/default/in/Grip"

-- Calibrated from this profile/controller pair.
local START_SPEED_MPS = 0.35
local END_SPEED_MPS = 0.25
local ATTACK_SPEED_MPS = 1.50
local ATTACK_SMOOTHED_SPEED_MPS = 1.00
local MIN_TRAVEL_M = 0.16
local ATTACK_DOWNWARD_SPEED_MPS = 0.80
local MIN_DOWNWARD_RATIO = 0.50
local MIN_DOWNWARD_TRAVEL_M = 0.10
local REARM_QUIET_SECONDS = 0.15
local COOLDOWN_SECONDS = 0.65
local TRIGGER_PULSE_SECONDS = 0.10
local MAX_VALID_SPEED_MPS = 10.0
local SMOOTHING = 0.35

local XINPUT_GAMEPAD_START = 0x0010
local XINPUT_GAMEPAD_BACK = 0x0020
local XINPUT_GAMEPAD_B = 0x2000
local XINPUT_GAMEPAD_RIGHT_SHOULDER = 0x0200

local enabled = true
local pose_position = UEVR_Vector3f.new()
local pose_rotation = UEVR_Quaternionf.new()
local previous_position = nil
local smoothed_speed = 0.0
local travel = 0.0
local downward_travel = 0.0
local quiet_time = 0.0
local cooldown = 0.0
local pulse_remaining = 0.0
local armed = true
local grip_action = nil
local right_grip_down = false
local manual_trigger_down = false
local menu_guard = false
local previous_menu_buttons = 0
local current_weapon_address = "nil"
local current_weapon_is_melee = false
local current_weapon_tag = ""
local last_speed = 0.0
local last_downward_speed = 0.0
local last_trigger_speed = 0.0

local function log(message)
    pcall(function() funcs.log_warn(TAG .. tostring(message)) end)
end

local function safe(fn)
    local ok, value = pcall(fn)
    if ok then return value end
    return nil
end

local function load_settings()
    local text = safe(function() return fs.read(SETTINGS_FILE) end)
    if text == nil then return end
    local value = text:match("enabled=([^\r\n]+)")
    if value ~= nil then enabled = value ~= "false" end
end

local function save_settings()
    pcall(function() fs.write(SETTINGS_FILE, "enabled=" .. tostring(enabled) .. "\n") end)
end

local function object_address(object)
    if object == nil then return "nil" end
    local value = safe(function() return object:get_address() end)
    return value ~= nil and tostring(value) or tostring(object)
end

local function reset_motion(rearm)
    previous_position = nil
    smoothed_speed = 0.0
    travel = 0.0
    downward_travel = 0.0
    quiet_time = 0.0
    last_speed = 0.0
    last_downward_speed = 0.0
    if rearm then armed = true end
end

local function get_current_weapon()
    local pawn = safe(function() return api:get_local_pawn(0) end)
    if pawn == nil then return nil end
    local has_getter = safe(function() return pawn.GetCurrentWeapon ~= nil end)
    if not has_getter then return nil end
    return safe(function() return pawn:GetCurrentWeapon() end)
end

local function update_weapon()
    local weapon = get_current_weapon()
    local address = object_address(weapon)
    if address == current_weapon_address then return weapon end

    current_weapon_address = address
    current_weapon_is_melee = false
    current_weapon_tag = ""
    pulse_remaining = 0.0
    reset_motion(true)

    if weapon == nil then
        log("weapon=nil; swing attacks blocked")
        return nil
    end

    local category = safe(function() return weapon.WeaponCategory end)
    local tag = category ~= nil and safe(function() return category.TagName end) or nil
    current_weapon_tag = tag ~= nil and tostring(tag) or ""
    current_weapon_is_melee = string.find(current_weapon_tag, "Melee", 1, true) ~= nil

    local name = safe(function() return weapon:get_full_name() end) or address
    log("weapon=" .. tostring(name)
        .. " tag=" .. (current_weapon_tag ~= "" and current_weapon_tag or "<unavailable>")
        .. " melee=" .. tostring(current_weapon_is_melee))

    return weapon
end

local function get_right_grip()
    if grip_action == nil then
        grip_action = safe(function() return vr.get_action_handle(GRIP_ACTION_PATH) end)
    end
    if grip_action == nil then return false end

    local source = safe(function() return vr.get_right_joystick_source() end)
    if source == nil then return false end
    return safe(function() return vr.is_action_active(grip_action, source) end) == true
end

local function gameplay_allowed()
    if not enabled or not current_weapon_is_melee or menu_guard then return false end
    if right_grip_down or manual_trigger_down then return false end
    if safe(function() return funcs.is_drawing_ui() end) == true then return false end
    if safe(function() return vr.is_using_controllers() end) ~= true then return false end

    -- This profile changes aim method to 0 for conversations and some 2D states.
    local aim_method = safe(function() return vr.get_aim_method() end)
    if tonumber(aim_method) == 0 then return false end

    return true
end

load_settings()

uevr.sdk.callbacks.on_xinput_get_state(function(retval, user_index, state)
    if state == nil or state.Gamepad == nil then return end

    local gamepad = state.Gamepad
    local original_trigger = tonumber(gamepad.bRightTrigger) or 0
    local buttons = tonumber(gamepad.wButtons) or 0
    manual_trigger_down = original_trigger > 30

    -- Right grip is reserved for grenades. Cancel even if the OpenXR grip cache
    -- has not yet updated on this engine tick.
    if (buttons & XINPUT_GAMEPAD_RIGHT_SHOULDER) ~= 0 then
        right_grip_down = true
        pulse_remaining = 0.0
    end

    local menu_buttons = buttons & (XINPUT_GAMEPAD_START | XINPUT_GAMEPAD_BACK | XINPUT_GAMEPAD_B)
    local pressed = menu_buttons & (~previous_menu_buttons)
    if (pressed & (XINPUT_GAMEPAD_START | XINPUT_GAMEPAD_BACK)) ~= 0 then
        menu_guard = not menu_guard
        pulse_remaining = 0.0
        reset_motion(true)
        log("menu guard=" .. tostring(menu_guard))
    elseif menu_guard and (pressed & XINPUT_GAMEPAD_B) ~= 0 then
        menu_guard = false
        reset_motion(true)
        log("menu guard=false")
    end
    previous_menu_buttons = menu_buttons

    -- Preserve the physical trigger exactly. Only raise it during our short pulse.
    if enabled and pulse_remaining > 0.0 and not right_grip_down then
        gamepad.bRightTrigger = math.max(original_trigger, 255)
    end
end)

uevr.sdk.callbacks.on_pre_engine_tick(function(engine, delta_time)
    local dt = tonumber(delta_time) or 0.0
    if dt <= 0.0 or dt > 0.1 then
        pulse_remaining = 0.0
        reset_motion(true)
        return
    end

    cooldown = math.max(0.0, cooldown - dt)
    pulse_remaining = math.max(0.0, pulse_remaining - dt)
    right_grip_down = get_right_grip()
    if right_grip_down then pulse_remaining = 0.0 end

    update_weapon()

    local right_index = safe(function() return vr.get_right_controller_index() end)
    if right_index == nil or right_index < 0 then
        reset_motion(true)
        return
    end

    local pose_ok = pcall(function()
        vr.get_pose(right_index, pose_position, pose_rotation)
    end)
    if not pose_ok then
        reset_motion(true)
        return
    end

    local current = {
        x = tonumber(pose_position.x) or 0.0,
        y = tonumber(pose_position.y) or 0.0,
        z = tonumber(pose_position.z) or 0.0
    }

    if previous_position == nil then
        previous_position = current
        return
    end

    local dx = current.x - previous_position.x
    local dy = current.y - previous_position.y
    local dz = current.z - previous_position.z
    previous_position = current

    local distance = math.sqrt(dx * dx + dy * dy + dz * dz)
    local speed = distance / dt
    last_speed = speed

    if speed > MAX_VALID_SPEED_MPS then
        log(string.format("ignored pose discontinuity speed=%.3f_mps", speed))
        reset_motion(true)
        return
    end

    smoothed_speed = smoothed_speed + SMOOTHING * (speed - smoothed_speed)

    local downward_speed = math.max(0.0, -dy / dt)
    local downward_ratio = speed > 0.0 and downward_speed / speed or 0.0
    last_downward_speed = downward_speed

    if not gameplay_allowed() then
        pulse_remaining = 0.0
        travel = 0.0
        downward_travel = 0.0
        quiet_time = 0.0
        armed = true
        return
    end

    if speed >= START_SPEED_MPS then
        travel = travel + distance
        quiet_time = 0.0

        -- OpenXR tracking space is Y-up. An upward wind-up resets committed
        -- downward travel so back/up motion cannot qualify a later sample.
        if dy < 0.0 then
            downward_travel = downward_travel - dy
        elseif dy > 0.0 then
            downward_travel = 0.0
        end
    elseif speed < END_SPEED_MPS then
        quiet_time = quiet_time + dt
        if quiet_time >= REARM_QUIET_SECONDS then
            travel = 0.0
            downward_travel = 0.0
            armed = true
        end
    end

    if armed
        and cooldown <= 0.0
        and travel >= MIN_TRAVEL_M
        and downward_travel >= MIN_DOWNWARD_TRAVEL_M
        and speed >= ATTACK_SPEED_MPS
        and smoothed_speed >= ATTACK_SMOOTHED_SPEED_MPS
        and downward_speed >= ATTACK_DOWNWARD_SPEED_MPS
        and downward_ratio >= MIN_DOWNWARD_RATIO then
        armed = false
        cooldown = COOLDOWN_SECONDS
        pulse_remaining = TRIGGER_PULSE_SECONDS
        last_trigger_speed = speed
        log(string.format(
            "ATTACK downstroke speed=%.3f_mps down=%.3f_mps ratio=%.2f travel=%.3f_m down_travel=%.3f_m",
            speed,
            downward_speed,
            downward_ratio,
            travel,
            downward_travel))
    end
end)

uevr.lua.add_script_panel("TOW2 Melee Swing", function()
    local changed, new_value = imgui.checkbox("Enable physical swing attacks", enabled)
    if changed then
        enabled = new_value
        pulse_remaining = 0.0
        reset_motion(true)
        save_settings()
        log("enabled=" .. tostring(enabled))
    end

    imgui.text(string.format("Speed: %.2f m/s; downward: %.2f m/s", last_speed, last_downward_speed))
    imgui.text(string.format("Downstroke threshold: %.2f m/s + %.2f m downward travel", ATTACK_SPEED_MPS, MIN_DOWNWARD_TRAVEL_M))
    imgui.text(string.format("Last attack speed: %.2f m/s", last_trigger_speed))
    imgui.text("Only committed downward strokes attack. Manual trigger remains native.")
    imgui.text("Right grip always suppresses swings.")
end)

log("loaded enabled=" .. tostring(enabled)
    .. " downstroke_speed=" .. tostring(ATTACK_SPEED_MPS)
    .. " travel=" .. tostring(MIN_TRAVEL_M)
    .. " down_travel=" .. tostring(MIN_DOWNWARD_TRAVEL_M)
    .. " cooldown=" .. tostring(COOLDOWN_SECONDS))
