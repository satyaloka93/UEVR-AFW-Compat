--[[ 
Usage
	Drop the lib folder containing this file into your project folder
	At the top of your script file add 
		local controllers = require("libs/controllers")
		
	In your code call function like this
		controllers.destroyControllers()
		
	In all of the functions, controllerID=0 is the left controller, controllerID=1 is the right controller and controllerID=2 is the hmd controller
	
	Available functions:
	
	controllers.onLevelChange() - call this function when there is a level change to clean up any allocated resources
		
	controllers.createController(controllerID) - creates the left controller (controllerID=0), right controller (controllerID=1) or hmd controller (controllerID=2)		
		example:
			function on_level_change(level)
				print("Level changed\n")
				controllers.onLevelChange()
				controllers.createController(0)
				controllers.createController(1)
				controllers.createController(2) 
			end
			
	controllers.createHMDController() - same as calling controllers.createController(2)
	
	controllers.getController(controllerID) - returns the component associated with the controllerID. 
		For controllerIDs 0 and 1 this is a "Class /Script/HeadMountedDisplay.MotionControllerComponent" class. 
		For controllerID 2 this is a "Class /Script/Engine.SceneComponent" class
		example:
			local hmdComponent = controllers.getController(2)
			
	controllers.getHMDController()  - same as calling controllers.getController(2)
	
	controllers.controllerExists(controllerID) - returns true if the given controllerID is already created. 
		Same as calling controllers.getController(controllerID) ~= nil
		example:
			local hmdExists = controllers.controllerExists(2)
			
	controllers.hmdControllerExists() - same as calling controllers.controllerExists(2)
	
	controllers.destroyController(controllerID) - deallocate the resources associated the given controllerID
		example:
			controllers.destroyController(2)
	
	controllers.destroyControllers() - deallocate the resources associated with all controllers
		example:
			controllers.destroyControllers()
			
	controllers.attachComponentToController(controllerID, childComponent, (optional)socketName, (optional)attachType, (optional)weld) - attach an 
		element derived from a component class to the given controller.
		Returns true if successful
		example:
			local weapon = pawn:GetCurrentWeapon()
			if weapon ~= nil  then
				local meshComponent = weapon.SkeletalMeshComponent
				if meshComponent ~= nil then
					meshComponent:DetachFromParent(false,false)
					meshComponent:SetVisibility(true, true)
					meshComponent:SetHiddenInGame(false, true)
					weaponConnected = controllers.attachComponentToController(1, meshComponent)
					uevrUtils.set_component_relative_transform(meshComponent, {X=0,Y=0,Z=0}, {Pitch=0,Yaw=0,Roll=0})
				end
			end

	controllers.getControllerLocation(controllerID) - gets the current position FVector in world space of the given controller or nil if none found
		example:
			local rightLocation = controllers.getControllerLocation(1)
			print("X is", rightLocation.X)

	controllers.getControllerRotation(controllerID) - gets the current rotation FRotator in world space of the given controller or nil if none found
		example:
			local rightRotation = controllers.getControllerRotation(1)
			print("Yaw is", rightRotation.Yaw)

	controllers.getControllerDirection(controllerID) - gets the current forward vector FVector of the given controller or nil if none found
		example:
			local hmdDirection = controllers.getControllerDirection(2)
			print("Forward Vector is", hmdDirection.X, hmdDirection.Y, hmdDirection.Z)

	controllers.getControllerUpVector(controllerID) - gets the current up vector FVector of the given controller or nil if none found
		example:
			local rightUpVector = controllers.getControllerUpVector(1)
			print("Up Vector is", rightUpVector.X, rightUpVector.Y, rightUpVector.Z)

	controllers.getControllerRightVector(controllerID) - gets the current right vector FVector of the given controller or nil if none found
		example:
			local leftRightVector = controllers.getControllerRightVector(0)
			print("Right Vector is", leftRightVector.X, leftRightVector.Y, leftRightVector.Z)

	controllers.getControllerTargetLocation(handed, collisionChannel, ignoreActors, traceComplex, minHitDistance) - performs line trace from controller and returns hit location
		example:
			local hitLocation = controllers.getControllerTargetLocation(0, 0, {}, false, 10)

	controllers.setLogLevel(val) - sets the logging level for controller debug output
		example:
			controllers.setLogLevel(LogLevel.Info)

]]--

local uevrUtils = require("libs/uevr_utils")

local M = {}

local sourceNames = {[0]="Left",[1]="Right"}
local actors = {}
local controllerComponents = {}
M.allowSyntheticControllers = false
M.usePoseAnchors = false

local poseAnchorComponents = {}
local poseAnchorParent = nil
local poseAnchorActor = nil
local poseAnchorLastError = {[0] = "init", [1] = "init"}
local WORLD_TO_METERS = 100.0

local function getField(obj, a, b)
	if obj == nil then return nil end
	local v = nil
	pcall(function() v = obj[a] end)
	if v == nil and b ~= nil then
		pcall(function() v = obj[b] end)
	end
	return v
end

local currentLogLevel = LogLevel.Error
function M.setLogLevel(val)
	currentLogLevel = val
end
function M.print(text, logLevel)
	if logLevel == nil then logLevel = LogLevel.Debug end
	if logLevel <= currentLogLevel then
		uevrUtils.print("[controllers] " .. text, logLevel)
	end
end

local function hardLog(text)
	pcall(function()
		uevr.params.functions.log_info("controllers.lua[pose]: " .. tostring(text))
	end)
end

local function isObjectUsable(obj)
	if obj == nil then return false end
	local ok = pcall(function() return obj:get_full_name() end)
	return ok
end

local function isTemplateControllerObject(obj)
	if obj == nil then return true end
	local fullName = ""
	pcall(function() fullName = obj:get_full_name() or "" end)
	if string.sub(fullName, 1, 9) == "Function " then return true end
	if string.find(fullName, "Default__MotionControllerComponent", 1, true) ~= nil then return true end
	if string.sub(fullName, 1, 34) == "MotionControllerComponent /Script/" then return true end
	return false
end

local mcClass = nil
local mcScanCache = {}
local lastMcScanT = 0.0
local MC_SCAN_INTERVAL_S = 0.25
local mcScanCursor = 0
local MC_SCAN_BUDGET = 2048

