-- shf.lua
-- Silent Hill f — Stage 1 VR integration layer
-- Handles: ApplyArmIK suppression, camera-turn zeroing, state gating, HMD-relative movement.
-- All game state reads go through local helpers; the public API is intentionally narrow.

local uevrUtils    = require("libs/uevr_utils")
local ik           = require("libs/ik")
local pawn_module  = require("libs/pawn")
local input        = require("libs/input")
local uevr_lib     = require("libs/core/uevr_lib")
local melee        = require("melee")

local api       = uevr.api
local vr        = uevr.params.vr
local callbacks = uevr.sdk.callbacks

local M = {}

local _PAWN_ROTATION_GAME = 1  -- PawnRotationMode.NONE = Body Orientation: Game

-- ─────────────────────────────────────────────────────────────────────────────
-- Cached references (re-acquired whenever pawn changes)
-- ─────────────────────────────────────────────────────────────────────────────
local _controller   = nil   -- NocePlayerController
local _animInstance = nil   -- ABP_Pl_Hina_C
local _armIkKilled  = false -- only write once per anim-instance lifetime
local _cameraManager  = nil   -- PlayerCameraManager (cached)
local _inCutscene     = false -- true when ViewTarget is a CineCameraActor
local _inInteraction  = false -- true when pawn IsInteracting (shimmy, gaps, etc.)
local _inGhostPawn    = false -- true when UEVR local pawn has no Mesh (traversal proxy)
local _inFogChase     = false -- true when fog monster chase detected (camera ≈180°, FOV≈55)
local _inExamine      = false -- true when ViewTarget is a plain CameraActor (examine puzzle)
local _inSpecialMode  = false -- any of the above

-- Examine puzzle: cached actor + original camera bounds for restore on exit
local _examineActor       = nil
local _examineBoundsOrig  = nil
local _EXAMINE_BOUND_SCALE = 5.0  -- expand movement range 5× for VR
local _ikMeshList     = nil   -- IK hand meshes; hidden during special mode
local _inAttackState  = false -- true while a melee attack montage is playing
local _atpcComp       = nil   -- ATPCCameraComponent; tick disabled during attacks

local _lanternActor    = nil   -- BP_Wep_Melee_Lantern_C live instance
local _lanternSearched = false -- true once we've attempted the class scan this level
local _LANTERN_CLASS = "BlueprintGeneratedClass /Game/Weapon/Melee/Lantern/BP_Wep_Melee_Lantern.BP_Wep_Melee_Lantern_C"

-- ─────────────────────────────────────────────────────────────────────────────
-- Costume change detection
--   CharMeshType.DataTable changes when the player swaps costumes at the shrine.
--   We track its address and destroy+recreate the IK rig when it changes so the
--   new costume mesh is used for hand IK.
-- ─────────────────────────────────────────────────────────────────────────────
local _lastCharMeshDataTableAddr = nil
local _costumeCheckTimer         = 0
local _COSTUME_CHECK_INTERVAL    = 0.5  -- poll every 500 ms

-- Weapon CapsuleComponent hide
local _lastWeaponAddr  = nil  -- address string of last-seen CurrentWeapon
local _weaponCapsule   = nil  -- CapsuleComponent from CurrentWeapon.Capsule


local function _refreshRefs()
    -- Camera manager refreshed unconditionally — stays valid during pawn swaps
    -- (e.g. fog-chase sequence where pawn == nil).
    if uevrUtils.getValid(_cameraManager) == nil then
        local pc = uevr.api:get_player_controller(0)
        if pc ~= nil then
            local ok, cm = pcall(function() return pc.PlayerCameraManager end)
            if ok and cm ~= nil and cm ~= false then
                _cameraManager = cm
            end
        end
    end

    local p = uevrUtils.getValid(pawn)
    if p == nil then
        _controller   = nil
        _animInstance = nil
        _armIkKilled  = false
        return
    end

    -- Controller
    if uevrUtils.getValid(_controller) == nil then
        _controller = uevrUtils.getValid(p, {"Controller"})
    end

    -- AnimInstance (via Mesh → GetAnimInstance)
    if uevrUtils.getValid(_animInstance) == nil then
        _armIkKilled = false
        local mesh = uevrUtils.getValid(p, {"Mesh"})
        if mesh ~= nil and mesh.GetAnimInstance ~= nil then
            local ai = mesh:GetAnimInstance()
            if ai ~= nil then
                _animInstance = ai
            end
        end
    end
end


