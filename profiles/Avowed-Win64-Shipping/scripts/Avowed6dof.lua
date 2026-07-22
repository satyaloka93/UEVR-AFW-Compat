local api = uevr.api
local vr = uevr.params.vr

local VERSION = "2.00"
local uevrUtils = require('libs/uevr_utils')
local controllers = require('libs/controllers')
local ui = require('libs/ui')

uevrUtils.setDeveloperMode(false)
controllers.setLogLevel(LogLevel.Info)
ui.init()

local config_filename = "avowed.txt"
local help_filename = "avowedhelp.txt"
local help_data = ""
local debug_type = 0 -- 0 for no debug, 1 for console, 2 for log.txt, 3 for both console and log.txt
local needs_config_write = false
disableMod = 0
local right_stick_down_b = 1
local right_stick_up_sprint = 1
local remap_lb_button = 1
local LOCK_AIM_HAND_TO_RIGHT = 1
local USE_ATTACHED_WIDGETS = 0
local USE_OLD_OBJ_HOOK_METHOD = 0
ALLOW_UOBJECTHOOK_ATTACH_FALLBACK = 1
DIRECT_WEAPON_DRIVE = 1
DIRECT_DRIVE_MOVE_OWNER_ACTOR = 1
NATIVE_BONE_DRIVER_OWNS_WEAPONS = 1
direct_drive_diag = {}
local direct_drive_index_conflict_logged = false
DIRECT_OFFSETS = {
    right = { pos = {X=0.0, Y=0.0, Z=0.0}, rot = {Pitch=0.0, Yaw=0.0, Roll=0.0} },
    left  = { pos = {X=0.0, Y=0.0, Z=0.0}, rot = {Pitch=0.0, Yaw=0.0, Roll=0.0} },
    wand = { pos = {X=0.0, Y=0.0, Z=0.0}, rot = {Pitch=-8.0, Yaw=0.0, Roll=0.0} },
    grimoire = { pos = {X=0.0, Y=0.0, Z=0.0}, rot = {Pitch=90.0, Yaw=0.0, Roll=0.0} }
}
local TOD_MORN_START = 0.00
local TOD_EVE_START = 0.55
local TOD_NIGHT_START = 0.70
local TOD_DAY_START = 0.19
local time_of_day = 0
local is_time_of_day_hooked = false

vr.set_mod_value("VR_MotionControlsInactivityTimer","9999.000000")
--remap.init()

local AttachedLowerHud = nil
local AttachedMinimap = nil
local AttachedQuests = nil
local is_hud_attached = false

-- Widget brightness adjustment
local skylight_component = nil
local last_brightness_update = 0.0
local BRIGHTNESS_UPDATE_INTERVAL = 1.0 -- Update every 1 second
local WIDGET_BRIGHTNESS = 25  -- Constant brightness multiplier
local hud_switcher_hook = false
local unsheath_hook = false
local was_left_trigger_down = false
local was_right_trigger_down = false
was_in_event = false
local is_in_menu = false
local is_in_main_menu = false
local melee_swing = true
local should_fix_weapon_spread = true
local pause_opened = false
local pause_opened_reference = 0
local pause_close_hook = false
local last_sheathed_visible = nil


local frame_counter = 0        -- Simulated frame counter (incremented each callback)
local combo_timer = 0          -- Tracks time elapsed for the combo
local left_swipe_in_progress = nil
local new_level_timer = 0.0
local new_level_hud_timer = 0.0

-- Define Constants for Return Values
local NO_SWIPE = 0
local LEFT_SWIPE = 1 -- Corresponds to Right-to-Left movement (-X direction)
local RIGHT_SWIPE = 2 -- Corresponds to Left-to-Right movement (+X direction)

-- Gesture Detection Parameters
local SWIPE_THRESHOLD = 0.2 -- Minimum distance to complete a swipe (e.g., 10cm)
local COMBO_THRESHOLD = 0.15 -- Maximum time in seconds to complete the swipe
local DELTA_X_START_THRESHOLD = 0.005 -- Minimum movement to START the swipe
local BLOCK_THRESHOLD = 0.25
local CHARGE_THRESHOLD = 0.18 -- Adjust this value (e.g., 0.35m or 35cm) for desired height
local NEW_LEVEL_THRESHOLD = 10.0
local NEW_LEVEL_HUD_THRESHOLD = 5.0
local MELEE_SWING_AND_BLOCK = 1
local SWORD_BLOCK_DEGREES_THRESHOLD = 30
local USE_SIMPLE_SWORD_BLOCK = 0
local SHIELD_BLOCK_SIZE = 0.35
local SHIELD_NORMAL_SIZE = 0.50
local NORMAL_UI_SIZE = 6.5


local PUNCH_DISTANCE_THRESHOLD = 0.2 -- Total distance required for a punch (e.g., 25 cm)
local PUNCH_START_THRESHOLD = 0.005  -- Minimum Z movement per frame to initiate the punch (must be fast)
local PUNCH_COMBO_THRESHOLD = 0.15   -- Maximum time allowed for the entire punch (e.g., 0.4 seconds)

local CHEAT_AUTO_REGEN_ESSENCE = 1
local CHEAT_AUTO_REGEN_HEALTH = 1
local CHEAT_AUTO_REGEN_PERC = 1.67
local CHEAT_GUN_SPEED_MULT = 1.0

local left_swipe_result = false
local right_swipe_result = false

-- Shared frame delta used by callbacks that don't provide a delta parameter.
delta = 1.0 / 90.0

local right_haptic_timer = 0.0
local left_haptic_timer = 0.0
local last_wand_fire_diag_t = 0.0

local current_aim_method = vr:get_mod_value("VR_AimMethod")
last_applied_aim_method = tostring(current_aim_method)
local was_in_menu = false
local prev_level = nil

local previous_left_weapon = nil
local previous_right_weapon = nil

local current_right_weapon = nil
local current_left_weapon = nil
local current_right_weapon_base = nil
local current_left_weapon_base = nil

local attached_right_weapon = nil
local attached_left_weapon = nil
local attached_right_weapon_base = nil
local attached_left_weapon_base = nil
local slow_timer = 0.0
local changed_loadout = true
local loadout_change_cooldown_s = 0.0
local LOADOUT_CHANGE_COOLDOWN_MAX = 0.35
local last_loadout_change_hook_t = 0.0
local LOADOUT_HOOK_DEBOUNCE_S = 0.90
local last_weapon_probe_t = 0.0
local WEAPON_PROBE_INTERVAL_S = 0.50
local LOADOUT_NIL_RETRY_INTERVAL_S = 0.35
last_left_nil_reprobe_t = 0.0
LEFT_NIL_REPROBE_INTERVAL_S = 0.50
local weapon_probe_nil_streak = { right = 0, left = 0 }
local last_probe_nil_log_t = 0.0
local WEAPON_PROBE_NIL_STREAK_CLEAR = 2
post_event_recovery_throttle_frames = 0
POST_EVENT_RECOVERY_THROTTLE_MAX_FRAMES = 1800
POST_EVENT_RECOVERY_THROTTLE_MOD = 10
post_event_recovery_throttle_counter = 0
controller_attach_fail_streak = { right = 0, left = 0 }
FALLBACK_FAIL_STREAK_THRESHOLD = 2
local last_visibility_refresh_t = 0.0
local VISIBILITY_REFRESH_INTERVAL_S = 0.40
old_attach_refresh_last_t = { right = 0.0, left = 0.0 }
OLD_ATTACH_REFRESH_INTERVAL_S = 1.00
local deferred_detach_queue = { pause_until_t = 0.0, death_grace_s = 2.0 }
local DEFERRED_DETACH_DELAY_S = 0.12
local DEFERRED_DETACH_TTL_S = 5.0
local DEFERRED_DETACH_BATCH = 2

local weapon_swing_hook = nil
local menu_open_hook = nil
local menu_close_hook = nil
local was_sword_block = false
local FORCE_CLEAR_TIME = 5.0
local force_clear_examinable_timer = FORCE_CLEAR_TIME

local hud_minimap_c = nil
local gamehud_hook = false


local ENABLE_LUMEN = 1

local function debugOut(message)
    if debug_type == 0 then return end
    if debug_type == 1 then 
        print(message) 
    elseif debug_type == 2 then 
        uevr.params.functions.log_info("avowed6dof.lua: " .. message) 
    elseif debug_type == 3 then
        print(message)
        uevr.params.functions.log_info("avowed6dof.lua: " .. message)
    end
end

function set_aim_method_if_needed(value)
    local value_str = tostring(value)
    if last_applied_aim_method == value_str then
        return false
    end

    vr.set_mod_value("VR_AimMethod", value_str)
    last_applied_aim_method = value_str
    return true
end

AvowedLuaPerf = {
    pre_tick_sum = 0.0,
    pre_tick_max = 0.0,
    pre_tick_samples = 0,
    xinput_sum = 0.0,
    xinput_max = 0.0,
    xinput_samples = 0,
    xinput_blocks = {
        state = { sum = 0.0, max = 0.0, samples = 0 },
        probe = { sum = 0.0, max = 0.0, samples = 0 },
        loadout = { sum = 0.0, max = 0.0, samples = 0 },
        visibility = { sum = 0.0, max = 0.0, samples = 0 },
        aim = { sum = 0.0, max = 0.0, samples = 0 },
        gesture = { sum = 0.0, max = 0.0, samples = 0 }
    },
    loadout_hook_count = 0,
    probe_update_count = 0,
    reattach_right_attempts = 0,
    reattach_left_attempts = 0,
    old_attach_refresh_right = 0,
    old_attach_refresh_left = 0,
    attach_failures = 0,
    fallback_old_attach_count = 0,
    visibility_refreshes = 0,
    swap_hide_previous_count = 0,
    left_trigger_frames = 0,
    left_trigger_menu_returns = 0,
    left_trigger_event_returns = 0,
    left_trigger_levelwait_returns = 0,
    left_trigger_non_grimoire_frames = 0,
    left_grimoire_candidate_frames = 0,
    left_grimoire_trigger_frames = 0,
    right_trigger_frames = 0,
    right_trigger_menu_returns = 0,
    right_trigger_event_returns = 0,
    right_trigger_levelwait_returns = 0,
    right_trigger_other_frames = 0,
    right_wand_candidate_frames = 0,
    right_wand_trigger_frames = 0,
    right_grimoire_candidate_frames = 0,
    right_grimoire_trigger_frames = 0,
    right_bow_candidate_frames = 0,
    right_bow_trigger_frames = 0,
    post_event_recoveries = 0,
    post_event_probe_skips = 0,
    post_event_loadout_skips = 0,
    loadout_swap_button_frames = 0,
    loadout_swap_button_releases = 0,
    loadout_hook_after_swap_input = 0,
    loadout_swap_to_hook_sum_ms = 0.0,
    loadout_swap_to_hook_max_ms = 0.0,
    right_trigger_recent_loadout_frames = 0,
    right_trigger_other_recent_loadout_frames = 0,
    right_trigger_wand_attach_mismatch_frames = 0,
    right_trigger_other_recent_event_frames = 0,
    right_trigger_other_recent_sheathed_frames = 0,
    right_trigger_other_recent_2h_frames = 0,
    right_trigger_other_probe_spike_frames = 0,
    right_trigger_other_loadout_spike_frames = 0,
    right_trigger_other_xinput_spike_frames = 0,
    bow_frames = 0,
    wand_frames = 0,
    melee_frames = 0,
    last_log = 0.0
}

function avowed_lua_perf_avg(sum, count)
    if count == nil or count <= 0 then
        return 0.0
    end

    return sum / count
end

function avowed_lua_perf_record_pre_tick(ms)
    AvowedLuaPerf.pre_tick_sum = AvowedLuaPerf.pre_tick_sum + ms
    AvowedLuaPerf.pre_tick_samples = AvowedLuaPerf.pre_tick_samples + 1
    if ms > AvowedLuaPerf.pre_tick_max then
        AvowedLuaPerf.pre_tick_max = ms
    end
end

function avowed_lua_perf_record_xinput(ms)
    AvowedLuaPerf.xinput_sum = AvowedLuaPerf.xinput_sum + ms
    AvowedLuaPerf.xinput_samples = AvowedLuaPerf.xinput_samples + 1
    if ms > AvowedLuaPerf.xinput_max then
        AvowedLuaPerf.xinput_max = ms
    end
end

function avowed_lua_perf_record_xinput_block(block_name, ms)
    local block = nil

    if AvowedLuaPerf ~= nil and AvowedLuaPerf.xinput_blocks ~= nil then
        block = AvowedLuaPerf.xinput_blocks[block_name]
    end

    if block == nil then
        return
    end

    block.sum = block.sum + ms
    block.samples = block.samples + 1
    if ms > block.max then
        block.max = ms
    end
end

function avowed_lua_perf_reset_xinput_blocks()
    if AvowedLuaPerf == nil or AvowedLuaPerf.xinput_blocks == nil then
        return
    end

    for _, block in pairs(AvowedLuaPerf.xinput_blocks) do
        block.sum = 0.0
        block.max = 0.0
        block.samples = 0
    end
end

function avowed_lua_perf_log_if_needed()
    local now_t = os.clock()

    if AvowedLuaPerf.last_log == 0.0 then
        AvowedLuaPerf.last_log = now_t
        return
    end

    if (now_t - AvowedLuaPerf.last_log) < 5.0 then
        return
    end

    uevr.params.functions.log_info(string.format(
        "[AvowedLuaPerf] enabled=%d old_attach=%d direct=%d bone_driver_owns_weapons=%d avg_pre_tick_ms=%.3f max_pre_tick_ms=%.3f avg_xinput_ms=%.3f max_xinput_ms=%.3f avg_xinput_state_ms=%.3f max_xinput_state_ms=%.3f avg_xinput_probe_ms=%.3f max_xinput_probe_ms=%.3f avg_xinput_loadout_ms=%.3f max_xinput_loadout_ms=%.3f avg_xinput_visibility_ms=%.3f max_xinput_visibility_ms=%.3f avg_xinput_aim_ms=%.3f max_xinput_aim_ms=%.3f avg_xinput_gesture_ms=%.3f max_xinput_gesture_ms=%.3f loadout_hooks=%d probe_updates=%d reattach_right_attempts=%d reattach_left_attempts=%d old_refresh_right=%d old_refresh_left=%d attach_failures=%d fallback_old_attach=%d visibility_refreshes=%d swap_hide_previous=%d left_trigger_frames=%d left_trigger_menu_returns=%d left_trigger_event_returns=%d left_trigger_levelwait_returns=%d left_trigger_non_grimoire_frames=%d left_grimoire_candidate_frames=%d left_grimoire_trigger_frames=%d right_trigger_frames=%d right_trigger_menu_returns=%d right_trigger_event_returns=%d right_trigger_levelwait_returns=%d right_trigger_other_frames=%d right_wand_candidate_frames=%d right_wand_trigger_frames=%d right_grimoire_candidate_frames=%d right_grimoire_trigger_frames=%d right_bow_candidate_frames=%d right_bow_trigger_frames=%d post_event_recoveries=%d post_event_probe_skips=%d post_event_loadout_skips=%d loadout_swap_button_frames=%d loadout_swap_button_releases=%d loadout_hook_after_swap_input=%d avg_loadout_swap_to_hook_ms=%.3f max_loadout_swap_to_hook_ms=%.3f right_trigger_recent_loadout_frames=%d right_trigger_other_recent_loadout_frames=%d right_trigger_wand_attach_mismatch_frames=%d bow_frames=%d wand_frames=%d melee_frames=%d",
        disableMod == 0 and 1 or 0,
        USE_OLD_OBJ_HOOK_METHOD,
        DIRECT_WEAPON_DRIVE,
        NATIVE_BONE_DRIVER_OWNS_WEAPONS,
        avowed_lua_perf_avg(AvowedLuaPerf.pre_tick_sum, AvowedLuaPerf.pre_tick_samples),
        AvowedLuaPerf.pre_tick_max,
        avowed_lua_perf_avg(AvowedLuaPerf.xinput_sum, AvowedLuaPerf.xinput_samples),
        AvowedLuaPerf.xinput_max,
        avowed_lua_perf_avg(AvowedLuaPerf.xinput_blocks.state.sum, AvowedLuaPerf.xinput_blocks.state.samples),
        AvowedLuaPerf.xinput_blocks.state.max,
        avowed_lua_perf_avg(AvowedLuaPerf.xinput_blocks.probe.sum, AvowedLuaPerf.xinput_blocks.probe.samples),
        AvowedLuaPerf.xinput_blocks.probe.max,
        avowed_lua_perf_avg(AvowedLuaPerf.xinput_blocks.loadout.sum, AvowedLuaPerf.xinput_blocks.loadout.samples),
        AvowedLuaPerf.xinput_blocks.loadout.max,
        avowed_lua_perf_avg(AvowedLuaPerf.xinput_blocks.visibility.sum, AvowedLuaPerf.xinput_blocks.visibility.samples),
        AvowedLuaPerf.xinput_blocks.visibility.max,
        avowed_lua_perf_avg(AvowedLuaPerf.xinput_blocks.aim.sum, AvowedLuaPerf.xinput_blocks.aim.samples),
        AvowedLuaPerf.xinput_blocks.aim.max,
        avowed_lua_perf_avg(AvowedLuaPerf.xinput_blocks.gesture.sum, AvowedLuaPerf.xinput_blocks.gesture.samples),
        AvowedLuaPerf.xinput_blocks.gesture.max,
        AvowedLuaPerf.loadout_hook_count,
        AvowedLuaPerf.probe_update_count,
        AvowedLuaPerf.reattach_right_attempts,
        AvowedLuaPerf.reattach_left_attempts,
        AvowedLuaPerf.old_attach_refresh_right,
        AvowedLuaPerf.old_attach_refresh_left,
        AvowedLuaPerf.attach_failures,
        AvowedLuaPerf.fallback_old_attach_count,
        AvowedLuaPerf.visibility_refreshes,
        AvowedLuaPerf.swap_hide_previous_count,
        AvowedLuaPerf.left_trigger_frames,
        AvowedLuaPerf.left_trigger_menu_returns,
        AvowedLuaPerf.left_trigger_event_returns,
        AvowedLuaPerf.left_trigger_levelwait_returns,
        AvowedLuaPerf.left_trigger_non_grimoire_frames,
        AvowedLuaPerf.left_grimoire_candidate_frames,
        AvowedLuaPerf.left_grimoire_trigger_frames,
        AvowedLuaPerf.right_trigger_frames,
        AvowedLuaPerf.right_trigger_menu_returns,
        AvowedLuaPerf.right_trigger_event_returns,
        AvowedLuaPerf.right_trigger_levelwait_returns,
        AvowedLuaPerf.right_trigger_other_frames,
        AvowedLuaPerf.right_wand_candidate_frames,
        AvowedLuaPerf.right_wand_trigger_frames,
        AvowedLuaPerf.right_grimoire_candidate_frames,
        AvowedLuaPerf.right_grimoire_trigger_frames,
        AvowedLuaPerf.right_bow_candidate_frames,
        AvowedLuaPerf.right_bow_trigger_frames,
        AvowedLuaPerf.post_event_recoveries,
        AvowedLuaPerf.post_event_probe_skips,
        AvowedLuaPerf.post_event_loadout_skips,
        AvowedLuaPerf.loadout_swap_button_frames,
        AvowedLuaPerf.loadout_swap_button_releases,
        AvowedLuaPerf.loadout_hook_after_swap_input,
        avowed_lua_perf_avg(AvowedLuaPerf.loadout_swap_to_hook_sum_ms, AvowedLuaPerf.loadout_hook_after_swap_input),
        AvowedLuaPerf.loadout_swap_to_hook_max_ms,
        AvowedLuaPerf.right_trigger_recent_loadout_frames,
        AvowedLuaPerf.right_trigger_other_recent_loadout_frames,
        AvowedLuaPerf.right_trigger_wand_attach_mismatch_frames,
        AvowedLuaPerf.bow_frames,
        AvowedLuaPerf.wand_frames,
        AvowedLuaPerf.melee_frames
    ))

    uevr.params.functions.log_info(string.format(
        "[AvowedRTPerf] other_recent_event=%d other_recent_sheathed=%d other_recent_2h=%d other_probe_spike=%d other_loadout_spike=%d other_xinput_spike=%d",
        AvowedLuaPerf.right_trigger_other_recent_event_frames,
        AvowedLuaPerf.right_trigger_other_recent_sheathed_frames,
        AvowedLuaPerf.right_trigger_other_recent_2h_frames,
        AvowedLuaPerf.right_trigger_other_probe_spike_frames,
        AvowedLuaPerf.right_trigger_other_loadout_spike_frames,
        AvowedLuaPerf.right_trigger_other_xinput_spike_frames
    ))

    AvowedLuaPerf.pre_tick_sum = 0.0
    AvowedLuaPerf.pre_tick_max = 0.0
    AvowedLuaPerf.pre_tick_samples = 0
    AvowedLuaPerf.xinput_sum = 0.0
    AvowedLuaPerf.xinput_max = 0.0
    AvowedLuaPerf.xinput_samples = 0
    avowed_lua_perf_reset_xinput_blocks()
    AvowedLuaPerf.loadout_hook_count = 0
    AvowedLuaPerf.probe_update_count = 0
    AvowedLuaPerf.reattach_right_attempts = 0
    AvowedLuaPerf.reattach_left_attempts = 0
    AvowedLuaPerf.old_attach_refresh_right = 0
    AvowedLuaPerf.old_attach_refresh_left = 0
    AvowedLuaPerf.attach_failures = 0
    AvowedLuaPerf.fallback_old_attach_count = 0
    AvowedLuaPerf.visibility_refreshes = 0
    AvowedLuaPerf.swap_hide_previous_count = 0
    AvowedLuaPerf.left_trigger_frames = 0
    AvowedLuaPerf.left_trigger_menu_returns = 0
    AvowedLuaPerf.left_trigger_event_returns = 0
    AvowedLuaPerf.left_trigger_levelwait_returns = 0
    AvowedLuaPerf.left_trigger_non_grimoire_frames = 0
    AvowedLuaPerf.left_grimoire_candidate_frames = 0
    AvowedLuaPerf.left_grimoire_trigger_frames = 0
    AvowedLuaPerf.right_trigger_frames = 0
    AvowedLuaPerf.right_trigger_menu_returns = 0
    AvowedLuaPerf.right_trigger_event_returns = 0
    AvowedLuaPerf.right_trigger_levelwait_returns = 0
    AvowedLuaPerf.right_trigger_other_frames = 0
    AvowedLuaPerf.right_wand_candidate_frames = 0
    AvowedLuaPerf.right_wand_trigger_frames = 0
    AvowedLuaPerf.right_grimoire_candidate_frames = 0
    AvowedLuaPerf.right_grimoire_trigger_frames = 0
    AvowedLuaPerf.right_bow_candidate_frames = 0
    AvowedLuaPerf.right_bow_trigger_frames = 0
    AvowedLuaPerf.post_event_recoveries = 0
    AvowedLuaPerf.post_event_probe_skips = 0
    AvowedLuaPerf.post_event_loadout_skips = 0
    AvowedLuaPerf.loadout_swap_button_frames = 0
    AvowedLuaPerf.loadout_swap_button_releases = 0
    AvowedLuaPerf.loadout_hook_after_swap_input = 0
    AvowedLuaPerf.loadout_swap_to_hook_sum_ms = 0.0
    AvowedLuaPerf.loadout_swap_to_hook_max_ms = 0.0
    AvowedLuaPerf.right_trigger_recent_loadout_frames = 0
    AvowedLuaPerf.right_trigger_other_recent_loadout_frames = 0
    AvowedLuaPerf.right_trigger_wand_attach_mismatch_frames = 0
    AvowedLuaPerf.right_trigger_other_recent_event_frames = 0
    AvowedLuaPerf.right_trigger_other_recent_sheathed_frames = 0
    AvowedLuaPerf.right_trigger_other_recent_2h_frames = 0
    AvowedLuaPerf.right_trigger_other_probe_spike_frames = 0
    AvowedLuaPerf.right_trigger_other_loadout_spike_frames = 0
    AvowedLuaPerf.right_trigger_other_xinput_spike_frames = 0
    AvowedLuaPerf.bow_frames = 0
    AvowedLuaPerf.wand_frames = 0
    AvowedLuaPerf.melee_frames = 0
    AvowedLuaPerf.last_log = now_t
end