local function getMotionControllerClass()
	if mcClass == nil then
		mcClass = uevr.api:find_uobject("Class /Script/HeadMountedDisplay.MotionControllerComponent")
	end
	return mcClass
end

local function scanMotionControllerComponents(force)
	local nowT = os.clock()
	if force ~= true and (nowT - lastMcScanT) < MC_SCAN_INTERVAL_S then
		return mcScanCache
	end

	lastMcScanT = nowT
	mcScanCache = {}

	local cls = getMotionControllerClass()
	if cls ~= nil then
		local ok_list, list = pcall(function() return cls:get_objects_matching(false) end)
		if ok_list and list ~= nil and #list > 0 then
			for _, obj in pairs(list) do
				if isObjectUsable(obj) and not isTemplateControllerObject(obj) then
					mcScanCache[#mcScanCache + 1] = obj
				end
			end
			return mcScanCache
		end
	end

	-- fallback: incremental FUObjectArray scan (budgeted) to avoid full-array stalls
	local ok_arr, arr = pcall(function() return uevr.types.FUObjectArray.get() end)
	if not ok_arr or arr == nil then
		return mcScanCache
	end

	local ok_count, count = pcall(function() return arr:get_object_count() end)
	if not ok_count or count == nil or count <= 0 then
		return mcScanCache
	end

	if mcScanCursor >= count then
		mcScanCursor = 0
	end

	local budget = MC_SCAN_BUDGET
	if force == true then budget = budget * 2 end

	for _ = 1, budget do
		local idx = mcScanCursor
		mcScanCursor = mcScanCursor + 1
		if mcScanCursor >= count then
			mcScanCursor = 0
		end

		local ok_obj, obj = pcall(function() return arr:get_object(idx) end)
		if ok_obj and obj ~= nil and isObjectUsable(obj) and not isTemplateControllerObject(obj) then
			local isMatch = false
			if cls ~= nil then
				pcall(function() isMatch = obj:is_a(cls) end)
			end
			if not isMatch then
				local fullName = ""
				pcall(function() fullName = obj:get_full_name() or "" end)
				local isComponentInstance = string.sub(fullName, 1, 26) == "MotionControllerComponent "
				if isComponentInstance then
					isMatch = true
				end
			end
			if isMatch then
				mcScanCache[#mcScanCache + 1] = obj
				if #mcScanCache >= 32 then
					break
				end
			end
		end
	end

	return mcScanCache
end

local function getPoseAnchorParent()
	if poseAnchorParent ~= nil and isObjectUsable(poseAnchorParent) then
		return poseAnchorParent
	end

	local parent = nil
	pcall(function()
		local pc = uevr.api:get_player_controller(0)
		if pc ~= nil and pc.PlayerCameraManager ~= nil and pc.PlayerCameraManager.ViewTarget ~= nil and pc.PlayerCameraManager.ViewTarget.Target ~= nil then
			local target = pc.PlayerCameraManager.ViewTarget.Target
			if isObjectUsable(target) then
				parent = target
			end
		end
	end)

	if parent == nil then
		pcall(function()
			local pawn = uevr.api:get_local_pawn(0)
			if pawn == nil then pawn = uevr.api:get_local_pawn() end
			if pawn ~= nil and isObjectUsable(pawn) then
				parent = pawn
			end
		end)
	end

	if parent == nil then
		pcall(function()
			if poseAnchorActor == nil or not isObjectUsable(poseAnchorActor) then
				poseAnchorActor = uevrUtils.spawn_actor(uevrUtils.get_transform(), 1, nil)
			end
			if poseAnchorActor ~= nil and isObjectUsable(poseAnchorActor) then
				parent = poseAnchorActor
			end
		end)
	end

	poseAnchorParent = parent
	return poseAnchorParent
end

local function ensurePoseAnchorComponent(hand)
	local c = poseAnchorComponents[hand]
	if c ~= nil and isObjectUsable(c) then
		return c
	end

	local parent = getPoseAnchorParent()
	if parent == nil then
		poseAnchorLastError[hand] = "parent=nil"
		hardLog("hand=" .. tostring(hand) .. " parent=nil")
		M.print("Pose anchor parent is nil for hand " .. tostring(hand), LogLevel.Warning)
		return nil
	end

	local created = nil
	pcall(function()
		created = uevrUtils.create_component_of_class("Class /Script/Engine.SceneComponent", false, uevrUtils.get_transform(), false, parent)
	end)

	if created ~= nil and isObjectUsable(created) then
		poseAnchorComponents[hand] = created
		poseAnchorLastError[hand] = "ok"
		hardLog("hand=" .. tostring(hand) .. " created anchor")
		M.print("Created pose anchor component for hand " .. tostring(hand), LogLevel.Info)
		return created
	end

	poseAnchorLastError[hand] = "create_component_failed"
	hardLog("hand=" .. tostring(hand) .. " create_component_failed")
	M.print("Failed to create pose anchor component for hand " .. tostring(hand), LogLevel.Warning)
	return nil
end

local function updatePoseAnchor(hand)
	if M.usePoseAnchors ~= true then return nil end
	if hand ~= 0 and hand ~= 1 then return nil end

	local anchor = ensurePoseAnchorComponent(hand)
	if anchor == nil then return nil end

	local controllerIndex = uevrUtils.getControllerIndex(hand)
	local hmdIndex = uevrUtils.getControllerIndex(2)
	if controllerIndex == nil or hmdIndex == nil or controllerIndex < 0 or hmdIndex < 0 then
		poseAnchorLastError[hand] = "index_missing"
		hardLog("hand=" .. tostring(hand) .. " index_missing c=" .. tostring(controllerIndex) .. " h=" .. tostring(hmdIndex))
		return anchor
	end

	local controllerPos = UEVR_Vector3f.new()
	local controllerRotQ = UEVR_Quaternionf.new()
	local hmdPos = UEVR_Vector3f.new()
	local hmdRotQ = UEVR_Quaternionf.new()

	local controllerPoseOk = false
	pcall(function()
		if uevr.params.vr.get_grip_pose ~= nil then
			uevr.params.vr.get_grip_pose(controllerIndex, controllerPos, controllerRotQ)
		else
			uevr.params.vr.get_pose(controllerIndex, controllerPos, controllerRotQ)
		end
		controllerPoseOk = true
	end)
	local hmdPoseOk = false
	pcall(function()
		uevr.params.vr.get_pose(hmdIndex, hmdPos, hmdRotQ)
		hmdPoseOk = true
	end)
	if not controllerPoseOk or not hmdPoseOk then
		poseAnchorLastError[hand] = "get_pose_failed"
		hardLog("hand=" .. tostring(hand) .. " get_pose_failed c=" .. tostring(controllerPoseOk) .. " h=" .. tostring(hmdPoseOk))
		return anchor
	end

	local cpx = getField(controllerPos, "x", "X")
	local cpy = getField(controllerPos, "y", "Y")
	local cpz = getField(controllerPos, "z", "Z")
	local hpx = getField(hmdPos, "x", "X")
	local hpy = getField(hmdPos, "y", "Y")
	local hpz = getField(hmdPos, "z", "Z")
	if cpx == nil or cpy == nil or cpz == nil or hpx == nil or hpy == nil or hpz == nil then
		poseAnchorLastError[hand] = "pose_position_missing"
		hardLog("hand=" .. tostring(hand) .. " pose_position_missing")
		return anchor
	end

	local dx = cpx - hpx
	local dy = cpy - hpy
	local dz = cpz - hpz

	local relPos = {X = -dz * WORLD_TO_METERS, Y = dx * WORLD_TO_METERS, Z = dy * WORLD_TO_METERS}

	local crx = getField(controllerRotQ, "x", "X")
	local cry = getField(controllerRotQ, "y", "Y")
	local crz = getField(controllerRotQ, "z", "Z")
	local crw = getField(controllerRotQ, "w", "W")
	local hrx = getField(hmdRotQ, "x", "X")
	local hry = getField(hmdRotQ, "y", "Y")
	local hrz = getField(hmdRotQ, "z", "Z")
	local hrw = getField(hmdRotQ, "w", "W")
	if crx == nil or cry == nil or crz == nil or crw == nil or hrx == nil or hry == nil or hrz == nil or hrw == nil then
		poseAnchorLastError[hand] = "pose_rotation_missing"
		hardLog("hand=" .. tostring(hand) .. " pose_rotation_missing")
		return anchor
	end

	local controllerRot = uevrUtils.rotatorFromQuat(crz, crx, -cry, -crw)
	local hmdRot = uevrUtils.rotatorFromQuat(hrz, hrx, -hry, -hrw)
	local relRot = uevrUtils.sumRotators(controllerRot, {Pitch=-hmdRot.Pitch, Yaw=-hmdRot.Yaw, Roll=-hmdRot.Roll})

	uevrUtils.set_component_relative_transform(anchor, relPos, relRot)
	poseAnchorLastError[hand] = "updated"
	return anchor
end

local function updatePoseAnchors()
	if M.usePoseAnchors ~= true then return end
	if (poseAnchorComponents[0] ~= nil and isObjectUsable(poseAnchorComponents[0])) or (poseAnchorComponents[1] ~= nil and isObjectUsable(poseAnchorComponents[1])) then
		local li = uevrUtils.getControllerIndex(0)
		local ri = uevrUtils.getControllerIndex(1)
		if li ~= nil and ri ~= nil and li >= 0 and ri >= 0 and li == ri then
			hardLog("left/right controller index conflict li=ri=" .. tostring(li))
		end
		pcall(function() updatePoseAnchor(0) end)
		pcall(function() updatePoseAnchor(1) end)
	end
end

local function getControllerAddress(component)
	if component == nil then return nil end
	local addr = nil
	pcall(function() addr = component:get_address() end)
	return addr
end

local function isClaimedByOtherHand(component, controllerID)
	if controllerID ~= 0 and controllerID ~= 1 then return false end
	local otherID = controllerID == 0 and 1 or 0
	local other = controllerComponents[otherID]
	if other == nil or not isObjectUsable(other) then
		return false
	end
	local a = getControllerAddress(component)
	local b = getControllerAddress(other)
	return a ~= nil and b ~= nil and a == b
end

local function isControllerComponentMatch(component, controllerID)
	if component == nil or not isObjectUsable(component) then
		return false
	end
	if isTemplateControllerObject(component) then
		return false
	end

	if controllerID == 2 then
		return true
	end

	local src = ""
	pcall(function()
		if component.MotionSource ~= nil and component.MotionSource.to_string ~= nil then
			src = string.lower(component.MotionSource:to_string() or "")
		end
	end)

	local srcHasLeft = string.find(src, "left", 1, true) ~= nil
	local srcHasRight = string.find(src, "right", 1, true) ~= nil
	local sourceSideKnown = srcHasLeft or srcHasRight

	local sourceMatch = false
	if sourceSideKnown then
		if controllerID == 0 then
			sourceMatch = srcHasLeft and not srcHasRight
		else
			sourceMatch = srcHasRight and not srcHasLeft
		end
	end

	local handMatch = false
	if not sourceSideKnown then
		pcall(function()
			handMatch = (component.Hand ~= nil and component.Hand == controllerID)
		end)
	end

	if (sourceMatch or handMatch) and not isClaimedByOtherHand(component, controllerID) then
		return true
	end

	return false
end

local function getCachedController(controllerID)
	local cached = controllerComponents[controllerID]
	if cached ~= nil and isControllerComponentMatch(cached, controllerID) then
		return cached
	end

	local actor = actors[controllerID]
	if actor ~= nil then
		local components = actor.BlueprintCreatedComponents
		if components ~= nil then
			for index, component in pairs(components) do
				if isControllerComponentMatch(component, controllerID) then
					controllerComponents[controllerID] = component
					return component	
				end
			end
		end
	end
	return nil
end 


local function destroyActor(actor)
	if actor ~= nil then
		pcall(function()
			local components = actor.BlueprintCreatedComponents
			for index, component in pairs(components) do
				if component ~= nil then
					M.print("Destroying controller component " .. component:get_full_name()) 
					pcall(function()
						if actor.K2_DestroyComponent ~= nil then
							actor:K2_DestroyComponent(component)
							M.print("HMD Controller component destroyed")
						end
					end)	
				end
			end
			if actor.K2_DestroyActor ~= nil then
				actor:K2_DestroyActor()
				M.print("HMD Controller actor destroyed")
			end
		end)	
	end
end

local function createControllerComponent(parentActor, sourceName, handIndex)	
	local sourceNameStr = sourceName or "Unknown"
	local handIndexStr = handIndex ~= nil and tostring(handIndex) or "Unknown"
	M.print("Creating controller " .. sourceNameStr .. " " .. handIndexStr) -- thanks to Lukasblaster
	if parentActor ~= nil then
		local motionControllerComponent = uevrUtils.create_component_of_class("Class /Script/HeadMountedDisplay.MotionControllerComponent", true, uevrUtils.get_transform(), false, parentActor)
		--local motionControllerComponent = parentActor:AddComponentByClass(uevrUtils.get_class("Class /Script/HeadMountedDisplay.MotionControllerComponent"), true, uevrUtils.get_transform(), false)
		if motionControllerComponent ~= nil then
			motionControllerComponent:SetCollisionEnabled(0, false)	
			motionControllerComponent.MotionSource = uevrUtils.fname_from_string(sourceName)
			if motionControllerComponent.Hand ~= nil then
				motionControllerComponent.Hand = handIndex
			end

			-- Drive synthetic controller components from VR hand poses natively.
			if UEVR_UObjectHook.get_or_add_motion_controller_state ~= nil then
				local state = UEVR_UObjectHook.get_or_add_motion_controller_state(motionControllerComponent)
				if state ~= nil then
					state:set_hand(handIndex)
					state:set_permanent(true)
				end
			end

			controllerComponents[handIndex] = motionControllerComponent
			M.print("Controller created")
			return motionControllerComponent
		end
	else
		M.print("Couldn't create controller because parentActor was nil")
	end
	return nil
end

local function createHMDControllerComponent()	
	M.print("Creating HMD controller")
	local hmdIndex = 2
	local parentActor = uevrUtils.spawn_actor(uevrUtils.get_transform(), 1, nil)
	if parentActor ~= nil then
		M.print("Created HMD controller actor " .. parentActor:get_full_name())
		local motionControllerComponent = uevrUtils.create_component_of_class("Class /Script/Engine.SceneComponent", true, uevrUtils.get_transform(), false, parentActor)
		--local motionControllerComponent = parentActor:AddComponentByClass(uevrUtils.get_class("Class /Script/Engine.SceneComponent"), true, uevrUtils.get_transform(), false)
		if motionControllerComponent ~= nil then
			local hmdState = UEVR_UObjectHook.get_or_add_motion_controller_state(motionControllerComponent)	
			if hmdState ~= nil then
				hmdState:set_hand(hmdIndex) 
				hmdState:set_permanent(true)
				actors[hmdIndex] = parentActor
				controllerComponents[hmdIndex] = motionControllerComponent
				M.print("Controller created")
				return motionControllerComponent
			else
				M.print("HMD Controller state creation failed", LogLevel.Warning)
			end	
		else
			M.print("HMD Controller component creation failed", LogLevel.Warning)
		end
	else
		M.print("HMD Controller actor creation failed", LogLevel.Warning)
	end
	destroyActor(parentActor)
	return nil
end

local function createActor(controllerID)
	actors[controllerID] = uevrUtils.spawn_actor(uevrUtils.get_transform(), 1, nil)
	return actors[controllerID]
end

local function resetMotionControllers()
	if M.allowSyntheticControllers ~= true then
		return
	end
	M.print("Removing all motion controller states")
	if UEVR_UObjectHook.remove_all_motion_controller_states ~= nil then
		UEVR_UObjectHook.remove_all_motion_controller_states()
	end
end

function M.onLevelChange()
	resetMotionControllers()
    M.resetControllers()
end

function M.getHMDController()
	return getCachedController(2)
end


function M.getController(controllerID, useCached)
	if useCached == nil then useCached = true end
	if useCached == true then
		local c = getCachedController(controllerID)
		if c ~= nil then
			return c
		end
		-- Fallback to discovery and cache it once found.
		c = M.getController(controllerID, false)
		if c ~= nil then
			controllerComponents[controllerID] = c
			if c.GetOwner ~= nil then
				pcall(function() actors[controllerID] = c:GetOwner() end)
			end
		end
		return c
	else
		if controllerID == 2 then
			return M.getHMDController()
		else
			M.print("Getting controller without cache")
			local controllers = scanMotionControllerComponents(false)
			if controllers ~= nil then
				for index, controller in pairs(controllers) do
					if isControllerComponentMatch(controller, controllerID) then
						return controller
					end
				end
			end
		end
	end

	return nil
end

function M.getAttachTarget(controllerID, useCached)
	-- Prefer pose anchors when enabled so we don't bind to stale/waist-level controller components.
	if M.usePoseAnchors == true and (controllerID == 0 or controllerID == 1) then
		local anchor = updatePoseAnchor(controllerID)
		if anchor ~= nil and poseAnchorLastError[controllerID] == "updated" then
			return anchor
		end
		-- In pose-anchor mode, fail closed and let caller fallback to UObjectHook attach.
		return nil
	end

	local c = M.getController(controllerID, useCached)
	if c ~= nil then return c end
	return nil
end

function M.getPoseAnchorDebugState(controllerID)
	if controllerID ~= 0 and controllerID ~= 1 then return "n/a" end
	local c = poseAnchorComponents[controllerID]
	local usable = (c ~= nil and isObjectUsable(c)) and "usable" or "nil"
	local err = poseAnchorLastError[controllerID] or "none"
	return "pose=" .. usable .. " err=" .. tostring(err)
end

function M.debugDumpControllerCandidates(controllerID, reason)
	local label = sourceNames[controllerID] or tostring(controllerID)
	local function out(msg)
		pcall(function()
			uevr.params.functions.log_info("controllers.lua[diag]: " .. tostring(msg))
		end)
	end

	out("=== Controller diagnostics begin hand=" .. tostring(label) .. " reason=" .. tostring(reason))

	local function dumpOne(prefix, c)
		if c == nil or not isObjectUsable(c) then
			out(prefix .. " = nil")
			return
		end

		local src = "nil"
		local hand = "nil"
		local addr = nil
		local name = "nil"
		local owner = "nil"
		pcall(function() addr = c:get_address() end)
		pcall(function() if c.MotionSource ~= nil and c.MotionSource.to_string ~= nil then src = c.MotionSource:to_string() end end)
		pcall(function() if c.Hand ~= nil then hand = tostring(c.Hand) end end)
		pcall(function() name = c:get_full_name() end)
		pcall(function() if c.GetOwner ~= nil and c:GetOwner() ~= nil then owner = c:GetOwner():get_full_name() end end)
		out(prefix .. " addr=" .. tostring(addr) .. " src=" .. tostring(src) .. " hand=" .. tostring(hand) .. " owner=" .. tostring(owner) .. " name=" .. tostring(name))
	end

	dumpOne("cached[L]", controllerComponents[0])
	dumpOne("cached[R]", controllerComponents[1])
	dumpOne("pose[L]", poseAnchorComponents[0])
	dumpOne("pose[R]", poseAnchorComponents[1])
	out("poseState[L] " .. tostring(M.getPoseAnchorDebugState(0)))
	out("poseState[R] " .. tostring(M.getPoseAnchorDebugState(1)))

	local all = scanMotionControllerComponents(true)
	if all == nil or #all == 0 then
		out("all motion controllers = nil")
		out("=== Controller diagnostics end")
		return
	end

	for i, c in pairs(all) do
		local leftMatch = isControllerComponentMatch(c, 0)
		local rightMatch = isControllerComponentMatch(c, 1)
		local src = "nil"
		local hand = "nil"
		local addr = nil
		local name = "nil"
		pcall(function() addr = c:get_address() end)
		pcall(function() if c.MotionSource ~= nil and c.MotionSource.to_string ~= nil then src = c.MotionSource:to_string() end end)
		pcall(function() if c.Hand ~= nil then hand = tostring(c.Hand) end end)
		pcall(function() name = c:get_full_name() end)
		out("cand[" .. tostring(i) .. "] addr=" .. tostring(addr) .. " src=" .. tostring(src) .. " hand=" .. tostring(hand) .. " leftMatch=" .. tostring(leftMatch) .. " rightMatch=" .. tostring(rightMatch) .. " name=" .. tostring(name))
	end

	out("=== Controller diagnostics end")
end

--called after a script restart
function M.restoreExistingComponents()
	for i = 0, 1 do
		if getCachedController(i) == nil then
			local controller = M.getController(i, false)
			if controller ~= nil then
				-- M.print isnt ready at this point so just use print
				print("Restoring existing controller " .. i .. ": " .. controller:get_full_name() .. " " .. controller:GetOwner():get_full_name())
				actors[i] = controller:GetOwner()
				controllerComponents[i] = controller
			end
		end
	end
end

function M.hmdControllerExists()
	return M.getHMDController() ~= nil
end

function M.controllerExists(controllerID, useCached)
	--return M.getController(controllerID, false) ~= nil

	if useCached == nil then useCached = true end
	local controller = M.getController(controllerID, useCached)
	-- if useCached == true and controller == nil then
		-- controller = M.getController(controllerID, false)
	-- end
	return controller ~= nil
end

function M.createHMDController()
	local controller = nil
	if not M.hmdControllerExists() then
		controller = createHMDControllerComponent()
	end
	return controller
end

local function createRightControllerComponent()	
	createControllerComponent(createActor(1), "Right", 1)
end
local function createLeftControllerComponent()	
	createControllerComponent(createActor(0), "Left", 0)
end

function M.createController(controllerID)
	M.print("Creating controller " ..  controllerID)
	if controllerID == 2 then
		return M.createHMDController()
	else
		local controller = M.getController(controllerID, true)
		if controller == nil then
			controller = M.getController(controllerID, false)
			if controller ~= nil then
				controllerComponents[controllerID] = controller
				if controller.GetOwner ~= nil then
					pcall(function() actors[controllerID] = controller:GetOwner() end)
				end
			elseif M.allowSyntheticControllers == true then
				controller = createControllerComponent(createActor(controllerID), sourceNames[controllerID], controllerID)
			end
		end
		return controller
	end
end

function M.destroyController(controllerID)
	destroyActor(actors[controllerID])
	actors[controllerID] = nil
	controllerComponents[controllerID] = nil
end

function M.destroyControllers()
	M.destroyController(0)
	M.destroyController(1)
	M.destroyController(2)
	M.resetControllers()
end

function M.resetControllers()
	actors[0] = nil
	actors[1] = nil
	actors[2] = nil
	actors = {}
	controllerComponents[0] = nil
	controllerComponents[1] = nil
	controllerComponents[2] = nil
	controllerComponents = {}
	poseAnchorComponents[0] = nil
	poseAnchorComponents[1] = nil
	poseAnchorComponents = {}
	poseAnchorLastError[0] = "reset"
	poseAnchorLastError[1] = "reset"
	poseAnchorParent = nil
	if poseAnchorActor ~= nil and isObjectUsable(poseAnchorActor) and poseAnchorActor.K2_DestroyActor ~= nil then
		pcall(function() poseAnchorActor:K2_DestroyActor() end)
	end
	poseAnchorActor = nil
end

--controllerID 0-left, 1-right, 2-head
function M.attachComponentToController(controllerID, childComponent, socketName, attachType, weld, createIfNotExists)
	if socketName == nil then socketName = "" end
	if attachType == nil then attachType = 0 end
	if weld == nil then weld = false end
	if childComponent ~= nil then
		M.print("Attaching component " .. childComponent:get_full_name() .. " to controller " .. controllerID)
		local controller = M.getAttachTarget(controllerID, true)
		if controller == nil and createIfNotExists == true then
			controller = M.createController(controllerID)
			controller = M.getAttachTarget(controllerID, true)
		end
		if controller == nil then
			controller = M.getAttachTarget(controllerID, false)
			if controller ~= nil and controller.GetOwner ~= nil then
				pcall(function() actors[controllerID] = controller:GetOwner() end)
			end
		end
		if controller ~= nil then
			pcall(function()
				local ms = "nil"
				if controller.MotionSource ~= nil and controller.MotionSource.to_string ~= nil then
					ms = controller.MotionSource:to_string()
				end
				local hand = controller.Hand ~= nil and tostring(controller.Hand) or "nil"
				M.print("Attaching to controller component: source=" .. ms .. " hand=" .. hand, LogLevel.Info)
			end)
			return childComponent:K2_AttachTo(controller, uevrUtils.fname_from_string(socketName), attachType, weld)
		else
			M.print("Could not attach component to controller " .. controllerID .. " because controller is nil")
		end
	else
		M.print("Could not attach component to controller " .. controllerID .. "  because childComponent is nil")
	end
	return false
end

-- returns an FVector or nil
function M.getControllerLocation(controllerID)
	local controller = M.getController(controllerID, true)
	if controller ~= nil then
		return controller:K2_GetComponentLocation()
	-- else
		-- --try getting the pose directly
		-- local index = uevrUtils.getControllerIndex(controllerID)
		-- if index ~= nil then
			-- uevr.params.vr.get_pose(index, temp_vec3f, temp_quatf)
			-- return uevrUtils.vector(temp_vec3f.X,temp_vec3f.Y,temp_vec3f.Z)
		-- end	
	end
	return nil
end

function M.getControllerRotation(controllerID)
	local controller = M.getController(controllerID, true)
	if controller ~= nil then
		return controller:K2_GetComponentRotation()
	-- else
		-- --try getting the pose directly
		-- local index = uevrUtils.getControllerIndex(controllerID)
		-- if index ~= nil then
			-- uevr.params.vr.get_pose(index, temp_vec3f, temp_quatf)
			-- local poseQuat = uevrUtils.quat(temp_quatf.Z, temp_quatf.X, -temp_quatf.Y, -temp_quatf.W)  --reordered terms to convert UEVR to unreal coord system
			-- local poseRotator = kismet_math_library:Quat_Rotator(poseQuat)
			-- return poseRotator
		-- end	
	end
	return nil
end

function M.getControllerDirection(controllerID)
	local controller = M.getController(controllerID, true)
	if controller ~= nil then
		return kismet_math_library:GetForwardVector(M.getControllerRotation(controllerID))
	end
	return nil
end

function M.getControllerUpVector(controllerID)
	local controller = M.getController(controllerID, true)
	if controller ~= nil then
		return kismet_math_library:GetUpVector(M.getControllerRotation(controllerID))
	end
	return nil
end

function M.getControllerRightVector(controllerID)
	local controller = M.getController(controllerID, true)
	if controller ~= nil then
		return kismet_math_library:GetRightVector(M.getControllerRotation(controllerID))
	end
	return nil
end

function M.getControllerTargetLocation(handed, collisionChannel, ignoreActors, traceComplex, minHitDistance)
	if not M.controllerExists(handed) then
		M.createController(handed)
	end
	local direction = M.getControllerDirection(handed)
	if direction ~= nil then
		local startLocation = M.getControllerLocation(handed)
		if startLocation ~= nil then
			return uevrUtils.getTargetLocation(startLocation, direction, collisionChannel, ignoreActors, traceComplex, minHitDistance)
		else
			M.print("Error in getControllerTargetLocation. Controller location was nil")
		end
	else
		M.print("Error in getControllerTargetLocation. Controller direction was nil")
	end
	return nil
end

local isRestored = false
uevrUtils.registerPreLevelChangeCallback(function(level)
	M.print("Pre-Level changed in controllers")
	M.onLevelChange()
	if not isRestored then
		M.restoreExistingComponents()
		isRestored = true
	end
end)

uevrUtils.registerLevelChangeCallback(function(level)
	M.restoreExistingComponents()
	if M.allowSyntheticControllers == true then
		M.createController(0)
		M.createController(1)
		M.createController(2)
	end
end)

uevr.sdk.callbacks.on_pre_engine_tick(function(engine, delta)
	updatePoseAnchors()
end)

return M






-- function createHMDComponent()
	-- pawn = uevr.api:get_local_pawn(0)
	-- if hmdActor ~= nil then 
		-- destroyHMDComponent()
	-- end
	-- if pawn ~= nil then
		-- print("Create HMD component called\n")
		-- local pos = pawn:K2_GetActorLocation()
		-- if hmdActor == nil then
			-- hmdActor = uevrUtils.spawn_actor( uevrUtils.get_transform({X=pos.X, Y=pos.Y, Z=pos.Z}), 1, nil)
		-- end
		-- if hmdActor == nil then
			-- print("Failed to spawn HMD actor\n")
		-- else
			-- temp_transform.Translation = pos
			-- temp_transform.Rotation.W = 1.0
			-- temp_transform.Scale3D = Vector3f.new(1.0, 1.0, 1.0)
			-- if hmdComponent == nil then
				-- local scene_component_c = uevrUtils.find_required_object("Class /Script/Engine.SceneComponent")
				-- hmdComponent = hmdActor:AddComponentByClass(scene_component_c, true, temp_transform, false)
				-- --hmdComponent = uevr.api:add_component_by_class(hmdActor, scene_component_c)
				-- --scene_component_c = uevr.api:add_component_by_class(hmdActor, scene_component_c)	
			-- end
			-- if hmdComponent == nil then
				-- print("Failed to add HMD component\n")
			-- else
				-- hmdState = UEVR_UObjectHook.get_or_add_motion_controller_state(hmdComponent)	
				-- if hmdState ~= nil then
					-- hmdState:set_hand(2) -- HMD
					-- hmdState:set_permanent(true)
				-- else 
					-- print("Failed to add hmdComponent to motion controller\n")
				-- end
				-- --hmdActor:FinishAddComponent(hmdComponent, false, temp_transform)
			-- end

		-- end
	-- end
	-- if hmdActor ~= nil and hmdComponent ~= nil and hmdState ~= nil then
		-- print("HMD Component created\n")
		-- return true
	-- else
		-- print("HMD Component creation failed\n")
	-- end
	-- return false
-- end

-- function destroyHMDComponent()
	-- print("Destroying HMD Component\n")
	-- if hmdComponent ~= nil and UEVR_UObjectHook.exists(hmdComponent) then
		-- print("Disconnecting component from VR HMD\n")
		-- pcall(function()
			-- UEVR_UObjectHook.remove_motion_controller_state(hmdComponent)
			-- hmdState = nil
			-- print("Motion Control state disconnected from HMD\n")	
		-- end)	
		-- print("Destroying hmdComponent\n")
		-- pcall(function()
			-- if hmdComponent.K2_DestroyComponent ~= nil then
				-- local scene_component_c = uevrUtils.find_required_object("Class /Script/Engine.SceneComponent")
				-- hmdComponent:K2_DestroyComponent(scene_component_c)
				-- hmdComponent = nil
				-- print("hmdComponent destroyed\n")
			-- else
				-- print("Could not destroy hmdComponent.\n")
			-- end
		-- end)	
	-- end
	-- print("Destroying hmdActor\n")
	-- if hmdActor ~= nil and UEVR_UObjectHook.exists(hmdActor) then
		-- pcall(function()
			-- if hmdActor.K2_DestroyActor ~= nil then
				-- hmdActor:K2_DestroyActor()
			-- end
			-- hmdActor = nil
		-- end)	
	-- end
	-- if hmdState == nil and hmdComponent == nil and hmdActor == nil then
		-- print("HMD component destroyed\n")
	-- else
		-- print("HMD component was not properly destroyed ",hmdState,hmdComponent,hmdActor,"\n")
	-- end
	-- hmdState = nil
	-- hmdComponent = nil
	-- hmdActor = nil
-- end






-- local hmdActor = nil
-- local leftActor = nil
-- local rightActor = nil
--local leftComponent = nil
-- local function destroyActorComponentsByClass(actor, compClass)
	-- print("Destroying controller components\n")
	-- if actor ~= nil and actor.K2_GetComponentsByClass ~= nil then
		-- local components = actor:K2_GetComponentsByClass(compClass)
		-- print("Components=",components,"\n")
		-- if components ~= nil then
			-- for index, component in pairs(components) do
				-- if component ~= nil then
					-- print("Destroying controller component",component:get_full_name(),"\n")
					-- pcall(function()
						-- if actor.K2_DestroyComponent ~= nil then
							-- actor:K2_DestroyComponent(component)
							-- print("Controller component destroyed\n")
						-- end
					-- end)	
				-- end
			-- end
		-- end 
	-- end
-- end

-- local function destroyActor(actor)
	-- print("Destroying controller actor",actor,"\n")
	-- if actor ~= nil and UEVR_UObjectHook.exists(actor) then
		-- destroyActorComponentsByClass(actor, motion_controller_component_c)
		-- --destroyActorComponentsByClass(actor, static_mesh_component_c)
		-- pcall(function()
			-- if actor.K2_DestroyActor ~= nil then
				-- actor:K2_DestroyActor()
			-- end
			-- actor = nil
			-- print("Controller actor destroyed",actor,"\n")
		-- end)	
		-- if actor ~= nil then print("Controller actor was not destroyed in an expected way\n") end
	-- end
	-- return nil
-- end


-- function M.attachComponentToController()
	-- --print(rightMotionControllerComponent)
	-- local pos = pawn:K2_GetActorLocation()				
	-- --if hmdComponent == nil then
		-- local static_mesh_component_c = uevr.api:find_uobject("Class /Script/Engine.StaticMeshComponent")
		-- local static_mesh = uevr.api:find_uobject("StaticMesh /Engine/EngineMeshes/Sphere.Sphere")

		-- hmdComponent = rightActor:AddComponentByClass(static_mesh_component_c, true, uevrUtils.get_transform(nil, nil, {X=0.1, Y=0.1, Z=0.1}), false)
		-- hmdComponent:SetCollisionEnabled(0)
		-- hmdComponent:SetStaticMesh(static_mesh)
		
		-- --local scene_component_c = find_required_object("Class /Script/Engine.SceneComponent")
		-- --hmdComponent = rightActor:AddComponentByClass(scene_component_c, true, temp_transform, false)
-- --debugModule.dumpObject(hmdComponent)
		
		-- -- hmdComponent = Statics:SpawnObject(static_mesh_component_c, rightMotionControllerComponent)
		-- -- hmdComponent:K2_SetWorldTransform(temp_transform, false, reusable_hit_result, false)
		-- -- hmdComponent:SetCollisionEnabled(0)
		-- -- hmdComponent:SetStaticMesh(static_mesh)
		-- -- hmdComponent.LDMaxDrawDistance=1500.0
		-- -- hmdComponent.CachedMaxDrawDistance=1500.0
		-- --hmdComponent.CreationMethod=2
		
		-- hmdComponent:K2_AttachTo(rightMotionControllerComponent, uevrUtils.fname_from_string(""), 1, false)

-- -- debugModule.dumpObject(hmdComponent)
	-- --end

-- end


--function M.createLeftActor(pawn)
	-- if leftActor ~= nil then 
		-- destroyHMDActor()
	-- end
	-- local hmdComponent = nil
	-- local hmdState = nil
	-- if pawn ~= nil then
		-- print("Create HMD actor called\n")
		-- local pos = pawn:K2_GetActorLocation()
		-- if leftActor == nil then
			-- leftActor = spawn_actor( pos, 1, nil)
		-- end
		-- if leftActor == nil then
			-- print("Failed to spawn HMD actor\n")
		-- else
			-- temp_transform.Translation = pos
			-- temp_transform.Rotation.W = 1.0
			-- temp_transform.Scale3D = Vector3f.new(0.1, 0.1, 0.1)
			    
				-- -- left_hand_component = api:add_component_by_class(left_hand_actor, motion_controller_component_c)
				-- -- left_hand_component.MotionSource = kismet_string_library:Conv_StringToName("Left")
				-- -- if left_hand_component.Hand ~= nil then
					-- -- left_hand_component.Hand = 0
					-- -- right_hand_component.Hand = 1
				-- -- end
				
			-- if hmdComponent == nil then
				-- local static_mesh_component_c = uevr.api:find_uobject("Class /Script/Engine.StaticMeshComponent")
				-- local static_mesh = uevr.api:find_uobject("StaticMesh /Engine/EngineMeshes/Sphere.Sphere")

				-- -- hmdComponent = leftActor:AddComponentByClass(static_mesh_component_c, true, temp_transform, false)
				-- -- hmdComponent:SetCollisionEnabled(0)
				-- -- hmdComponent:SetStaticMesh(static_mesh, true)
				-- -- --local scene_component_c = find_required_object("Class /Script/Engine.SceneComponent")
				-- -- --hmdComponent = leftActor:AddComponentByClass(scene_component_c, true, temp_transform, false)
-- -- debugModule.dumpObject(hmdComponent)
				
				-- hmdComponent = Statics:SpawnObject(static_mesh_component_c, leftActor)
				-- hmdComponent:K2_SetWorldTransform(temp_transform, false, reusable_hit_result, false)
				-- hmdComponent:SetCollisionEnabled(0)
				-- hmdComponent:SetStaticMesh(static_mesh)
				-- hmdComponent.LDMaxDrawDistance=1500.0
				-- hmdComponent.CachedMaxDrawDistance=1500.0
				-- hmdComponent.CreationMethod=2
				-- leftComponent = hmdComponent

			-- end
			-- if hmdComponent == nil then
				-- print("Failed to add HMD component\n")
			-- else
				-- hmdState = UEVR_UObjectHook.get_or_add_motion_controller_state(hmdComponent)	
				-- if hmdState ~= nil then
					-- hmdState:set_hand(0) -- left
					-- hmdState:set_permanent(true)
				-- else 
					-- print("Failed to add leftActor to motion controller\n")
				-- end
				-- --leftActor:FinishAddComponent(hmdComponent, false, temp_transform)
			-- end

		-- end
	-- end
	-- if leftActor ~= nil and hmdComponent ~= nil and hmdState ~= nil then
		-- print("HMD Component created\n")
		-- print(hmdComponent:get_full_name(),"\n")
		-- return true
	-- else
		-- print("HMD Component creation failed\n")
	-- end
	-- return false
-- end



-- local hmd_actor = nil -- The purpose of the HMD actor is to accurately track the HMD's world transform
-- local left_hand_actor = nil
-- local right_hand_actor = nil
-- local left_hand_component = nil
-- local right_hand_component = nil
-- local hmd_component = nil

-- local function spawn_actor(world_context, actor_class, location, collision_method, owner)
    -- temp_transform.Translation = location
    -- temp_transform.Rotation.W = 1.0
    -- temp_transform.Scale3D = Vector3f.new(1.0, 1.0, 1.0)

    -- local actor = Statics:BeginDeferredActorSpawnFromClass(world_context, actor_class, temp_transform, collision_method, owner)

    -- if actor == nil then
        -- print("Failed to spawn actor")
        -- return nil
    -- end

    -- Statics:FinishSpawningActor(actor, temp_transform)
    -- print("Spawned actor")

    -- return actor
-- end

-- local function reset_hand_actors()
    -- -- We are using pcall on this because for some reason the actors are not always valid
    -- -- even if exists returns true
    -- if left_hand_actor ~= nil and UEVR_UObjectHook.exists(left_hand_actor) then
        -- pcall(function()
            -- if left_hand_actor.K2_DestroyActor ~= nil then
                -- left_hand_actor:K2_DestroyActor()
            -- end
        -- end)
    -- end

    -- if right_hand_actor ~= nil and UEVR_UObjectHook.exists(right_hand_actor) then
        -- pcall(function()
            -- if right_hand_actor.K2_DestroyActor ~= nil then
                -- right_hand_actor:K2_DestroyActor()
            -- end
        -- end)
    -- end

    -- if hmd_actor ~= nil and UEVR_UObjectHook.exists(hmd_actor) then
        -- pcall(function()
            -- if hmd_actor.K2_DestroyActor ~= nil then
                -- hmd_actor:K2_DestroyActor()
            -- end
        -- end)
    -- end

    -- left_hand_actor = nil
    -- right_hand_actor = nil
    -- hmd_actor = nil
    -- right_hand_component = nil
    -- left_hand_component = nil
-- end

-- function M.onLevelChange()
    -- left_hand_actor = nil
    -- right_hand_actor = nil
    -- left_hand_component = nil
    -- right_hand_component = nil
	-- --M.spawn_hand_actors()
	-- M.createController(0)
	-- M.createController(1)
-- end

-- function M.spawn_hand_actors()
    -- local game_engine = UEVR_UObjectHook.get_first_object_by_class(game_engine_class)

    -- local viewport = game_engine.GameViewport
    -- if viewport == nil then
        -- print("Viewport is nil")
        -- return
    -- end

    -- local world = viewport.World
    -- if world == nil then
        -- print("World is nil")
        -- return
    -- end

    -- reset_hand_actors()

    -- local pawn = uevr.api:get_local_pawn(0)

    -- if pawn == nil then
        -- --print("Pawn is nil")
        -- return
    -- end

    -- local pos = pawn:K2_GetActorLocation()

    -- left_hand_actor = spawn_actor(world, actor_c, pos, 1, nil)

    -- if left_hand_actor == nil then
        -- print("Failed to spawn left hand actor")
        -- return
    -- end

    -- right_hand_actor = spawn_actor(world, actor_c, pos, 1, nil)

    -- if right_hand_actor == nil then
        -- print("Failed to spawn right hand actor")
        -- return
    -- end

    -- hmd_actor = spawn_actor(world, actor_c, pos, 1, nil)

    -- if hmd_actor == nil then
        -- print("Failed to spawn hmd actor")
        -- return
    -- end

    -- print("Spawned hand actors")

    -- -- Add scene components to the hand actors
    -- left_hand_component = uevr.api:add_component_by_class(left_hand_actor, motion_controller_component_c)
    -- right_hand_component = uevr.api:add_component_by_class(right_hand_actor, motion_controller_component_c)
    -- hmd_component = uevr.api:add_component_by_class(hmd_actor, scene_component_c)

    -- if left_hand_component == nil then
        -- print("Failed to add left hand scene component")
        -- return
    -- end

    -- if right_hand_component == nil then
        -- print("Failed to add right hand scene component")
        -- return
    -- end

    -- if hmd_component == nil then
        -- print("Failed to add hmd scene component")
        -- return
    -- end

    -- left_hand_component.MotionSource = kismet_string_library:Conv_StringToName("Left")
    -- right_hand_component.MotionSource = kismet_string_library:Conv_StringToName("Right")

    -- -- Not all engine versions have the Hand property
    -- if left_hand_component.Hand ~= nil then
        -- left_hand_component.Hand = 0
        -- right_hand_component.Hand = 1
    -- end

    -- print("Added scene components")

    -- -- -- The HMD is the only one we need to add manually as UObjectHook doesn't support motion controller components as the HMD
    -- -- local hmdstate = UEVR_UObjectHook.get_or_add_motion_controller_state(hmd_component)

    -- -- if hmdstate then
        -- -- hmdstate:set_hand(2) -- HMD
        -- -- hmdstate:set_permanent(true)
    -- -- end

    -- print(string.format("%x", left_hand_actor:get_address()) .. " " .. string.format("%x", right_hand_actor:get_address()) .. " " .. string.format("%x", hmd_actor:get_address()))
-- end