-- ─────────────────────────────────────────────────────────────────────────────
-- 1. ApplyArmIK suppression
--    Called once per anim-instance lifetime from the IK mesh-created callback
--    AND every pre-tick as a failsafe (the ABP can reinitialise on level load).
-- ─────────────────────────────────────────────────────────────────────────────
local function _suppressNativeArmIK()
    if _animInstance == nil or _armIkKilled then return end
    local ok, err = pcall(function()
        if _animInstance.ApplyArmIK ~= nil then
            _animInstance.ApplyArmIK = false
            _armIkKilled = true
        end
    end)
    if not ok then
        -- animInstance may have gone stale; force re-acquire next tick
        _animInstance = nil
        _armIkKilled  = false
    end
end

-- ─────────────────────────────────────────────────────────────────────────────
-- 1b. Root motion rotation suppression
--    The AnimBP resets RootMotionMode = 2 (RootMotionFromEverything) every
--    tick. Sprint and locomotion animations include a rotational root motion
--    component that physically rotates the capsule, fighting our body yaw
--    K2_SetWorldRotation writes and causing camera snaps.
--    Force mode = 1 (IgnoreRootMotion) each pre-tick so capsule rotation is
--    driven exclusively by our body yaw system, not animation root motion.
-- ─────────────────────────────────────────────────────────────────────────────
local _ROOT_MOTION_MONTAGES_ONLY = 2  -- ERootMotionMode::RootMotionFromMontagesOnly
-- Mode 2 suppresses state-machine rotational root motion (no camera snaps)
-- while still allowing montage root motion (shimmy/traversal animations translate through gaps)
local function _suppressRootMotionRotation()
    if _animInstance == nil then return end
    pcall(function()
        if _animInstance.RootMotionMode ~= nil and
           _animInstance.RootMotionMode ~= _ROOT_MOTION_MONTAGES_ONLY then
            _animInstance.RootMotionMode = _ROOT_MOTION_MONTAGES_ONLY
        end
    end)
end

-- Called from main.lua inside ik.registerOnMeshCreatedCallback
function M.onIKMeshCreated(meshList, rig)
    rig:setAnimationsFromHandsParametersFile("hands_parameters")
    _ikMeshList = meshList  -- cache for cutscene visibility toggling
    -- Refresh refs immediately — the IK PMC just came up, pawn is valid
    _refreshRefs()
    _suppressNativeArmIK()
end

-- ─────────────────────────────────────────────────────────────────────────────
-- 2. State gate
--    Returns true when VR input/overrides should be active.
--    Reads via pcall so a missing method never breaks the tick.
-- ─────────────────────────────────────────────────────────────────────────────
local function _safePawnCall(methodName)
    local p = uevrUtils.getValid(pawn)
    if p == nil or p[methodName] == nil then return false end
    local ok, result = pcall(function() return p[methodName](p) end)
    return ok and result == true
end

local function _canAcceptVRInput()
    if uevrUtils.getValid(pawn) == nil then return false end
    if _inSpecialMode                        then return false end
    if _safePawnCall("InDeadState")          then return false end
    if _safePawnCall("GetIsInEvent")         then return false end
    if _safePawnCall("InAttackStateMachine") then return false end
    if _safePawnCall("InDamageState")        then return false end
    if _safePawnCall("IsInteracting")        then return false end
    return true
end

-- Expose for main.lua input.registerIsDisabledCallback
function M.isVRInputDisabled()
    return not _canAcceptVRInput(), 10
end

-- ─────────────────────────────────────────────────────────────────────────────
-- 3. Camera turn suppression
--    The game accumulates camera yaw from InputTurn / InputTurnRate on the
--    NocePlayerController every tick. Zero both fields so the game camera
--    never drifts away from the HMD pose. UEVR already drives the actual
--    camera from the HMD natively — we only need to stop the game overriding it.
-- ─────────────────────────────────────────────────────────────────────────────
local function _zeroCameraTurn()
    if _controller == nil then return end
    local ok, _ = pcall(function()
        if _controller.InputTurn     ~= nil then _controller.InputTurn     = 0.0 end
        if _controller.InputTurnRate ~= nil then _controller.InputTurnRate = 0.0 end
        if _controller.InputLookUp   ~= nil then _controller.InputLookUp   = 0.0 end
    end)
    if not ok then
        _controller = nil  -- stale; re-acquire next tick
    end
end

