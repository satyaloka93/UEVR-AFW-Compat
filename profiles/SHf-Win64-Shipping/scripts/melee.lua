-- melee.lua
-- Silent Hill f — VR collision-driven melee system
--
-- Attack detection: weapon tip (controller pos + forward × length) must overlap an
--   enemy's capsule (radius 35 UE units, half-height 76) while the controller is
--   moving above _MIN_SWING_SPEED.  Light vs Heavy is decided by swing speed.
--
-- Montage tracking: uevrUtils.registerMontageChangeCallback watches the pawn's
--   Mesh.AnimScriptInstance; montages matching _Atk_ trigger attack state.
--
-- During attack: pawn body mesh is force-hidden every tick (game re-enables it
--   for the 3rd-person attack animation each frame).

local uevrUtils   = require("libs/uevr_utils")
local uevr_lib    = require("libs/core/uevr_lib")
local controllers = require("libs/controllers")

local M = {}

-- ─────────────────────────────────────────────────────────────────────────────
-- Config
-- ─────────────────────────────────────────────────────────────────────────────
local _ATTACK_PLAY_RATE  = 40.0
local _INJECT_FRAMES     = 12      -- frames to hold injected button (~200 ms @ 60 fps)
local _HEAD_Z_OFFSET     = 150.0   -- UE units above pawn origin to pin the eye position

-- Collision detection
local _ENEMY_CLASS       = "Class /Script/GameNoce.NoceEnemyCharacter"
local _WEAPON_LENGTH     = 80.0    -- UE units: controller grip → weapon tip (fallback)
local _HIT_BUFFER        = 15.0    -- UU added to each enemy's actual capsule radius/half-height
                                   -- acts as a forgiveness margin on top of the real capsule size
local _HIT_HALF_BUFFER   = 15.0    -- same for vertical axis
local _MIN_SWING_SPEED   = 300.0   -- UE units/sec — below this, no collision check runs
                                   -- ~100 UU = 1 VR metre, so 300 = 3 m/s committed swing
local _HEAVY_SPEED_THRESH= 600.0   -- above this speed → heavy (R2), else → light (R1)
local _HIT_COOLDOWN_SECS = 0.8     -- minimum seconds between consecutive hit triggers
local _ENEMY_REFRESH_INT = 10      -- refresh enemy positions every N engine ticks (~0.16 s)

-- Per-weapon tip length overrides (keyed by substring of weapon class name)
-- Extend this table as more weapons are added.
local _WEAPON_LENGTHS = {
    Pipe     = 85.0,
    Lantern  = 30.0,
    Hammer   = 110.0,   -- sledgehammer is longer than pipe
    Axe      = 90.0,
}

-- ─────────────────────────────────────────────────────────────────────────────
-- State
-- ─────────────────────────────────────────────────────────────────────────────
local _isAttacking    = false
local _attackMontage  = nil
local _attackAnimInst = nil

local _injectLight = 0
local _injectHeavy = 0

-- Collision state
local _prevCtrlPos      = nil   -- {X,Y,Z} right controller world pos last tick
local _ctrlSpeed        = 0.0   -- UE units/sec this tick
local _hitCooldown      = 0.0   -- seconds remaining before next hit can fire
local _enemyPositions   = {}    -- cached {X,Y,Z,r,hh} world positions — reused in-place each refresh
local _enemyRefreshTick = 0     -- counts up to _ENEMY_REFRESH_INT

-- Debug visualization
local _debugEnabled    = false  -- toggled via M.setDebug / configui checkbox
local _debugSphereComp = nil    -- StaticMeshComponent (bManualAttachment=true, positioned each tick)
-- BasicShapes/Sphere is 100 UU diameter; scale 0.3 → 30 UU diameter = 15 UU radius
local _DEBUG_SCALE = 0.3