-- used for our swipe function
if gesture_state == nil then
    gesture_state = {
        -- State structure is now simplified since we only need to track LtoR and RtoL per controller
        left_LtoR = { in_progress = false, start_pos = UEVR_Vector3f.new(), timer = 0.0 },
        left_RtoL = { in_progress = false, start_pos = UEVR_Vector3f.new(), timer = 0.0 },
        right_LtoR = { in_progress = false, start_pos = UEVR_Vector3f.new(), timer = 0.0 },
        right_RtoL = { in_progress = false, start_pos = UEVR_Vector3f.new(), timer = 0.0 },
        left_punch = { in_progress = false, start_pos = UEVR_Vector3f.new(), timer = 0.0 },
        right_punch = { in_progress = false, start_pos = UEVR_Vector3f.new(), timer = 0.0 },

        -- Last known position for each controller index
        last_positions = {},
    }
end

local function find_required_object(name)
    local obj = uevr.api:find_uobject(name)
    if not obj then
        return nil
    end

    return obj
end

local find_static_class = function(name)
    local c = uevr.api:find_uobject(name)
    if(c ~= nil) then
        return c:get_class_default_object()
    else
        return nil
    end
end



local kismet_system_library = nil

local function get_cvar_int(name)
    local IntVal = 0
    
    if kismet_system_library == nil then 
        kismet_system_library = find_static_class("Class /Script/Engine.KismetSystemLibrary")
    end
    local console_manager = api:get_console_manager()
    if console_manager ~= nil and console_manager.find_variable then
        local var = console_manager:find_variable(name)
        
        if kismet_system_library ~= nil and var ~= nil then
            if kismet_system_library.GetConsoleVariableIntValue then
                IntVal = kismet_system_library:GetConsoleVariableIntValue(name)
            end
        end
    end
    -- Return the integer value AND the boolean status
    return IntVal 
end

local function get_cvar_float(name)
    local FloatVal = 0.0 -- Initialize to a float value
    local readable = false -- Initialize the status flag
    
    if disableMod == 0 then
        if kismet_system_library == nil then 
            kismet_system_library = find_static_class("Class /Script/Engine.KismetSystemLibrary")
        end
        local console_manager = api:get_console_manager()
        if console_manager ~= nil and console_manager.find_variable then
            local var = console_manager:find_variable(name)

            if kismet_system_library ~= nil and var ~= nil then
                if kismet_system_library.GetConsoleVariableFloatValue then
                    FloatVal = kismet_system_library:GetConsoleVariableFloatValue(name)
                    readable = true -- Reading was successful
                end
            end
        end
    end
    
    -- Return the float value AND the boolean status
    return FloatVal, readable
end


local function set_cvar_int(cvar, value)
	local old_cvar_val, readable = get_cvar_int(cvar)
	
	if value ~= old_cvar_val or readable == false then 
		local console_manager = api:get_console_manager()
		local var = console_manager:find_variable(cvar)
		if var ~= nil then
			var:set_int(value)
		end
	end
end


local function set_cvar_float(cvar, value)
	local old_cvar_val, readable = get_cvar_float(cvar)
	
	if value ~= old_cvar_val or readable == false then 
		local console_manager = api:get_console_manager()
		local var = console_manager:find_variable(cvar)
		if var ~= nil then
			var:set_float(value)
		end
	end
end

local equipment_enchant_c = find_required_object("Class /Script/Alabama.InventoryItemEnchantListWidget")
local equipment_upgrade_c = find_required_object("Class /Script/Alabama.EquipmentUpgradeWidget")
local ledger_sub_page_c = find_required_object("Class /Script/Alabama.LedgerSubPage")
local vitals_bar_c = find_required_object("Class /Script/Alabama.VitalsBar")
local conditional_lib_c = find_required_object("Class /Script/Alabama.AlabamaConditionalLibrary")
local examinable_widget_c = find_required_object("Class /Script/Alabama.ExaminableWidget")

local wbp_vitals_bar_c = nil
local CineClassObj = find_required_object("Class /Script/CinematicCamera.CineCameraActor")
local EquippedItem_c = find_required_object("Class /Script/Alabama.EquippedItem")
local EquippedItem_candidates = {
    "Class /Script/Alabama.EquippedItem",
    "Class /Script/AlabamaRuntime.EquippedItem",
    "Class /Script/AlabamaGame.EquippedItem"
}
local equipped_item_last_log_t = 0.0
local equipped_item_last_scan_t = 0.0
local equipped_item_prop_scan_cursor = 0
local equipped_item_prop_scan_budget = 1536
local equipped_item_slot_index_cache = { Right = -1, Left = -1 }
local equipped_item_slot_last = {
    Right = { component = nil, base = nil, t = 0.0 },
    Left = { component = nil, base = nil, t = 0.0 }
}
local equipped_item_slot_grace_s = 1.25

local function clear_slot_cache(slot)
    if slot == nil then
        equipped_item_slot_index_cache.Right = -1
        equipped_item_slot_index_cache.Left = -1
        equipped_item_slot_last.Right = { component = nil, base = nil, t = 0.0 }
        equipped_item_slot_last.Left = { component = nil, base = nil, t = 0.0 }
        weapon_probe_nil_streak.right = 0
        weapon_probe_nil_streak.left = 0
        return
    end

    equipped_item_slot_index_cache[slot] = -1
    equipped_item_slot_last[slot] = { component = nil, base = nil, t = 0.0 }
end

function request_weapon_resync(reason, immediate)
    clear_slot_cache()
    changed_loadout = true

    if immediate == true then
        loadout_change_cooldown_s = 0.0
    else
        loadout_change_cooldown_s = LOADOUT_CHANGE_COOLDOWN_MAX
    end

    previous_right_weapon = nil
    previous_left_weapon = nil
    last_weapon_probe_t = 0.0

    if last_sheathed_visible == false then
        last_sheathed_visible = true
    end

    debugOut("Requested weapon resync (" .. tostring(reason) .. ")")
end

local function sanitize_slot_result(slot, component, base)
    if component == nil then return nil, nil end

    local same = function(a, b)
        if a == nil or b == nil then return false end
        if type(same_object) == "function" then
            return same_object(a, b)
        end
        return a == b
    end

    if slot == "Left" then
        if same(component, current_right_weapon) or same(base, current_right_weapon_base) then
            return nil, nil
        end
    elseif slot == "Right" then
        if same(component, current_left_weapon) or same(base, current_left_weapon_base) then
            return nil, nil
        end
    end

    return component, base
end

local set_visibility_for_component_chain

local function refresh_current_weapon_visibility()
    if NATIVE_BONE_DRIVER_OWNS_WEAPONS == 1 then
        return
    end

    if last_sheathed_visible == false then
        return
    end

    local now_t = os.clock()
    if (now_t - last_visibility_refresh_t) < VISIBILITY_REFRESH_INTERVAL_S then
        return
    end
    last_visibility_refresh_t = now_t
    AvowedLuaPerf.visibility_refreshes = AvowedLuaPerf.visibility_refreshes + 1

    pcall(function()
        -- In direct-drive mode, avoid forcing full SpawnedVisualComponents visible,
        -- which can resurrect pooled/hidden weapon visuals.
        if DIRECT_WEAPON_DRIVE == 1 then
            if attached_right_weapon ~= nil and is_object_usable(attached_right_weapon) == true then
                attached_right_weapon:SetVisibility(true, true)
                attached_right_weapon:SetHiddenInGame(false, true)
            end
            if attached_left_weapon ~= nil and is_object_usable(attached_left_weapon) == true and not same_object(attached_left_weapon, attached_right_weapon) then
                attached_left_weapon:SetVisibility(true, true)
                attached_left_weapon:SetHiddenInGame(false, true)
            end
            return
        end

        local rw = current_right_weapon
        local rb = current_right_weapon_base
        if rw == nil then rw = attached_right_weapon end
        if rb == nil then rb = attached_right_weapon_base end
        if rw ~= nil and is_object_usable(rw) == true then
            rw:SetVisibility(true, true)
            rw:SetHiddenInGame(false, true)
            if rb ~= nil and is_object_usable(rb) == true then
                set_visibility_for_component_chain(rb, true)
            end
        end

        local lw = current_left_weapon
        local lb = current_left_weapon_base
        if lw == nil then lw = attached_left_weapon end
        if lb == nil then lb = attached_left_weapon_base end
        if lw ~= nil and is_object_usable(lw) == true then
            lw:SetVisibility(true, true)
            lw:SetHiddenInGame(false, true)
            if lb ~= nil and is_object_usable(lb) == true then
                set_visibility_for_component_chain(lb, true)
            end
        end
    end)
end

local function resolve_equipped_item_class()
    if EquippedItem_c ~= nil then
        return EquippedItem_c
    end

    for _, cname in ipairs(EquippedItem_candidates) do
        local c = find_required_object(cname)
        if c ~= nil then
            EquippedItem_c = c
            debugOut("Resolved EquippedItem class: " .. cname)
            return EquippedItem_c
        end
    end

    -- Fallback: brute-force class search once every few seconds for patch-renamed script packages.
    local now_t = os.clock()
    if (now_t - equipped_item_last_scan_t) < 3.0 then
        return nil
    end
    equipped_item_last_scan_t = now_t

    local ok, result = pcall(function()
        local uobjects = uevr.types.FUObjectArray.get()
        if uobjects == nil then return nil end

        local count = uobjects:get_object_count()
        for i = 0, count - 1 do
            local o = uobjects:get_object(i)
            if o ~= nil then
                local name = o:get_full_name()
                if name ~= nil and string.find(name, "Class /Script/") and string.find(name, "EquippedItem") then
                    if o.get_objects_matching ~= nil then
                        EquippedItem_c = o
                        debugOut("Resolved EquippedItem class by scan: " .. tostring(name))
                        return EquippedItem_c
                    end
                end
            end
        end

        return nil
    end)

    if ok then
        return result
    end

    return nil
end
local temp_vec3f = Vector3f.new(0, 0, 0)

local function fix_weapon_speed_for_weapon(weapon, value)
    new_value = value / CHEAT_GUN_SPEED_MULT -- gun multipliers are done globally, so we need to divide by that if its set.
    
    if weapon ~= nil and weapon.AttackAttributeSet ~= nil then
        if weapon.AttackAttributeSet.AttackSpeedMult ~= nil then
            if weapon.AttackAttributeSet.AttackSpeedMult.BaseValue ~= nil and weapon.AttackAttributeSet.AttackSpeedMult.CurrentValue ~= nil then
                weapon.AttackAttributeSet.AttackSpeedMult.BaseValue = new_value
                weapon.AttackAttributeSet.AttackSpeedMult.CurrentValue = new_value
            end
        end
    end
end

local function is_weapon_melee(weapon)
            -- Buckler (shield)
            -- 1H_Axe, 1H_Club, 1H_Dagger, 1H_Rod_Proto, 1H_Sword, 1H_Wand
            -- 2H_Axe, 2H_Sword, 2H_Bow, 
            -- 0H_Grimoire
            -- OgreFist (creature?), 1H_Unarmed
            -- 1H_Pistol, 
    if weapon ~= nil then
        local status, weapon_name = pcall(function() return weapon:get_full_name() end)
        if string.find(weapon_name, "Dagger") or 
		   string.find(weapon_name, "Sword") or
		   string.find(weapon_name, "Spear") or
		   string.find(weapon_name, "Club") or
		   string.find(weapon_name, "Axe") or
		   string.find(weapon_name, "Hammer") or
		   string.find(weapon_name, "Mace") then
            return true
        end
    else -- unarmed are melee
        return true 
    end
    
    return false
end

local function is_shield(weapon)
    if weapon ~= nil then
        local status, weapon_name = pcall(function() return weapon:get_full_name() end)
        if string.find(weapon_name, "Shield") or 
		   string.find(weapon_name, "Buckler")then
            return true
        end
    end
    
    return false
end

set_visibility_for_component_chain = function(item, visible)
    if item == nil then return end -- Guard clause for nil item

    local visual_components = item.SpawnedVisualComponents
    if visual_components ~= nil then
        for _, visual_component in ipairs(visual_components) do
            local component_name = visual_component:get_fname():to_string()
            
            -- Check if the name ends with an underscore followed by one or more digits
            -- only match components that end in _248109865  (some number)
            if string.match(component_name, "_%d+$") then
                debugOut("Set visibility for component: " .. component_name)
                visual_component:SetVisibility(visible, true)
            end
        end
    end
end

local function extract_visual_component(candidate)
    if candidate == nil then return nil, nil end

    local function is_likely_fx_name(comp_name)
        if comp_name == nil then return false end
        local lname = string.lower(comp_name)
        return string.find(lname, "niagara") or string.find(lname, "vfx") or string.find(lname, "_fx") or string.find(lname, "fx_") or string.find(lname, "trail") or string.find(lname, "particle") or string.find(lname, "preview") or string.find(lname, "highlight") or string.find(lname, "decal")
    end

    -- Prefer engine-selected active mesh first; SpawnedVisualComponents can include transient FX meshes.
    local ok_mesh, mesh = pcall(function()
        if candidate.GetActiveMeshComponent ~= nil then
            return candidate:GetActiveMeshComponent()
        end
        if candidate.MeshComponent ~= nil then
            return candidate.MeshComponent
        end
        if candidate.Mesh ~= nil then
            return candidate.Mesh
        end
        return nil
    end)

    if ok_mesh and mesh ~= nil then
        local ok_name, mesh_name = pcall(function() return mesh:get_fname():to_string() end)
        if (not ok_name) or (mesh_name == nil) or (not is_likely_fx_name(mesh_name)) then
            return mesh, candidate
        end
    end

    local visual_components = candidate.SpawnedVisualComponents
    if visual_components ~= nil then
        for _, visual_component in ipairs(visual_components) do
            local ok_name, comp_name = pcall(function() return visual_component:get_fname():to_string() end)
            if ok_name and comp_name ~= nil then
                local looks_like_fx = is_likely_fx_name(comp_name)
                if not looks_like_fx and (string.find(comp_name, "Skeletal") or string.find(comp_name, "Static")) then
                    return visual_component, candidate
                end
            end
        end
    end

    return nil, nil
end

local function try_get_weapon_from_pawn_methods(pawn, slot)
    if pawn == nil then return nil, nil end

    local method_names = {}

    if slot == "Right" then
        method_names = {
            "GetCurrentWeapon",
            "GetRightHandWeapon",
            "GetMainHandWeapon",
            "GetEquippedRightWeapon"
        }
    else
        method_names = {
            "GetOffhandWeapon",
            "GetLeftHandWeapon",
            "GetSecondaryWeapon",
            "GetEquippedLeftWeapon"
        }
    end

    for _, method_name in ipairs(method_names) do
        local ok_obj, obj = pcall(function()
            local fn = pawn[method_name]
            if fn ~= nil then
                return pawn[method_name](pawn)
            end
            return nil
        end)

        if ok_obj and obj ~= nil then
            local component, base = extract_visual_component(obj)
            if component ~= nil then
                debugOut("get_equipped_items: pawn fallback via " .. method_name)
                return component, base
            end
        end
    end

    return nil, nil
end

local function is_object_usable(obj)
    if obj == nil then return false end
    local ok = pcall(function() return obj:get_full_name() end)
    return ok
end

function get_obj_address(obj)
    if obj == nil then return nil end
    local addr = nil
    pcall(function() addr = obj:get_address() end)
    return addr
end

function same_object(a, b)
    if a == b then return true end
    if a == nil or b == nil then return false end
    local aa = get_obj_address(a)
    local ab = get_obj_address(b)
    return aa ~= nil and ab ~= nil and aa == ab
end

function should_allow_nil_clear(probe_weapon, current_weapon, attached_weapon, nil_streak)
    if probe_weapon ~= nil then
        return true
    end

    local threshold = WEAPON_PROBE_NIL_STREAK_CLEAR
    if current_weapon ~= nil or attached_weapon ~= nil then
        threshold = 6
    end

    return nil_streak >= threshold
end

function bump_attach_fail_streak(which_hand)
    if which_hand == "right" then
        controller_attach_fail_streak.right = (controller_attach_fail_streak.right or 0) + 1
        return controller_attach_fail_streak.right
    end

    controller_attach_fail_streak.left = (controller_attach_fail_streak.left or 0) + 1
    return controller_attach_fail_streak.left
end

function reset_attach_fail_streak(which_hand)
    if which_hand == "right" then
        controller_attach_fail_streak.right = 0
    else
        controller_attach_fail_streak.left = 0
    end
end

function should_try_uobjecthook_fallback(which_hand)
    local streak = 0
    if which_hand == "right" then
        streak = controller_attach_fail_streak.right or 0
    else
        streak = controller_attach_fail_streak.left or 0
    end

    return streak >= FALLBACK_FAIL_STREAK_THRESHOLD
end

function should_pause_deferred_detach(now_dd)
    if now_dd < deferred_detach_queue.pause_until_t then
        return true
    end

    local pawn = nil
    if api ~= nil and api.get_local_pawn ~= nil then
        pawn = api:get_local_pawn()
    end
    if pawn == nil then
        deferred_detach_queue.pause_until_t = now_dd + 0.50
        return true
    end

    local is_dead = false
    pcall(function()
        local attrs = pawn.AttributeSetCore
        if attrs ~= nil and attrs.Health ~= nil and attrs.Health.CurrentValue ~= nil then
            if attrs.Health.CurrentValue <= 0.001 then
                is_dead = true
            end
        end
    end)

    if is_dead then
        deferred_detach_queue.pause_until_t = now_dd + deferred_detach_queue.death_grace_s
        return true
    end

    return false
end

function enqueue_deferred_detach(w, hand)
    if NATIVE_BONE_DRIVER_OWNS_WEAPONS == 1 then
        return
    end

    local addr = get_obj_address(w)
    -- Deduplicate by address
    for _, entry in ipairs(deferred_detach_queue) do
        if entry.addr ~= nil and entry.addr == addr then
            debugOut("Deferred detach dedup: " .. tostring(hand) .. " addr=" .. tostring(addr))
            return
        end
    end
    deferred_detach_queue[#deferred_detach_queue + 1] = {
        weapon = w, addr = addr, hand = hand,
        detach_at = os.clock() + DEFERRED_DETACH_DELAY_S,
        expire_at = os.clock() + DEFERRED_DETACH_TTL_S
    }
    debugOut("Enqueued deferred detach: " .. tostring(hand) .. " addr=" .. tostring(addr))
end

function process_deferred_detach_queue()
    if NATIVE_BONE_DRIVER_OWNS_WEAPONS == 1 then
        if #deferred_detach_queue > 0 then
            deferred_detach_queue = { pause_until_t = 0.0, death_grace_s = 2.0 }
        end
        return
    end

    if #deferred_detach_queue == 0 then return end
    local now_dd = os.clock()

    -- Pause during menu/death/respawn transitions
    if is_in_menu == true or is_in_main_menu == true or pause_opened == true then
        return
    end

    if should_pause_deferred_detach(now_dd) then
        return
    end
    local processed = 0
    local i = 1
    while i <= #deferred_detach_queue do
        local entry = deferred_detach_queue[i]
        -- TTL expiry: drop stale entries silently
        if now_dd >= entry.expire_at then
            debugOut("Deferred detach expired (TTL): " .. tostring(entry.hand))
            table.remove(deferred_detach_queue, i)
        elseif now_dd >= entry.detach_at then
            if processed >= DEFERRED_DETACH_BATCH then break end
            local w = entry.weapon
            if w ~= nil and is_object_usable(w) == true then
                local cur_addr = get_obj_address(w)
                if cur_addr ~= nil and cur_addr == entry.addr then
                    -- Don't remove if weapon was re-attached to either hand
                    if not same_object(w, attached_right_weapon) and not same_object(w, attached_left_weapon) then
                        pcall(function()
                            if UEVR_UObjectHook.remove_motion_controller_state ~= nil then
                                UEVR_UObjectHook.remove_motion_controller_state(w)
                            end
                        end)
                        debugOut("Deferred detach executed: " .. tostring(entry.hand) .. " addr=" .. tostring(entry.addr))
                    else
                        debugOut("Deferred detach skipped (re-attached): " .. tostring(entry.hand))
                    end
                else
                    debugOut("Deferred detach skipped (addr gone): " .. tostring(entry.hand))
                end
            else
                debugOut("Deferred detach skipped (unusable): " .. tostring(entry.hand))
            end
            table.remove(deferred_detach_queue, i)
            processed = processed + 1
        else
            i = i + 1
        end
    end
end

local function is_equipped_item_object(obj)
    if obj == nil then return false end

    -- The raw FUObjectArray fallback contains every UObject. Reject non-equipped-item
    -- objects before probing reflected Alabama properties; failed property lookups on
    -- thousands of unrelated objects were causing 40-150 ms XInput stalls.
    if EquippedItem_c == nil then
        return true
    end

    local ok_class, is_equipped = pcall(function()
        return obj:is_a(EquippedItem_c)
    end)

    return ok_class and is_equipped == true
end

local function matches_slot_owner(obj, pawn, slot_lower)
    if obj == nil or pawn == nil then return false end

    local owner_ok, owner = pcall(function() return obj.OwningAlabamaCharacter end)
    if not owner_ok or owner == nil or owner ~= pawn then
        return false
    end

    local slot_ok, equip_tag = pcall(function()
        local s = obj.EquipSlot
        if s == nil or s.TagName == nil then return "" end
        return s.TagName:to_string()
    end)

    if not slot_ok or equip_tag == nil or not string.find(string.lower(equip_tag), slot_lower, 1, true) then
        return false
    end

    local ok_unsheathed, unsheathed = pcall(function()
        if obj.IsUnsheathed ~= nil then
            return obj:IsUnsheathed() and true or false
        end
        return true
    end)

    return ok_unsheathed and unsheathed == true
end

local function cache_slot_weapon(slot, component, base)
    if slot == nil then return end
    equipped_item_slot_last[slot].component = component
    equipped_item_slot_last[slot].base = base
    equipped_item_slot_last[slot].t = os.clock()
end

local function try_get_cached_slot_weapon(slot)
    local entry = equipped_item_slot_last[slot]
    if entry == nil then return nil, nil end

    if entry.component ~= nil and entry.base ~= nil and (os.clock() - entry.t) <= equipped_item_slot_grace_s then
        if is_object_usable(entry.component) and is_object_usable(entry.base) then
            local c, b = sanitize_slot_result(slot, entry.component, entry.base)
            if c ~= nil then
                return c, b
            end
        end
    end

    return nil, nil
end

local function try_get_weapon_by_property_scan(pawn, slot)
    local ok_arr, arr = pcall(function() return uevr.types.FUObjectArray.get() end)
    if not ok_arr or arr == nil then
        return nil, nil
    end

    local ok_count, count = pcall(function() return arr:get_object_count() end)
    if not ok_count or count == nil or count <= 0 then
        return nil, nil
    end

    local slot_lower = string.lower(slot)

    local cached_idx = equipped_item_slot_index_cache[slot]
    if cached_idx ~= nil and cached_idx >= 0 and cached_idx < count then
        local ok_obj, obj = pcall(function() return arr:get_object(cached_idx) end)
        if ok_obj and is_equipped_item_object(obj) and matches_slot_owner(obj, pawn, slot_lower) then
            local ok_extract, component, base = pcall(extract_visual_component, obj)
            if ok_extract and component ~= nil then
                component, base = sanitize_slot_result(slot, component, base)
                if component ~= nil then
                    cache_slot_weapon(slot, component, base)
                    return component, base
                end
            end
        end
    end

    if equipped_item_prop_scan_cursor >= count then
        equipped_item_prop_scan_cursor = 0
    end

    local scanned = 0

    while scanned < equipped_item_prop_scan_budget and scanned < count do
        local idx = equipped_item_prop_scan_cursor
        equipped_item_prop_scan_cursor = equipped_item_prop_scan_cursor + 1
        if equipped_item_prop_scan_cursor >= count then
            equipped_item_prop_scan_cursor = 0
        end

        scanned = scanned + 1

        local ok_obj, obj = pcall(function() return arr:get_object(idx) end)
        if not ok_obj or obj == nil then
            goto continue_scan
        end

        if not is_equipped_item_object(obj) or not matches_slot_owner(obj, pawn, slot_lower) then
            goto continue_scan
        end

        local ok_extract, component, base = pcall(extract_visual_component, obj)
        if ok_extract and component ~= nil then
            component, base = sanitize_slot_result(slot, component, base)
            if component ~= nil then
                equipped_item_slot_index_cache[slot] = idx
                cache_slot_weapon(slot, component, base)
                debugOut("get_equipped_items: property-scan fallback hit @ index " .. tostring(idx))
                return component, base
            end
        end

        ::continue_scan::
    end

    return nil, nil
end

