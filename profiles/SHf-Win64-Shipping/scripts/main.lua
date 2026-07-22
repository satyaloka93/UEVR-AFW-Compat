local uevrDev    = require('libs/uevr_dev')
local ik         = require('libs/ik')
local uevrUtils  = require('libs/uevr_utils')
local controllers= require('libs/controllers')
local configui   = require('libs/configui')
local reticule   = require('libs/reticule')
local hands      = require('libs/hands')
local attachments= require('libs/attachments')
local input      = require('libs/input')
local flickerFixer = require('libs/flicker_fixer')
local animation  = require('libs/animation')
local montage    = require('libs/montage')
local pawn_module = require('libs/pawn')
local ui         = require('libs/ui')
local shf        = require('shf')
local melee      = require('melee')
local examine    = require('examine')

local isDeveloperMode = true

-- ─────────────────────────────────────────────────────────────────────────────
-- Suppress dev/internal panels from the UEVR sidebar.
-- uevr.lua.add_script_panel is called once at panel-creation time, so this
-- intercept must run BEFORE any module init() that creates those panels.
-- ─────────────────────────────────────────────────────────────────────────────
local _HIDDEN_PANELS = {
    ["Pawn Dev Config"]   = true,
    ["Input Dev Config"]  = true,
    ["Montage Config Dev"]= true,
    ["Dev Utils"]         = true,
    ["Hand Config"]       = true,
    ["IK Dev Config"]     = true,
}
if uevr.lua and uevr.lua.add_script_panel then
    local _orig_add_panel = uevr.lua.add_script_panel
    uevr.lua.add_script_panel = function(label, drawFn)
        if _HIDDEN_PANELS[label] then return end
        return _orig_add_panel(label, drawFn)
    end
end

pawn_module.init(isDeveloperMode)
attachments.init(isDeveloperMode)
input.init(isDeveloperMode)
montage.init(isDeveloperMode)  -- enables dev montage tracker UI + registerMontageChangeCallback
uevrDev.init()
uevrUtils.setDeveloperMode(false)
ik.init(true, LogLevel.Info)
hands.enableConfigurationTool()

-- Disable UEVR movement/rotation overrides during cutscenes, attacks, death, etc.
-- shf.isVRInputDisabled() polls the game's own state methods and returns (bool, priority).
input.registerIsDisabledCallback(function()
    return shf.isVRInputDisabled()
end)

-- Wire hand animations into the IK rig and suppress native ApplyArmIK
-- once the PMC is live. shf.onIKMeshCreated handles both in one call.
local _ikMesh = nil  -- cached IK body mesh for weapon attachment

ik.registerOnMeshCreatedCallback(function(meshList, rig)
    shf.onIKMeshCreated(meshList, rig)
    _ikMesh = meshList and meshList[1]  -- cache for grip callback
    melee.setIKMesh(_ikMesh)            -- keep melee in sync for Claw hit detection
end)


-- ─────────────────────────────────────────────────────────────────────────────
-- Weapon attachment
-- ─────────────────────────────────────────────────────────────────────────────
local function getWeaponMesh()
    if uevrUtils.getValid(pawn) == nil then return nil end
    local weapon = pawn.CurrentWeapon
    if weapon == nil then return nil end
    return uevrUtils.getValid(weapon, {"WeaponMesh"})
end

attachments.registerOnGripUpdateCallback(function()
    local weaponMesh = getWeaponMesh()
    local rightHandComp = _ikMesh
    return weaponMesh, rightHandComp, "hand_r", nil, nil, nil, true
end)

-- ─────────────────────────────────────────────────────────────────────────────
-- Weapon re-equip on level load
-- The game creates WeaponMesh on load, but the IK rig rebuild (destroyComponent)
-- recursively destroys it as a child of PoseableMeshComponent. We monitor until
-- the mesh is stable for 2 full seconds, re-equipping if it gets destroyed.
-- ─────────────────────────────────────────────────────────────────────────────
local function getInventoryComponent()
    return uevrUtils.GetInstanceMatching(
        "Class /Script/GameNoce.NocePlayerInventoryComponent",
        "PersistentLevel"
    )
end

