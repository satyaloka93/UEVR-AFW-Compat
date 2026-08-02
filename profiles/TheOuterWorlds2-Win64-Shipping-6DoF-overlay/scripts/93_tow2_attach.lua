-- 93_tow2_attach.lua — explicit weapon attachment for TOW2 6DoF.
--
-- WHY THIS EXISTS: TOW2 replaces its first-person weapon component at runtime.
-- Follow Acknowledged Pawn -> FPVMesh -> AttachChildren and apply the attachment
-- directly through the Lua API whenever the visible mesh address changes. The
-- required backend enrolls this explicit late component after validating it;
-- without that backend path, a state can exist while UObjectHook.exists() is
-- false and tick_attachments() skips it.
--
-- Calibrated offsets:
--   location  x=-1.63  y=0.01  z=-1.93
--   rotation  quat w=0.99995 x=-0.01 -> ~-1.15 deg pitch, i.e. near identity
--   hand=1 (right), permanent=true
--
-- NOTE: the binding is set_permanent (correct spelling). The published UEVR
-- docs say 'set_permanant' -- the docs are wrong for this backend.

local HAND      = 1
local PERMANENT = true
local LOC       = { x = -1.63, y = 0.01, z = -1.93 }
local ROT       = { x = -1.15, y = 0.0,  z = 0.0 }   -- euler degrees
local POLL      = 0.5

local lastMesh, timer = nil, 0.0

local function LOG(m) pcall(function() uevr.params.functions.log_warn("[tow2attach] " .. m) end) end

-- Follow the exact path the saved state used; no global object scans.
local function findWeaponMesh(pawn)
    local found = nil
    pcall(function()
        local fpv = pawn.FPVMesh
        if fpv == nil then return end
        local kids = fpv.AttachChildren
        if kids == nil then return end
        for i in ipairs(kids) do
            local c = kids[i]
            if c ~= nil then
                local cls = tostring(c:get_class():get_fname():to_string())
                if cls == "SkeletalMeshComponent" then found = c return end
            end
        end
    end)
    return found
end

uevr.sdk.callbacks.on_pre_engine_tick(function(engine, delta)
    if delta == nil or delta <= 0 or delta > 1.0 then return end
    timer = timer + delta
    if timer < POLL then return end
    timer = 0.0

    pcall(function()
        local pawn = uevr.api:get_local_pawn(0)
        if pawn == nil then lastMesh = nil return end

        local mesh = findWeaponMesh(pawn)
        if mesh == nil then
            if lastMesh ~= nil then LOG("weapon mesh gone") end
            lastMesh = nil
            return
        end

        local addr = nil
        local okA, a = pcall(function() return mesh:get_address() end)
        addr = okA and a or nil
        if addr == lastMesh then return end          -- already handled
        lastMesh = addr

        local state = UEVR_UObjectHook.get_or_add_motion_controller_state(mesh)
        if state == nil then LOG("no motion controller state returned") return end

        state:set_hand(HAND)
        state:set_location_offset(Vector3f.new(LOC.x, LOC.y, LOC.z))
        state:set_rotation_offset(Vector3f.new(ROT.x, ROT.y, ROT.z))
        state:set_permanent(PERMANENT)
        LOG("ATTACHED " .. tostring(mesh:get_full_name()) .. " hand=" .. HAND)
    end)
end)

LOG("loaded")