-- Claw IK mesh reference — injected via M.setIKMesh() when the IK rig is built.
-- Used so the collision sphere tracks the active claw hand bone on the IK rig.
local _clawIKMesh     = nil
local _CLAW_BONE      = "hand_small_r"
local _clawIsLarge    = false  -- true when large claw (ClawG) is active
local _clawTipOffset  = 0.0   -- UU forward offset for normal claw  (hand_small_r)
local _clawGTipOffset = 0.0   -- UU forward offset for large claw   (index_01_large_r)

-- Returns the tip offset appropriate for whichever claw arm is currently active.
local function _activeTipOffset()
    if _clawIsLarge then return _clawGTipOffset end
    return _clawTipOffset
end

-- ─────────────────────────────────────────────────────────────────────────────
-- Helpers
-- ─────────────────────────────────────────────────────────────────────────────
-- ─────────────────────────────────────────────────────────────────────────────
-- Debug sphere helpers
-- ─────────────────────────────────────────────────────────────────────────────
-- Returns the tip-offset length for the currently equipped weapon.
-- Defined here (before the debug tick) so the tick closure can reference it.
local function _getWeaponLength()
    local p = uevrUtils.getValid(pawn)
    if p == nil then return _WEAPON_LENGTH end
    local ok, wep = pcall(function() return p.CurrentWeapon end)
    if not ok or wep == nil then return _WEAPON_LENGTH end
    local ok2, cname = pcall(function()
        return wep:get_class():get_fname():to_string()
    end)
    if ok2 and cname ~= nil then
        for key, len in pairs(_WEAPON_LENGTHS) do
            if cname:find(key) then return len end
        end
    end
    return _WEAPON_LENGTH
end

-- Returns true when the equipped weapon is the Claw (has no WeaponMesh).
local function _isClawWeapon(wep)
    if wep == nil then return false end
    local ok, fullName = pcall(function() return wep:get_full_name() end)
    return ok and fullName ~= nil and fullName:find("Claw", 1, true) ~= nil
end

local _SPHERE_MESH_PATH = "StaticMesh /Engine/BasicShapes/Sphere.Sphere"
local _SPHERE_MAT_PATH  = "Material /Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"
local _SMC_CLASS_PATH   = "Class /Script/Engine.StaticMeshComponent"

local function _getOrCreateDebugSphere()
    -- Validate existing comp hasn't been GC'd
    if _debugSphereComp ~= nil then
        local ok = pcall(function() return _debugSphereComp.bHiddenInGame end)
        if not ok then _debugSphereComp = nil end
    end

    if _debugSphereComp == nil then
        local p = uevrUtils.getValid(pawn)
        if p == nil then return nil end

        local smcClass = uevr.api:find_uobject(_SMC_CLASS_PATH)
        if smcClass == nil then return nil end

        local identityT = { Rotation={X=0,Y=0,Z=0,W=1}, Translation={X=0,Y=0,Z=0}, Scale3D={X=1,Y=1,Z=1} }
        -- bManualAttachment=true → no parent, component floats freely in world space
        local comp = p:AddComponentByClass(smcClass, true, identityT, false)
        if comp == nil or comp == false then return nil end

        -- MUST set Movable before any transform changes — Static mobility ignores them silently
        pcall(function() comp:SetMobility(2) end)  -- EComponentMobility::Movable

        local sphereMesh = uevr.api:find_uobject(_SPHERE_MESH_PATH)
        if sphereMesh ~= nil then pcall(function() comp:SetStaticMesh(sphereMesh) end) end

        local mat = uevr.api:find_uobject(_SPHERE_MAT_PATH)
        if mat ~= nil then pcall(function() comp:SetMaterial(0, mat) end) end

        pcall(function() comp:SetCollisionEnabled(0) end)
        pcall(function() comp:SetWorldScale3D({X=_DEBUG_SCALE, Y=_DEBUG_SCALE, Z=_DEBUG_SCALE}) end)
        pcall(function() comp:SetCastShadow(false) end)
        pcall(function() comp:SetHiddenInGame(false, false) end)

        _debugSphereComp = comp
    end

    return _debugSphereComp