local function get_equipped_items(slot)
    local pawn = api:get_local_pawn()

    if pawn == nil then
        return nil, nil
    end

    local equipped_item_class = resolve_equipped_item_class()
    local items = nil

    if equipped_item_class ~= nil then
        local ok_items, class_items = pcall(function()
            return equipped_item_class:get_objects_matching(false)
        end)

        if ok_items and class_items ~= nil and #class_items > 0 then
            items = class_items
        else
            if USE_OLD_OBJ_HOOK_METHOD == 1 then
                local ok_hook, hook_items = pcall(function()
                    return UEVR_UObjectHook.get_objects_by_class(equipped_item_class, false)
                end)

                if ok_hook and hook_items ~= nil and #hook_items > 0 then
                    items = hook_items
                end
            end
        end
    end

    if items ~= nil then
        local slot_lower = string.lower(slot)

        for _, item in ipairs(items) do
            local ok_name, full_name = pcall(function() return item:get_full_name() end)
            if not ok_name or full_name == nil or string.find(full_name, "Unarmed") then
                goto continue_item
            end

            local owner_prop_ok, item_owner = pcall(function()
                return item.OwningAlabamaCharacter
            end)
            if not owner_prop_ok or (item_owner ~= nil and item_owner ~= pawn) then
                goto continue_item
            end

            local slot_ok, equip_tag = pcall(function()
                local ItemSlot = item.EquipSlot
                if ItemSlot == nil or ItemSlot.TagName == nil then return "" end
                return ItemSlot.TagName:to_string()
            end)

            if not slot_ok or equip_tag == nil or not string.find(string.lower(equip_tag), slot_lower, 1, true) then
                goto continue_item
            end

            local ok_unsheathed, unsheathed = pcall(function()
                if item.IsUnsheathed ~= nil then
                    return item:IsUnsheathed() and true or false
                end
                return true
            end)

            if not ok_unsheathed or not unsheathed then
                goto continue_item
            end

            local ok_extract, component, base = pcall(extract_visual_component, item)
            if ok_extract and component ~= nil then
                component, base = sanitize_slot_result(slot, component, base)
                if component ~= nil then
                    if is_weapon_melee(component) == true then
                        fix_weapon_speed_for_weapon(base, 10.0)
                    end
                    cache_slot_weapon(slot, component, base)
                    return component, base
                end
            end

            ::continue_item::
        end
    end

    local pawn_component, pawn_base = try_get_weapon_from_pawn_methods(pawn, slot)
    if pawn_component ~= nil then
        pawn_component, pawn_base = sanitize_slot_result(slot, pawn_component, pawn_base)
        if pawn_component ~= nil then
            cache_slot_weapon(slot, pawn_component, pawn_base)
            return pawn_component, pawn_base
        end
    end

    local cached_component, cached_base = try_get_cached_slot_weapon(slot)
    if cached_component ~= nil then
        return cached_component, cached_base
    end

    local prop_component, prop_base = try_get_weapon_by_property_scan(pawn, slot)
    if prop_component ~= nil then
        return prop_component, prop_base
    end

    local now_t = os.clock()
    if (now_t - equipped_item_last_log_t) > 1.0 then
        equipped_item_last_log_t = now_t
        debugOut("get_equipped_items: no weapon found for slot=" .. tostring(slot))
    end

    return nil, nil
end

-------------------------------------------------------------------------------
-- Pass in the class from find_required_object and the name to match and get
-- the first instance that matches the string.
-------------------------------------------------------------------------------
local function GetFirstMatchingObject(class_object_c, match_string)
	local object_list = class_object_c:get_objects_matching(false)
    for i, instance in ipairs(object_list) do
        if string.find(instance:get_full_name(), match_string, 1, true) then
			return instance
		end
	end
    
    return nil
end


-------------------------------------------------------------------------------
-- hook_function
--
-- Hooks a UEVR function. 
--
-- class_name = the class to find, such as "Class /Script.GunfireRuntime.RangedWeapon"
-- function_name = the function to Hook
-- native = true or false whether or not to set the native function flag.
-- prefn = the function to run if you hook pre. Pass nil to not use
-- postfn = the function to run if you hook post. Pass nil to not use.
-- dbgout = true to print the debug outputs, false to not
--
-- Example:
--    hook_function("Class /Script/GunfireRuntime.RangedWeapon", "OnFireBegin", true, nil, gun_firingbegin_hook, true)
--
-- Returns: true on success, false on failure.
-------------------------------------------------------------------------------
local function hook_function(class_name, function_name, native, prefn, postfn, dbgout)
    local status, result = pcall(function()
        local class_obj = uevr.api:find_uobject(class_name)
        if(class_obj ~= nil) then
            if dbgout then print("hook_function: found class obj for", class_name) end
            local class_fn = class_obj:find_function(function_name)
            if(class_fn ~= nil) then 
                if dbgout then print("hook_function: found function", function_name, "for", class_name) end
                if (native == true) then
                    class_fn:set_function_flags(class_fn:get_function_flags() | 0x400)
                    if dbgout then print("hook_function: set native flag") end
                else
                    class_fn:set_function_flags(class_fn:get_function_flags() & ~0x400)
                    if dbgout then print("hook_function: cleared native flag") end
                end
                
                class_fn:hook_ptr(prefn, postfn)
                result = true
                if dbgout then print("hook_function: set function hook for", prefn, "and", postfn) end
            else
                if dbgout then print("hook_function cannot find function for", function_name) end
            end
        else
            if dbgout then print("class obj was nil in hook_function") end
        end
        
        return result
    end)
    
    return result
end

local PrevClassName = nil

local function check_cutscene_or_conversation()
    local cutscene = false
    local conversation = false
    
    if CineClassObj == nil then
        CineClassObj = find_required_object("Class /Script/CinematicCamera.CineCameraActor")
    end
    if CineClassObj == nil then return false end
    
    local PC = api:get_player_controller(0)
    if PC == nil then return false end
    
    local CamMgr = PC.PlayerCameraManager
    if CamMgr == nil then return false end
    
    local ViewTarget = CamMgr.ViewTarget
    if ViewTarget == nil then return false end
    
    local TargetObj = ViewTarget.Target
    if TargetObj == nil then return false end
    if TargetObj.is_a == nil then return false end
    
    local ClassType = TargetObj:get_class()
    if ClassType ~= nil then
        if PrevClassName ~= ClassType:get_full_name() then
            PrevClassName = ClassType:get_full_name()
            if PrevClassName == "Class /Script/Alabama.AlabamaStageCameraActor" or 
               PrevClassName == "BlueprintGeneratedClass /Game/Cinematics/BP_SapadalDream.BP_SapadalDream_C" or 
               PrevClassName == "BlueprintGeneratedClass /Game/Cinematics/BP_Sapadal.BP_Sapadal_C" then
                IsActor = true
                conversation = true -- in conversation
                set_aim_method_if_needed("0")
                vr.set_mod_value("UI_Size", "0.250000")
                vr.set_mod_value("UI_Distance", "0.500000")
            elseif PrevClassName == "Class /Script/CinematicCamera.CineCameraActor" then
                set_aim_method_if_needed("0")
                IsActor = true
            else
                IsActor = false
                conversation = false -- left conversation
                set_aim_method_if_needed(current_aim_method)
                vr.set_mod_value("UI_Size", tostring(NORMAL_UI_SIZE))
                vr.set_mod_value("UI_Distance", "10.000000")
            end
            
            if IsActor == false then
                if PrevClassName == "Class /Script/Engine.CameraActor" then
                    IsActor = true
                    debugOut("In 2d cinema")
                    cutscene = true
                end
            end
        end
    end
            
    return IsActor
end

--callback from uevrUtils that fires whenever the level changes
function level_change(level, levelName)
    -- "MainMenu", "ADR_04_SS"
    debugOut("****Level changed to " .. levelName)
    
    if levelName ~= "MainMenu" then
        is_in_main_menu = false
        -- flag to rescan for these objects
        --hud_switcher_hook = false
        --unsheath_hook = false
        is_hud_attached = false -- flag to reattach hud objects.
        new_level_timer = 0.0
        new_level_hud_timer = 0.0
        wbp_vitals_bar_c = nil
        hud_minimap_c = nil
        is_time_of_day_hooked = false
        
    else
        is_in_main_menu = true
    end
end

local function UpdateVitalsBar(which_bar, current_val, max_val)
    if wbp_vitals_bar_c == nil then
        wbp_vitals_bar_c = find_required_object("WidgetBlueprintGeneratedClass /Game/UI/Widgets/HudLayer/Vitals/WBP_VitalsBar.WBP_VitalsBar_C")
    end
    
    if wbp_vitals_bar_c == nil then return end
    
    local bar_inst = nil
    if which_bar == "health" then
        bar_inst = GetFirstMatchingObject(wbp_vitals_bar_c, "WBP_VitalsBar_Health")
    elseif which_bar == "essence" then
        bar_inst = GetFirstMatchingObject(wbp_vitals_bar_c, "WBP_VitalsBar_Essence")
    end
    
    if bar_inst == nil then
        return
    end
    
    if bar_inst.UpdateBarStrings ~= nil then
        bar_inst:UpdateBarStrings(true, true, false, current_val, max_val, 0.0)
    end
end

local function check_single_direction_swipe(controller_index, required_direction_sign, swipe_state_obj, delta)
    
    local is_swipe_complete = false
    
    -- --- 1. Get Current Pose & Delta X ---
    local current_position = UEVR_Vector3f.new()
    local controller_rotation = UEVR_Quaternionf.new()
    vr.get_pose(controller_index, current_position, controller_rotation)
    
    local last_position = gesture_state.last_positions[controller_index]
    
    if last_position then
        local delta_x_frame = current_position.x - last_position.x
        
        -- --- 2. START GESTURE LOGIC ---
        if not swipe_state_obj.in_progress then
            local is_starting = (delta_x_frame * required_direction_sign) > DELTA_X_START_THRESHOLD
            
            if is_starting then
                swipe_state_obj.timer = 0.0
                swipe_state_obj.in_progress = true
                -- Store the starting position (only X is strictly necessary for horizontal swipe)
                swipe_state_obj.start_pos.x = current_position.x
            end
        
        -- --- 3. SWIPE IN PROGRESS LOGIC ---
        else
            swipe_state_obj.timer = swipe_state_obj.timer + delta

            local total_delta_x = current_position.x - swipe_state_obj.start_pos.x
            
            local is_distance_met = (total_delta_x * required_direction_sign) >= SWIPE_THRESHOLD
            local is_in_time = swipe_state_obj.timer <= COMBO_THRESHOLD
            
            if is_distance_met and is_in_time then
                is_swipe_complete = true
            end
            
            -- --- 4. CHECK GESTURE END/TIMEOUT ---
            local is_timeout = swipe_state_obj.timer > COMBO_THRESHOLD
            
            if is_swipe_complete or is_timeout then
                swipe_state_obj.in_progress = false
                swipe_state_obj.timer = 0.0
            end
        end
    end
    
    -- --- 5. We return the current position along with the swipe status
    return is_swipe_complete, current_position
end

-- This function detects if the right controller is rolled approximately 90 degrees
-- (like holding a sword horizontally for a block).

-- This function detects a specific block pose: Right controller rolled 90 degrees 
-- (horizontal sword), AND the controller is not significantly pitched (sword not
-- pointed forward or backward).

-- This function detects a specific block pose: Right controller rolled 90 degrees 
-- (horizontal sword), by checking for a large PITCH angle and a near-zero ROLL angle.

local function GetSwordBlockSimple()
    local controller_index = vr.get_right_controller_index()
    
    -- Ensure the right controller is connected/tracked
    if controller_index ~= -1 then
        local controller_pos = UEVR_Vector3f.new()
        local controller_rot = UEVR_Quaternionf.new() -- This holds the rotation (x, y, z, w)
        vr.get_pose(controller_index, controller_pos, controller_rot)
        
        -- Use shorthand for quaternion components
        local qw, qx, qy, qz = controller_rot.w, controller_rot.x, controller_rot.y, controller_rot.z
        
        if qy > 0.45 and qy < 0.65 then
            return true
        end
    end
    
    return false
end

local function GetSwordBlock()
    if USE_SIMPLE_SWORD_BLOCK == 1 then return GetSwordBlockSimple() end
    
    local threshold_angle_degrees = SWORD_BLOCK_DEGREES_THRESHOLD
    
    -- Only the right hand holds the sword for block detection
    local controller_index = vr.get_right_controller_index()
    if controller_index == -1 then
        return false
    end

    -- Get controller pose
    local pos = UEVR_Vector3f.new()
    local rot = UEVR_Quaternionf.new()
    vr.get_pose(controller_index, pos, rot)

    -- Convert quaternion to a forward vector
    -- Assumes forward = controller's -Z axis.
    -- If your sword points the opposite direction, flip signs.
    local x = 2*(rot.x*rot.z + rot.w*rot.y)
    local y = 2*(rot.y*rot.z - rot.w*rot.x)
    local z = 1 - 2*(rot.x*rot.x + rot.y*rot.y)
    
    -- forward vector is (x, y, -z) depending on coordinate system 
    -- but for rotation comparison we only need relative components.
    local forward = { x = x, y = y, z = z }

    -- Normalize forward vector
    local len = math.sqrt(x*x + y*y + z*z)
    if len == 0 then return false end
    forward.x = forward.x / len
    forward.y = forward.y / len
    forward.z = forward.z / len
    
    forward.x = -forward.x
    forward.y = -forward.y
    forward.z = -forward.z
    ----------------------------------------------------
    -- Sword block orientation criteria
    ----------------------------------------------------
    -- 1. Sword pointing left (tip rotated ~90° from forward)
    --    forward.x should be strongly negative.
    --
    -- 2. Sword roughly horizontal (not pointed up or down)
    --    forward.y should be near 0.
    ----------------------------------------------------
    
    -- Left direction vector in world space
    local left = { x = -1, y = 0, z = 0 }
    
    -- Dot product between sword direction and "left"
    local dot = forward.x * left.x + forward.y * left.y + forward.z * left.z
    
    -- Convert dot to degrees
    local angle = math.deg(math.acos(dot))

    -- If the tip is facing left within angle threshold => block
    return angle < threshold_angle_degrees
end

local function GetBlock(which_hand, threshold)
    local controller_index = vr.get_left_controller_index()
    if which_hand == "right" then
        controller_index = vr.get_right_controller_index()
    end
    
    local hmd_index = vr.get_hmd_index()
    
    -- Ensure both devices are connected/tracked
    if controller_index ~= -1 and hmd_index ~= -1 then
        
        -- Get Poses
        local controller_pos = UEVR_Vector3f.new()
        local controller_rot = UEVR_Quaternionf.new()
        vr.get_pose(controller_index, controller_pos, controller_rot)
        
        local hmd_pos = UEVR_Vector3f.new()
        local hmd_rot = UEVR_Quaternionf.new()
        vr.get_pose(hmd_index, hmd_pos, hmd_rot)
        
        -- Calculate the Euclidean distance between the two points (Vector subtraction, then length/magnitude)
        local dx = controller_pos.x - hmd_pos.x
        local dy = controller_pos.y - hmd_pos.y
        local dz = controller_pos.z - hmd_pos.z
        
        -- Distance = sqrt(dx^2 + dy^2 + dz^2)
        local distance = math.sqrt(dx*dx + dy*dy + dz*dz)
        
        -- Check if the distance is within the blocking threshold
        if distance <= threshold then
            return true
        else
            return false
        end
    end
    
    return false    
end


local function GetSwipe(controller_index, delta)

    -- If the index is -1 (controller not present), return NO_SWIPE
    if controller_index == -1 then
        return NO_SWIPE
    end

    local is_left_controller = (controller_index == vr.get_left_controller_index())
    local current_position = UEVR_Vector3f.new() -- Will hold the position from the first successful check
    local swipe_result = NO_SWIPE

    -- Determine which state objects to use based on the controller index
    local state_LtoR = is_left_controller and gesture_state.left_LtoR or gesture_state.right_LtoR
    local state_RtoL = is_left_controller and gesture_state.left_RtoL or gesture_state.right_RtoL
    
    -- --- 1. Check Left-to-Right Swipe (+X Direction) ---
    -- L-to-R motion corresponds to a "Right Swipe" (code 2)
    local is_l_to_r_complete, pos_l_to_r = check_single_direction_swipe(controller_index, 1.0, state_LtoR, delta)
    if is_l_to_r_complete then
        swipe_result = RIGHT_SWIPE 
        current_position = pos_l_to_r
    end

    -- --- 2. Check Right-to-Left Swipe (-X Direction) ---
    -- R-to-L motion corresponds to a "Left Swipe" (code 1)
    if swipe_result == NO_SWIPE then
        local is_r_to_l_complete, pos_r_to_l = check_single_direction_swipe(controller_index, -1.0, state_RtoL, delta)
        if is_r_to_l_complete then
            swipe_result = LEFT_SWIPE
            current_position = pos_r_to_l
        elseif pos_r_to_l then
            -- If no swipe was completed, use the position from the second check
            -- since it was performed last and is the freshest data.
            current_position = pos_r_to_l 
        end
    else
        -- We still need to call the second check to update its state, even if the first succeeded.
        -- But we ignore the return value as we already found a swipe.
        local _, temp_pos = check_single_direction_swipe(controller_index, -1.0, state_RtoL, delta)
        if temp_pos then
             -- The position from the last check is always the freshest
             current_position = temp_pos
        end
    end

    -- --- 3. Update last position ONCE for the NEXT frame's delta calculation ---
    if current_position and current_position.x ~= 0 then
        gesture_state.last_positions[controller_index] = current_position
    end
    
    return swipe_result
end

local function hook_on_loadout_change_core(fn, obj, locals, result)
    if disableMod ~= 0 then
        return
    end

    -- Ignore loadout churn while menus/inventory are open.
    if is_in_menu == true or is_in_main_menu == true or pause_opened == true then
        return
    end

    if changed_loadout == true or loadout_change_cooldown_s > 0.0 then
        return
    end

    local now_t = os.clock()
    if (now_t - last_loadout_change_hook_t) < LOADOUT_HOOK_DEBOUNCE_S then
        return
    end
    last_loadout_change_hook_t = now_t

    debugOut("changed loadout core")
    AvowedLuaPerf.loadout_hook_count = AvowedLuaPerf.loadout_hook_count + 1
    loadout_hook_last_t = now_t
    if loadout_swap_input_pending == true then
        AvowedLuaPerf.loadout_hook_after_swap_input = AvowedLuaPerf.loadout_hook_after_swap_input + 1
        AvowedLuaPerf.loadout_swap_to_hook_sum_ms = AvowedLuaPerf.loadout_swap_to_hook_sum_ms + ((now_t - loadout_swap_input_last_t) * 1000.0)
        if ((now_t - loadout_swap_input_last_t) * 1000.0) > AvowedLuaPerf.loadout_swap_to_hook_max_ms then
            AvowedLuaPerf.loadout_swap_to_hook_max_ms = ((now_t - loadout_swap_input_last_t) * 1000.0)
        end
        debugOut("LoadoutChanged after swap input in " .. tostring(math.floor(((now_t - loadout_swap_input_last_t) * 1000.0) + 0.5)) .. " ms")
        loadout_swap_input_pending = false
    end

    -- Do not force-detach/hide here; that creates heavy churn during inventory spam
    -- and can cause transient mesh disappearance. Let the main loop reconcile instead.
    changed_loadout = true
    loadout_change_cooldown_s = LOADOUT_CHANGE_COOLDOWN_MAX
end

local function regen_health_and_essence()
    if CHEAT_AUTO_REGEN_ESSENCE == 0 and CHEAT_AUTO_REGEN_HEALTH == 0 then return end
    
    local pawn = api:get_local_pawn()
    if pawn ~= nil then
        local attribute_set = pawn.AttributeSetCore
        if attribute_set ~= nil then
            if CHEAT_AUTO_REGEN_HEALTH == 1 and attribute_set.Health ~= nil and attribute_set.MaxHealth ~= nil then
                local Health = attribute_set.Health.CurrentValue
                local MaxHealth = attribute_set.MaxHealth.CurrentValue
                if Health < MaxHealth then
                    local NewHealth = Health + ((CHEAT_AUTO_REGEN_PERC / 100) * MaxHealth)
                    if NewHealth > MaxHealth then NewHealth = MaxHealth end
                    attribute_set.Health.CurrentValue = NewHealth
                    attribute_set.Health.BaseValue = NewHealth
                    UpdateVitalsBar("health", NewHealth, MaxHealth)
                end
            end
            
            if CHEAT_AUTO_REGEN_ESSENCE == 1 and attribute_set.Essence ~= nil and attribute_set.MaxEssence ~= nil then
                local Essence = attribute_set.Essence.CurrentValue
                local MaxEssence = attribute_set.MaxEssence.CurrentValue
                if Essence < MaxEssence then
                    local NewEssence = Essence + ((CHEAT_AUTO_REGEN_PERC / 100) * MaxEssence)
                    if NewEssence > MaxEssence then NewEssence = MaxEssence end
                    attribute_set.Essence.CurrentValue = NewEssence
                    attribute_set.Essence.BaseValue = NewEssence
                    UpdateVitalsBar("essence", NewEssence, MaxEssence)
                end
            end
        end
    end
end

local function fix_weapon_spread(spread)
    local pawn = api:get_local_pawn()
    if pawn ~= nil then
        local attribute_set = pawn.AttributeSetCore
        if attribute_set ~= nil then
            debugOut("Found attribute set: ")
            if attribute_set.WeaponSpreadMultiplier and attribute_set.WeaponSpreadMultiplier.BaseValue then
                attribute_set.WeaponSpreadMultiplier.BaseValue = spread
            end
            
            if attribute_set.WeaponSpreadMultiplier and attribute_set.WeaponSpreadMultiplier.CurrentValue then
                attribute_set.WeaponSpreadMultiplier.CurrentValue = spread
            end
        end
    end
end


local function fix_weapon_speed(value)
   
    local pawn = api:get_local_pawn()
    if pawn ~= nil then
        local attribute_set = pawn.AttributeSetCore
        if attribute_set ~= nil then
            if attribute_set.AttackSpeedMultiplier and attribute_set.AttackSpeedMultiplier.BaseValue then
                attribute_set.AttackSpeedMultiplier.BaseValue = value
            end
            
            if attribute_set.AttackSpeedMultiplier and attribute_set.AttackSpeedMultiplier.CurrentValue then
                attribute_set.AttackSpeedMultiplier.CurrentValue = value
            end
        end
    end
end

local function is_menu_open()
    local in_menu = false
    if ledger_sub_page_c == nil then return false end
    
    -- any of the menu ledger pages
    local obj_instances = ledger_sub_page_c:get_objects_matching(false)
    if obj_instances == nil then return false end
    local count = #obj_instances
    
    if count > 10 then
        in_menu = true
    end
    
    -- camp equipment upgrade stations
    if equipment_upgrade_c == nil then equipment_upgrade_c = find_required_object("Class /Script/Alabama.EquipmentUpgradeWidget") end
    if in_menu == false and equipment_upgrade_c ~= nil then
        local equip_instances = equipment_upgrade_c:get_objects_matching(false)
        if equip_instances ~= nil and #equip_instances > 1 then
            in_menu = true
        end
    end
    
    -- camp enchant stations
    if equipment_enchant_c == nil then equipment_enchant_c = find_required_object("Class /Script/Alabama.InventoryItemEnchantListWidget") end
    if in_menu == false and equipment_enchant_c ~= nil then
        local enchant_instances = equipment_enchant_c:get_objects_matching(false)
        if enchant_instances ~= nil and #enchant_instances > 1 then
            in_menu = true
        end
    end
    
    -- books
    if force_clear_examinable_timer >= FORCE_CLEAR_TIME then
        if examinable_widget_c == nil then examinable_widget_c = find_required_object("Class /Script/Alabama.ExaminableWidget") end
        if in_menu == false and examinable_widget_c ~= nil then
            local examinable_instances = examinable_widget_c:get_objects_matching(false)
            if examinable_instances ~= nil and #examinable_instances >= 1 then
                in_menu = true
            end
        end
    end
    
    -- pause menu
    if in_menu == false then in_menu = pause_opened end
    
    return in_menu
    
end