-- ─────────────────────────────────────────────────────────────────────────────
-- 4. HMD-relative movement direction
--    Read the raw stick input from the controller, rotate it by the HMD yaw
--    (from UEVR's currentHeadRotator via on_post_calculate_stereo_view_offset),
--    then write the remapped values back to InputForward / InputRight so
--    the game's movement component gets camera-relative directions.
-- ─────────────────────────────────────────────────────────────────────────────
local _hmdYaw = 0.0  -- updated every stereo view offset callback

uevr.params.sdk.callbacks.on_post_calculate_stereo_view_offset(function(device, view_index, world_to_meters, position, rotation, is_double)
    -- Only capture from one eye to avoid double-write
    if view_index == 0 then
        _hmdYaw = rotation.Yaw
    end
end)

local function _remapMovement()
    if _controller == nil then return end

    local ok, _ = pcall(function()
        local fwd = _controller.InputForward
        local rgt = _controller.InputRight
        if fwd == nil or rgt == nil then return end

        -- Only remap if there is actual stick input
        if math.abs(fwd) < 0.01 and math.abs(rgt) < 0.01 then return end

        -- Get the pawn's current world yaw so we know how much to rotate by
        local pawnYaw = 0.0
        local p = uevrUtils.getValid(pawn)
        if p ~= nil and p.RootComponent ~= nil then
            local rot = p.RootComponent:K2_GetComponentRotation()
            if rot ~= nil then pawnYaw = rot.Yaw end
        end

        -- delta = how far HMD is rotated relative to pawn facing
        local delta = uevrUtils.clampAngle180(_hmdYaw - pawnYaw)
        local rad   = math.rad(delta)
        local cos_d = math.cos(rad)
        local sin_d = math.sin(rad)

        -- Rotate the input vector by the HMD-to-pawn delta
        local newFwd =  fwd * cos_d - rgt * sin_d
        local newRgt =  fwd * sin_d + rgt * cos_d

        _controller.InputForward = newFwd
        _controller.InputRight   = newRgt
    end)

    if not ok then
        _controller = nil  -- stale; re-acquire next tick
    end
end

-- ─────────────────────────────────────────────────────────────────────────────
-- 6. Special mode detection
--    Three independent detectors; any one being true enters special mode.
--    a) CineCameraActor ViewTarget  → cutscene
--    b) Ghost pawn (Mesh == nil)    → shimmy/traversal proxy
--    c) Fog-monster chase           → camera yaw ≈ ±170-180° AND FOV ≈ 55°
-- ─────────────────────────────────────────────────────────────────────────────
local function _checkIsCutscene()
    if _inAttackState then return false end  -- attack animations cannot trigger cutscene mode
    local cm = uevrUtils.getValid(_cameraManager)
    if cm == nil then return false end
    local ok, result = pcall(function()
        local target = cm.ViewTarget and cm.ViewTarget.Target
        if target == nil then return false end
        local fullName = target:get_full_name()
        return fullName ~= nil and fullName:find("CineCameraActor") ~= nil
    end)
    return ok and result == true
end

-- During shimmy/gap traversal UEVR swaps in a ghost proxy pawn that has no
-- Mesh component. The real pawn's AnimInstance is still cached and still gets
-- _suppressRootMotionRotation() applied, blocking the root-motion translation
-- that drives the character through the gap. Detect this via Mesh == nil.
local function _checkIsGhostPawn()
    local p = uevrUtils.getValid(pawn)
    if p == nil then return false end
    local ok, mesh = pcall(function() return p.Mesh end)
    return ok and mesh == nil
end

-- Fog chase: ViewTarget.POV.FOV drops from 65 (normal) to 55 (chase).
-- Threshold at 60 gives clear separation with no false positives.
local _FOG_CHASE_FOV_THRESHOLD = 60.0

local function _checkIsFogChase()
    if _inAttackState then return false end  -- attack FOV compression ≠ fog chase
    local cm = uevrUtils.getValid(_cameraManager)
    if cm == nil then return false end
    local ok, result = pcall(function()
        local fov = cm.ViewTarget.POV.FOV
        return fov ~= nil and fov < _FOG_CHASE_FOV_THRESHOLD
    end)
    return ok and result == true
end