end

-- Per-tick debug sphere update — independent of the swing gate
uevr.sdk.callbacks.on_pre_engine_tick(function(engine, delta)
    if not _debugEnabled then
        if _debugSphereComp ~= nil then
            pcall(function() _debugSphereComp:SetHiddenInGame(true, false) end)
        end
        return
    end

    -- Primary: read tip position from weapon socket OR (Claw) from IK mesh bone
    local tipPos = nil
    pcall(function()
        local p   = uevrUtils.getValid(pawn)
        local wep = p and p.CurrentWeapon
        if wep == nil then return end
        -- Claw has no WeaponMesh — read hand_small_r from the IK rig instead
        if _isClawWeapon(wep) and _clawIKMesh ~= nil then
            local pos = _clawIKMesh:GetSocketLocation(uevrUtils.fname_from_string(_CLAW_BONE))
            if pos ~= nil then
                -- Apply forward offset (controller forward direction × _clawTipOffset)
                if _activeTipOffset() ~= 0.0 then
                    local fwd = controllers.getControllerDirection(1)
                    if fwd ~= nil then
                        tipPos = { X = pos.X + fwd.X * _activeTipOffset(),
                                   Y = pos.Y + fwd.Y * _activeTipOffset(),
                                   Z = pos.Z + fwd.Z * _activeTipOffset() }
                    else
                        tipPos = pos
                    end
                else
                    tipPos = pos
                end
            end
            return
        end
        local mesh = wep.WeaponMesh
        if mesh == nil then return end
        tipPos = mesh:GetSocketLocation(uevrUtils.fname_from_string("Top"))
    end)

    -- Fallback: controller forward × weapon length (for when socket isn't found)
    if tipPos == nil then
        local cpos = controllers.getControllerLocation(1)
        local fwd  = controllers.getControllerDirection(1)
        if cpos == nil or fwd == nil then return end
        local tipLen = _getWeaponLength()
        tipPos = { X = cpos.X + fwd.X * tipLen,
                   Y = cpos.Y + fwd.Y * tipLen,
                   Z = cpos.Z + fwd.Z * tipLen }
    end

    local comp = _getOrCreateDebugSphere()
    if comp == nil then return end

    pcall(function()
        comp:SetHiddenInGame(false, false)
        comp:K2_SetWorldLocation({X=tipPos.X, Y=tipPos.Y, Z=tipPos.Z}, false, {}, false)
    end)
end)


local function _isAttackMontageName(name)
    if name == nil or name == "" then return false end
    return name:find("_Atk_") ~= nil and
           (name:find("ComboL") ~= nil or
            name:find("ComboH") ~= nil or
            name:find("ChargedAttack") ~= nil)
end

local function _hasWeapon()
    local p = uevrUtils.getValid(pawn)
    if p == nil then return false end
    local ok, has = pcall(function() return p.CurrentWeapon ~= nil end)
    return ok and has == true
end

-- Capsule overlap: lateral distance² < (radius+buffer)² AND vertical distance < half-height+buffer
local function _inEnemyCapsule(tx, ty, tz, enemy)
    local dx = tx - enemy.X
    local dy = ty - enemy.Y
    local dz = tz - enemy.Z
    local r  = enemy.r  + _HIT_BUFFER
    local hh = enemy.hh + _HIT_HALF_BUFFER
    return (dx*dx + dy*dy) < (r * r)
           and math.abs(dz) < hh
end

-- ─────────────────────────────────────────────────────────────────────────────
-- Montage change callback — reliable attack start/end detection
-- ─────────────────────────────────────────────────────────────────────────────
uevrUtils.registerMontageChangeCallback(function(montageObj, name, animInstance)
    if _isAttackMontageName(name) then
        _isAttacking    = true
        _attackMontage  = montageObj
        _attackAnimInst = animInstance

        pcall(function()
            if animInstance ~= nil and montageObj ~= nil then
                animInstance:Montage_SetPlayRate(montageObj, _ATTACK_PLAY_RATE)
            end
        end)
        pcall(function()
            local p = uevrUtils.getValid(pawn)
            if p ~= nil then p.AnimRootMotionTranslationScale = 0.0 end
        end)
    else
        if _isAttacking then
            _isAttacking = false
            pcall(function()
                if _attackAnimInst ~= nil and _attackMontage ~= nil then
                    _attackAnimInst:Montage_SetPlayRate(_attackMontage, 1.0)
                end
            end)
            pcall(function()
                local p = uevrUtils.getValid(pawn)
                if p ~= nil then p.AnimRootMotionTranslationScale = 1.0 end
            end)
            _attackMontage  = nil
            _attackAnimInst = nil
        end
    end
end)

-- ─────────────────────────────────────────────────────────────────────────────
-- Per-tick: force pawn body mesh hidden during attack
-- ─────────────────────────────────────────────────────────────────────────────
uevr.sdk.callbacks.on_pre_engine_tick(function(engine, delta)
    if not _isAttacking then return end
    pcall(function()
        local p = uevrUtils.getValid(pawn)
        if p == nil then return end
        local mesh = p.Mesh
        if mesh ~= nil then mesh:SetHiddenInGame(true, false) end
    end)
end)

-- ─────────────────────────────────────────────────────────────────────────────
-- XInput injection — hold injected button for _INJECT_FRAMES frames
-- ─────────────────────────────────────────────────────────────────────────────
uevr.sdk.callbacks.on_xinput_get_state(function(retval, user_index, state)
    if state == nil then return end
    if _injectLight > 0 then
        state.Gamepad.wButtons = state.Gamepad.wButtons | XINPUT_GAMEPAD_RIGHT_SHOULDER
        _injectLight = _injectLight - 1
    end
    if _injectHeavy > 0 then
        state.Gamepad.bRightTrigger = math.max(state.Gamepad.bRightTrigger, 255)
        _injectHeavy = _injectHeavy - 1
    end
end)

-- ─────────────────────────────────────────────────────────────────────────────
-- Collision-driven attack tick
-- Runs every engine tick; only does expensive work while actively swinging.
-- ─────────────────────────────────────────────────────────────────────────────
uevr.sdk.callbacks.on_pre_engine_tick(function(engine, delta)
    -- 1. Track right-controller velocity ─────────────────────────────────────
    local cpos = controllers.getControllerLocation(1)
    if cpos ~= nil then
        if _prevCtrlPos ~= nil and delta > 0 then
            local dx = cpos.X - _prevCtrlPos.X
            local dy = cpos.Y - _prevCtrlPos.Y
            local dz = cpos.Z - _prevCtrlPos.Z
            _ctrlSpeed = math.sqrt(dx*dx + dy*dy + dz*dz) / delta
        end
        _prevCtrlPos = { X = cpos.X, Y = cpos.Y, Z = cpos.Z }
    else
        _ctrlSpeed = 0.0
    end

    -- 2. Cooldown tick ────────────────────────────────────────────────────────
    if _hitCooldown > 0 then
        _hitCooldown = _hitCooldown - delta
    end

    -- 3. Refresh enemy positions every N ticks (bridge calls happen here, not in swing path)
    _enemyRefreshTick = _enemyRefreshTick + 1
    if _enemyRefreshTick >= _ENEMY_REFRESH_INT then
        _enemyRefreshTick = 0
        local raw   = uevr_lib.find_all_of(_ENEMY_CLASS, false) or {}
        local count = 0
        for _, enemy in ipairs(raw) do
            local ok, loc = pcall(function() return enemy:K2_GetActorLocation() end)
            if ok and loc ~= nil then
                count = count + 1
                -- Read actual capsule dimensions (fall back to safe defaults if unavailable)
                local r, hh = 35.0, 76.0  -- conservative defaults (small enemy size)
                pcall(function()
                    local cap = enemy.CapsuleComponent
                    if cap ~= nil then
                        local cr = cap.CapsuleRadius
                        local ch = cap.CapsuleHalfHeight
                        if cr and cr > 0 then r  = cr end
                        if ch and ch > 0 then hh = ch end
                    end
                end)
                -- Reuse existing table in-place; only allocate for new slots
                if _enemyPositions[count] == nil then
                    _enemyPositions[count] = { X = loc.X, Y = loc.Y, Z = loc.Z, r = r, hh = hh }
                else
                    _enemyPositions[count].X  = loc.X
                    _enemyPositions[count].Y  = loc.Y
                    _enemyPositions[count].Z  = loc.Z
                    _enemyPositions[count].r  = r
                    _enemyPositions[count].hh = hh
                end
            end
        end
        -- Trim stale entries if enemy count dropped
        for i = count + 1, #_enemyPositions do
            _enemyPositions[i] = nil
        end
    end

    -- 4. Gate: only run collision check when actually swinging ────────────────
    if _ctrlSpeed < _MIN_SWING_SPEED then return end
    if _hitCooldown > 0               then return end
    if not _hasWeapon()               then return end
    if cpos == nil                    then return end

    -- 5. Compute weapon-tip world position
    --    Normal weapons: "Top" socket on WeaponMesh
    --    Claw:           hand_small_r bone on the IK rig mesh (no WeaponMesh exists)
    local tx, ty, tz
    local socketOk = false
    pcall(function()
        local p   = uevrUtils.getValid(pawn)
        local wep = p and p.CurrentWeapon
        if wep == nil then return end
        -- Claw weapon — use IK mesh bone position + optional forward offset
        if _isClawWeapon(wep) and _clawIKMesh ~= nil then
            local pos = _clawIKMesh:GetSocketLocation(uevrUtils.fname_from_string(_CLAW_BONE))
            if pos == nil then return end
            if _activeTipOffset() ~= 0.0 then
                local fwd = controllers.getControllerDirection(1)
                if fwd ~= nil then
                    tx = pos.X + fwd.X * _activeTipOffset()
                    ty = pos.Y + fwd.Y * _activeTipOffset()
                    tz = pos.Z + fwd.Z * _activeTipOffset()
                else
                    tx, ty, tz = pos.X, pos.Y, pos.Z
                end
            else
                tx, ty, tz = pos.X, pos.Y, pos.Z
            end
            socketOk = true
            return
        end
        -- Normal weapons — use "Top" socket on WeaponMesh
        local mesh = wep.WeaponMesh
        if mesh == nil then return end
        local pos = mesh:GetSocketLocation(uevrUtils.fname_from_string("Top"))
        if pos == nil then return end
        tx, ty, tz = pos.X, pos.Y, pos.Z
        socketOk = true
    end)

    if not socketOk then
        -- Fallback: project forward from controller grip
        local fwd = controllers.getControllerDirection(1)
        if fwd == nil then return end
        local tipLen = _getWeaponLength()
        tx = cpos.X + fwd.X * tipLen
        ty = cpos.Y + fwd.Y * tipLen
        tz = cpos.Z + fwd.Z * tipLen
    end

    -- 6. Check cached positions — pure Lua table reads, no bridge calls ────────
    for _, pos in ipairs(_enemyPositions) do
        if _inEnemyCapsule(tx, ty, tz, pos) then
                -- HIT — fire attack and start cooldown
                _hitCooldown = _HIT_COOLDOWN_SECS

                if _ctrlSpeed >= _HEAVY_SPEED_THRESH then
                    _injectHeavy = _INJECT_FRAMES
                    pcall(function()
                        uevr.params.vr.trigger_haptic_vibration(
                            0, 0.15, 0.8, 1.0,
                            uevr.params.vr.get_right_joystick_source())
                    end)
                else
                    _injectLight = _INJECT_FRAMES
                    pcall(function()
                        uevr.params.vr.trigger_haptic_vibration(
                            0, 0.08, 0.5, 1.0,
                            uevr.params.vr.get_right_joystick_source())
                    end)
                end

                return  -- one hit per tick max
        end
    end
end)

-- ─────────────────────────────────────────────────────────────────────────────
-- Stereo view offset override — pin camera to pawn during attack
-- ─────────────────────────────────────────────────────────────────────────────
uevr.params.sdk.callbacks.on_early_calculate_stereo_view_offset(function(device, view_index, world_to_meters, position, rotation, is_double)
    if view_index ~= 0 then return end
    if not _isAttacking then return end
    pcall(function()
        local p = uevrUtils.getValid(pawn)
        if p == nil then return end
        local loc = p:K2_GetActorLocation()
        if loc == nil then return end
        position.x = loc.x
        position.y = loc.y
        position.z = loc.z + _HEAD_Z_OFFSET
    end)
end)

-- ─────────────────────────────────────────────────────────────────────────────
-- Public API
-- ─────────────────────────────────────────────────────────────────────────────
function M.isAttacking() return _isAttacking end
function M.init() end  -- callbacks registered at load time

--- Toggle the weapon-tip debug sphere.
function M.setDebug(enabled)
    _debugEnabled = enabled == true
    if not _debugEnabled and _debugSphereComp ~= nil then
        pcall(function() _debugSphereComp:SetHiddenInGame(true, false) end)
    end
end

--- Inject the IK rig body mesh so Claw hits can track the claw hand bone.
-- Called from main.lua's registerOnMeshCreatedCallback every time the rig
-- is built or rebuilt (e.g. on a Claw pawn swap).
function M.setIKMesh(mesh)
    _clawIKMesh = mesh
end

--- Switch which IK rig bone the collision sphere tracks.
-- Pass the normal claw bone ("hand_small_r") or any large-claw bone ("index_01_large_r", etc.).
-- Also updates the _clawIsLarge flag so _activeTipOffset() picks the right offset.
function M.setClawBone(name)
    _CLAW_BONE   = name
    _clawIsLarge = (name ~= "hand_small_r")
end

--- Live-update any collision config value from outside (e.g. configui sliders).
-- @param key  string  one of: minSwingSpeed, heavySpeedThresh, hitBuffer, hitHalfBuffer, hitCooldown
-- @param value number
function M.setConfig(key, value)
    if key == "minSwingSpeed"     then _MIN_SWING_SPEED    = value
    elseif key == "heavySpeed"    then _HEAVY_SPEED_THRESH = value
    elseif key == "weaponLength"  then _WEAPON_LENGTH       = value
    elseif key == "hitBuffer"     then _HIT_BUFFER          = value
    elseif key == "hitHalfBuffer" then _HIT_HALF_BUFFER     = value
    elseif key == "hitCooldown"   then _HIT_COOLDOWN_SECS   = value
    elseif key == "clawTipOffset" then _clawTipOffset       = value
    elseif key == "clawGTipOffset"then _clawGTipOffset      = value
    end
end

--- Return current values so the UI can display them after a save/load.
function M.getConfig()
    return {
        minSwingSpeed  = _MIN_SWING_SPEED,
        heavySpeed     = _HEAVY_SPEED_THRESH,
        weaponLength   = _WEAPON_LENGTH,
        hitBuffer      = _HIT_BUFFER,
        hitHalfBuffer  = _HIT_HALF_BUFFER,
        hitCooldown    = _HIT_COOLDOWN_SECS,
        clawTipOffset  = _clawTipOffset,
        clawGTipOffset = _clawGTipOffset,
    }
end

return M