local function set_weapon_opacity(current_weapon, enable)
--ShieldMaterial->SetScalarParameterValue("AlphaMask", 0.5);   // Half visible
--ShieldMaterial->SetScalarParameterValue("MaskAxis", 0);      // Horizontal
--ShieldMaterial->SetScalarParameterValue("EnableAlphaMask", 1.0);
-- RuneDynamicMaterial
    debugOut(string.format("Setting shield opacity: %s", enable))
	if current_weapon ~= nil then
		local material = current_weapon.RuneDynamicMaterial
		if material ~= nil and material.SetScalarParameterValue ~= nil then
			if enable == true then
                debugOut("setting opacity 0.5")
				material:SetScalarParameterValue("AlphaMask", 0.5)
				material:SetScalarParameterValue("MaskAxis", 0)
				material:SetScalarParameterValue("EnableAlphaMask", 1.0)
			else
                debugOut("setting opacity 1.0")
				material:SetScalarParameterValue("AlphaMask", 1.0)
				material:SetScalarParameterValue("MaskAxis", 0)
				material:SetScalarParameterValue("EnableAlphaMask", 0.0)
			end
		end
	end
end

local function detach_weapon_old(which_hand)
    if NATIVE_BONE_DRIVER_OWNS_WEAPONS == 1 then
        return
    end

    if which_hand == "right" then
        if attached_right_weapon ~= nil then
            enqueue_deferred_detach(attached_right_weapon, "right")
            attached_right_weapon = nil
            attached_right_weapon_base = nil
        end
    else
        if attached_left_weapon ~= nil then
            enqueue_deferred_detach(attached_left_weapon, "left")
            attached_left_weapon = nil
            attached_left_weapon_base = nil
        end
    end
end

function force_remove_script_weapon_state(w, hide_mesh)
    if w == nil or is_object_usable(w) ~= true then
        return
    end

    pcall(function()
        if DIRECT_WEAPON_DRIVE ~= 1 and w.DetachFromParent ~= nil then
            w:DetachFromParent(false, false)
        end

        if UEVR_UObjectHook.remove_motion_controller_state ~= nil then
            UEVR_UObjectHook.remove_motion_controller_state(w)
        end

        if hide_mesh == true then
            w:SetHiddenInGame(true, true)
            w:SetVisibility(false, true)
        end
    end)
end

function force_clear_script_weapon_states()
    force_remove_script_weapon_state(attached_right_weapon, true)
    force_remove_script_weapon_state(attached_left_weapon, true)

    if not same_object(current_right_weapon, attached_right_weapon) then
        force_remove_script_weapon_state(current_right_weapon, false)
    end
    if not same_object(current_left_weapon, attached_left_weapon) then
        force_remove_script_weapon_state(current_left_weapon, false)
    end
    if not same_object(previous_right_weapon, attached_right_weapon) and not same_object(previous_right_weapon, current_right_weapon) then
        force_remove_script_weapon_state(previous_right_weapon, false)
    end
    if not same_object(previous_left_weapon, attached_left_weapon) and not same_object(previous_left_weapon, current_left_weapon) then
        force_remove_script_weapon_state(previous_left_weapon, false)
    end

    deferred_detach_queue = { pause_until_t = 0.0, death_grace_s = 2.0 }
end

function hide_previous_same_hand_weapon_mesh(previous_weapon, previous_weapon_base, which_hand)
    if previous_weapon == nil or is_object_usable(previous_weapon) ~= true then
        return
    end

    pcall(function()
        previous_weapon:SetHiddenInGame(true, true)
        previous_weapon:SetVisibility(false, true)

        if previous_weapon_base ~= nil and is_object_usable(previous_weapon_base) == true and set_visibility_for_component_chain ~= nil then
            set_visibility_for_component_chain(previous_weapon_base, false)
            -- Do not clear/hide wand Niagara here; direct FX manipulation breaks ice-tip animations.
        end

        AvowedLuaPerf.swap_hide_previous_count = AvowedLuaPerf.swap_hide_previous_count + 1
        debugOut("Hid previous weapon mesh for hand " .. tostring(which_hand))
    end)
end

local function detach_weapon(which_hand)
    if NATIVE_BONE_DRIVER_OWNS_WEAPONS == 1 then
        return
    end

    if USE_OLD_OBJ_HOOK_METHOD == 1 then
        return detach_weapon_old(which_hand)
    end
    
    if which_hand == "right" then
        if attached_right_weapon ~= nil then
            local w = attached_right_weapon
            if is_object_usable(w) == true then
                pcall(function()
                    -- Prevent stacked meshes on loadout swaps in component/anchor mode.
                    -- In direct-drive mode, keep hierarchy/sockets intact for projectile emitters.
                    if DIRECT_WEAPON_DRIVE ~= 1 and w.DetachFromParent ~= nil then
                        w:DetachFromParent(false, false)
                    end
                    if UEVR_UObjectHook.remove_motion_controller_state ~= nil then
                        UEVR_UObjectHook.remove_motion_controller_state(w)
                    end
                    w:SetHiddenInGame(true, true)
                    w:SetVisibility(false, true)
                end)
            end
			--controllers.destroyController(1)
            attached_right_weapon = nil
            attached_right_weapon_base = nil
        end
    else
        if attached_left_weapon ~= nil then
            local w = attached_left_weapon
            if is_object_usable(w) == true then
                pcall(function()
                    if DIRECT_WEAPON_DRIVE ~= 1 and w.DetachFromParent ~= nil then
                        w:DetachFromParent(false, false)
                    end
                    if UEVR_UObjectHook.remove_motion_controller_state ~= nil then
                        UEVR_UObjectHook.remove_motion_controller_state(w)
                    end
                    w:SetHiddenInGame(true, true)
                    w:SetVisibility(false, true)
                end)
            end
			--controllers.destroyController(0)
            attached_left_weapon = nil
            attached_left_weapon_base = nil
        end
    end
end

function set_script_enabled(enabled)
    disableMod = enabled and 0 or 1

    if disableMod ~= 0 then
        force_clear_script_weapon_states()

        attached_right_weapon = nil
        attached_left_weapon = nil
        attached_right_weapon_base = nil
        attached_left_weapon_base = nil
        current_right_weapon = nil
        current_left_weapon = nil
        current_right_weapon_base = nil
        current_left_weapon_base = nil
        previous_right_weapon = nil
        previous_left_weapon = nil
        changed_loadout = false
        loadout_change_cooldown_s = 0.0
        old_attach_refresh_last_t.right = 0.0
        old_attach_refresh_last_t.left = 0.0
        post_event_recovery_throttle_frames = 0
        post_event_recovery_throttle_counter = 0
    else
        changed_loadout = true
        loadout_change_cooldown_s = 0.0
        post_event_recovery_throttle_frames = 0
        post_event_recovery_throttle_counter = 0
        clear_slot_cache()
    end

    if AvowedLuaPerf ~= nil then
        AvowedLuaPerf.pre_tick_sum = 0.0
        AvowedLuaPerf.pre_tick_max = 0.0
        AvowedLuaPerf.pre_tick_samples = 0
        AvowedLuaPerf.xinput_sum = 0.0
        AvowedLuaPerf.xinput_max = 0.0
        AvowedLuaPerf.xinput_samples = 0
        avowed_lua_perf_reset_xinput_blocks()
        AvowedLuaPerf.loadout_hook_count = 0
        AvowedLuaPerf.probe_update_count = 0
        AvowedLuaPerf.reattach_right_attempts = 0
        AvowedLuaPerf.reattach_left_attempts = 0
        AvowedLuaPerf.old_attach_refresh_right = 0
        AvowedLuaPerf.old_attach_refresh_left = 0
        AvowedLuaPerf.attach_failures = 0
        AvowedLuaPerf.fallback_old_attach_count = 0
        AvowedLuaPerf.visibility_refreshes = 0
        AvowedLuaPerf.swap_hide_previous_count = 0
        AvowedLuaPerf.left_trigger_frames = 0
        AvowedLuaPerf.left_trigger_menu_returns = 0
        AvowedLuaPerf.left_trigger_event_returns = 0
        AvowedLuaPerf.left_trigger_levelwait_returns = 0
        AvowedLuaPerf.left_trigger_non_grimoire_frames = 0
        AvowedLuaPerf.left_grimoire_candidate_frames = 0
        AvowedLuaPerf.left_grimoire_trigger_frames = 0
        AvowedLuaPerf.right_trigger_frames = 0
        AvowedLuaPerf.right_trigger_menu_returns = 0
        AvowedLuaPerf.right_trigger_event_returns = 0
        AvowedLuaPerf.right_trigger_levelwait_returns = 0
        AvowedLuaPerf.right_trigger_other_frames = 0
        AvowedLuaPerf.right_wand_candidate_frames = 0
        AvowedLuaPerf.right_wand_trigger_frames = 0
        AvowedLuaPerf.right_grimoire_candidate_frames = 0
        AvowedLuaPerf.right_grimoire_trigger_frames = 0
        AvowedLuaPerf.right_bow_candidate_frames = 0
        AvowedLuaPerf.right_bow_trigger_frames = 0
        AvowedLuaPerf.post_event_recoveries = 0
        AvowedLuaPerf.post_event_probe_skips = 0
        AvowedLuaPerf.post_event_loadout_skips = 0
        AvowedLuaPerf.loadout_swap_button_frames = 0
        AvowedLuaPerf.loadout_swap_button_releases = 0
        AvowedLuaPerf.loadout_hook_after_swap_input = 0
        AvowedLuaPerf.loadout_swap_to_hook_sum_ms = 0.0
        AvowedLuaPerf.loadout_swap_to_hook_max_ms = 0.0
        AvowedLuaPerf.right_trigger_recent_loadout_frames = 0
        AvowedLuaPerf.right_trigger_other_recent_loadout_frames = 0
        AvowedLuaPerf.right_trigger_wand_attach_mismatch_frames = 0
        AvowedLuaPerf.bow_frames = 0
        AvowedLuaPerf.wand_frames = 0
        AvowedLuaPerf.melee_frames = 0
        AvowedLuaPerf.last_log = 0.0
    end

    current_aim_method = vr:get_mod_value("VR_AimMethod")
    last_applied_aim_method = tostring(current_aim_method)
end

local UPrimitiveComponent_C = find_required_object("Class /Script.Engine.PrimitiveComponent")

function is_wand_fx_component(component)
    if component == nil then return false end

    local comp_name = ""
    local class_name = ""
    pcall(function() comp_name = component:get_fname():to_string() or "" end)
    pcall(function()
        local cls = component:get_class()
        if cls ~= nil then class_name = cls:get_full_name() or "" end
    end)

    comp_name = string.lower(comp_name)
    class_name = string.lower(class_name)

    return string.find(class_name, "niagara", 1, true) ~= nil or
           string.find(comp_name, "niagara", 1, true) ~= nil or
           string.find(comp_name, "vfx", 1, true) ~= nil or
           string.find(comp_name, "particle", 1, true) ~= nil or
           string.find(comp_name, "flame", 1, true) ~= nil or
           string.find(comp_name, "fire", 1, true) ~= nil
end

function apply_wand_fx_attach_state(weapon_base, hand_number)
    if weapon_base == nil or is_object_usable(weapon_base) ~= true then return end

    pcall(function()
        local visual_components = weapon_base.SpawnedVisualComponents
        if visual_components == nil then return end

        for _, visual_component in ipairs(visual_components) do
            if is_wand_fx_component(visual_component) == true and is_object_usable(visual_component) == true then
                local attach = UEVR_UObjectHook.get_or_add_motion_controller_state(visual_component)
                visual_component:SetVisibility(true, true)
                visual_component:SetHiddenInGame(false, true)
                attach:set_hand(hand_number)
                attach:set_permanent(false)
                attach:set_rotation_offset(temp_vec3f:set(1.570, 0, 0))
            end
        end
    end)
end

function clear_wand_fx_attach_state(weapon_base, hide_fx)
    if weapon_base == nil or is_object_usable(weapon_base) ~= true then return end

    pcall(function()
        local visual_components = weapon_base.SpawnedVisualComponents
        if visual_components == nil then return end

        for _, visual_component in ipairs(visual_components) do
            if is_wand_fx_component(visual_component) == true and is_object_usable(visual_component) == true then
                if UEVR_UObjectHook.remove_motion_controller_state ~= nil then
                    UEVR_UObjectHook.remove_motion_controller_state(visual_component)
                end
                if hide_fx == true then
                    visual_component:SetHiddenInGame(true, true)
                    visual_component:SetVisibility(false, true)
                end
            end
        end
    end)
end

function apply_old_attach_state(current_weapon, hand_number, weapon_base)
    local attach_ok = false

    pcall(function()
        local weapon_name = ""
        if current_weapon.AttachParent ~= nil and current_weapon.AttachParent.get_full_name ~= nil then
            weapon_name = current_weapon.AttachParent:get_full_name() or ""
        elseif current_weapon.get_full_name ~= nil then
            weapon_name = current_weapon:get_full_name() or ""
        end

        local attach = UEVR_UObjectHook.get_or_add_motion_controller_state(current_weapon)
        current_weapon:SetVisibility(true, false)
        current_weapon:SetHiddenInGame(false, true)

        attach:set_hand(hand_number)
        attach:set_permanent(false)

        if string.find(weapon_name, "Wand") then
            attach:set_rotation_offset(temp_vec3f:set(1.570, 0, 0))
            -- Do not attach wand Niagara/FX directly; this made fire hover disappear and broke ice-tip animations.
        elseif string.find(weapon_name, "Bow") then
            attach:set_rotation_offset(temp_vec3f:set(.1, -.10, 0))
        elseif string.find(weapon_name, "Arquebus") then
            attach:set_rotation_offset(temp_vec3f:set(0, 0, 0))
        elseif string.find(weapon_name, "Shield") then
            attach:set_rotation_offset(temp_vec3f:set(0, 0, .75))
            if current_weapon.RelativeScale3D ~= nil then
                current_weapon.RelativeScale3D.X = SHIELD_NORMAL_SIZE
                current_weapon.RelativeScale3D.Y = SHIELD_NORMAL_SIZE
                current_weapon.RelativeScale3D.Z = SHIELD_NORMAL_SIZE
            end
        elseif not string.find(weapon_name, "Pistol") then
            attach:set_rotation_offset(temp_vec3f:set(1.000, 0, 0))
        end

        attach_ok = true
    end)

    return attach_ok
end

local function reattach_weapon_old(current_weapon, which_hand)
    if NATIVE_BONE_DRIVER_OWNS_WEAPONS == 1 then
        return true
    end

    local hand_number = 0
    if which_hand == "right" then hand_number = 1 end

    if current_weapon == nil then
        debugOut("No weapon for hand " .. which_hand .. " (old attach), keeping previous attachment")
        return true
    end

    local previously_attached = nil
    local previously_attached_base = nil
    if which_hand == "right" then
        previously_attached = attached_right_weapon
        previously_attached_base = attached_right_weapon_base
    else
        previously_attached = attached_left_weapon
        previously_attached_base = attached_left_weapon_base
    end

    -- Same weapon already attached (address-based check)
    if same_object(previously_attached, current_weapon) then
        local refresh_key = which_hand == "right" and "right" or "left"
        local now_t = os.clock()

        if (now_t - (old_attach_refresh_last_t[refresh_key] or 0.0)) >= OLD_ATTACH_REFRESH_INTERVAL_S then
            if apply_old_attach_state(current_weapon, hand_number, previously_attached_base) then
                old_attach_refresh_last_t[refresh_key] = now_t
                if which_hand == "right" then
                    AvowedLuaPerf.old_attach_refresh_right = AvowedLuaPerf.old_attach_refresh_right + 1
                else
                    AvowedLuaPerf.old_attach_refresh_left = AvowedLuaPerf.old_attach_refresh_left + 1
                end
                debugOut("Refreshed old attach state for hand " .. which_hand)
            else
                AvowedLuaPerf.attach_failures = AvowedLuaPerf.attach_failures + 1
            end
        end

        return true
    end

    debugOut("Weapon changed for hand " .. which_hand .. ", attach-first then defer old detach")

    if which_hand == "right" then
        AvowedLuaPerf.reattach_right_attempts = AvowedLuaPerf.reattach_right_attempts + 1
    else
        AvowedLuaPerf.reattach_left_attempts = AvowedLuaPerf.reattach_left_attempts + 1
    end

    -- Phase 1: Attach new weapon FIRST before touching old state
    debugOut("Weapon is valid, attaching")
    local attach_ok = apply_old_attach_state(current_weapon, hand_number, which_hand == "right" and current_right_weapon_base or current_left_weapon_base)

    -- Phase 2: Only update tracking and queue old detach if new attach succeeded
    if attach_ok then
        old_attach_refresh_last_t[which_hand == "right" and "right" or "left"] = os.clock()
        if which_hand == "right" then
            if previously_attached ~= nil and not same_object(previously_attached, current_weapon) then
                hide_previous_same_hand_weapon_mesh(previously_attached, previously_attached_base, "right")
                enqueue_deferred_detach(previously_attached, "right")
            end
            attached_right_weapon = current_weapon
            attached_right_weapon_base = current_right_weapon_base
        else
            if previously_attached ~= nil and not same_object(previously_attached, current_weapon) then
                hide_previous_same_hand_weapon_mesh(previously_attached, previously_attached_base, "left")
                enqueue_deferred_detach(previously_attached, "left")
            end
            attached_left_weapon = current_weapon
            attached_left_weapon_base = current_left_weapon_base
        end
    else
        AvowedLuaPerf.attach_failures = AvowedLuaPerf.attach_failures + 1
        debugOut("New weapon attach FAILED for hand " .. which_hand .. ", keeping old attachment")
    end

    return attach_ok
end

function get_camera_world_pose_for_weapon_drive()
    local pc = nil
    local cam_mgr = nil
    local cam_loc = nil
    local cam_rot = nil

    pcall(function() pc = api:get_player_controller(0) end)
    if pc ~= nil then
        pcall(function() cam_mgr = pc.PlayerCameraManager end)
    end

    if cam_mgr ~= nil then
        pcall(function()
            if cam_mgr.GetCameraLocation ~= nil then
                cam_loc = cam_mgr:GetCameraLocation()
            end
        end)
        pcall(function()
            if cam_mgr.GetCameraRotation ~= nil then
                cam_rot = cam_mgr:GetCameraRotation()
            end
        end)
    end

    if cam_loc == nil or cam_rot == nil then
        local pawn = nil
        pcall(function() pawn = api:get_local_pawn() end)
        if pawn ~= nil then
            if cam_loc == nil then pcall(function() cam_loc = pawn:K2_GetActorLocation() end) end
            if cam_rot == nil then pcall(function() cam_rot = pawn:K2_GetActorRotation() end) end
        end
    end

    return cam_loc, cam_rot
end

function get_weapon_name_for_direct_drive(weapon)
    local weapon_name = ""
    pcall(function() weapon_name = weapon:get_full_name() or "" end)
    return weapon_name
end

function get_hand_relative_pose_for_weapon_drive(hand_number, weapon)
    local right_controller_index = vr.get_right_controller_index()
    local left_controller_index = vr.get_left_controller_index()
    local controller_index = (hand_number == 1) and right_controller_index or left_controller_index
    local hmd_index = vr.get_hmd_index()

    if right_controller_index ~= nil and left_controller_index ~= nil and right_controller_index >= 0 and left_controller_index >= 0 and right_controller_index == left_controller_index then
        if hand_number == 0 then
            if direct_drive_index_conflict_logged ~= true then
                direct_drive_index_conflict_logged = true
                debugOut("Direct drive index conflict: left/right controller index are equal (" .. tostring(left_controller_index) .. "), skipping left-hand drive this frame")
            end
            return nil, nil
        end
    else
        direct_drive_index_conflict_logged = false
    end

    if controller_index == nil or hmd_index == nil or controller_index < 0 or hmd_index < 0 then
        return nil, nil
    end

    local controller_pos = UEVR_Vector3f.new()
    local controller_rot_q = UEVR_Quaternionf.new()
    local hmd_pos = UEVR_Vector3f.new()
    local hmd_rot_q = UEVR_Quaternionf.new()

    local weapon_name = get_weapon_name_for_direct_drive(weapon)
    local use_aim_pose = false
    if string.find(weapon_name, "Wand") or string.find(weapon_name, "Bow") or string.find(weapon_name, "Pistol") or string.find(weapon_name, "Arquebus") then
        use_aim_pose = true
    end

    local ok_controller = false
    pcall(function()
        if use_aim_pose and vr.get_aim_pose ~= nil then
            vr.get_aim_pose(controller_index, controller_pos, controller_rot_q)
        elseif vr.get_grip_pose ~= nil then
            vr.get_grip_pose(controller_index, controller_pos, controller_rot_q)
        else
            vr.get_pose(controller_index, controller_pos, controller_rot_q)
        end
        ok_controller = true
    end)

    local ok_hmd = false
    pcall(function()
        vr.get_pose(hmd_index, hmd_pos, hmd_rot_q)
        ok_hmd = true
    end)

    if not ok_controller or not ok_hmd then
        return nil, nil
    end

    local dx = controller_pos.x - hmd_pos.x
    local dy = controller_pos.y - hmd_pos.y
    local dz = controller_pos.z - hmd_pos.z

    local rel_pos = {
        X = -dz * 100.0,
        Y = dx * 100.0,
        Z = dy * 100.0
    }

    local controller_rot = uevrUtils.rotatorFromQuat(controller_rot_q.z, controller_rot_q.x, -controller_rot_q.y, -controller_rot_q.w)
    local hmd_rot = uevrUtils.rotatorFromQuat(hmd_rot_q.z, hmd_rot_q.x, -hmd_rot_q.y, -hmd_rot_q.w)
    local rel_rot = uevrUtils.sumRotators(controller_rot, {Pitch=-hmd_rot.Pitch, Yaw=-hmd_rot.Yaw, Roll=-hmd_rot.Roll})

    return rel_pos, rel_rot
end

function rotate_xy_by_yaw(v, yaw_deg)
    local r = math.rad(yaw_deg or 0.0)
    local c = math.cos(r)
    local s = math.sin(r)
    -- UE-style yaw composition (left-handed) to avoid mirrored lateral hand motion.
    return {
        X = (v.X * c) + (v.Y * s),
        Y = (-v.X * s) + (v.Y * c),
        Z = v.Z
    }
end

function get_weapon_rotation_offset(weapon)
    local weapon_name = get_weapon_name_for_direct_drive(weapon)

    if string.find(weapon_name, "Wand") then
        return {Pitch=270.0,Yaw=0.0,Roll=0.0}
    elseif string.find(weapon_name, "Bow") then
        return {Pitch=-5.0,Yaw=6.0,Roll=0.0}
    elseif string.find(weapon_name, "Arquebus") or string.find(weapon_name, "Pistol") then
        return {Pitch=0.0,Yaw=0.0,Roll=0.0}
    elseif string.find(weapon_name, "Shield") then
        return {Pitch=0.0,Yaw=0.0,Roll=0.0}
    elseif string.find(weapon_name, "Grimoire") then
        return {Pitch=90.0,Yaw=0.0,Roll=0.0}
    end

    return {Pitch=-50.0,Yaw=0.0,Roll=0.0}
end

function get_direct_drive_offsets(weapon, hand_number)
    local weapon_name = get_weapon_name_for_direct_drive(weapon)
    local side = (hand_number == 1) and DIRECT_OFFSETS.right or DIRECT_OFFSETS.left

    local pos = {X=0.0, Y=0.0, Z=0.0}
    local rot = {Pitch=0.0, Yaw=0.0, Roll=0.0}

    if side ~= nil then
        if side.pos ~= nil then
            pos.X = side.pos.X or 0.0
            pos.Y = side.pos.Y or 0.0
            pos.Z = side.pos.Z or 0.0
        end
        if side.rot ~= nil then
            rot.Pitch = side.rot.Pitch or 0.0
            rot.Yaw = side.rot.Yaw or 0.0
            rot.Roll = side.rot.Roll or 0.0
        end
    end

    local extra = nil
    if string.find(weapon_name, "Wand") then
        extra = DIRECT_OFFSETS.wand
    elseif string.find(weapon_name, "Grimoire") then
        extra = DIRECT_OFFSETS.grimoire
    end

    if extra ~= nil then
        if extra.pos ~= nil then
            pos.X = pos.X + (extra.pos.X or 0.0)
            pos.Y = pos.Y + (extra.pos.Y or 0.0)
            pos.Z = pos.Z + (extra.pos.Z or 0.0)
        end
        if extra.rot ~= nil then
            rot.Pitch = rot.Pitch + (extra.rot.Pitch or 0.0)
            rot.Yaw = rot.Yaw + (extra.rot.Yaw or 0.0)
            rot.Roll = rot.Roll + (extra.rot.Roll or 0.0)
        end
    end

    return pos, rot
end

