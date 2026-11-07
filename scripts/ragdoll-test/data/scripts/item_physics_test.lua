local core = require('openmw.core')
local types = require('openmw.types')
local util = require('openmw.util')
local world = require('openmw.world')

local elapsed, deadline, stage = 0, 1, 'enter'
local bottle, pickup, removed, fixed
local origin, savedPosition, savedRotation, pickupCount
local office = 'Seyda Neen, Census and Excise Office'

local function spawn(player, id, offset)
    local object = world.createObject(id)
    object:teleport(player.cell, player.position + offset, { onGround = false })
    return object
end

local function update(dt)
    elapsed = elapsed + dt
    if elapsed < deadline then return end
    local player = world.players[1]
    if not player or not player.cell then return end
    assert(world.physics and world.physics.isEnabled(), 'Native item physics is disabled')

    if stage == 'enter' then
        player:teleport(office, util.vector3(-60, 70, 255), { onGround = true })
        stage, deadline = 'spawn', elapsed + 1
    elseif stage == 'spawn' then
        for _, actor in ipairs(world.activeActors) do
            if actor ~= player then actor.enabled = false end
        end
        bottle = spawn(player, 'misc_com_bottle_01', util.vector3(0, 55, 150))
        stage, deadline = 'record spawn', elapsed + .2
    elseif stage == 'record spawn' then
        origin = bottle.position
        stage, deadline = 'lazy', elapsed + 1
    elseif stage == 'lazy' then
        assert((bottle.position - origin):length() < .1, 'Untouched item woke on cell load')
        print('ITEM_TEST_LAZY_ACTIVATION')
        world.physics.applyImpulse(bottle, util.vector3(0, 0, 0), player)
        stage, deadline = 'fell', elapsed + 4
    elseif stage == 'fell' then
        assert(bottle.position.z < origin.z - 20, 'Item gravity did not update its world record')
        assert(bottle.position.z > origin.z - 500, 'Item fell through the interior floor')
        print('ITEM_TEST_GRAVITY_AND_FLOOR', bottle.position)
        pickupCount = types.Actor.inventory(player):countOf('iron longsword')
        pickup = spawn(player, 'iron longsword', util.vector3(45, 60, 80))
        removed = spawn(player, 'misc_com_bottle_02', util.vector3(-40, 60, 80))
        world.physics.applyImpulse(pickup, util.vector3(120, 0, 200), player)
        world.physics.applyImpulse(removed, util.vector3(-120, 0, 200), player)
        stage, deadline = 'remove moving', elapsed + .2
    elseif stage == 'remove moving' then
        pickup:moveInto(player)
        removed:remove()
        stage, deadline = 'removed', elapsed + 1
    elseif stage == 'removed' then
        assert(types.Actor.inventory(player):countOf('iron longsword') == pickupCount + 1, 'Moving item pickup lost inventory')
        print('ITEM_TEST_PICKUP_AND_DELETE_IN_FLIGHT')
        bottle:teleport(player.cell, player.position + util.vector3(0, 65, 60), { onGround = false })
        stage, deadline = 'grab', elapsed + .5
    elseif stage == 'grab' then
        origin = bottle.position
        world.physics.grab(bottle, bottle.position)
        stage, deadline = 'held', elapsed + 2
    elseif stage == 'held' then
        assert((bottle.position - origin):length() < 25, 'Grab constraint did not hold the item')
        print('ITEM_TEST_GRAB_HELD')
        world.physics.throw()
        stage, deadline = 'thrown', elapsed + .7
    elseif stage == 'thrown' then
        assert((bottle.position - origin):length() > 10, 'Throw did not move the item')
        print('ITEM_TEST_THROW_MOVED')
        world.physics.applyImpulse(bottle, util.vector3(200, 0, 100), player)
        stage, deadline = 'teleport', elapsed + .15
    elseif stage == 'teleport' then
        origin = player.position + util.vector3(-35, 70, 95)
        bottle:teleport(player.cell, origin, { onGround = false })
        stage, deadline = 'teleported', elapsed + 1
    elseif stage == 'teleported' then
        assert((bottle.position - origin):length() < .1, 'Native simulation overrode a scripted teleport')
        print('ITEM_TEST_TELEPORT_CANCELS_MOTION')
        world.physics.applyImpulse(bottle, util.vector3(0, 0, 0), player)
        stage, deadline = 'save', elapsed + 4
    elseif stage == 'save' then
        savedPosition, savedRotation = bottle.position, bottle.rotation
        stage, deadline = 'waiting for reload', elapsed + 30
        types.Player.sendMenuEvent(player, 'NativeItemPhysics_Save', {})
    elseif stage == 'loaded' then
        assert(bottle:isValid() and bottle.cell, 'Moved item missing after reload')
        assert((bottle.position - savedPosition):length() < .1, 'Moved position did not survive save/reload')
        local forward = util.vector3(0, 1, 0)
        assert((bottle.rotation:apply(forward) - savedRotation:apply(forward)):length() < .01,
            'Moved rotation did not survive save/reload')
        print('ITEM_TEST_SAVE_RELOAD')
        print('ITEM_TEST_COMPLETE')
        stage = 'done'
        core.quit()
    elseif stage == 'waiting for reload' then
        error('Save/reload timed out')
    end
    assert(elapsed < 120, 'Item replay timed out at ' .. stage)
end

return {
    engineHandlers = {
        onSave = function()
            return { bottle = bottle, position = savedPosition, rotation = savedRotation }
        end,
        onLoad = function(data)
            if data and data.bottle and data.position then
                bottle, savedPosition, savedRotation = data.bottle, data.position, data.rotation
                elapsed, stage, deadline = 0, 'loaded', 2
            end
        end,
        onUpdate = function(dt)
            local ok, err = pcall(update, dt)
            if not ok then
                print('ITEM_TEST_FAIL', err)
                stage = 'done'
                core.quit()
            end
        end,
    },
}
