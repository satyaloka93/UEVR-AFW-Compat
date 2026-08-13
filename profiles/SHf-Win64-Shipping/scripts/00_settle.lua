-- 00_settle.lua — loads first (NTFS upcased order: digits sort before letters; a
-- '_' prefix would load LAST). Rewritten 2026-08-01 after discovering that
-- uevr.sdk.callbacks is a C++ usertype: assigning to its fields reports success
-- but reads still return the original binding, so the previous "settle gate" and
-- "callback drops" here were inert and have been removed. What remains is real:
--   1. module stubbing via package.loaded (a genuine Lua table)
--   2. logging at warn level (this profile's log level filters info out)
-- Crash attribution now lives in libs/uevr_utils.lua's executeUEVRCallbacks,
-- which is the actual dispatch hub and can be instrumented.

local function LOG(msg)
    pcall(function() uevr.params.functions.log_warn("[settle] " .. msg) end)
end

LOG("shim loading")

-- NOTE 2026-08-01: module stubbing was REMOVED here. Stubs replaced real modules
-- with no-op tables, and live code (interaction.lua/montage.lua -> libs/ui,
-- uevr_dev -> reticule) then received nil where it expected objects. Their
-- introduction lines up exactly with crashes starting ~17s after launch, before
-- a save could be loaded. Do not reintroduce stubbing as a bisection tool here;
-- park whole top-level scripts instead, which is inert by comparison.

-- Settle gate. Crashes cluster ~0.2s after a pawn appears, i.e. exactly when
-- scripts first touch a freshly created pawn. Wrapping uevr.sdk.callbacks does
-- NOT work (C++ usertype), so instead we publish a global flag and each script's
-- tick callback checks it. Consumers: shf.lua, melee.lua, 90_weapon_attach.lua.
local SETTLE_SECONDS = 4.0
local lastPawn = nil
local pawnTime = 0.0
_G.SHF_SETTLED = false
_G.SHF_PAWN_ADDR = nil   -- published for other scripts; avoids duplicate lookups

pcall(function()
    uevr.sdk.callbacks.on_pre_engine_tick(function(engine, delta)
        local ok, pawn = pcall(function() return uevr.api:get_local_pawn(0) end)
        local addr = nil
        if ok and pawn ~= nil then
            local okA, a = pcall(function() return pawn:get_address() end)
            addr = okA and a or nil
        end

        _G.SHF_PAWN_ADDR = addr

        if addr ~= lastPawn then
            LOG("pawn changed: " .. tostring(lastPawn) .. " -> " .. tostring(addr)
                .. " (gate closed)")
            lastPawn = addr
            pawnTime = 0.0
            _G.SHF_SETTLED = false
        end

        if addr == nil then return end
        if delta ~= nil and delta > 0 and delta < 1.0 then
            pawnTime = pawnTime + delta
        end
        if not _G.SHF_SETTLED and pawnTime >= SETTLE_SECONDS then
            _G.SHF_SETTLED = true
            LOG("GATE OPEN after " .. string.format("%.1f", pawnTime) .. "s stable pawn")
        end
    end)
end)

LOG("shim loaded")