-- ─────────────────────────────────────────────────────────────────────────────
-- Examine puzzle: plain CameraActor ViewTarget (not CineCameraActor)
--   The bookcase / examine-plane puzzles switch ViewTarget to a regular
--   CameraActor attached to BP_Examine_Plane_C.  UEVR's UObjectHook keeps the
--   camera locked to Hina's head if not detected; we enter special mode so the
--   hook is released and the game's puzzle camera drives the VR view.
--   We also expand the CameraMoveBox bounds 5× so the limited flat-screen pan
--   range is large enough for the player to see the full puzzle in VR.
-- ─────────────────────────────────────────────────────────────────────────────
local function _checkIsExamine()
    if _inAttackState then return false end
    local cm = uevrUtils.getValid(_cameraManager)
    if cm == nil then return false end
    local ok, result = pcall(function()
        local target = cm.ViewTarget and cm.ViewTarget.Target
        if target == nil then return false end
        local fn = target:get_full_name()
        -- Plain CameraActor (not Cine) is the examine puzzle; CineCameraActors
        -- are cutscenes handled by _checkIsCutscene.
        return fn ~= nil
            and fn:find("CameraActor") ~= nil
            and fn:find("CineCameraActor") == nil
    end)
    return ok and result == true
end

-- Walk the camera attachment chain to find the BP_Examine_Plane_C actor.
-- Chain: ViewTarget (CameraActor) → RootComponent → AttachParent (DefaultSceneRoot
-- on the examine plane) → GetOwner() → BP_Examine_Plane_C.
local function _getExamineActorFromCamera()
    local cm = uevrUtils.getValid(_cameraManager)
    if cm == nil then return nil end
    local ok, actor = pcall(function()
        local target = cm.ViewTarget and cm.ViewTarget.Target
        if target == nil then return nil end
        local root = target.RootComponent
        if root == nil then return nil end
        local attachParent = root.AttachParent
        if attachParent == nil then return nil end
        return attachParent:GetOwner()
    end)
    return (ok and actor ~= nil and actor ~= false) and actor or nil
end

local function _onExamineStart()
    local actor = _getExamineActorFromCamera()
    if actor == nil then return end
    _examineActor = actor
    pcall(function()
        local mn = actor.CameraBoundMin
        local mx = actor.CameraBoundMax
        if mn == nil or mx == nil then return end
        local s  = _EXAMINE_BOUND_SCALE
        -- Read min/max Y and Z (bridge may return uppercase or lowercase keys)
        local mn_y = mn.Y or mn.y or -30
        local mn_z = mn.Z or mn.z or -50
        local mx_y = mx.Y or mx.y or  30
        local mx_z = mx.Z or mx.z or  50
        _examineBoundsOrig = { min_y=mn_y, min_z=mn_z, max_y=mx_y, max_z=mx_z }
        actor.CameraBoundMin = { X=0, Y=mn_y*s, Z=mn_z*s }
        actor.CameraBoundMax = { X=0, Y=mx_y*s, Z=mx_z*s }
        -- Expand the physical CameraMoveBox so the game's clamping matches
        local box = actor.CameraMoveBox
        if box ~= nil then
            pcall(function()
                box:SetBoxExtent({ X=15, Y=mx_y*s, Z=mx_z*s }, false)
            end)
        end
    end)
end

local function _onExamineEnd()
    if _examineActor ~= nil and _examineBoundsOrig ~= nil then
        pcall(function()
            local o = _examineBoundsOrig
            _examineActor.CameraBoundMin = { X=0, Y=o.min_y, Z=o.min_z }
            _examineActor.CameraBoundMax = { X=0, Y=o.max_y, Z=o.max_z }
            local box = _examineActor.CameraMoveBox
            if box ~= nil then
                pcall(function()
                    box:SetBoxExtent({ X=15, Y=o.max_y, Z=o.max_z }, false)
                end)
            end
        end)
    end
    _examineActor      = nil
    _examineBoundsOrig = nil
end

-- Acquire ATPCCameraComponent via the CameraRotate BP component (which exposes it as a UProperty).
-- p.NocePlayerCameraRotate.ATPCCamera works because the BP component is a reflected property.
local function _getATPCComp()
    local p = uevrUtils.getValid(pawn)
    if p == nil then return nil end
    local ok, comp = pcall(function()
        local rot = p.NocePlayerCameraRotate
        return rot ~= nil and rot.ATPCCamera or nil
    end)
    return ok and comp or nil
end

