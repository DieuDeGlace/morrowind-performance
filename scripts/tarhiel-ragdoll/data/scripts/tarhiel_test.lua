local core = require('openmw.core')
local world = require('openmw.world')
local util = require('openmw.util')
local types = require('openmw.types')
local vfs = require('openmw.vfs')
local smoke = vfs.fileExists('tarhiel-smoke.txt')
local elapsed = 0
local tarhiel
local deathTime
local cameraReady = false

local function update(dt)
    elapsed = elapsed + dt
    local player = world.players[1]
    if not player or not player.cell or elapsed < 1 then return end

    if not tarhiel then
        tarhiel = world.getObjectByFormId(core.getFormId('Morrowind.esm', 0x73d4b))
        assert(tarhiel and tarhiel.recordId == 'agronian guy', 'Tarhiel reference missing')
        local cell = world.getExteriorCell(-3, -8)
        player:teleport(cell, util.vector3(-16890, -58575, 1000), { onGround = true })
        return
    end

    if not cameraReady and player.cell.gridX == -3 and player.cell.gridY == -8 then
        player:sendEvent('TarhielTest_Camera')
        cameraReady = true
    end

    if not deathTime and types.Actor.isDeathFinished(tarhiel) then
        deathTime = elapsed
        assert(types.Actor.isDead(tarhiel), 'Expected a dead Tarhiel')
        local inventory = types.Actor.inventory(tarhiel)
        assert(inventory:countOf('sc_icarianflight') == 3,
            'Expected Tarhiel to retain his inventory')
        print('TARHIEL_TEST_DEATH_AND_INVENTORY', tarhiel.position)
        if smoke then player:sendEvent('TarhielTest_Capture') end
    end

    if smoke and deathTime and elapsed > deathTime + 17 then
        print('TARHIEL_TEST_COMPLETE')
        core.quit()
    elseif smoke and elapsed > 33 then
        error('Timed out waiting for Tarhiel to fall')
    end
end

return {
    engineHandlers = {
        onUpdate = function(dt)
            local ok, err = pcall(update, dt)
            if not ok then
                print('TARHIEL_TEST_FAIL', err)
                if smoke then core.quit() end
            end
        end,
    },
}