function stabilize_direct_weapon_rotation(weapon, hand_number, cam_rot, world_rot)
    local weapon_name = get_weapon_name_for_direct_drive(weapon)
    local _, off_rot = get_direct_drive_offsets(weapon, hand_number)

    if string.find(weapon_name, "Grimoire") then
        return {
            Pitch = 90.0 + (off_rot.Pitch or 0.0),
            Yaw = (cam_rot.Yaw or 0.0) + (off_rot.Yaw or 0.0),
            Roll = (off_rot.Roll or 0.0)
        }
    end

    -- Keep true controller-driven 6DoF orientation for weapons by default.
    return world_rot
end

function apply_direct_weapon_drive(weapon, hand_number)
    if weapon == nil or is_object_usable(weapon) ~= true then
        return false
    end

    local cam_loc, cam_rot = get_camera_world_pose_for_weapon_drive()
    if cam_loc == nil or cam_rot == nil then
        local k = "cam_nil_" .. tostring(hand_number)
        if direct_drive_diag[k] ~= true then
            direct_drive_diag[k] = true
            debugOut("Direct drive missing camera pose for hand " .. tostring(hand_number))
        end
        return false
    end

    local rel_pos, rel_rot = get_hand_relative_pose_for_weapon_drive(hand_number, weapon)
    if rel_pos == nil or rel_rot == nil then
        local k = "pose_nil_" .. tostring(hand_number)
        if direct_drive_diag[k] ~= true then
            direct_drive_diag[k] = true
            debugOut("Direct drive missing VR pose for hand " .. tostring(hand_number))
        end
        return false
    end

    local off_pos, off_rot = get_direct_drive_offsets(weapon, hand_number)
    rel_pos = {
        X = rel_pos.X + (off_pos.X or 0.0),
        Y = rel_pos.Y + (off_pos.Y or 0.0),
        Z = rel_pos.Z + (off_pos.Z or 0.0)
    }

    local rotated = rotate_xy_by_yaw(rel_pos, cam_rot.Yaw or 0.0)
    local world_pos = {
        X = cam_loc.X + rotated.X,
        Y = cam_loc.Y + rotated.Y,
        Z = cam_loc.Z + rotated.Z
    }

    local world_rot = uevrUtils.sumRotators(cam_rot, rel_rot, get_weapon_rotation_offset(weapon), off_rot)
    world_rot = stabilize_direct_weapon_rotation(weapon, hand_number, cam_rot, world_rot)

    pcall(function()
        weapon:SetHiddenInGame(false, true)
        weapon:SetVisibility(true, true)

        if DIRECT_DRIVE_MOVE_OWNER_ACTOR == 1 and weapon.GetOwner ~= nil then
            local owner = weapon:GetOwner()
            if owner ~= nil then
                if owner.K2_SetActorLocationAndRotation ~= nil then
                    owner:K2_SetActorLocationAndRotation(uevrUtils.vector(world_pos), uevrUtils.rotator(world_rot), false, reusable_hit_result, false)
                else
                    if owner.K2_SetActorLocation ~= nil then
                        owner:K2_SetActorLocation(uevrUtils.vector(world_pos), false, reusable_hit_result, false)
                    end
                    if owner.K2_SetActorRotation ~= nil then
                        owner:K2_SetActorRotation(uevrUtils.rotator(world_rot), false)
                    end
                end
            end
        end

        if weapon.K2_SetWorldLocationAndRotation ~= nil then
            weapon:K2_SetWorldLocationAndRotation(uevrUtils.vector(world_pos), uevrUtils.rotator(world_rot), false, reusable_hit_result, false)
        else
            if weapon.K2_SetWorldLocation ~= nil then
                weapon:K2_SetWorldLocation(uevrUtils.vector(world_pos), false, reusable_hit_result, false)
            end
            if weapon.K2_SetWorldRotation ~= nil then
                weapon:K2_SetWorldRotation(uevrUtils.rotator(world_rot), false, reusable_hit_result, false)
            end
        end
    end)

    local ok_key = "ok_" .. tostring(hand_number)
    if direct_drive_diag[ok_key] ~= true then
        direct_drive_diag[ok_key] = true
        debugOut("Direct drive active for hand " .. tostring(hand_number))
    end

    return true
end

function set_component_absolute_for_direct_drive(component)
    if component == nil then return end

    pcall(function()
        if component.SetAbsolute ~= nil then
            component:SetAbsolute(true, true, false)
            return
        end

        if component.SetUsingAbsoluteLocation ~= nil then component:SetUsingAbsoluteLocation(true) end
        if component.SetUsingAbsoluteRotation ~= nil then component:SetUsingAbsoluteRotation(true) end
        if component.SetUsingAbsoluteScale ~= nil then component:SetUsingAbsoluteScale(false) end
    end)

    pcall(function()
        if component.bAbsoluteLocation ~= nil then component.bAbsoluteLocation = true end
        if component.bAbsoluteRotation ~= nil then component.bAbsoluteRotation = true end
        if component.bAbsoluteScale ~= nil then component.bAbsoluteScale = false end
    end)
end

function reattach_weapon_direct(current_weapon, which_hand)
    if NATIVE_BONE_DRIVER_OWNS_WEAPONS == 1 then
        return true
    end

    local hand_number = 0
    if which_hand == "right" then hand_number = 1 end

    if current_weapon == nil then
        debugOut("No weapon for hand " .. which_hand .. " (direct drive), keeping previous attachment")
        return true
    end

    local previously_attached = nil
    if which_hand == "right" then
        previously_attached = attached_right_weapon
    else
        previously_attached = attached_left_weapon
    end

    if same_object(previously_attached, current_weapon) then
        return true
    end

    if which_hand == "left" and attached_right_weapon ~= nil and same_object(current_weapon, attached_right_weapon) then
        debugOut("Direct attach skip left: same component as right hand")
        attached_left_weapon = nil
        attached_left_weapon_base = nil
        return true
    end

    if which_hand == "right" and attached_left_weapon ~= nil and same_object(current_weapon, attached_left_weapon) then
        debugOut("Direct attach skip right: same component as left hand")
        attached_right_weapon = nil
        attached_right_weapon_base = nil
        return true
    end

    if previously_attached ~= nil and not same_object(previously_attached, current_weapon) then
        detach_weapon(which_hand)
    end

    pcall(function()
        -- Keep hierarchy for muzzle/sockets/projectile origins; force world-absolute transform instead.
        set_component_absolute_for_direct_drive(current_weapon)
        debugOut("Direct attach target (" .. tostring(which_hand) .. "): " .. tostring(current_weapon:get_full_name()))

        if UEVR_UObjectHook.remove_motion_controller_state ~= nil then
            UEVR_UObjectHook.remove_motion_controller_state(current_weapon)
        end
        current_weapon:SetHiddenInGame(false, true)
        current_weapon:SetVisibility(true, true)
    end)

    if which_hand == "right" then
        attached_right_weapon = current_weapon
        attached_right_weapon_base = current_right_weapon_base
    else
        attached_left_weapon = current_weapon
        attached_left_weapon_base = current_left_weapon_base
    end

    return apply_direct_weapon_drive(current_weapon, hand_number)
end

function update_direct_weapon_drive_tick()
    if NATIVE_BONE_DRIVER_OWNS_WEAPONS == 1 then return end
    if DIRECT_WEAPON_DRIVE ~= 1 then return end
    if is_in_main_menu == true then return end

    if last_sheathed_visible == false then
        pcall(function()
            if attached_right_weapon ~= nil then
                attached_right_weapon:SetHiddenInGame(true, true)
                attached_right_weapon:SetVisibility(false, true)
            end
            if attached_left_weapon ~= nil then
                attached_left_weapon:SetHiddenInGame(true, true)
                attached_left_weapon:SetVisibility(false, true)
            end
        end)
        return
    end

    if attached_right_weapon ~= nil then
        apply_direct_weapon_drive(attached_right_weapon, 1)
    end

    if attached_left_weapon ~= nil and is_2_handed ~= true then
        if not same_object(attached_left_weapon, attached_right_weapon) then
            apply_direct_weapon_drive(attached_left_weapon, 0)
        end
    end
end

local function reattach_weapon(current_weapon, which_hand)
    if NATIVE_BONE_DRIVER_OWNS_WEAPONS == 1 then
        return true
    end

    if USE_OLD_OBJ_HOOK_METHOD == 1 then
        return reattach_weapon_old(current_weapon, which_hand)
    end

    if DIRECT_WEAPON_DRIVE == 1 then
        return reattach_weapon_direct(current_weapon, which_hand)
    end

	local changed = false
	local status = false
    local current_weapon_base = nil
	
    local hand_number = 0
    if which_hand == "right" then hand_number = 1 end
    local attach = nil
    
    debugOut("Weapon changed for hand " .. which_hand)

    if which_hand == "right" then
		current_weapon_base = current_right_weapon_base

        pcall(function()
            local c = (controllers.getAttachTarget ~= nil) and controllers.getAttachTarget(1) or controllers.getController(1)
            if c ~= nil then
                local ms = "nil"
                if c.MotionSource ~= nil and c.MotionSource.to_string ~= nil then
                    ms = c.MotionSource:to_string()
                end
                local hand = c.Hand ~= nil and tostring(c.Hand) or "nil"
                debugOut("Right controller resolved: source=" .. ms .. " hand=" .. hand)
            else
                debugOut("Right controller resolved: nil")
            end
        end)
    else
		current_weapon_base = current_left_weapon_base

        pcall(function()
            local c = (controllers.getAttachTarget ~= nil) and controllers.getAttachTarget(0) or controllers.getController(0)
            if c ~= nil then
                local ms = "nil"
                if c.MotionSource ~= nil and c.MotionSource.to_string ~= nil then
                    ms = c.MotionSource:to_string()
                end
                local hand = c.Hand ~= nil and tostring(c.Hand) or "nil"
                debugOut("Left controller resolved: source=" .. ms .. " hand=" .. hand)
            else
                debugOut("Left controller resolved: nil")
            end
        end)
    end

    if current_weapon ~= nil then
        local ok_name, weapon_name = pcall(function() return current_weapon:get_full_name() end)

        if ok_name and is_weapon_spell_or_ranged_name(weapon_name) then
            local previously_attached = nil

            if which_hand == "right" then
                previously_attached = attached_right_weapon
            else
                previously_attached = attached_left_weapon
            end

            if previously_attached ~= nil and not same_object(previously_attached, current_weapon) then
                pcall(function()
                    if UEVR_UObjectHook.remove_motion_controller_state ~= nil then
                        UEVR_UObjectHook.remove_motion_controller_state(previously_attached)
                    end
                end)
            end

            if which_hand == "right" then
                attached_right_weapon = nil
                attached_right_weapon_base = nil
            else
                attached_left_weapon = nil
                attached_left_weapon_base = nil
            end

            reset_attach_fail_streak(which_hand)
            return true
        end
    end

    local controller_component = (controllers.getAttachTarget ~= nil) and controllers.getAttachTarget(hand_number) or controllers.getController(hand_number)
    if controller_component ~= nil then
        local other_hand = hand_number == 1 and 0 or 1
        local other_component = (controllers.getAttachTarget ~= nil) and controllers.getAttachTarget(other_hand) or controllers.getController(other_hand)
        if other_component ~= nil then
            local a1, a2 = nil, nil
            pcall(function() a1 = controller_component:get_address() end)
            pcall(function() a2 = other_component:get_address() end)
            if a1 ~= nil and a2 ~= nil and a1 == a2 then
                debugOut("Controller conflict: both hands resolved to same component, hand=" .. tostring(hand_number))
                if controllers.debugDumpControllerCandidates ~= nil then
                    controllers.debugDumpControllerCandidates(hand_number, "same-address-conflict")
                end
                controller_component = nil
            end
        end
    end

    if controller_component == nil and current_weapon ~= nil then
        if controllers.debugDumpControllerCandidates ~= nil then
            controllers.debugDumpControllerCandidates(hand_number, "nil-controller-component")
        end
        local pose_state = "n/a"
        if controllers.getPoseAnchorDebugState ~= nil then
            pose_state = controllers.getPoseAnchorDebugState(hand_number)
        end

        local fail_streak = bump_attach_fail_streak(which_hand)

        if ALLOW_UOBJECTHOOK_ATTACH_FALLBACK == 1 and should_try_uobjecthook_fallback(which_hand) then
            AvowedLuaPerf.fallback_old_attach_count = AvowedLuaPerf.fallback_old_attach_count + 1
            debugOut("No valid attach target for hand " .. tostring(hand_number) .. " (" .. tostring(pose_state) .. "), using UObjectHook attach fallback after streak=" .. tostring(fail_streak))
            return reattach_weapon_old(current_weapon, which_hand)
        end

        debugOut("No valid attach target for hand " .. tostring(hand_number) .. " (" .. tostring(pose_state) .. "), skipping attach (component-only mode), streak=" .. tostring(fail_streak))
        return false
    end

    -- Buckler (shield)
    -- 1H_Axe, 1H_Club, 1H_Dagger, 1H_Rod_Proto, 1H_Sword, 1H_Wand
    -- 2H_Axe, 2H_Sword, 2H_Bow, 
    -- 0H_Grimoire
    -- OgreFist (creature?), 1H_Unarmed
    -- 1H_Pistol, 
    if current_weapon ~= nil then -- Attach new weapon if it exists
        if which_hand == "right" then
            AvowedLuaPerf.reattach_right_attempts = AvowedLuaPerf.reattach_right_attempts + 1
        else
            AvowedLuaPerf.reattach_left_attempts = AvowedLuaPerf.reattach_left_attempts + 1
        end

        status, changed = pcall(function()
            local previously_attached = nil
            local previously_attached_base = nil
            if which_hand == "right" then
                previously_attached = attached_right_weapon
                previously_attached_base = attached_right_weapon_base
            else
                previously_attached = attached_left_weapon
                previously_attached_base = attached_left_weapon_base
            end

            debugOut("Weapon is valid, attaching")
            local weapon_name = current_weapon:get_full_name()
			debugOut("Weapon Name: " .. weapon_name)
			-- avoid pre-detach churn; attach first, then adjust visibility
			current_weapon:SetVisibility(true, true)
			current_weapon:SetHiddenInGame(false, true)
            current_weapon.ForcedLodModel = 1 -- helps reduce flicker
            
            -- current_weapon:K2_AttachToComponent(current_weapon_base,"Root",0,0,0,false)
			local weaponConnected = controllers.attachComponentToController(hand_number, current_weapon, "", 0, false, true)
            if weaponConnected ~= true then
                if controllers.debugDumpControllerCandidates ~= nil then
                    controllers.debugDumpControllerCandidates(hand_number, "attach-failed")
                end
                local pose_state = "n/a"
                if controllers.getPoseAnchorDebugState ~= nil then
                    pose_state = controllers.getPoseAnchorDebugState(hand_number)
                end

                local fail_streak = bump_attach_fail_streak(which_hand)

                if ALLOW_UOBJECTHOOK_ATTACH_FALLBACK == 1 and should_try_uobjecthook_fallback(which_hand) then
                    AvowedLuaPerf.fallback_old_attach_count = AvowedLuaPerf.fallback_old_attach_count + 1
                    debugOut("controllers.attachComponentToController failed for hand " .. tostring(hand_number) .. " (" .. tostring(pose_state) .. "), using UObjectHook attach fallback after streak=" .. tostring(fail_streak))
                    return reattach_weapon_old(current_weapon, which_hand)
                end

                AvowedLuaPerf.attach_failures = AvowedLuaPerf.attach_failures + 1
                debugOut("controllers.attachComponentToController failed for hand " .. tostring(hand_number) .. " (" .. tostring(pose_state) .. "), keeping previous attachment, streak=" .. tostring(fail_streak))
                return false
            end

            reset_attach_fail_streak(which_hand)

            if which_hand == "right" then
                if previously_attached ~= nil and previously_attached ~= current_weapon then
                    hide_previous_same_hand_weapon_mesh(previously_attached, previously_attached_base, "right")
                    detach_weapon("right")
                end
                attached_right_weapon = current_weapon
                attached_right_weapon_base = current_right_weapon_base
            else
                if previously_attached ~= nil and previously_attached ~= current_weapon then
                    hide_previous_same_hand_weapon_mesh(previously_attached, previously_attached_base, "left")
                    detach_weapon("left")
                end
                attached_left_weapon = current_weapon
                attached_left_weapon_base = current_left_weapon_base
            end
            
            if string.find(weapon_name, "Wand") then
				uevrUtils.set_component_relative_transform(current_weapon, {X=0,Y=0,Z=0}, {Pitch=270.0,Yaw=0.0,Roll=0.0})
            elseif string.find(weapon_name, "Bow") then
				uevrUtils.set_component_relative_transform(current_weapon, {X=0,Y=0,Z=0}, {Pitch=-5.0,Yaw=6.0,Roll=0.0})
            elseif string.find(weapon_name, "Arquebus") or string.find(weapon_name, "Pistol") then
				uevrUtils.set_component_relative_transform(current_weapon, {X=0,Y=0,Z=0}, {Pitch=0,Yaw=0,Roll=0})
            elseif string.find(weapon_name, "Shield") then
				uevrUtils.set_component_relative_transform(current_weapon, {X=0,Y=0,Z=0}, {Pitch=0,Yaw=0,Roll=0.0}, {X=SHIELD_NORMAL_SIZE, Y=SHIELD_NORMAL_SIZE, Z=SHIELD_NORMAL_SIZE})
            else
				uevrUtils.set_component_relative_transform(current_weapon, {X=0,Y=0,Z=0}, {Pitch=-50.0,Yaw=0.0,Roll=0.0})
            end

            pcall(function()
                if current_weapon_base ~= nil then
                    -- Avoid globally unhiding full visual chains for ranged/spell weapons;
                    -- this can resurrect pooled FX meshes (e.g. green orb meshes).
                    local wn = string.lower(weapon_name or "")
                    local skip_chain = string.find(wn, "wand") or string.find(wn, "grimoire") or string.find(wn, "bow")
                    if not skip_chain then
                        set_visibility_for_component_chain(current_weapon_base, true)
                    end
                end
                current_weapon:SetVisibility(true, true)
                current_weapon:SetHiddenInGame(false, true)
            end)
                        
            return true 
        end)
    else
        debugOut("No weapon for hand " .. which_hand .. ", keeping previous attachment")
    end

	return changed
end

local function hook_on_eval_time(fn, obj, locals, result)
    time_of_day = locals.TimeOfDayNormalized
end


local function reinit_level_objects()
    --hud_switcher_hook = false
    --unsheath_hook = false
    changed_loadout = true
    loadout_change_cooldown_s = LOADOUT_CHANGE_COOLDOWN_MAX
    clear_slot_cache()
    
    -- Reset widget attachment for new level
    if USE_ATTACHED_WIDGETS == 1 then
        new_level_hud_timer = 0.0
        is_hud_attached = false
        skylight_component = nil  -- Force re-detection of SkyLight in new level
        debugOut("[WidgetBrightness] Level changed - resetting widget attachment")
        
 
        if is_time_of_day_hooked == false then
            is_time_of_day_hooked = true
            hook_function("BlueprintGeneratedClass /Game/Art/WeatherSystem/AlabamaWeatherSystem.AlabamaWeatherSystem_C", "OnEvalutateTimeOfDay", false, nil, hook_on_eval_time, true)
            --hook_function("BlueprintGeneratedClass /Game/Art/ENV/Utility/Lighting/LightingUtilities/BP_ToD_ExposureAdjust/BP_ToD_ExposureAdjust.BP_ToD_ExposureAdjust_C", "OnEvalutateTimeOfDay", false, nil, hook_on_eval_time, true)
        end
    end
end

-- Sheath / unsheath weapon callback (based on crosshair)
local function set_sheathed(fn, obj, locals, result)
    local Visible = true
    if locals.bIsSheathed == true then Visible = false end

    if last_sheathed_visible ~= nil and last_sheathed_visible == Visible then
        return
    end
    last_sheathed_visible = Visible
    sheathe_state_change_last_t = os.clock()
    
    if Visible == true then
        changed_loadout = true
        loadout_change_cooldown_s = LOADOUT_CHANGE_COOLDOWN_MAX
        clear_slot_cache()
        -- flags a weapon change so the weapons will be reattached
        previous_right_weapon = nil
        previous_left_weapon = nil
    else
        -- avoid hard-detach churn on sheath transitions; game can transiently hide meshes
        -- and we preserve attachment/visibility through refresh watchdog
    end
end



-- health, essense, the whole lower left
local function SetLowerLeftWidgetParams(Widget)
    uevrUtils.set_component_relative_transform(
        Widget,
        {X=0.0, Y=0.0, Z=10.0},      -- location
        {Pitch=48.0, Yaw=155.0, Roll=-19.0},  -- rotation
        {X=0.02, Y=0.02, Z=0.02}  -- scale
    )
end

local function SetMinimapWidgetParams(Widget)
    uevrUtils.set_component_relative_transform(
        Widget,
        {X=-8.8, Y=4.4, Z=2.9},      -- location
        {Pitch=58.0, Yaw=155.0, Roll=-19.0},  -- rotation
        {X=0.04, Y=0.04, Z=0.04}  -- scale
    )
end

local function SetQuestWidgetParams(Widget)
    uevrUtils.set_component_relative_transform(
        Widget,
        {X=-2.1, Y=-1.8, Z=8.8},      -- location
        {Pitch=178.0, Yaw=86.0, Roll=-34.0},  -- rotation
        {X=0.025, Y=0.025, Z=0.025}  -- scale
    )
end


-- Find SkyLight component in the level for brightness detection
local function find_skylight()
    if skylight_component ~= nil then return skylight_component end
    
    local pawn = api:get_local_pawn(0)
    if not pawn then return nil end
    
    local world = pawn:get_world()
    if not world then return nil end
    
    -- Try to find SkyLightComponent in the world
    pcall(function()
        local skylight_class = find_required_object("Class /Script/Engine.SkyLightComponent")
        if skylight_class then
            local skylights = skylight_class:get_objects_matching(false)
            if skylights and #skylights > 0 then
                skylight_component = skylights[1]
                debugOut("[WidgetBrightness] Found SkyLight component")
            end
        end
    end)
    
    return skylight_component
end

-- Adjust widget brightness
local function update_widget_brightness()
    if not is_hud_attached then return end
    if AttachedLowerHud == nil and AttachedMinimap == nil and AttachedQuests == nil then return end
    
    --time_of_day == 0.20 brightness_multiplier = 10
    --time_of_day == 0.27 brightness_multiplier = 20
    
    
    -- .69 t0 0,19 is night
    
    local brightness_multiplier = 15
    if time_of_day >= TOD_NIGHT_START or time_of_day < TOD_DAY_START then
        brightness_multiplier = 5
    end
    print("time of day: ", time_of_day, " brightness: ", brightness_multiplier)

    if WIDGET_BRIGHTNESS >= 1 then
        brightness_multiplier = WIDGET_BRIGHTNESS
    end
    
    -- Apply brightness to all attached widgets
    local function apply_brightness_to_widget(widget)
        if not widget then return end
        
        pcall(function()
            if widget.SetTintColorAndOpacity then
                widget:SetTintColorAndOpacity({R=brightness_multiplier, G=brightness_multiplier, B=brightness_multiplier, A=1})
            end
            
            if widget.ColorAndOpacity then
                local col = widget.ColorAndOpacity
                if type(col) == "table" then
                    col.R = (col.R or 1) * brightness_multiplier
                    col.G = (col.G or 1) * brightness_multiplier
                    col.B = (col.B or 1) * brightness_multiplier
                    widget.ColorAndOpacity = col
                end
            end
            
            if widget.bVisible ~= nil then
                widget.bVisible = true
            end
            
            if widget.RenderOpacity ~= nil then
                widget.RenderOpacity = 1.0
            end
        end)
    end
    
    apply_brightness_to_widget(AttachedLowerHud)
    apply_brightness_to_widget(AttachedMinimap)
    apply_brightness_to_widget(AttachedQuests)
end

--local wanted_mat = api:find_uobject("MaterialInstanceConstant /Engine/EngineMaterials/Widget3DPassThrough_Translucent.Widget3DPassThrough_Translucent")
local wanted_mat = api:find_uobject("MaterialInstanceConstant /Engine/EngineMaterials/Widget3DPassThrough_Opaque.Widget3DPassThrough_Opaque")

local function FixWidgetProperties(new_widget)
    new_widget.BlendMode = 2 -- Set to transparent
    new_widget.TimingPolicy = 0 -- Set to realtime
    
    if wanted_mat ~= nil then
        new_widget:SetMaterial(0, wanted_mat)
    end

    -- STALKER 2 LIGHTING FIXES: Disable scene lighting to prevent darkness
    pcall(function()
        if new_widget.SetCastShadow then new_widget:SetCastShadow(false) end
        if new_widget.CastShadow ~= nil then new_widget.CastShadow = false end
        if new_widget.SetReceivesDecals then new_widget:SetReceivesDecals(false) end
        if new_widget.SetLightingChannels then new_widget:SetLightingChannels(false, false, false) end
        if new_widget.SetBlendMode then new_widget:SetBlendMode(2) end -- Translucent
        debugOut("[WidgetLighting] Applied lighting fixes to LowerHud")
    end)
