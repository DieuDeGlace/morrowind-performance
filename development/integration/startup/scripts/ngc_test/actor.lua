local core = require('openmw.core')
local self = require('openmw.self')
local types = require('openmw.types')
local requirements = require('scripts.ngc.openmw.requirements')
local settings = require('scripts.ngc.openmw.settings')
local active, ready, cleaned = false, false, false
return {
    eventHandlers = {NGCTestActivate = function()
        active = true
        if not types.Player.objectIsInstance(self) then self:enableAI(false) end
    end},
    engineHandlers = {onUpdate = function()
        if not active then return end
        assert(requirements.ready, 'Required ESP records are missing')
        local applied = types.Actor.spells(self)[settings.accuracySpell] ~= nil
        if not ready and applied then
            assert(types.Actor.activeEffects(self):getEffect('fortifyattack').magnitude >= settings.accuracyBonus,
                'ESP accuracy ability was not applied to the actor')
            ready = true
            core.sendGlobalEvent('NGCTestReady', {actor=self.object})
        elseif ready and not applied and not cleaned then
            cleaned = true
            core.sendGlobalEvent('NGCTestCleaned', {actor=self.object})
        end
    end},
}
