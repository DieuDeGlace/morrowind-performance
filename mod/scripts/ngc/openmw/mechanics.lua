-- This module is shared by the actor/player scripts within one Lua context.
-- Native controls are optional; stock OpenMW uses the documented Lua fallback.
local core = require('openmw.core')
local self = require('openmw.self')
local settings = require('scripts.ngc.openmw.settings')
local M = { native = (core.COMBAT_API_VERSION or 0) >= 1 }
M.ready = core.API_REVISION >= 129
    and core.contentFiles.has('Next Generation Combat.esp')
    and core.contentFiles.has('Next Generation Combat - Hand To Hand Fixes.esp')
    and core.magic.spells.records[settings.accuracySpell] ~= nil
local hamstring, bow = 1, 1

local function movement()
    if M.native then self.controls.movementSpeedMultiplier = hamstring * bow end
end

function M.setAccuracy(enabled)
    if M.native then self.controls.hitChanceOverride = enabled and 100 or nil end
end

function M.setGuard(enabled)
    if M.native then self.controls.blockChanceOverride = enabled and 100 or nil end
end

function M.setUnarmedEvents(enabled)
    if M.native then self.controls.unarmedAttackEvents = enabled end
end

function M.setHamstring(multiplier)
    if hamstring == multiplier then return end
    hamstring = multiplier
    movement()
end

function M.setBowSlow(multiplier)
    if bow == multiplier then return end
    bow = multiplier
    movement()
end

function M.reset()
    M.setAccuracy(false)
    M.setGuard(false)
    M.setUnarmedEvents(false)
    hamstring, bow = 1, 1
    movement()
end

return M