end

local function AttachWidgets()
    local success = false
    local left_corner_hud = nil
    local mini_map_hud = nil
    local quest_hud = nil

    if wanted_mat == nil then 
        wanted_mat = api:find_uobject("MaterialInstanceConstant /Engine/EngineMaterials/Widget3DPassThrough_Translucent.Widget3DPassThrough_Translucent")
    end
    
    if wanted_mat == nil then 
        debugOut("wanted_mat = nil")
    end
    
    -- note find_required_object can be costly, but these are blueprint generated and only exist once the level is loaded.
    -- this function is also called only after level load and on timer not every tick.
    -- when this function returns true, it will be considered done otherwise it will keep running every timer iteration.
    
    -- left controller
    if controllers.getController(0) == nil then
        debugOut("Creating controller 0")
        controllers.createController(0)
    end
    
    -- right controller
    if controllers.getController(1) == nil then
        debugOut("Creating controller 1")
        controllers.createController(1)
    end

    -- note the hud icons are in 
    -- WidgetBlueprintGeneratedClass /Game/UI/Widgets/Common/WBP_AbilityIconDisplay.WBP_AbilityIconDisplay_C
    -- the HUDPlayerAbility_1 to _6 are the ability icons
    -- the HUD_GrimoireIndex_ are eqipped grimoire.
    -- not sure what the GP_ are.
    -- Likely each of these icons has to be individually attached and positioned over the existing overlay.
    
    -- find the lower left HUD element.
    debugOut("Scanning for class hud_elements_c")
    local hud_elements_c = find_required_object("WidgetBlueprintGeneratedClass /Game/UI/Screens/PlayerHUD/SBP_HUDElements.SBP_HUDElements_C")
    if hud_elements_c ~= nil then
        if AttachedLowerHud ~= nil then uevrUtils.detachAndDestroyComponent(AttachedLowerHud, false, false) end
        AttachedLowerHud = uevrUtils.createWidgetComponent("WidgetBlueprintGeneratedClass /Game/UI/Screens/PlayerHUD/SBP_HUDElements.SBP_HUDElements_C", {manualAttachment=false, removeFromViewport=true, twoSided=false, drawSize=vector_2(2000, 1000)})
        if AttachedLowerHud ~= nil then 
            SetLowerLeftWidgetParams(AttachedLowerHud) 
            AttachedLowerHud.WidgetClass = hud_elements_c
            FixWidgetProperties(AttachedLowerHud)
        end
        debugOut("Attach to controllers")
        success = controllers.attachComponentToController(0, AttachedLowerHud, "LowerLeft", nil, nil, true)
    end
    
    if success == false then
        debugOut("Bailing on attaching the rest because lower left hud failed attach. Will try next loop")
        return success
    end

    -- Find the compass (minimap)
    debugOut("Scanning for class hud_minimap_c")
    local hud_minimap_c = find_required_object("WidgetBlueprintGeneratedClass /Game/UI/Widgets/Minimap/MinimapWidget.MinimapWidget_C")
    if hud_minimap_c ~= nil then
        if AttachedMinimap ~= nil then uevrUtils.detachAndDestroyComponent(AttachedMinimap, false, false) end
        AttachedMinimap = uevrUtils.createWidgetComponent("WidgetBlueprintGeneratedClass /Game/UI/Widgets/Minimap/MinimapWidget.MinimapWidget_C", {manualAttachment=false, removeFromViewport=true, twoSided=false, drawSize=vector_2(1000, 1000)})
        if AttachedMinimap ~= nil then 
            SetMinimapWidgetParams(AttachedMinimap) 
            AttachedMinimap.WidgetClass = hud_minimap_c
            FixWidgetProperties(AttachedMinimap)
        end
        debugOut("Attach to controllers")
        success = controllers.attachComponentToController(0, AttachedMinimap, "Minimap", nil, nil, true)

--[[
        if success == true then
            -- fix minimap color and transparency
            local minimap_instance = uevrUtils.getActiveWidgetByClass("WidgetBlueprintGeneratedClass /Game/UI/Widgets/Minimap/MinimapWidget.MinimapWidget_C")
            if minimap_instance ~= nil and minimap_instance.SetColorAndOpacity ~= nil then
                local Color = minimap_instance:GetLinearColor__DelegateSignature()
                Color.R = 0.5
                Color.G = 0.5
                Color.B = 0.5
                Color.A = 0.75
                
                minimap_instance:SetColorAndOpacity(Color)
                
                if minimap_instance.RemoveFromParent then 
                   -- minimap_instance:RemoveFromParent()
                end
            end
        end
]]
    end
    
    if success == false then
        debugOut("Bailing on attaching the rest because minimap failed attach. Will try next loop")
        return success
    end

    -- Find the quest hud
    debugOut("Scanning for class hud_quest_c")
    local hud_quest_c = find_required_object("WidgetBlueprintGeneratedClass /Game/UI/Widgets/Overlay/WBP_QuestTracker.WBP_QuestTracker_C")
    if hud_quest_c ~= nil then
        if AttachedQuests ~= nil then uevrUtils.detachAndDestroyComponent(AttachedQuests, false, false) end
        AttachedQuests = uevrUtils.createWidgetComponent("WidgetBlueprintGeneratedClass /Game/UI/Widgets/Overlay/WBP_QuestTracker.WBP_QuestTracker_C", {manualAttachment=false, removeFromViewport=true, twoSided=false, drawSize=vector_2(1000, 1000)})
        if AttachedQuests ~= nil then 
            SetQuestWidgetParams(AttachedQuests) 
            AttachedQuests.WidgetClass = hud_quest_c
            FixWidgetProperties(AttachedMinimap)
        end
        debugOut("Attach to controllers")
        success = controllers.attachComponentToController(0, AttachedQuests, "QuestList", nil, nil, true)
    end
    return success
end

function hook_handle_item_equipped(fn, obj, locals, result)
    local now_t = os.clock()
    if (now_t - last_loadout_change_hook_t) < LOADOUT_HOOK_DEBOUNCE_S then
        return
    end
    last_loadout_change_hook_t = now_t

    debugOut("Item equipped")    
    changed_loadout = true
    loadout_change_cooldown_s = LOADOUT_CHANGE_COOLDOWN_MAX
end

function hook_handle_item_unequipped(fn, obj, locals, result)
    local now_t = os.clock()
    if (now_t - last_loadout_change_hook_t) < LOADOUT_HOOK_DEBOUNCE_S then
        return
    end
    last_loadout_change_hook_t = now_t

    debugOut("Item unequipped")    
    changed_loadout = true
    loadout_change_cooldown_s = LOADOUT_CHANGE_COOLDOWN_MAX
end


function handle_pause_screen_close(fn, obj, locals, result)
    pause_opened_reference = pause_opened_reference - 1
    if pause_opened_reference < 0 then pause_opened_reference = 0 end
    
    if pause_opened_reference == 0 then pause_opened = false end
end

function handle_pause_screen_open(fn, obj, locals, result)
    pause_opened = true
end

function handle_pause_screen_open_2(fn, obj, locals, result)
    pause_opened_reference = pause_opened_reference + 1
end

-- Crosshair loadout hook enabled with debounce/menu guards.
hook_function("Class /Script/Alabama.CrosshairWidget", "OnLoadoutChanged", true, nil, hook_on_loadout_change_core, (debug_type > 0))
-- Inventory hooks temporarily disabled for crash isolation; periodic probe handles loadout detection
-- hook_function("Class /Script/Alabama.InventorySubPage", "OnHandleItemEquipped", true, nil, hook_handle_item_equipped, (debug_type > 0))
-- hook_function("Class /Script/Alabama.InventorySubPage", "OnHandleItemUnquipped", true, nil, hook_handle_item_unequipped, (debug_type > 0))
local dialog_scrolling_choices_c = find_required_object("WidgetBlueprintGeneratedClass /Game/UI/Screens/Dialogs/SBP_DialogScrollingChoicesWithButtonsWithPause.SBP_DialogScrollingChoicesWithButtonsWithPause_C")

uevr.sdk.callbacks.on_pre_engine_tick(function(engine, delta)
    local perf_start_t = os.clock()

    local function body()
        _G.delta = delta

        if disableMod ~= 0 then
            return
        end

        slow_timer = slow_timer + delta

    -- Process deferred detach queue (delayed cleanup to avoid use-after-free during swap)
    process_deferred_detach_queue()

    -- Native direct weapon drive path (controller poses -> world transforms)
    update_direct_weapon_drive_tick()

    process_wand_function_scan()

    force_clear_examinable_timer = force_clear_examinable_timer + delta
    if force_clear_examinable_timer > FORCE_CLEAR_TIME then
        force_clear_examinable_timer = FORCE_CLEAR_TIME
    end
    
    if is_in_main_menu == false then
    
        combo_timer = combo_timer + delta 
        
		-- these are timer based things to do when a new level is loaded
        if new_level_timer < NEW_LEVEL_THRESHOLD then
            new_level_timer = new_level_timer + delta
            if new_level_timer > NEW_LEVEL_THRESHOLD then
                reinit_level_objects()
            end
        end
        
		-- attach the HUD to the left controller. This works only on some levels.
		-- on other levels the widgets are black.
		if USE_ATTACHED_WIDGETS == 1 then
			if new_level_hud_timer < NEW_LEVEL_HUD_THRESHOLD then
				new_level_hud_timer = new_level_hud_timer + delta
				--debugOut(string.format("new_level_hud_timer = %f, delta = %f, NEW_LEVEL_HUD_THRESHOLD = %f", new_level_hud_timer, delta, NEW_LEVEL_HUD_THRESHOLD))
				if new_level_hud_timer > NEW_LEVEL_HUD_THRESHOLD then
					debugOut(string.format("[WidgetAttach] Timer reached threshold (%.2f > %.2f), attempting attachment...", new_level_hud_timer, NEW_LEVEL_HUD_THRESHOLD))
					if is_hud_attached == false then 
						is_hud_attached = AttachWidgets()
					end
					
					if is_hud_attached == false then
						debugOut("HUD attach failed first run, setting to run again.")
						new_level_hud_timer = 0.0
					else
						debugOut("HUD created and attached!")
					end
				end
			else
				-- Timer already exceeded threshold but widgets not attached - retry
				if is_hud_attached == false then
					debugOut("[WidgetAttach] Timer exceeded but not attached, retrying...")
					new_level_hud_timer = 0.0  -- Reset to try again
				end
			end
		end
		
		-- Update widget brightness (every 1 second)
		if USE_ATTACHED_WIDGETS == 1 and is_hud_attached then
			last_brightness_update = last_brightness_update + delta
			if last_brightness_update >= BRIGHTNESS_UPDATE_INTERVAL then
				update_widget_brightness()
				last_brightness_update = 0.0
			end
		end
		
        if right_haptic_timer > 0.0 then 
            right_haptic_timer = right_haptic_timer - delta
        end
        
        if left_haptic_timer > 0.0 then
            left_haptic_timer = left_haptic_timer - delta
        end
        
        if right_haptic_timer < 0.0 then
            right_haptic_timer = 0.0
            -- secs from now (dont think this works), duration, frequency, amplitude
            vr.trigger_haptic_vibration(0, 0.10, 200.0, 30.0, vr.get_right_joystick_source());
        end

        if left_haptic_timer < 0.0 then
            left_haptic_timer = 0.0
            vr.trigger_haptic_vibration(0, 0.10, 200.0, 30.0, vr.get_left_joystick_source());
        end
    end

    
	-- These things don't need to run every tick so we timer them
    if slow_timer >= 2.0 then
        slow_timer = 0.0
    
        local world = uevrUtils.get_world()
        if world ~= nil then
            local level = world.PersistentLevel
            if level ~= nil and level ~= prev_level then
                prev_level = level
                level_change(level, uevrUtils.getShortName(level:get_outer()))
            end
        end
        
        if is_in_main_menu == true then return end

        --[[
        if hud_minimap_c == nil then
            hud_minimap_c = find_required_object("WidgetBlueprintGeneratedClass /Game/UI/Widgets/Minimap/MinimapWidget.MinimapWidget_C")
        end
        
        if hud_minimap_c ~= nil then
            print("minimap not nil")
            local hud_minimap_inst = GetFirstMatchingObject(hud_minimap_c, "Alabama")
            print("hud: ", hud_minimap_inst:get_full_name())
            if hud_minimap_inst ~= nil then
                local alignment_vector = hud_minimap_inst:GetAlignmentInViewport()
                
                print("Original Alignment: ", alignment_vector.X, alignment_vector.Y)
                
            end
        end
]]
--[[        
        if hud_switcher_hook == false then 
            hud_switcher_hook = hook_function("WidgetBlueprintGeneratedClass /Game/UI/Widgets/HudLayer/WBP_HUDLoadoutSwitcher.WBP_HUDLoadoutSwitcher_C", "OnLoadoutChanged", false, nil, hook_on_loadout_change, true)
        end
]]        
        if gamehud_hook == false then
            gamehud_hook = hook_function("Class /Script/Alabama.GameHud", "HandlePauseScreenToggle", true, nil, handle_pause_screen_open, (debug_type > 0))
        end

        if dialog_scrolling_choices_c == nil then
            find_required_object("WidgetBlueprintGeneratedClass /Game/UI/Screens/Dialogs/SBP_DialogScrollingChoicesWithButtonsWithPause.SBP_DialogScrollingChoicesWithButtonsWithPause_C")
        end
        
        if pause_close_hook == false then
            pause_close_hook = hook_function("WidgetBlueprintGeneratedClass /Game/UI/Screens/Dialogs/SBP_DialogScrollingChoicesWithButtonsWithPause.SBP_DialogScrollingChoicesWithButtonsWithPause_C", "Closed", true, nil, handle_pause_screen_close, (debug_type > 0))
            hook_function("WidgetBlueprintGeneratedClass /Game/UI/Screens/Dialogs/SBP_DialogScrollingChoicesWithButtonsWithPause.SBP_DialogScrollingChoicesWithButtonsWithPause_C", "Shown", true, nil, handle_pause_screen_open_2, (debug_type > 0))
        end
        
        -- /Script/UiSystem/ScreenWidget.OnClosed
        if unsheath_hook == false then
            unsheath_hook = hook_function("WidgetBlueprintGeneratedClass /Game/UI/Screens/PlayerHUD/WBP_CrosshairManager.WBP_CrosshairManager_C", "SetSheathedVisual", false, set_sheathed, nil, (debug_type > 0))
        end
        
        regen_health_and_essence()
        fix_weapon_speed(CHEAT_GUN_SPEED_MULT)

        local pawn = api:get_local_pawn()

        if pawn == nil then
            return
        end

        local FPMesh = pawn.FirstPersonMesh
        if FPMesh == nil or FPMesh.AttachChildren == nil then
            return
        end

        local children = FPMesh.AttachChildren

        --UEVR can't handle multiname via uobject hook profiles so it must be scripted
        for _, child in ipairs(children) do
            if string.find(child:get_full_name(), "Poseable") then
                child:SetVisibility(false, false) -- Hide upper body
            end
        end
        
    end

    -- See if we can close pause screen.
    if pause_opened_reference > 0 and dialog_scrolling_choices_c ~= nil then
    
        local dialog_instances = dialog_scrolling_choices_c:get_objects_matching(false)
        if dialog_instances == nil or #dialog_instances == 0 then
            pause_opened_reference = 0
            pause_opened = false
        end
    end
    
    if (melee_swing and is_in_menu == false) then
        local left_index = vr.get_left_controller_index()
        local right_index = vr.get_right_controller_index()
        
        -- Pass delta time to the function
        left_swipe_result = GetSwipe(left_index, delta)
        right_swipe_result = GetSwipe(right_index, delta)
    end  
    end

    body()
    avowed_lua_perf_record_pre_tick((os.clock() - perf_start_t) * 1000.0)
    avowed_lua_perf_log_if_needed()
end)

local function is_unarmed(weapon)
    if weapon ~= nil then
        local status, weapon_name = pcall(function() return weapon:get_full_name() end)
        if string.find(weapon_name, "Unarmed") then
            return true
		else
			return false
        end
    else
        return true
    end
end

local function is_weapon_grimoire(weapon)
    if weapon ~= nil then
        local status, weapon_name = pcall(function() return weapon:get_full_name() end)
        if string.find(weapon_name, "Grimoire") then
            return true
        end
    end
    
    return false
end

function is_weapon_wand(weapon)
    if weapon ~= nil then
        local status, weapon_name = pcall(function() return weapon:get_full_name() end)
        if status and weapon_name ~= nil and string.find(weapon_name, "Wand") then
            return true
        end
    end

    return false
end

function is_weapon_bow(weapon)
    if weapon ~= nil then
        local status, weapon_name = pcall(function() return weapon:get_full_name() end)
        if status and weapon_name ~= nil and string.find(weapon_name, "Bow") then
            return true
        end
    end

    return false
end

function is_weapon_spell_or_ranged_name(weapon_name)
    if weapon_name == nil then return false end
    local wn = string.lower(weapon_name)

    return string.find(wn, "wand") ~= nil or
           string.find(wn, "grimoire") ~= nil or
           string.find(wn, "bow") ~= nil or
           string.find(wn, "arquebus") ~= nil or
           string.find(wn, "pistol") ~= nil
end

local function is_weapon_2_handed(weapon)
    -- Capture both the success status and the actual result
    local success, is_2handed = pcall(function()
        -- Dagger, Sword, Wand, Grimoire, Pistol
        if weapon ~= nil then
            local status, weapon_name = pcall(function() return weapon:get_full_name() end)
            -- Note: If get_full_name() or string.find() causes an error,
            -- this logic will still be protected by pcall.
            if string.find(weapon_name, "_2H_") then
                -- This debugOut *will* run if pcall is successful and the string check is true.
                return true
            end
        end
        
        return false
    end)

    -- You can add logic here to handle if pcall failed (i.e., success is false)
    if not success then
        -- Handle error, e.g., if weapon was nil and it crashed trying to call it.
        -- 'is_2handed' now holds the error message.
        debugOut("Error in is_weapon_2_handed: " .. tostring(is_2handed))
        return false -- Default to false on error
    end
    
    -- If successful, return the actual result from the inner function.
    return is_2handed
end

-- Fire-wand source/projectile diagnostics removed for stable baseline.
function avowed_fx_diag_log(message) end
function log_wand_visual_components_once(weapon, base) end

-- Broad projectile FUObject diagnostics removed for stable baseline.
function avowed_projectile_diag_log(message) end
function request_wand_projectile_diag(reason) end

-- Budgeted wand/projectile function scan removed for stable baseline.
wand_function_scan_requested = false
wand_function_scan_active = false
wand_function_scan_done = true
function avowed_function_diag_log(message) end
function request_wand_function_scan_once(reason) end
function process_wand_function_scan() end

local function debug_print_weapons()
    pcall(function()
    debugOut("Loadout changed weapons are: ")
    if current_right_weapon ~= nil then
        debugOut("current right: " .. current_right_weapon:get_full_name())
    else
        debugOut("current right: nil")
    end
    
    if current_left_weapon ~= nil then
        debugOut("current left: " .. current_left_weapon:get_full_name())
    else
        debugOut("current left: nil")
    end

    if previous_right_weapon ~= nil then
       pcall(function() debugOut("prev right: " .. previous_right_weapon:get_full_name()) end)
    else
       debugOut("prev right: nil")
    end
    if previous_left_weapon ~= nil then
       pcall(function() debugOut("prev left: " .. previous_left_weapon:get_full_name()) end)
    else
       debugOut("prev left: nil")
    end
    end)
end

was_shield_block = false
a_was_down = false
b_was_down = false
y_was_down = false
loadout_swap_input_pending = false
loadout_swap_input_last_t = 0.0
loadout_hook_last_t = 0.0
event_exit_last_t = 0.0
sheathe_state_change_last_t = 0.0
two_handed_state_change_last_t = 0.0
last_is_2_handed_diag = nil
last_probe_block_ms = 0.0
last_loadout_block_ms = 0.0
last_xinput_total_ms = 0.0
right_trigger_diag_last_t = 0.0
RIGHT_TRIGGER_DIAG_INTERVAL_S = 0.35
RIGHT_TRIGGER_RECENT_LOADOUT_WINDOW_S = 1.50
RIGHT_TRIGGER_RECENT_EVENT_WINDOW_S = 2.00
RIGHT_TRIGGER_RECENT_SHEATHE_WINDOW_S = 2.00
RIGHT_TRIGGER_RECENT_2H_WINDOW_S = 2.00
RIGHT_TRIGGER_PROBE_SPIKE_MS = 2.00
RIGHT_TRIGGER_LOADOUT_SPIKE_MS = 2.00
RIGHT_TRIGGER_XINPUT_SPIKE_MS = 5.00

function get_recent_diag_state(last_t, window_s)
    if last_t == nil or last_t <= 0.0 then
        return -1.0, false
    end

    local age_ms = (os.clock() - last_t) * 1000.0
    return age_ms, age_ms <= (window_s * 1000.0)
end

function get_weapon_diag_label(weapon)
    if weapon == nil then
        return "nil"
    end

    local name = "unknown"
    pcall(function()
        if weapon.get_fname ~= nil then
            name = weapon:get_fname():to_string()
        elseif weapon.get_full_name ~= nil then
            name = weapon:get_full_name()
        end
    end)

    local addr = get_obj_address(weapon)
    if addr ~= nil then
        return tostring(name) .. "@" .. tostring(addr)
    end

    return tostring(name)
end

