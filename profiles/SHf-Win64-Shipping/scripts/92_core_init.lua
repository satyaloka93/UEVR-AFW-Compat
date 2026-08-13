-- 92_core_init.lua — minimal replacement for main.lua.
--
-- WHY: main.lua is the crashy half of the bisection, but it is monolithic --
-- it hard-requires every library (montage, uevr_dev, reticule, ui, configui,
-- hands config tool) purely to build developer/config panels, and only a small
-- part of it provides actual gameplay wiring. This script keeps that gameplay
-- wiring and drops the rest.
--
-- KEPT (this is what makes weapons work properly):
--   * pawn/attachments/input/ik initialisation
--   * the IK mesh callback, so shf.lua and melee.lua get the body mesh
--   * attachments grip callback -> attaches weapon.WeaponMesh to the IK hand
--     bone socket "hand_r". This is the mechanism that makes the HAND CLOSE
--     around the weapon (it drives a per-weapon grip pose), which a plain
--     UObjectHook motion-controller attach cannot reproduce.
--
-- DROPPED (developer/config surface only, no gameplay effect):
--   montage.init, uevrDev.init, hands.enableConfigurationTool, every configui
--   panel, and the melee config sliders. Melee still uses its built-in defaults
--   from data/shf_melee_config.json.
--
-- Button remap (X<->B / dodge) lives separately in 91_button_swap.lua.

local uevrUtils   = require('libs/uevr_utils')
local ik          = require('libs/ik')
local attachments = require('libs/attachments')
local input       = require('libs/input')
local pawn_module = require('libs/pawn')
local shf         = require('shf')
local melee       = require('melee')

local function LOG(msg)
    pcall(function() uevr.params.functions.log_warn("[core] " .. msg) end)
end

-- Developer mode off: this is what suppresses the dev panels that main.lua
-- otherwise created and then had to hide again.
local DEV = false

pcall(function() pawn_module.init(DEV) end)
pcall(function() attachments.init(DEV) end)
pcall(function() input.init(DEV) end)
pcall(function() uevrUtils.setDeveloperMode(false) end)
pcall(function() ik.init(true, LogLevel and LogLevel.Info or nil) end)

-- Route VR-input suppression through shf.lua's state machine (cutscenes etc.).
pcall(function()
    input.registerIsDisabledCallback(function()
        return shf.isVRInputDisabled()
    end)
end)

-- Cache the IK body mesh; shf and melee both need it.
local _ikMesh = nil
pcall(function()
    ik.registerOnMeshCreatedCallback(function(meshList, rig)
        pcall(function() shf.onIKMeshCreated(meshList, rig) end)
        _ikMesh = meshList and meshList[1]
        pcall(function() melee.setIKMesh(_ikMesh) end)
        LOG("IK mesh ready")
    end)
end)

-- Weapon grip: hand the attachments system the weapon mesh + the hand bone.
local function getWeaponMesh()
    if uevrUtils.getValid(pawn) == nil then return nil end
    local weapon = pawn.CurrentWeapon
    if weapon == nil then return nil end
    return uevrUtils.getValid(weapon, {"WeaponMesh"})
end

pcall(function()
    attachments.registerOnGripUpdateCallback(function()
        local weaponMesh = getWeaponMesh()
        return weaponMesh, _ikMesh, "hand_r", nil, nil, nil, true
    end)
end)

LOG("loaded (minimal main.lua replacement; dev/config panels omitted)")

-- ─────────────────────────────────────────────────────────────────────────────
-- Reload hardening (added after a save/exit/reload session showed two faults:
-- the weapon mesh stayed invisible, and thumbstick input registered as attacks
-- until things settled).
--
-- Cause of both: references that survive a reload while pointing at destroyed
-- objects. melee keeps the IK mesh from the PREVIOUS level until a new one is
-- created, so its positions jump wildly and read as swings; and the recreated
-- weapon never gets attachments.initAttachment() run on it, which is what
-- normally forces SetVisibility/SetHiddenInGame/SetRenderInMainPass.
-- ─────────────────────────────────────────────────────────────────────────────

local _lastPawnAddr = nil
local _visTimer = 0.0
local VIS_INTERVAL = 1.0

local function ensureWeaponVisible()
    pcall(function()
        local mesh = getWeaponMesh()
        if mesh == nil then return end
        mesh:SetVisibility(true, true)
        mesh:SetHiddenInGame(false, true)
        pcall(function() mesh:call("SetRenderInMainPass", true) end)
    end)
end

uevr.sdk.callbacks.on_pre_engine_tick(function(engine, delta)
    if delta == nil or delta <= 0 or delta > 1.0 then return end

    -- Drop stale cross-level references the moment the pawn changes. This runs
    -- ungated on purpose: clearing must happen BEFORE the gate reopens.
    -- Reads the address 00_settle.lua already resolved (it runs first, being
    -- alphabetically earlier) rather than doing a second per-tick lookup.
    local addr = _G.SHF_PAWN_ADDR
    if addr ~= _lastPawnAddr then
        _lastPawnAddr = addr
        _ikMesh = nil
        pcall(function() melee.setIKMesh(nil) end)   -- stale mesh => phantom swings
        LOG("pawn changed; cleared stale IK mesh")
    end

    if not _G.SHF_SETTLED then return end

    -- Re-assert weapon visibility periodically; cheap and idempotent.
    _visTimer = _visTimer + delta
    if _visTimer >= VIS_INTERVAL then
        _visTimer = 0.0
        ensureWeaponVisible()
    end
end)
