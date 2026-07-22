-- examine.lua
-- Silent Hill f -- Altar examine repositioning (DISABLED)
--
-- Previously repositioned the ExamineObject actor to follow the right
-- controller during the altar puzzle. Removed because the puzzle camera
-- system in shf.lua already handles VR correctly; moving the item caused
-- the puzzle to break.

local M = {}

function M.isExamining() return false end
function M.setOffset(_) end

return M