-- Track attack montages and suppress the ATPC camera during them.
-- The ATPC is a C++ component that computes a 3rd-person position during attacks;
-- disabling its tick freezes it at the current (1st-person head) position.
uevrUtils.registerMontageChangeCallback(function(montageObj, name, animInstance)
    if name ~= nil and name:find("_Atk_") ~= nil and
       (name:find("ComboL") ~= nil or name:find("ComboH") ~= nil or
        name:find("ChargedAttack") ~= nil) then
        _inAttackState = true
        if _atpcComp == nil then _atpcComp = _getATPCComp() end
        pcall(function()
            if _atpcComp ~= nil then
                _atpcComp:SetComponentTickEnabled(false)
            end
        end)
    else
        _inAttackState = false
        pcall(function()
            if _atpcComp ~= nil then
                _atpcComp:SetComponentTickEnabled(true)
            end
        end)
    end
end)


local function _setIKHandsVisible(visible)
    if _ikMeshList == nil then return end
    pcall(function()
        for _, mesh in ipairs(_ikMeshList) do
            local m = uevrUtils.getValid(mesh)
            if m ~= nil and m.SetVisibility ~= nil then
                m:SetVisibility(visible, true)
            end
        end
    end)
end

local _EXTRA_MESH_NAMES = {"Pawn.SkeletalMesh_Head", "Pawn.SkeletalMesh_Hair"}

local function _setPawnExtraMeshesVisible(visible)
    pcall(function()
        for _, descriptor in ipairs(_EXTRA_MESH_NAMES) do
            local m = uevrUtils.getObjectFromDescriptor(descriptor)
            if m ~= nil then
                if m.SetVisibility     ~= nil then m:SetVisibility(visible, false) end
                if m.SetHiddenInGame   ~= nil then m:SetHiddenInGame(not visible, false) end
                if m.SetRenderInMainPass ~= nil then m:call("SetRenderInMainPass", visible) end
            end
        end
    end)
end

-- ─────────────────────────────────────────────────────────────────────────────
-- UObjectHook camera detach/reattach
--   During cutscenes / fog chase / ghost pawn the game's own ATPC camera must
--   drive the VR view. Disabling UObjectHook releases the pawn attachment so
--   UEVR falls back to the game-computed camera position.
-- ─────────────────────────────────────────────────────────────────────────────
local function _setUObjectHookEnabled(enabled)
    pcall(function()
        UEVR_UObjectHook.set_disabled(not enabled)
    end)
end

local function _setPawnBodyMeshVisible(visible)
    pcall(function()
        local p = uevrUtils.getValid(pawn)
        if p == nil then return end
        -- Try the standard ACharacter Mesh property first; fall back to the
        -- component name 'CharacterMesh0' for BP_Pl_Hina_Claw_C which doesn't
        -- expose Mesh via Lua property reflection.
        local mesh = uevrUtils.getValid(p, {"Mesh"})
        if mesh == nil then
            mesh = uevrUtils.getObjectFromDescriptor("Pawn.CharacterMesh0")
        end
        if mesh == nil then return end
        -- Use all three methods — the game may use any of them to hide the mesh
        mesh:SetVisibility(visible, false)
        mesh:SetHiddenInGame(not visible, false)
        pcall(function() mesh:call("SetRenderInMainPass", visible) end)
    end)
end

local function _findLantern()
    if _lanternActor ~= nil and not UEVR_UObjectHook.exists(_lanternActor) then
        _lanternActor = nil
    end
    if _lanternActor == nil then
        if _lanternSearched then return nil end
        _lanternSearched = true
        local inst = uevr_lib.find_first_of(_LANTERN_CLASS, false)
        if inst == nil or inst == false then return nil end
        _lanternActor = inst
    end
    return _lanternActor
end

-- Force the lantern actor visible/hidden. Called during cutscene enter/re-enforce.
local function _setLanternVisible(visible)
    local actor = _findLantern()
    if actor == nil then return end
    pcall(function() actor:SetActorHiddenInGame(not visible) end)
end

local function _onSpecialModeStart()
    _setIKHandsVisible(false)
    _setPawnExtraMeshesVisible(true)
    _setPawnBodyMeshVisible(true)   -- show body mesh for cutscene
    _setLanternVisible(true)        -- lantern may be hidden by game cinematic logic
    _setUObjectHookEnabled(false)      -- release pawn camera attachment
    if _inExamine then _onExamineStart() end  -- expand camera bounds for examine puzzles
    pcall(function() vr:recenter_view() end)
end

local function _onSpecialModeEnd()
    _setIKHandsVisible(true)
    _setPawnExtraMeshesVisible(false)
    _setPawnBodyMeshVisible(false)  -- hide body mesh for first-person gameplay
    _onExamineEnd()                    -- restore examine camera bounds if applicable
    _setUObjectHookEnabled(true)       -- restore pawn camera attachment
    pcall(function() vr:recenter_view() end)