uevrUtils.registerLevelChangeCallback(function()
    local _retryId
    local _retryCount = 0
    local _phase = 1        -- 1=wait/unequip, 2=re-equip
    local _stableCount = 0  -- consecutive ticks where WeaponMesh was valid
    local _seenMesh = false -- have we ever seen the mesh this load?

    _retryId = uevrUtils.setInterval(200, function()
        _retryCount = _retryCount + 1
        if _retryCount > 90 then  -- 18 second window
            uevrUtils.clearInterval(_retryId)
            return
        end

        local p = uevrUtils.getValid(pawn)
        if p == nil then return end

        local weapon = p.CurrentWeapon
        local hasMesh = weapon ~= nil and uevrUtils.getValid(weapon, {"WeaponMesh"}) ~= nil

        if hasMesh then
            _seenMesh = true
            _stableCount = _stableCount + 1
            -- Wait for 10 consecutive stable checks (~2s) to outlast the IK rebuild window
            if _stableCount >= 10 then
                uevrUtils.clearInterval(_retryId)
            end
            return
        end

        -- Mesh disappeared after being seen — IK rig rebuild destroyed it, re-equip
        if _seenMesh then
            _seenMesh = false
            _stableCount = 0
            _phase = 1
        end

        -- Phase 1: unequip to reset bIsEquipped, then move to re-equip
        if _phase == 1 then
            if weapon ~= nil then
                pcall(function() p:UnEquipCurrentWeapon() end)
                _phase = 2
            elseif _retryCount >= 15 then
                _phase = 2
            end
            return
        end

        -- Phase 2: re-equip via the live InventoryComponent
        local inv = getInventoryComponent()
        if inv == nil then return end
        pcall(function() inv:TryEquipLastUsedWeapon() end)
    end)
end)

