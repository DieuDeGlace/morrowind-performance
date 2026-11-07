local core = require('openmw.core')
local types = require('openmw.types')
local util = require('openmw.util')
local vfs = require('openmw.vfs')
local world = require('openmw.world')
local elapsed, deadline, stage = 0, 1, 'enter'
local bottle
local office = 'Seyda Neen, Census and Excise Office'
local function update(dt)
    elapsed = elapsed + dt
    if elapsed < deadline then return end
    local player = world.players[1]
    if not player or not player.cell then return end
    assert(world.physics.isEnabled(), 'Native items disabled')
    if stage == 'enter' then
        assert(vfs.fileExists('meshes/debris/misc_com_bottle_&01&10__blue_straw_chunk_0.nif'),
            'Install the optional reference mod assets for the debris replay; leave its Lua manifest disabled')
        player:teleport(office, util.vector3(-60, 70, 255), { onGround = true })
        stage, deadline = 'spawn', elapsed + 1
    elseif stage == 'spawn' then
        for _, actor in ipairs(world.activeActors) do
            if actor ~= player then actor.enabled = false end
        end
        world.createObject('iron longsword'):moveInto(player)
        bottle = world.createObject('misc_com_bottle_01')
        bottle:teleport(player.cell, player.position + util.vector3(0, 65, 60), { onGround = false })
        stage, deadline = 'strike', elapsed + .5
    elseif stage == 'strike' then
        player:sendEvent('RagdollTest_Strike', { target = bottle, weapon = 'iron longsword', chargeTime = 2 })
        stage, deadline = 'verify', elapsed + 7
    elseif stage == 'verify' then
        assert(not bottle:isValid() or bottle.count <= 0 or not bottle.cell,
            'Charged melee did not fracture the item with matching installed debris')
        print('ITEM_IMPACT_TEST_MELEE_AND_OPTIONAL_DEBRIS')
        print('ITEM_IMPACT_TEST_COMPLETE')
        stage = 'done'
        core.quit()
    end
end
return { engineHandlers = { onUpdate = function(dt)
    local ok, err = pcall(update, dt)
    if not ok then
        print('ITEM_IMPACT_TEST_FAIL', err)
        core.quit()
    end
end } }