end

local _specialModeTimer = 0
local _SPECIAL_MODE_INTERVAL = 0.1  -- 10 Hz poll

local function _updateSpecialModeState(delta)
    _specialModeTimer = _specialModeTimer + delta
    if _specialModeTimer < _SPECIAL_MODE_INTERVAL then return end
    _specialModeTimer = 0

    _inCutscene    = _checkIsCutscene()
    _inInteraction = _safePawnCall("IsInteracting")
    _inGhostPawn   = _checkIsGhostPawn()
    _inExamine     = _checkIsExamine()
    _inFogChase    = _checkIsFogChase()
    -- Fog chase is handled separately (rotation mode override, not full disable)
    local nowSpecial = _inCutscene or _inInteraction or _inGhostPawn or _inExamine

    if nowSpecial ~= _inSpecialMode then
        _inSpecialMode = nowSpecial
        if _inSpecialMode then
            _onSpecialModeStart()
        else
            _onSpecialModeEnd()
        end
    else
        -- Re-enforce every interval; Lua is sole manager of head/hair meshes and IK rig
        _setPawnExtraMeshesVisible(_inSpecialMode or _inFogChase)
        if _inSpecialMode then
            _setLanternVisible(true)  -- re-enforce in case game cinematic logic re-hides it
            _setPawnBodyMeshVisible(true)  -- re-enforce for Claw pawn (game may re-hide CharacterMesh0)
        end
        if _inFogChase then
            _setIKHandsVisible(false)
        end
    end
end

-- ─────────────────────────────────────────────────────────────────────────────
-- 7. Pawn mesh visibility during cutscenes
--    The Pawn Dev Config hides Pawn.Mesh for all three slots (Body, Arms,
--    Arms Animation) during normal gameplay. The pawn module polls these
--    callbacks every 100 ms and applies the result. During a cutscene we
--    return false (not hidden) at priority 100 to override the config.
--    Outside cutscenes we return nil so the config default (hide) wins.
-- ─────────────────────────────────────────────────────────────────────────────
local function _cutsceneMeshHidden()
    if _inSpecialMode or _inFogChase then return false, 100 end
    return nil
end

pawn_module.registerIsPawnBodyHiddenCallback(_cutsceneMeshHidden)
pawn_module.registerIsPawnArmsHiddenCallback(_cutsceneMeshHidden)
pawn_module.registerIsPawnAnimationArmsHiddenCallback(_cutsceneMeshHidden)

-- ─────────────────────────────────────────────────────────────────────────────
-- 6b. Fog chase rotation mode
--     When detected, switch body orientation to Game (PawnRotationMode.NONE=1)
--     so UEVR stops fighting the game's ControlRotation. Player can still move;
--     the game's native rotation logic handles facing. Restored on chase end.
-- ─────────────────────────────────────────────────────────────────────────────
local _fogChaseModeActive = false

local function _updateFogChaseMode()
    if _inFogChase and not _fogChaseModeActive then
        input.setOverridePawnRotationMode(_PAWN_ROTATION_GAME)
        _setIKHandsVisible(false)
        _setPawnExtraMeshesVisible(true)
        _setUObjectHookEnabled(false)      -- release pawn camera attachment
        _fogChaseModeActive = true
    elseif not _inFogChase and _fogChaseModeActive then
        input.setOverridePawnRotationMode(nil)
        -- Only restore IK/mesh if we're NOT immediately entering a cutscene/special mode.
        if not _inSpecialMode then
            _setIKHandsVisible(true)
            _setPawnExtraMeshesVisible(false)
            _setUObjectHookEnabled(true)   -- restore pawn camera attachment
        end
        _fogChaseModeActive = false
    end
end