uevr.sdk.callbacks.on_xinput_get_state(function(retval, user_index, state)
    local perf_start_t = os.clock()

    local function body()
        if disableMod ~= 0 then
            return
        end

        local block_t = os.clock()
        local a_just_released = false
    local b_just_released = false
    local left_trigger_down = state.Gamepad.bLeftTrigger > 200
    local right_trigger_down = state.Gamepad.bRightTrigger > 200

    if left_trigger_down then
        AvowedLuaPerf.left_trigger_frames = AvowedLuaPerf.left_trigger_frames + 1
    end
    if right_trigger_down then
        AvowedLuaPerf.right_trigger_frames = AvowedLuaPerf.right_trigger_frames + 1
    end
    
    if state.Gamepad.wButtons & XINPUT_GAMEPAD_B > 0 then     
        b_was_down = true
    elseif b_was_down == true then
        b_just_released = true
        b_was_down = false
    end
    
    if state.Gamepad.wButtons & XINPUT_GAMEPAD_A > 0 then     
        a_was_down = true
    elseif a_was_down == true then
        a_just_released = true
        a_was_down = false
    end

    local any_trigger_down = left_trigger_down == true or right_trigger_down == true

    if state.Gamepad.wButtons & XINPUT_GAMEPAD_Y > 0 then
        y_was_down = true
        AvowedLuaPerf.loadout_swap_button_frames = AvowedLuaPerf.loadout_swap_button_frames + 1
    elseif y_was_down == true then
        y_was_down = false
        loadout_swap_input_pending = true
        loadout_swap_input_last_t = os.clock()
        AvowedLuaPerf.loadout_swap_button_releases = AvowedLuaPerf.loadout_swap_button_releases + 1
    end
    
    -- this exits reading books and will prevent going into book read mode for FORCE_CLEAR_TIME seconds
    if force_clear_examinable_timer == FORCE_CLEAR_TIME and (a_just_released == true  or b_just_released == true) then
        force_clear_examinable_timer = 0.0
    end
    
    is_in_menu = is_menu_open()
    _G.AvowedIsMenuOpen = is_in_menu or is_in_main_menu -- Export for hands script
    if is_in_menu == true or is_in_main_menu == true then
        if was_in_menu == false then
            was_in_menu = true
            set_aim_method_if_needed("0")
            vr.set_mod_value("UI_Size", "0.250000")
            vr.set_mod_value("UI_Distance", "0.500000")
        end
        if left_trigger_down then
            AvowedLuaPerf.left_trigger_menu_returns = AvowedLuaPerf.left_trigger_menu_returns + 1
        end
        if right_trigger_down then
            AvowedLuaPerf.right_trigger_menu_returns = AvowedLuaPerf.right_trigger_menu_returns + 1
        end
        avowed_lua_perf_record_xinput_block("state", (os.clock() - block_t) * 1000.0)
        return
    else
        if was_in_menu == true then
            was_in_menu = false
            vr.set_mod_value("UI_Distance", "10.000000")
            vr.set_mod_value("UI_Size", tostring(NORMAL_UI_SIZE))
            set_aim_method_if_needed(current_aim_method)
        end
    end
    
    local is_in_event = check_cutscene_or_conversation()
    if is_in_event == true then
        was_in_event = true
        if left_trigger_down then
            AvowedLuaPerf.left_trigger_event_returns = AvowedLuaPerf.left_trigger_event_returns + 1
        end
        if right_trigger_down then
            AvowedLuaPerf.right_trigger_event_returns = AvowedLuaPerf.right_trigger_event_returns + 1
        end
        avowed_lua_perf_record_xinput_block("state", (os.clock() - block_t) * 1000.0)
        return
    elseif was_in_event == true then
        was_in_event = false
        event_exit_last_t = os.clock()
        request_weapon_resync("event_exit", true)
        post_event_recovery_throttle_frames = POST_EVENT_RECOVERY_THROTTLE_MAX_FRAMES
        post_event_recovery_throttle_counter = 0
        AvowedLuaPerf.post_event_recoveries = AvowedLuaPerf.post_event_recoveries + 1
    end

    run_post_event_heavy = true
    if post_event_recovery_throttle_frames > 0 then
        post_event_recovery_throttle_counter = post_event_recovery_throttle_counter + 1
        post_event_recovery_throttle_frames = post_event_recovery_throttle_frames - 1
        if post_event_recovery_throttle_counter > 1 and (post_event_recovery_throttle_counter % POST_EVENT_RECOVERY_THROTTLE_MOD) ~= 0 then
            run_post_event_heavy = false
        end
    else
        post_event_recovery_throttle_counter = 0
    end

    if loadout_change_cooldown_s > 0.0 then
        loadout_change_cooldown_s = loadout_change_cooldown_s - delta
        if loadout_change_cooldown_s < 0.0 then
            loadout_change_cooldown_s = 0.0
        end
    end

    if changed_loadout == true and loadout_change_cooldown_s > 0.0 then
        refresh_current_weapon_visibility()
    end
    
    -- note everything below here requires not being in Menu or conversation etc.

    -- wait for level to stabilize
    if new_level_timer < NEW_LEVEL_THRESHOLD then
        if left_trigger_down then
            AvowedLuaPerf.left_trigger_levelwait_returns = AvowedLuaPerf.left_trigger_levelwait_returns + 1
        end
        if right_trigger_down then
            AvowedLuaPerf.right_trigger_levelwait_returns = AvowedLuaPerf.right_trigger_levelwait_returns + 1
        end
        avowed_lua_perf_record_xinput_block("state", (os.clock() - block_t) * 1000.0)
        return
    end

    avowed_lua_perf_record_xinput_block("state", (os.clock() - block_t) * 1000.0)
    block_t = os.clock()

    local grimoire_hand = "none"
    local grimoire_trigger = 0
    local is_2_handed = is_weapon_2_handed(current_right_weapon)

    -- periodic probe to detect loadout changes that don't trigger callbacks
    local now_probe_t = os.clock()
    if run_post_event_heavy ~= true then
        AvowedLuaPerf.post_event_probe_skips = AvowedLuaPerf.post_event_probe_skips + 1
    elseif any_trigger_down ~= true and (now_probe_t - last_weapon_probe_t) >= WEAPON_PROBE_INTERVAL_S then
        last_weapon_probe_t = now_probe_t

        local probe_right, probe_right_base = get_equipped_items("Right")
        local probe_left, probe_left_base = get_equipped_items("Left")

        local probe_right_nil = (probe_right == nil)
        local probe_left_nil = (probe_left == nil)

        if probe_right_nil then
            weapon_probe_nil_streak.right = weapon_probe_nil_streak.right + 1
        else
            weapon_probe_nil_streak.right = 0
        end

        if probe_left_nil then
            weapon_probe_nil_streak.left = weapon_probe_nil_streak.left + 1
        else
            weapon_probe_nil_streak.left = 0
        end

        local right_changed = not same_object(probe_right, current_right_weapon)
        local left_changed = not same_object(probe_left, current_left_weapon)

        local allow_nil_clear_right = should_allow_nil_clear(probe_right, current_right_weapon, attached_right_weapon, weapon_probe_nil_streak.right)
        local allow_nil_clear_left = should_allow_nil_clear(probe_left, current_left_weapon, attached_left_weapon, weapon_probe_nil_streak.left)

        local applied_probe_change = false
        local held_nil = false

        if right_changed then
            if allow_nil_clear_right then
                current_right_weapon, current_right_weapon_base = probe_right, probe_right_base
                applied_probe_change = true
            else
                held_nil = true
            end
        end

        if left_changed then
            if allow_nil_clear_left then
                current_left_weapon, current_left_weapon_base = probe_left, probe_left_base
                applied_probe_change = true
            else
                held_nil = true
            end
        end

        if applied_probe_change then
            AvowedLuaPerf.probe_update_count = AvowedLuaPerf.probe_update_count + 1
            changed_loadout = true
        elseif held_nil then
            if (now_probe_t - last_probe_nil_log_t) > 1.0 then
                last_probe_nil_log_t = now_probe_t
                debugOut("Probe nil transient; holding previous weapons to avoid swap flicker")
            end
        end
    end

    last_probe_block_ms = (os.clock() - block_t) * 1000.0
    avowed_lua_perf_record_xinput_block("probe", last_probe_block_ms)
    block_t = os.clock()

    -- Weapon Hooks per hand and loadout.
    if run_post_event_heavy ~= true then
        AvowedLuaPerf.post_event_loadout_skips = AvowedLuaPerf.post_event_loadout_skips + 1
    elseif changed_loadout == true and loadout_change_cooldown_s > 0.0 and current_right_weapon == nil and current_left_weapon == nil then
        -- Startup/load transitions can report both hands nil for a few frames. Do not hot-loop expensive
        -- equipped-item scans every input frame; keep changed_loadout armed and retry after the cooldown.
        AvowedLuaPerf.post_event_loadout_skips = AvowedLuaPerf.post_event_loadout_skips + 1
    elseif changed_loadout == true and any_trigger_down == true then
        -- Preserve trigger responsiveness: never run expensive loadout reconciliation while a trigger is held.
        -- If startup/loadout data is still nil, attach will retry as soon as the trigger is released.
        AvowedLuaPerf.post_event_loadout_skips = AvowedLuaPerf.post_event_loadout_skips + 1
    elseif changed_loadout == true then
        clear_slot_cache()
        fix_weapon_speed(CHEAT_GUN_SPEED_MULT)
        current_right_weapon, current_right_weapon_base = get_equipped_items("Right")
        current_left_weapon, current_left_weapon_base = get_equipped_items("Left")

        if current_right_weapon == nil and current_left_weapon == nil then
            debugOut("Loadout update returned both nil; deferring reattach this frame")
            -- Avoid hot-loop churn when loadout references are transiently absent, but keep the loadout
            -- update armed so first-load attachment retries without requiring manual loadout swaps.
            changed_loadout = true
            loadout_change_cooldown_s = LOADOUT_NIL_RETRY_INTERVAL_S
        end
    else
        if current_right_weapon == nil then 
            current_right_weapon, current_right_weapon_base = get_equipped_items("Right") 
            if current_right_weapon ~= nil then 
                changed_loadout = true 
            end
        end
        if current_left_weapon == nil then
            if is_2_handed ~= true or (os.clock() - last_left_nil_reprobe_t) >= LEFT_NIL_REPROBE_INTERVAL_S then
                if is_2_handed == true then
                    last_left_nil_reprobe_t = os.clock()
                end
                current_left_weapon, current_left_weapon_base = get_equipped_items("Left")
                if current_left_weapon ~= nil then
                    changed_loadout = true
                end
            end
        end
    end

    is_2_handed = is_weapon_2_handed(current_right_weapon)
    if last_is_2_handed_diag == nil then
        last_is_2_handed_diag = is_2_handed
    elseif last_is_2_handed_diag ~= is_2_handed then
        last_is_2_handed_diag = is_2_handed
        two_handed_state_change_last_t = os.clock()
    end
    
    local loadout_changed_check_hand = false
    
    local pawn = api:get_local_pawn()
    
    if changed_loadout == true then
        debug_print_weapons()
        
		-- Export weapon names for hands script
		pcall(function()
			 local r_name = "None"
			 -- Prefer Item Object name if available, otherwise Component name
			 if current_right_weapon_base then
				 r_name = current_right_weapon_base:get_fname():to_string()
			 elseif current_right_weapon then
				 r_name = current_right_weapon:get_fname():to_string()
			 end
			 _G.AvowedWeaponRight = r_name
			 
			 -- Repeat for left if needed later
			 local l_name = "None" 
			 if current_left_weapon_base then
				 l_name = current_left_weapon_base:get_fname():to_string()
			 elseif current_left_weapon then
				 l_name = current_left_weapon:get_fname():to_string()
			 end
			 _G.AvowedWeaponLeft = l_name
		end)

        if NATIVE_BONE_DRIVER_OWNS_WEAPONS == 1 then
            -- Native C++ bone driver owns hand/weapon hierarchy in this mode.
            -- Keep script state current, but do not touch weapon components.
            previous_right_weapon = current_right_weapon
            previous_left_weapon = current_left_weapon
            attached_right_weapon = nil
            attached_left_weapon = nil
            attached_right_weapon_base = nil
            attached_left_weapon_base = nil
            reset_attach_fail_streak("right")
            reset_attach_fail_streak("left")
            changed_loadout = false
        else
		    local right_changed = (not same_object(current_right_weapon, previous_right_weapon)) or (attached_right_weapon == nil and current_right_weapon ~= nil)
            if right_changed then
			    if reattach_weapon(current_right_weapon, "right") == true then
                    debugOut("Right weapon attach complete")
                    previous_right_weapon = current_right_weapon
                end
            else
                debugOut("Right weapon unchanged, skipping reattach")
            end
            
            if is_2_handed == false then
                debugOut("right weapon 1H or unarmed, checking left")
                local left_changed = (not same_object(current_left_weapon, previous_left_weapon)) or (attached_left_weapon == nil and current_left_weapon ~= nil)
                if left_changed then
                    if reattach_weapon(current_left_weapon, "left") == true then
                        if current_left_weapon ~= nil then debugOut("left: " .. current_left_weapon:get_full_name()) end
                        previous_left_weapon = current_left_weapon
                    end
                else
                    debugOut("Left weapon unchanged, skipping reattach")
                end
            else
                debugOut(string.format("is_2_handed = %s", is_2_handed))
            end
            -- tightens the weapon spread for aimed weapons. Especially the wand.
            if current_right_weapon ~= nil or current_left_weapon ~= nil then
                if should_fix_weapon_spread == true then
                    debugOut("Fixing weapon spread")
                    fix_weapon_spread(0.1)
                end
            end
            
            changed_loadout = false
        end

        -- there's more we want to do but need some data outside this if scope for it.
        if changed_loadout == false then
            loadout_changed_check_hand = true
            -- Death/load transitions can invalidate the equipped-item base while Lua still holds it.
            -- Calling/indexing OnInitialize here caused a caught C++ exception and preceded a crash.
            -- It was previously noted as having no impact, so keep it disabled for stability.
            -- if current_right_weapon_base ~= nil and current_right_weapon_base.OnInitialize ~= nil then current_right_weapon_base:OnInitialize() end
            -- tested to get rid of vanishing weapon, no impact
            --if pawn ~= nil then
                --pawn:OnEquippedItemVisualsConstructed(current_right_weapon_base)
            --end
        end
    end

    last_loadout_block_ms = (os.clock() - block_t) * 1000.0
    avowed_lua_perf_record_xinput_block("loadout", last_loadout_block_ms)
    block_t = os.clock()

    refresh_current_weapon_visibility()
    avowed_lua_perf_record_xinput_block("visibility", (os.clock() - block_t) * 1000.0)
    block_t = os.clock()
    
	-- all this can move into hook weapon change for performance
	local is_left_weapon_melee    = is_weapon_melee(current_left_weapon)
	local is_right_weapon_melee   = is_weapon_melee(current_right_weapon)
    local is_right_weapon_ranged  = not is_right_weapon_melee
    local is_left_weapon_ranged   = not is_left_weapon_melee
    local is_left_weapon_shield   = is_shield(current_left_weapon)
	local is_right_weapon_unarmed = is_unarmed(current_right_weapon)
	local is_left_weapon_unarmed  = false
    local is_right_weapon_wand = is_weapon_wand(current_right_weapon)
    local is_right_weapon_bow = is_weapon_bow(current_right_weapon)
    local is_right_weapon_grimoire = false

    if is_right_weapon_wand == true then
        log_wand_visual_components_once(current_right_weapon, current_right_weapon_base)
        -- Projectile FUObject scan disabled: it identified useful candidates but caused massive fire-time lag spikes.
        request_wand_function_scan_once("right_wand_equipped")
    end
    
	if is_2_handed == false then
		is_left_weapon_unarmed = is_unarmed(current_left_weapon)
    else
        is_left_weapon_melee = false
        is_left_weapon_unarmed = false
	end

    if is_left_weapon_unarmed == true then 
        is_left_weapon_melee = true 
        is_left_weapon_unarmed = false
    end
    if is_right_weapon_unarmed == true then 
        is_right_weapon_melee = true 
        is_right_weapon_unarmed = false
    end
    
    if is_left_weapon_shield then
        is_left_weapon_ranged = false
    end
    
    local aim_hand = "2"
    -- setup default aim before user selects it with trigger.
    if loadout_changed_check_hand == true then
        loadout_changed_check_hand = false

        -- keep aim/UI hand stable across loadouts if enabled
        if LOCK_AIM_HAND_TO_RIGHT == 1 or is_right_weapon_wand == true then
            aim_hand = "2"
            debugOut("Forcing VR_AimMethod=2 (right hand lock or wand equipped)")
        else
            -- default to right hand except some circumstances.
            aim_hand = "2" -- Right

            -- left weapon is only ranged weapon
            if is_right_weapon_ranged == false and is_left_weapon_ranged == true then
                aim_hand = "3"
            end

            -- using 2 handed sword
            if is_2_handed == true and is_right_weapon_melee then
                aim_hand = "3"
            end
        end

        set_aim_method_if_needed(aim_hand)
    end
    
    -- Change aim method based on last activated weapon
    if current_left_weapon ~= nil or current_right_weapon ~= nil then 
        if LOCK_AIM_HAND_TO_RIGHT == 1 or is_right_weapon_wand == true then
            set_aim_method_if_needed("2")
        else
            if is_2_handed == true then
                -- aim on left hand so you can aim your strikes
                if is_right_weapon_melee == true then
                    set_aim_method_if_needed("3")
                else
                    set_aim_method_if_needed("2")
                end
            else
                if (state.Gamepad.bLeftTrigger > 200) then
                    set_aim_method_if_needed("3")
                elseif (state.Gamepad.bRightTrigger > 200) then
                    set_aim_method_if_needed("2")
                end
            end
        end

        if is_right_weapon_wand == true and (state.Gamepad.bRightTrigger > 200 or state.Gamepad.bLeftTrigger > 200) then
            set_aim_method_if_needed("2")

            local now_wand_diag = os.clock()
            if (now_wand_diag - last_wand_fire_diag_t) > 0.25 then
                last_wand_fire_diag_t = now_wand_diag
                local aim_dbg = tostring(vr:get_mod_value("VR_AimMethod"))
                debugOut("Wand fire diag: aim=" .. aim_dbg .. " RT=" .. tostring(state.Gamepad.bRightTrigger) .. " LT=" .. tostring(state.Gamepad.bLeftTrigger))
            end
        end

        current_aim_method = vr:get_mod_value("VR_AimMethod")
        last_applied_aim_method = tostring(current_aim_method)
    end

    if is_right_weapon_wand == true then
        AvowedLuaPerf.right_wand_candidate_frames = AvowedLuaPerf.right_wand_candidate_frames + 1
    end
    if is_right_weapon_bow == true then
        AvowedLuaPerf.right_bow_candidate_frames = AvowedLuaPerf.right_bow_candidate_frames + 1
    end
    
    -- this is all grimoire control using right stick while trigger is down
    if is_left_weapon_melee == false then
        if is_weapon_grimoire(current_left_weapon) == true then
            grimoire_hand = "left"
            AvowedLuaPerf.left_grimoire_candidate_frames = AvowedLuaPerf.left_grimoire_candidate_frames + 1
        elseif left_trigger_down then
            AvowedLuaPerf.left_trigger_non_grimoire_frames = AvowedLuaPerf.left_trigger_non_grimoire_frames + 1
        end
    elseif left_trigger_down then
        AvowedLuaPerf.left_trigger_non_grimoire_frames = AvowedLuaPerf.left_trigger_non_grimoire_frames + 1
    end

    if is_right_weapon_melee == false then
        if is_weapon_grimoire(current_right_weapon) == true then
            is_right_weapon_grimoire = true
            grimoire_hand = "right"
            AvowedLuaPerf.right_grimoire_candidate_frames = AvowedLuaPerf.right_grimoire_candidate_frames + 1
        end
    end
    
    if right_trigger_down then
        local now_rt_diag = os.clock()
        local recent_loadout_ms, recent_loadout_active = get_recent_diag_state(loadout_hook_last_t, RIGHT_TRIGGER_RECENT_LOADOUT_WINDOW_S)
        local recent_event_ms, recent_event_active = get_recent_diag_state(event_exit_last_t, RIGHT_TRIGGER_RECENT_EVENT_WINDOW_S)
        local recent_sheathed_ms, recent_sheathed_active = get_recent_diag_state(sheathe_state_change_last_t, RIGHT_TRIGGER_RECENT_SHEATHE_WINDOW_S)
        local recent_2h_ms, recent_2h_active = get_recent_diag_state(two_handed_state_change_last_t, RIGHT_TRIGGER_RECENT_2H_WINDOW_S)

        if recent_loadout_active then
            AvowedLuaPerf.right_trigger_recent_loadout_frames = AvowedLuaPerf.right_trigger_recent_loadout_frames + 1
        end

        if is_right_weapon_wand == true then
            AvowedLuaPerf.right_wand_trigger_frames = AvowedLuaPerf.right_wand_trigger_frames + 1
            if attached_right_weapon == nil or not same_object(attached_right_weapon, current_right_weapon) then
                AvowedLuaPerf.right_trigger_wand_attach_mismatch_frames = AvowedLuaPerf.right_trigger_wand_attach_mismatch_frames + 1
                if (now_rt_diag - right_trigger_diag_last_t) >= RIGHT_TRIGGER_DIAG_INTERVAL_S then
                    right_trigger_diag_last_t = now_rt_diag
                    debugOut("[RTDiag] wand_attach_mismatch recent_loadout_ms=" .. tostring(math.floor(recent_loadout_ms + 0.5)) .. " current=" .. get_weapon_diag_label(current_right_weapon) .. " attached=" .. get_weapon_diag_label(attached_right_weapon) .. " previous=" .. get_weapon_diag_label(previous_right_weapon) .. " aim=" .. tostring(vr:get_mod_value("VR_AimMethod")))
                end
            end
        elseif is_right_weapon_grimoire == true then
            AvowedLuaPerf.right_grimoire_trigger_frames = AvowedLuaPerf.right_grimoire_trigger_frames + 1
        elseif is_right_weapon_bow == true then
            AvowedLuaPerf.right_bow_trigger_frames = AvowedLuaPerf.right_bow_trigger_frames + 1
        else
            AvowedLuaPerf.right_trigger_other_frames = AvowedLuaPerf.right_trigger_other_frames + 1
            if recent_loadout_active then
                AvowedLuaPerf.right_trigger_other_recent_loadout_frames = AvowedLuaPerf.right_trigger_other_recent_loadout_frames + 1
            end
            if recent_event_active then
                AvowedLuaPerf.right_trigger_other_recent_event_frames = AvowedLuaPerf.right_trigger_other_recent_event_frames + 1
            end
            if recent_sheathed_active then
                AvowedLuaPerf.right_trigger_other_recent_sheathed_frames = AvowedLuaPerf.right_trigger_other_recent_sheathed_frames + 1
            end
            if recent_2h_active then
                AvowedLuaPerf.right_trigger_other_recent_2h_frames = AvowedLuaPerf.right_trigger_other_recent_2h_frames + 1
            end
            if last_probe_block_ms >= RIGHT_TRIGGER_PROBE_SPIKE_MS then
                AvowedLuaPerf.right_trigger_other_probe_spike_frames = AvowedLuaPerf.right_trigger_other_probe_spike_frames + 1
            end
            if last_loadout_block_ms >= RIGHT_TRIGGER_LOADOUT_SPIKE_MS then
                AvowedLuaPerf.right_trigger_other_loadout_spike_frames = AvowedLuaPerf.right_trigger_other_loadout_spike_frames + 1
            end
            if last_xinput_total_ms >= RIGHT_TRIGGER_XINPUT_SPIKE_MS then
                AvowedLuaPerf.right_trigger_other_xinput_spike_frames = AvowedLuaPerf.right_trigger_other_xinput_spike_frames + 1
            end
            if (now_rt_diag - right_trigger_diag_last_t) >= RIGHT_TRIGGER_DIAG_INTERVAL_S then
                right_trigger_diag_last_t = now_rt_diag
                debugOut("[RTDiag] other recent_loadout_ms=" .. tostring(math.floor(recent_loadout_ms + 0.5)) .. " recent_event_ms=" .. tostring(math.floor(recent_event_ms + 0.5)) .. " recent_sheathed_ms=" .. tostring(math.floor(recent_sheathed_ms + 0.5)) .. " recent_2h_ms=" .. tostring(math.floor(recent_2h_ms + 0.5)) .. " probe_ms=" .. tostring(math.floor(last_probe_block_ms + 0.5)) .. " loadout_ms=" .. tostring(math.floor(last_loadout_block_ms + 0.5)) .. " xinput_ms=" .. tostring(math.floor(last_xinput_total_ms + 0.5)) .. " current=" .. get_weapon_diag_label(current_right_weapon) .. " attached=" .. get_weapon_diag_label(attached_right_weapon) .. " previous=" .. get_weapon_diag_label(previous_right_weapon) .. " left_current=" .. get_weapon_diag_label(current_left_weapon) .. " aim=" .. tostring(vr:get_mod_value("VR_AimMethod")))
            end
        end
    end

    if grimoire_hand == "left" then
        grimoire_trigger = state.Gamepad.bLeftTrigger
    elseif grimoire_hand == "right" then
        grimoire_trigger = state.Gamepad.bRightTrigger
    end
    
    if grimoire_trigger > 200 then
        if grimoire_hand == "left" then
            AvowedLuaPerf.left_grimoire_trigger_frames = AvowedLuaPerf.left_grimoire_trigger_frames + 1
        end
        if state.Gamepad.bLeftTrigger > 200 then
            if state.Gamepad.sThumbLY > 25000 then 
                state.Gamepad.wButtons = state.Gamepad.wButtons | XINPUT_GAMEPAD_Y
                state.Gamepad.sThumbLY = 0
            elseif state.Gamepad.sThumbLY < -25000 then
                state.Gamepad.wButtons = state.Gamepad.wButtons | XINPUT_GAMEPAD_A
                state.Gamepad.sThumbLY = 0
            elseif state.Gamepad.sThumbLX > 25000 then
                state.Gamepad.wButtons = state.Gamepad.wButtons | XINPUT_GAMEPAD_B
                state.Gamepad.sThumbLX = 0
            elseif state.Gamepad.sThumbLX < -25000 then
                state.Gamepad.wButtons = state.Gamepad.wButtons | XINPUT_GAMEPAD_X
                state.Gamepad.sThumbLX = 0
            end
        end
    end
    -- end grimoire control
    
    -- optional control remaps
    -- if LB is down, set aim to left hand.
    if remap_lb_button == 1 then
        if state.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER > 0 then
            if LOCK_AIM_HAND_TO_RIGHT ~= 1 and is_right_weapon_wand ~= true then
                set_aim_method_if_needed("3")
            end
            -- clear LB
            state.Gamepad.wButtons = state.Gamepad.wButtons & ~XINPUT_GAMEPAD_LEFT_SHOULDER
            
            -- LB + B = LB, the spell wheel, so add LB and clear B
            if state.Gamepad.wButtons & XINPUT_GAMEPAD_B > 0 then
                state.Gamepad.wButtons = state.Gamepad.wButtons | XINPUT_GAMEPAD_LEFT_SHOULDER
                state.Gamepad.wButtons = state.Gamepad.wButtons & ~XINPUT_GAMEPAD_B
            elseif state.Gamepad.sThumbLY > 25000 then 
                state.Gamepad.wButtons = state.Gamepad.wButtons | XINPUT_GAMEPAD_DPAD_UP
                state.Gamepad.sThumbLY = 0
            elseif state.Gamepad.sThumbLY < -25000 then
                state.Gamepad.wButtons = state.Gamepad.wButtons | XINPUT_GAMEPAD_DPAD_DOWN
                state.Gamepad.sThumbLY = 0
            elseif state.Gamepad.sThumbLX > 25000 then
                state.Gamepad.wButtons = state.Gamepad.wButtons | XINPUT_GAMEPAD_DPAD_RIGHT
                state.Gamepad.sThumbLX = 0
            elseif state.Gamepad.sThumbLX < -25000 then
                state.Gamepad.wButtons = state.Gamepad.wButtons | XINPUT_GAMEPAD_DPAD_LEFT
                state.Gamepad.sThumbLX = 0
            end
            
        end
    end

    if right_stick_down_b == 1 then
        if state.Gamepad.sThumbRY <= -25000 then
            state.Gamepad.sThumbRY = 0
            state.Gamepad.wButtons = state.Gamepad.wButtons | XINPUT_GAMEPAD_B
        end
    end
    
    if right_stick_up_sprint == 1 then
        if state.Gamepad.sThumbRY >= 25000 then
            state.Gamepad.sThumbRY = 0
            state.Gamepad.wButtons = state.Gamepad.wButtons | XINPUT_GAMEPAD_LEFT_THUMB
        end
    end

    avowed_lua_perf_record_xinput_block("aim", (os.clock() - block_t) * 1000.0)
    block_t = os.clock()
    
	------------------------------------------------------------------
	-- GESTURE SYSTEM BELOW --
	------------------------------------------------------------------
	
	-- return here if no gesture swipes are used.
	if MELEE_SWING_AND_BLOCK == 0 then return end
	
    -- read swipes and disable RT / LT for melee actions
    if is_left_weapon_melee == true then
        if left_swipe_result > 0 then
            debugOut("left_swipe_result detected swinging left weapon")
            state.Gamepad.bLeftTrigger = 255
            left_haptic_timer = 0.15
            fix_weapon_speed_for_weapon(current_left_weapon_base, 10.0)
        elseif GetBlock("left", CHARGE_THRESHOLD) == true then -- block gesture also used for charge attack
            state.Gamepad.bLeftTrigger = 255
            fix_weapon_speed_for_weapon(current_left_weapon_base, 0.5)
            vr.trigger_haptic_vibration(0, 0.20, 100.0, 30.0, vr.get_left_joystick_source())
            was_left_trigger_down = true
            set_aim_method_if_needed("2") -- set aim to other hand while charge is overhead.
        else
            fix_weapon_speed_for_weapon(current_left_weapon_base, 10.0)
            state.Gamepad.bLeftTrigger = 0
            if was_left_trigger_down == true then
                set_aim_method_if_needed(aim_hand)
            end
            was_left_trigger_down = false
        end
    end
    
    if is_right_weapon_melee == true then
        if right_swipe_result > 0 then
            debugOut("right_swipe_result detected swinging right weapon")
            state.Gamepad.bRightTrigger = 255
            right_haptic_timer = 0.15
            fix_weapon_speed_for_weapon(current_right_weapon_base, 10.0)
        elseif GetBlock("right", CHARGE_THRESHOLD) == true then -- block gesture also used for charge attack
            state.Gamepad.bRightTrigger = 255
            fix_weapon_speed_for_weapon(current_right_weapon_base, 0.5)
            vr.trigger_haptic_vibration(0, 0.20, 100.0, 30.0, vr.get_right_joystick_source())
            was_right_trigger_down = true
            if LOCK_AIM_HAND_TO_RIGHT == 1 then
                set_aim_method_if_needed("2")
            else
                set_aim_method_if_needed("3") -- set aim to other hand while charge is overhead.
            end
        else
            fix_weapon_speed_for_weapon(current_right_weapon_base, 10.0) -- while swinging
            state.Gamepad.bRightTrigger = 0
            if was_right_trigger_down == true then
                set_aim_method_if_needed(aim_hand)
            end
            was_right_trigger_down = false
        end
        
        if is_2_handed == true then
            if GetSwordBlock() == true then
                state.Gamepad.bLeftTrigger = 255
                if was_sword_block == false then
                    was_sword_block = true
                    vr.trigger_haptic_vibration(0.0, 0.05, 1.0, 1000.0, vr.get_right_joystick_source());
                end
            else
                state.Gamepad.bLeftTrigger = 0
                if was_sword_block == true then
                    was_sword_block = false
                    vr.trigger_haptic_vibration(0.0, 0.05, 1.0, 1500.0, vr.get_right_joystick_source());
                end
            end
        end
    end
    
    if is_left_weapon_shield == true then
        if GetBlock("left", BLOCK_THRESHOLD) == true then
            state.Gamepad.bLeftTrigger = 255
			if was_shield_block == false then
				was_shield_block = true
				vr.trigger_haptic_vibration(0.0, 0.05, 1.0, 1000.0, vr.get_left_joystick_source())
                if current_left_weapon.RelativeScale3D ~= nil then
                    current_left_weapon.RelativeScale3D.X = SHIELD_BLOCK_SIZE
                    current_left_weapon.RelativeScale3D.Y = SHIELD_BLOCK_SIZE
                    current_left_weapon.RelativeScale3D.Z = SHIELD_BLOCK_SIZE
                end

				--set_weapon_opacity(current_left_weapon, true)
			end
        else
			state.Gamepad.bLeftTrigger = 0
			if was_shield_block == true then
				was_shield_block = false
				vr.trigger_haptic_vibration(0.0, 0.05, 1.0, 1500.0, vr.get_left_joystick_source());
                if current_left_weapon.RelativeScale3D ~= nil then
                    current_left_weapon.RelativeScale3D.X = SHIELD_NORMAL_SIZE
                    current_left_weapon.RelativeScale3D.Y = SHIELD_NORMAL_SIZE
                    current_left_weapon.RelativeScale3D.Z = SHIELD_NORMAL_SIZE
                end
			end
        end
    end

    if is_right_weapon_wand == true then
        AvowedLuaPerf.wand_frames = AvowedLuaPerf.wand_frames + 1
    end

    pcall(function()
        if current_right_weapon ~= nil then
            local ok_bow_name, bow_name = pcall(function() return current_right_weapon:get_full_name() end)
            if ok_bow_name and bow_name ~= nil and string.find(bow_name, "Bow") then
                AvowedLuaPerf.bow_frames = AvowedLuaPerf.bow_frames + 1
            end
        end
    end)

    if is_right_weapon_melee == true or is_left_weapon_melee == true then
        AvowedLuaPerf.melee_frames = AvowedLuaPerf.melee_frames + 1
    end

    avowed_lua_perf_record_xinput_block("gesture", (os.clock() - block_t) * 1000.0)
    end

    body()
    last_xinput_total_ms = (os.clock() - perf_start_t) * 1000.0
    avowed_lua_perf_record_xinput(last_xinput_total_ms)
    avowed_lua_perf_log_if_needed()
end)