-- ─────────────────────────────────────────────────────────────────────────────
-- Button swap: X (Reload) ↔ B (Dodge)
-- Must be registered at top-level script scope (not inside a require'd module).
-- ─────────────────────────────────────────────────────────────────────────────
local _blockX = false
local _blockB = false

uevr.sdk.callbacks.on_xinput_get_state(function(retval, user_index, state)
    if state == nil then return end

    if state.Gamepad.wButtons & 0x4000 ~= 0 and not _blockB then
        _blockX = true
        state.Gamepad.wButtons = state.Gamepad.wButtons & ~(XINPUT_GAMEPAD_X)
        state.Gamepad.wButtons = state.Gamepad.wButtons | XINPUT_GAMEPAD_B
    else
        _blockX = false
    end

    if state.Gamepad.wButtons & 0x2000 ~= 0 and not _blockX then
        _blockB = true
        state.Gamepad.wButtons = state.Gamepad.wButtons & ~(XINPUT_GAMEPAD_B)
        state.Gamepad.wButtons = state.Gamepad.wButtons | XINPUT_GAMEPAD_X
    else
        _blockB = false
    end
end)

-- ─────────────────────────────────────────────────────────────────────────────
-- Melee collision config panel
-- Sliders live-update the runtime variables in melee.lua via melee.setConfig().
-- Values are auto-saved to data/shf_melee_config.json by configui.
-- ─────────────────────────────────────────────────────────────────────────────
local _MELEE_PREFIX = "shf_melee_"

configui.create({
    {
        panelLabel = "Silent Hill f Config",
        saveFile   = "shf_melee_config",
        layout = {
            -- ── Turning ──────────────────────────────────────────────────────
            -- Widget IDs intentionally match Input Dev Config so the existing
            -- onUpdate callbacks in input_config_dev.lua wire up automatically.
            { widgetType = "text", label = "Turning", wrapped = false },
            { widgetType = "spacing" },
            {
                widgetType   = "checkbox",
                id           = "uevr_input_useSnapTurn",
                label        = "Use Snap Turn",
                initialValue = false,
            },
            {
                widgetType   = "slider_int",
                id           = "uevr_input_snapAngle",
                label        = "Snap Turn Angle",
                range        = { 2, 180 },
                initialValue = 30,
            },
            {
                widgetType   = "slider_int",
                id           = "uevr_input_smoothTurnSpeed",
                label        = "Smooth Turn Speed",
                range        = { 1, 200 },
                initialValue = 80,
            },
            { widgetType = "spacing" },

            -- ── Collision Detection ───────────────────────────────────────────
            { widgetType = "text", label = "Collision Detection", wrapped = false },
            { widgetType = "spacing" },

            {
                widgetType   = "drag_float",
                id           = _MELEE_PREFIX .. "minSwingSpeed",
                label        = "Min Swing Speed  (UU/s)",
                speed        = 5.0,
                range        = { 50, 1200 },
                initialValue = 300.0,
            },
            { widgetType = "text", label = "  Tip speed required to check collision (300 = ~3 m/s)", wrapped = false },
            { widgetType = "spacing" },

            {
                widgetType   = "drag_float",
                id           = _MELEE_PREFIX .. "heavySpeed",
                label        = "Heavy Attack Speed  (UU/s)",
                speed        = 5.0,
                range        = { 100, 2000 },
                initialValue = 600.0,
            },
            { widgetType = "text", label = "  Above this triggers R2 (heavy). Below = R1 (light)", wrapped = false },
            { widgetType = "spacing" },

            { widgetType = "tree_node", id = "shf_advanced_tree", label = "Advanced", initialOpen = false },

            {
                widgetType   = "drag_float",
                id           = _MELEE_PREFIX .. "weaponLength",
                label        = "Weapon Tip Length  (UU)",
                speed        = 2.0,
                range        = { 10, 300 },
                initialValue = 80.0,
            },
            {
                widgetType   = "drag_float",
                id           = _MELEE_PREFIX .. "hitBuffer",
                label        = "Hit Buffer Radius  (UU)",
                speed        = 2.0,
                range        = { 0, 100 },
                initialValue = 15.0,
            },
            { widgetType = "text", label = "  Forgiveness margin added to each enemy's actual capsule radius", wrapped = false },
            {
                widgetType   = "drag_float",
                id           = _MELEE_PREFIX .. "hitHalfBuffer",
                label        = "Hit Buffer Half-Height  (UU)",
                speed        = 2.0,
                range        = { 0, 100 },
                initialValue = 15.0,
            },
            { widgetType = "text", label = "  Forgiveness margin added to each enemy's actual capsule half-height", wrapped = false },
            {
                widgetType   = "drag_float",
                id           = _MELEE_PREFIX .. "hitCooldown",
                label        = "Hit Cooldown  (sec)",
                speed        = 0.05,
                range        = { 0.1, 5.0 },
                initialValue = 0.8,
            },

            { widgetType = "spacing" },
            { widgetType = "text", label = "Claw Weapon", wrapped = false },
            {
                widgetType   = "drag_float",
                id           = _MELEE_PREFIX .. "clawTipOffset",
                label        = "Claw Tip Offset  (UU)",
                speed        = 1.0,
                range        = { -50, 150 },
                initialValue = 0.0,
            },
            { widgetType = "text", label = "  Projects collision point forward from hand_small_r along controller direction", wrapped = false },
            {
                widgetType   = "drag_float",
                id           = _MELEE_PREFIX .. "clawGTipOffset",
                label        = "Claw Large Tip Offset  (UU)",
                speed        = 1.0,
                range        = { -50, 150 },
                initialValue = 0.0,
            },
            { widgetType = "text", label = "  Projects collision point forward from index_01_large_r along controller direction", wrapped = false },

            { widgetType = "spacing" },
            { widgetType = "text", label = "Debug", wrapped = false },
            {
                widgetType   = "checkbox",
                id           = _MELEE_PREFIX .. "debugSphere",
                label        = "Show weapon tip sphere",
                initialValue = false,
            },
            { widgetType = "text", label = "  Shows exact point being tested for enemy overlap", wrapped = false },
            { widgetType = "tree_pop" },
        }
    }
})

-- Wire every slider to the live melee variables.
-- onCreateOrUpdate fires on first load (from saved JSON) AND on every drag.
local _meleeKeys = {
    "minSwingSpeed", "heavySpeed", "weaponLength",
    "hitBuffer", "hitHalfBuffer", "hitCooldown",
    "clawTipOffset", "clawGTipOffset",
}
for _, key in ipairs(_meleeKeys) do
    configui.onCreateOrUpdate(_MELEE_PREFIX .. key, function(value)
        melee.setConfig(key, value)
    end)
end

-- Debug sphere toggle
configui.onCreateOrUpdate(_MELEE_PREFIX .. "debugSphere", function(value)
    melee.setDebug(value)
end)