-- ─────────────────────────────────────────────────────────────────────────────
-- 6c. Costume change detection
--     Polls CharMeshType.DataTable address every 500ms. When it changes,
--     destroys and recreates the IK rig so the new costume mesh is picked up.
-- ─────────────────────────────────────────────────────────────────────────────
local function _checkCostumeChange(delta)
    _costumeCheckTimer = _costumeCheckTimer + delta
    if _costumeCheckTimer < _COSTUME_CHECK_INTERVAL then return end
    _costumeCheckTimer = 0

    local p = uevrUtils.getValid(pawn)
    if p == nil then return end

    local ok, addr = pcall(function()
        local dt = p.CharMeshType
        if dt == nil then return nil end
        local tbl = dt.DataTable
        if tbl == nil then return nil end
        return uevrUtils.getFullName(tbl)  -- stable UObject path e.g. "DataTable /Game/.../DT_CharMesh_Hina_White"
    end)
    if not ok or addr == nil then return end

    if _lastCharMeshDataTableAddr == nil then
        -- First read — just seed the baseline, don't trigger a reinit
        _lastCharMeshDataTableAddr = addr
        return
    end

    if addr ~= _lastCharMeshDataTableAddr then
        _lastCharMeshDataTableAddr = addr
        uevrUtils.print("[shf] Costume changed — rebuilding IK rig")
        ik.destroyAll()
        -- Brief delay lets the engine finish applying the new mesh before we recreate.
        -- tryAutoCreateArms in ik.lua polls every 1000ms so we trigger manually.
        setInterval(300, function()
            if not ik.exists() then
                ik.new({ animationsFile = "hands_parameters" })
            end
            return true -- one-shot: returning true cancels the interval
        end)
    end
end

-- =============================================================================
-- 6d. Unified IK profile management
--     Uses CurrentWeapon.WeaponName as the single source of truth every tick:
--       "Claw"  -> profile "claw"       / collision bone "hand_small_r"
--       "ClawG" -> profile "claw_large" / collision bone "index_01_large_r"
--       nil / any other weapon -> profile "default"
--     Whenever the desired profile differs from the last-built profile, the rig
--     is destroyed and rebuilt -- including across level boundaries.
-- =============================================================================
local _lastIKProfile    = nil   -- profile the rig was last built with (nil = unknown)
local _ikRebuildPending = false -- true while a destroy+rebuild is in-flight

local function _desiredIKProfile()
    local p = uevrUtils.getValid(pawn)
    if p == nil then return nil end
    local ok, wep = pcall(function() return p.CurrentWeapon end)
    if not ok or wep == nil then return "default" end
    local ok2, wn = pcall(function() return tostring(wep.WeaponName) end)
    if not ok2 or wn == nil or wn == "nil" then return "default" end
    if wn == "ClawG" then return "claw_large" end
    if wn == "Claw"  then return "claw" end
    return "default"
end

local function _clawBoneForProfile(profile)
    if profile == "claw_large" then return "index_01_large_r" end
    return "hand_small_r"
end

local function _checkIKProfile()
    if _ikRebuildPending then return end
    local profile = _desiredIKProfile()
    if profile == nil then return end
    if profile == _lastIKProfile then return end

    local prev = _lastIKProfile
    _lastIKProfile = profile

    pcall(function() melee.setClawBone(_clawBoneForProfile(profile)) end)

    if prev ~= nil then
        uevrUtils.print("[shf] IK profile: '" .. prev .. "' -> '" .. profile .. "' -- rebuilding rig")
    else
        uevrUtils.print("[shf] IK profile: initial '" .. profile .. "' -- rebuilding rig")
    end

    _ikRebuildPending = true
    _animInstance     = nil
    _armIkKilled      = false
    ik.destroyAll()
    ik.setActiveProfile(profile)
    setInterval(400, function()
        if not ik.exists() then
            ik.new({ animationsFile = "hands_parameters" })
        end
        _ikRebuildPending = false
        return true
    end)
end

-- ─────────────────────────────────────────────────────────────────────────────
-- Weapon capsule hide
--   The game re-shows the CapsuleComponent on CurrentWeapon every tick.
--   Force it hidden unconditionally (normal gameplay, cutscenes, shimmy, etc.)
-- ─────────────────────────────────────────────────────────────────────────────
local function _hideWeaponCapsule()
    local p = uevrUtils.getValid(pawn)
    if p == nil then
        _weaponCapsule  = nil
        _lastWeaponAddr = nil
        return
    end
    local ok, wep = pcall(function() return p.CurrentWeapon end)
    if not ok or wep == nil then
        _weaponCapsule  = nil
        _lastWeaponAddr = nil
        return
    end
    -- Re-acquire capsule only when weapon changes
    local wepAddr = tostring(wep)
    if wepAddr ~= _lastWeaponAddr then
        _lastWeaponAddr = wepAddr
        local okC, cap = pcall(function() return wep.Capsule end)
        _weaponCapsule = (okC and cap ~= nil and cap ~= false) and cap or nil
    end
    if _weaponCapsule ~= nil then
        pcall(function() _weaponCapsule:SetHiddenInGame(true, false) end)
    end
