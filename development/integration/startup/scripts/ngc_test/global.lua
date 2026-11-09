local core = require('openmw.core')
local storage = require('openmw.storage')
local util = require('openmw.util')
local world = require('openmw.world')
local elapsed, started = 0, false
local expected, ready, cleaned = {}, {}, {}
local function count(values)
    local n=0;for _ in pairs(values) do n=n+1 end;return n
end
return {
    engineHandlers = {onUpdate = function(dt)
        elapsed=elapsed+dt
        if elapsed>20 then print('NGC_RUNTIME_FAIL startup timeout');core.quit();return end
        if started or elapsed<1 or not world.players[1] then return end
        started=true
        local player=world.players[1]
        local actors={player,world.createObject('fargoth'),world.createObject('rat')}
        for index,actor in ipairs(actors) do
            if index>1 then actor:teleport(player.cell,player.position+util.vector3(index*60,0,0)) end
            expected[actor.id]=true
            actor:sendEvent('NGCTestActivate',{})
        end
    end},
    eventHandlers = {
        NGCTestReady = function(data)
            assert(expected[data.actor.id], 'Unexpected startup actor')
            ready[data.actor.id]=true
            if count(ready)==3 then
                print('NGC_ESP_ACCURACY_PASS player NPC creature')
                storage.globalSection('SettingsNGC'):set('toggleAlwaysHit',false)
            end
        end,
        NGCTestCleaned = function(data)
            assert(expected[data.actor.id], 'Unexpected cleanup actor')
            cleaned[data.actor.id]=true
            if count(cleaned)==3 then
                print('NGC_RUNTIME_PASS ESP ability applies and cleans up on player NPC creature')
                core.quit()
            end
        end,
    },
}
