-- Compatibility module: TheOuterWorlds2.lua requires this name at startup.
-- The validated UEVR backend owns CVar application. Do not register callbacks
-- or replay user_script.txt here. Keep the module present so the rest of the
-- gameplay profile can initialize; the former automatic loader is backed up.
return {}