end

-- ─────────────────────────────────────────────────────────────────────────────
uevr.sdk.callbacks.on_pre_engine_tick(function(engine, delta)
    _checkIKProfile()               -- weapon name -> IK profile; handles all transitions
    _refreshRefs()
    _hideWeaponCapsule()            -- keep weapon CapsuleComponent invisible at all times
    _updateSpecialModeState(delta)  -- detect cutscene + interaction special modes
    _updateFogChaseMode()           -- switch body orientation during fog chase
    _checkCostumeChange(delta)      -- detect costume swap, rebuild IK rig

    -- In special mode (cutscene / interaction) let the game run fully unmodified.
    -- Root motion translation drives shimmy/gap movement; suppressing it breaks it.
    if _inSpecialMode then return end

    _suppressNativeArmIK()        -- failsafe: catches post-level-load ABP reinit
    _suppressRootMotionRotation() -- prevent root motion rotation fighting CMC

    if not _canAcceptVRInput() then return end

    _zeroCameraTurn()
    _remapMovement()
end)




-- Post-engine tick: game's sprint/locomotion state machine runs during the tick,
-- so we force bOrientRotationToMovement back to false AFTER it to catch any
-- same-frame writes the pre-tick couldn't prevent.
-- We also zero fYawDiff here — this is the AnimBP's turn-in-place accumulator.
-- When it exceeds ~90° the ABP fires a turn-in-place animation whose root
-- motion conflicts with our K2_SetWorldRotation body yaw writes, causing the
-- capsule to snap. Since we own body rotation, the native turn-in-place system
-- should never run.
local function _suppressTurnInPlace()
    if _animInstance == nil then return end
    pcall(function()
        if _animInstance.fYawDiff ~= nil and _animInstance.fYawDiff ~= 0.0 then
            _animInstance.fYawDiff = 0.0
        end
    end)
end

uevr.sdk.callbacks.on_post_engine_tick(function(engine, delta)
    if not _inSpecialMode then
        _suppressTurnInPlace()  -- prevent AnimBP turn-in-place root motion conflicts
    end
end)

-- ─────────────────────────────────────────────────────────────────────────────
-- Level change / script reset cleanup
-- ─────────────────────────────────────────────────────────────────────────────
uevrUtils.registerPreLevelChangeCallback(function()
    _controller    = nil
    _animInstance  = nil
    _armIkKilled   = false
    _cameraManager = nil
    _inCutscene    = false
    _inInteraction = false
    _inGhostPawn   = false
    _inFogChase    = false
    _inExamine     = false
    _examineActor      = nil
    _examineBoundsOrig = nil
    _inSpecialMode = false
    _lastWeaponAddr    = nil
    _weaponCapsule     = nil
    _lastIKProfile         = nil
    _ikRebuildPending      = false
    _lastCharMeshDataTableAddr = nil
    _costumeCheckTimer         = 0
    _lanternActor    = nil
    _lanternSearched = false
    uevr_lib.clearClassCache(_LANTERN_CLASS)
    if _fogChaseModeActive then
        input.setOverridePawnRotationMode(nil)
        _fogChaseModeActive = false
    end
    -- Always re-enable UObjectHook on level change so camera attachment is restored
    pcall(function() UEVR_UObjectHook.set_disabled(false) end)
end)

uevr.params.sdk.callbacks.on_script_reset(function()
    _controller   = nil
    _animInstance = nil
    _armIkKilled  = false
    _hmdYaw       = 0.0
    _cameraManager  = nil
    _inCutscene     = false
    _inInteraction  = false
    _inGhostPawn    = false
    _inFogChase     = false
    _inExamine      = false
    _examineActor       = nil
    _examineBoundsOrig  = nil
    _inSpecialMode  = false
    _fogChaseModeActive        = false
    _lastWeaponAddr            = nil
    _weaponCapsule             = nil
    _lastIKProfile             = nil
    _ikRebuildPending          = false
    _lastCharMeshDataTableAddr = nil
    _costumeCheckTimer         = 0
    _ikMeshList          = nil
    _lanternActor    = nil
    _lanternSearched = false
    uevr_lib.clearClassCache(_LANTERN_CLASS)
    -- Always re-enable UObjectHook on reset so camera attachment is restored
    pcall(function() UEVR_UObjectHook.set_disabled(false) end)
    -- Don't call setOverridePawnRotationMode on reset — input module is also resetting
end)

return M
