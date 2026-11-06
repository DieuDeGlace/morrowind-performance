local self = require('openmw.self')
local types = require('openmw.types')

return {
    engineHandlers = {
        onInit = function() self:enableAI(false) end,
    },
    eventHandlers = {
        RagdollTest_Die = function()
            types.Actor.stats.dynamic.health(self).current = 0
        end,
    },
}