local function toggle_lumen(enabled)
    set_cvar_int("r.Lumen.DiffuseIndirect.Allow", enabled)
    if enabled == 1 then 
        set_cvar_float("r.TonemapperGamma", 0.0)
    else
        set_cvar_float("r.TonemapperGamma", 1.5)
    end
end

local function read_config()
    local config_data = fs.read(config_filename)
    if config_data then
        for key, value in config_data:gmatch("([^=]+)=([^\n]+)\n?") do
		
            local num_val = tonumber(value)
            if key == "disableMod" then
                disableMod = num_val
            end

            if key == "debug_type" then 
				debug_type = num_val 
            end
			
			if key == "SWIPE_THRESHOLD" then
				SWIPE_THRESHOLD = num_val
			end

            if key == "COMBO_THRESHOLD" then
                COMBO_THRESHOLD = num_val
            end
            
            if key == "BLOCK_THRESHOLD" then
                BLOCK_THRESHOLD = num_val
            end
            
            if key == "CHARGE_THRESHOLD" then
                CHARGE_THRESHOLD = num_val
            end
    
            if key == "CHEAT_AUTO_REGEN_ESSENCE" then
                CHEAT_AUTO_REGEN_ESSENCE = num_val
            end
            
            if key == "CHEAT_AUTO_REGEN_HEALTH" then
                CHEAT_AUTO_REGEN_HEALTH = num_val
            end
            
			if key == "CHEAT_AUTO_REGEN_PERC" then
				CHEAT_AUTO_REGEN_PERC = num_val
			end
			
			if key == "CHEAT_GUN_SPEED_MULT" then
				CHEAT_GUN_SPEED_MULT = num_val
			end

			if key == "MELEE_SWING_AND_BLOCK" then
				MELEE_SWING_AND_BLOCK = num_val
			end
            
			if key == "right_stick_down_b" then
				right_stick_down_b = num_val
			end
            
            if key == "right_stick_up_sprint" then
                right_stick_up_sprint = num_val
            end
            
            if key == "remap_lb_button" then 
                remap_lb_button = num_val
            end

            if key == "LOCK_AIM_HAND_TO_RIGHT" then
                LOCK_AIM_HAND_TO_RIGHT = num_val
            end
            
            if key == "USE_SIMPLE_SWORD_BLOCK" then
                USE_SIMPLE_SWORD_BLOCK = num_val
            end
            
            if key == "SWORD_BLOCK_DEGREES_THRESHOLD" then
                SWORD_BLOCK_DEGREES_THRESHOLD = num_val
            end
            
            if key == "ENABLE_LUMEN" then
                ENABLE_LUMEN = num_val
                toggle_lumen(ENABLE_LUMEN)
            end
                       
            if key == "SHIELD_NORMAL_SIZE" then
                SHIELD_NORMAL_SIZE = num_val
            end
            
            if key == "SHIELD_BLOCK_SIZE" then
                SHIELD_BLOCK_SIZE = num_val
            end
            
            if key == "NORMAL_UI_SIZE" then
                NORMAL_UI_SIZE = num_val
            end
            
            if key == "USE_OLD_OBJ_HOOK_METHOD" then
                USE_OLD_OBJ_HOOK_METHOD = num_val
            end

            if key == "ALLOW_UOBJECTHOOK_ATTACH_FALLBACK" then
                ALLOW_UOBJECTHOOK_ATTACH_FALLBACK = num_val
            end

            if key == "DIRECT_WEAPON_DRIVE" then
                DIRECT_WEAPON_DRIVE = num_val
            end

            if key == "DIRECT_DRIVE_MOVE_OWNER_ACTOR" then
                DIRECT_DRIVE_MOVE_OWNER_ACTOR = num_val
            end

            if key == "NATIVE_BONE_DRIVER_OWNS_WEAPONS" then
                NATIVE_BONE_DRIVER_OWNS_WEAPONS = num_val
            end
            
            if key == "USE_ATTACHED_WIDGETS" then
                USE_ATTACHED_WIDGETS = num_val
            end
            
            if key == "WIDGET_BRIGHTNESS" then
                WIDGET_BRIGHTNESS = num_val
            end
       end
    end
    
    
    help_data = fs.read(help_filename)
    
end

function write_config()
    local config = "" -- Initialize config as an empty string

    config = config .. string.format("disableMod=%d\n", disableMod)
	config = config .. string.format("MELEE_SWING_AND_BLOCK=%d\n", MELEE_SWING_AND_BLOCK)
	config = config .. string.format("SWIPE_THRESHOLD=%f\n", SWIPE_THRESHOLD) 
    config = config .. string.format("COMBO_THRESHOLD=%f\n", COMBO_THRESHOLD)
	config = config .. string.format("BLOCK_THRESHOLD=%f\n", BLOCK_THRESHOLD) 
    config = config .. string.format("CHARGE_THRESHOLD=%f\n", CHARGE_THRESHOLD) 
	config = config .. string.format("CHEAT_AUTO_REGEN_ESSENCE=%d\n", CHEAT_AUTO_REGEN_ESSENCE) 
    config = config .. string.format("CHEAT_AUTO_REGEN_HEALTH=%d\n", CHEAT_AUTO_REGEN_HEALTH) 
    config = config .. string.format("CHEAT_AUTO_REGEN_PERC=%.2f\n", CHEAT_AUTO_REGEN_PERC)
    config = config .. string.format("CHEAT_GUN_SPEED_MULT=%.2f\n", CHEAT_GUN_SPEED_MULT)
	config = config .. string.format("right_stick_down_b=%d\n", right_stick_down_b) 
    config = config .. string.format("right_stick_up_sprint=%d\n", right_stick_up_sprint) 
    config = config .. string.format("remap_lb_button=%d\n", remap_lb_button)
    config = config .. string.format("LOCK_AIM_HAND_TO_RIGHT=%d\n", LOCK_AIM_HAND_TO_RIGHT)
    config = config .. string.format("debug_type=%d\n", debug_type)
    config = config .. string.format("SWORD_BLOCK_DEGREES_THRESHOLD=%d\n", SWORD_BLOCK_DEGREES_THRESHOLD)
    config = config .. string.format("USE_SIMPLE_SWORD_BLOCK=%d\n", USE_SIMPLE_SWORD_BLOCK)
    config = config .. string.format("SHIELD_BLOCK_SIZE=%.2f\n", SHIELD_BLOCK_SIZE)
    config = config .. string.format("SHIELD_NORMAL_SIZE=%.2f\n", SHIELD_NORMAL_SIZE)
    config = config .. string.format("NORMAL_UI_SIZE=%.6f\n", NORMAL_UI_SIZE)
    config = config .. string.format("USE_OLD_OBJ_HOOK_METHOD=%d\n", USE_OLD_OBJ_HOOK_METHOD)
    config = config .. string.format("ALLOW_UOBJECTHOOK_ATTACH_FALLBACK=%d\n", ALLOW_UOBJECTHOOK_ATTACH_FALLBACK)
    config = config .. string.format("DIRECT_WEAPON_DRIVE=%d\n", DIRECT_WEAPON_DRIVE)
    config = config .. string.format("DIRECT_DRIVE_MOVE_OWNER_ACTOR=%d\n", DIRECT_DRIVE_MOVE_OWNER_ACTOR)
    config = config .. string.format("NATIVE_BONE_DRIVER_OWNS_WEAPONS=%d\n", NATIVE_BONE_DRIVER_OWNS_WEAPONS)
    config = config .. string.format("ENABLE_LUMEN=%d\n", ENABLE_LUMEN)
    config = config .. string.format("USE_ATTACHED_WIDGETS=%d\n", USE_ATTACHED_WIDGETS)
    config = config .. string.format("WIDGET_BRIGHTNESS=%f\n", WIDGET_BRIGHTNESS)
    
    fs.write(config_filename, config)
end



uevr.lua.add_script_panel("Avowed Config", function()
    imgui.text("Avowed Mod v" .. VERSION .. " by MarkMon and letmein50")
    local changed, new_value = imgui.checkbox("Enable Avowed Lua Script", disableMod == 0)
    if changed then
        set_script_enabled(new_value)
        needs_config_write = true
    end
    imgui.spacing()
    imgui.spacing()

	if imgui.collapsing_header("Help and Readme") then
        local changed, new_index = imgui.combo("Debug Logging", debug_type+1, {"Disabled", "Console", "log.txt", "log.txt + Console"})
        if changed then debug_type = new_index - 1; needs_config_write = true;  end

        imgui.spacing()
        imgui.spacing()
        imgui.text("Toggle weapon attach only if weapon hooks are problematic. The old method uses uevr")
        imgui.text("internal hooking system. But some weapons vanished after using magic or")
        imgui.text("swapping weapon sets. The new method uses jbusfield's library for attachments")
        imgui.text("and seems to work better. Weapons may flicker a little but dont go away.")
        changed, new_value = imgui.checkbox("Use Old Weapon Attach Method", USE_OLD_OBJ_HOOK_METHOD == 1)
        if changed then USE_OLD_OBJ_HOOK_METHOD = new_value and 1 or 0; needs_config_write = true; end

        changed, new_value = imgui.checkbox("Allow UObjectHook Attach Fallback", ALLOW_UOBJECTHOOK_ATTACH_FALLBACK == 1)
        if changed then ALLOW_UOBJECTHOOK_ATTACH_FALLBACK = new_value and 1 or 0; needs_config_write = true; end

        changed, new_value = imgui.checkbox("Direct Weapon Drive (Lua)", DIRECT_WEAPON_DRIVE == 1)
        if changed then DIRECT_WEAPON_DRIVE = new_value and 1 or 0; needs_config_write = true; end

        changed, new_value = imgui.checkbox("Direct Drive Moves Owner Actor", DIRECT_DRIVE_MOVE_OWNER_ACTOR == 1)
        if changed then DIRECT_DRIVE_MOVE_OWNER_ACTOR = new_value and 1 or 0; needs_config_write = true; end

        changed, new_value = imgui.checkbox("Native Bone Driver Owns Weapons", NATIVE_BONE_DRIVER_OWNS_WEAPONS == 1)
        if changed then NATIVE_BONE_DRIVER_OWNS_WEAPONS = new_value and 1 or 0; needs_config_write = true; end

        imgui.spacing()
        imgui.spacing()
        imgui.spacing()
        imgui.text(help_data)
    end
    imgui.spacing()

    if imgui.collapsing_header("Gameplay Options") then
		changed, new_value = imgui.slider_float("Normal UI Size", NORMAL_UI_SIZE, 1.0, 10.0)
        if changed then 
            NORMAL_UI_SIZE = new_value; 
            needs_config_write = true 
            vr.set_mod_value("UI_Size", tostring(NORMAL_UI_SIZE))
        end

        changed, new_value = imgui.checkbox("Health Auto Regenerate", CHEAT_AUTO_REGEN_HEALTH == 1)
        if changed then CHEAT_AUTO_REGEN_HEALTH = new_value and 1 or 0; needs_config_write = true; end
        
        changed, new_value = imgui.checkbox("Essence Auto Regenerate", CHEAT_AUTO_REGEN_ESSENCE == 1)
        if changed then CHEAT_AUTO_REGEN_ESSENCE = new_value and 1 or 0; needs_config_write = true; end
        
		changed, new_value = imgui.slider_float("Regen % Per Sec.", CHEAT_AUTO_REGEN_PERC, 0.0, 5.0)
        if changed then CHEAT_AUTO_REGEN_PERC = new_value; needs_config_write = true end
        
		changed, new_value = imgui.slider_float("Ranged Speed Mult.", CHEAT_GUN_SPEED_MULT, 1.0, 10.0)
        if changed then 
            CHEAT_GUN_SPEED_MULT = new_value; 
            needs_config_write = true 
        end
        imgui.text("Swap weapons after setting to see effect.\n")
	end
    
    imgui.spacing()
--[[    
    if imgui.collapsing_header("HUD") then
        
        changed, new_value = imgui.checkbox("HUD to Hands", (USE_ATTACHED_WIDGETS == 1))
        if changed then USE_ATTACHED_WIDGETS = new_value and 1 or 0; needs_config_write = true; end
        
		changed, new_value = imgui.slider_float("HUD widget brightness", WIDGET_BRIGHTNESS, 0.0, 100.0)
        if changed then 
            WIDGET_BRIGHTNESS = new_value; 
            needs_config_write = true 
        end
        imgui.text("Set to 0 for auto mode (recommended)")
    end
]]
    
    imgui.spacing()
    if imgui.collapsing_header("Gestures and Controls") then
        
        changed, new_value = imgui.checkbox("Right Stick Down Dodge (B)", right_stick_down_b == 1)
        if changed then right_stick_down_b = new_value and 1 or 0; needs_config_write = true; end

        changed, new_value = imgui.checkbox("Right Stick Up Sprint (L3)", right_stick_up_sprint == 1)
        if changed then right_stick_up_sprint = new_value and 1 or 0; needs_config_write = true; end

        changed, new_value = imgui.checkbox("Assign LB + Left Stick for dpad hotkeys", remap_lb_button == 1)
        if changed then remap_lb_button = new_value and 1 or 0; needs_config_write = true; end

        changed, new_value = imgui.checkbox("Lock Aim/UI Hand To Right", LOCK_AIM_HAND_TO_RIGHT == 1)
        if changed then LOCK_AIM_HAND_TO_RIGHT = new_value and 1 or 0; needs_config_write = true; end

	    imgui.spacing()
        imgui.text("Gestures are:\n")
        imgui.text("-Normal Attack: Melee weapon swing left and right for normal melee combo.\n")
        imgui.text("-Combo Attack: Hold melee weapon to your head, controller vibrates while\n")
        imgui.text("               charges, swipe down to hit\n")
        imgui.text("-Shield Block: Hold shield weapon to your head. Block UI shows on HUD\n")
        imgui.spacing()
        imgui.text("Melee gestures work for melee and unarmed. If using ranged weapon on off hand,\n")
        imgui.text("the offhand weapon directs the melee aim.\n")
        imgui.spacing()
        imgui.text("Distance is in virtual meters. Larger number requires bigger swing to register.")
        imgui.spacing()
		
        imgui.spacing()
        changed, new_value = imgui.checkbox("Use Melee Gesture System.", MELEE_SWING_AND_BLOCK == 1)
        if changed then MELEE_SWING_AND_BLOCK = new_value and 1 or 0; needs_config_write = true; end

		local changed, new_value = imgui.slider_float("Swing Threshold", SWIPE_THRESHOLD, 0.1, 1.0)
        if changed then
            local step = 0.1
            
            -- 3. Multiply by the step size (e.g., 5.0 * 0.5 = 2.5)
            new_value = math.floor((new_value / step) + 0.5) * step

            SWIPE_THRESHOLD = new_value
            needs_config_write = true
        end
            
        imgui.spacing()
        imgui.text("Distance is in virtual meters. Larger number requires bigger swing to register.")
        imgui.spacing()

        imgui.spacing()
        local changed, new_value = imgui.slider_float("Charge Threshold", CHARGE_THRESHOLD, 0.1, 0.4)
        if changed then
            local step = 0.05
            
            new_value = math.floor((new_value / step) + 0.5) * step

            CHARGE_THRESHOLD = new_value
            needs_config_write = true
        end
        imgui.text("Distance is in virtual meters. Left is closer to head to trigger.")

        imgui.spacing()
        local changed, new_value = imgui.slider_float("Shield Threshold", BLOCK_THRESHOLD, 0.1, 0.4)
        if changed then
            local step = 0.05
            
            new_value = math.floor((new_value / step) + 0.5) * step

            BLOCK_THRESHOLD = new_value
            needs_config_write = true
        end
        imgui.text("Distance is in virtual meters. Left is closer to head to trigger.")
        
        imgui.spacing()
        local changed, new_value = imgui.slider_int("Sword Block Threshold", SWORD_BLOCK_DEGREES_THRESHOLD, 15, 60)
        if changed then SWORD_BLOCK_DEGREES_THRESHOLD = new_value; needs_config_write = true; end
        imgui.text("Value is in degrees.")

        imgui.spacing()
        local changed, new_value = imgui.slider_float("Shield Size (Normal)", SHIELD_NORMAL_SIZE, 0.1, 1.0)
        if changed then
            SHIELD_NORMAL_SIZE = new_value
            needs_config_write = true
        end
        imgui.text("Shields are big and block gameplay visibility. You can shrink when equipped")
        
        imgui.spacing()
        local changed, new_value = imgui.slider_float("Shield Size (Blocking)", SHIELD_BLOCK_SIZE, 0.1, 1.0)
        if changed then
            SHIELD_BLOCK_SIZE = new_value
            needs_config_write = true
        end
        imgui.text("Blocking covers your eyes. Smaller shield for better visibility")
        
        imgui.spacing()
        changed, new_value = imgui.checkbox("Alternate Sword Block Detection.", USE_SIMPLE_SWORD_BLOCK == 1)
        if changed then USE_SIMPLE_SWORD_BLOCK = new_value and 1 or 0; needs_config_write = true; end
        imgui.text("A simpler but less precise detection of sword position. Hopefully\nthis is never needed.")

    end
    
    imgui.spacing()
    if imgui.collapsing_header("Lumen (Performance) Settings") then
        changed, new_value = imgui.checkbox("Enable Lumen", ENABLE_LUMEN == 1)
        if changed then 
            ENABLE_LUMEN = new_value and 1 or 0; 
            needs_config_write = true
            toggle_lumen(ENABLE_LUMEN)
        end
    end
    
    if needs_config_write == true then
        write_config()
	end
end)

read_config()
set_script_enabled(disableMod == 0)
