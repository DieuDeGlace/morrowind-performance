local core = require('openmw.core')
local types = require('openmw.types')
local util = require('openmw.util')
local vfs = require('openmw.vfs')
local world = require('openmw.world')

local smoke = vfs.fileExists('ragdoll-smoke.txt')
local dragSmoke = vfs.fileExists('ragdoll-drag-smoke.txt')
local office = 'Seyda Neen, Census and Excise Office'
local stage = 'enter'
local elapsed = 0
local deadline
local targets = {}

local function spawn(player, id, offset, onGround)
    local target = world.createObject(id)
    target:addScript('scripts/ragdoll_target.lua')
    target:teleport(player.cell, player.position + offset, { onGround = onGround })
    targets[#targets + 1] = target
    return target
end

local function killTargets()
    for _, target in ipairs(targets) do
        target:sendEvent('RagdollTest_Die')
    end
end

local function deathsFinished()
    for _, target in ipairs(targets) do
        if not types.Actor.isDeathFinished(target) then return false end
    end
    return true
end

local function update(dt)
    elapsed = elapsed + dt
    local player = world.players[1]
    if not player or not player.cell or elapsed < 1 then return end
    if deadline and elapsed < deadline then return end

    if stage == 'enter' then
        player:teleport(office, util.vector3(-60, 70, 255), { onGround = true })
        stage = 'interior'
        deadline = elapsed + 1
    elseif stage == 'interior' then
        -- Keep other NPCs out of this disposable test encounter.
        for _, actor in ipairs(world.activeActors) do
            if actor ~= player then actor.enabled = false end
        end
        local npc = spawn(player, 'fargoth', util.vector3(0, 55, 35), true)
        world.createObject('gold_001', 17):moveInto(npc)
        if dragSmoke then
            require('openmw.interfaces').Activation.addHandlerForObject(npc, function()
                print('RAGDOLL_TEST_DRAG_TAP')
                return false
            end)
        elseif not smoke then
            world.createObject('iron longsword'):moveInto(player)
            spawn(player, 'skeleton', util.vector3(75, 88, 35), true)
            spawn(player, 'rat_diseased', util.vector3(-64, 85, 35), true)
            player:sendEvent('RagdollTest_Look', npc)
        end
        stage = 'kill interior'
        deadline = elapsed + .35
    elseif stage == 'kill interior' then
        killTargets()
        stage = 'wait interior'
        deadline = elapsed + .25
    elseif stage == 'wait interior' and deathsFinished() then
        assert(types.Actor.inventory(targets[1]):countOf('gold_001') >= 17, 'Corpse inventory lost')
        print('RAGDOLL_TEST_INTERIOR_DEATH_AND_INVENTORY')
        if dragSmoke then
            stage = 'drag'
            deadline = elapsed + 12
        elseif smoke then
            stage = 'strike'
            deadline = elapsed + 11
        else
            stage = 'done'
        end
    elseif stage == 'drag' then
        player:sendEvent('RagdollTest_Drag', targets[1])
        stage = 'done'
    elseif stage == 'strike' then
        player:sendEvent('RagdollTest_Strike', { target = targets[1] })
        stage = 'leave interior'
        deadline = elapsed + 8
    elseif stage == 'leave interior' then
        world.createObject('iron longsword'):moveInto(player)
        for _, target in ipairs(targets) do target:remove() end
        targets = {}
        player:teleport(world.getExteriorCell(-3, -8), util.vector3(-16890, -58575, 1000), { onGround = true })
        stage = 'exterior'
        deadline = elapsed + 1
    elseif stage == 'exterior' then
        spawn(player, 'skeleton', util.vector3(-170, 155, 40), true)
        spawn(player, 'guar', util.vector3(130, 145, 45), true)
        spawn(player, 'cliff racer', util.vector3(20, 200, 180), false)
        spawn(player, 'shalk', util.vector3(-110, 90, 40), true)
        spawn(player, 'mudcrab', util.vector3(165, 235, 40), true)
        stage = 'kill exterior'
        deadline = elapsed + .65
    elseif stage == 'kill exterior' then
        killTargets()
        stage = 'wait exterior'
        deadline = elapsed + .25
    elseif stage == 'wait exterior' and deathsFinished() then
        print('RAGDOLL_TEST_EXTERIOR_DEATH')
        stage = 'approach guar'
        deadline = elapsed + 17
    elseif stage == 'approach guar' then
        local guar = targets[2]
        player:teleport(guar.cell, guar.position + util.vector3(0, -62, 36), { onGround = true })
        stage = 'strike guar'
        deadline = elapsed + 1
    elseif stage == 'strike guar' then
        player:sendEvent('RagdollTest_Strike', { target = targets[2], weapon = 'iron longsword' })
        stage = 'finish'
        deadline = elapsed + 8
    elseif stage == 'finish' then
        print('RAGDOLL_TEST_COMPLETE')
        core.quit()
    end

    if smoke and elapsed > 65 then error('Ragdoll replay timed out at ' .. stage) end
end

return {
    engineHandlers = {
        onUpdate = function(dt)
            local ok, err = pcall(update, dt)
            if not ok then
                print('RAGDOLL_TEST_FAIL', err)
                stage = 'done'
                if smoke or dragSmoke then core.quit() end
            end
        end,
    },
}
