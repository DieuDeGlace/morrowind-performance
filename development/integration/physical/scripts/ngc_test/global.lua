local storage = require('openmw.storage')
local core = require('openmw.core')
local world = require('openmw.world')
local util = require('openmw.util')
local elapsed, started, target, activated = 0, false, nil, false
local counts = {}
local position = util.vector3(409850, 410060, 1100)
local function finish()
    local hit, miss, front, rear, normal, full = counts.hit, counts.miss, counts.front, counts.rear, counts.normal, counts.full
    assert(hit and hit.success >= 2 and hit.unarmed >= 2 and hit.helper == hit.unarmed, 'Native 100% hit/fist commitment failed')
    assert(miss and miss.failed >= 2 and miss.unarmed >= 2 and miss.helper == miss.unarmed and miss.success == 0, 'Native 0% hit/fist commitment failed')
    assert(front and front.zero >= 2 and front.positive == 0, 'Native frontal shield block failed')
    assert(rear and rear.positive >= 2, 'Native shield blocked rear attacks')
    assert(normal and normal.positive >= 1 and full and full.positive >= 1, 'Bow projectile did not hit')
    assert(full.damage > normal.damage * 1.5, 'Full draw bonus did not reach the projectile hit')
    assert(normal.bonus == 0 and full.bonus == 1, 'Full draw one-shot multiplier was not consumed correctly')
    for name, c in pairs(counts) do
        print('NGC_PHYSICAL_RESULT', name, c.success, c.failed, c.unarmed, c.helper, c.zero, c.positive, c.damage)
    end
    print('NGC_RUNTIME_PASS physical native hit/miss, fists, shield arc and full draw')
    core.quit()
end
return {
    engineHandlers = {onUpdate = function(dt)
        elapsed = elapsed + dt
        if elapsed > 80 then print('NGC_RUNTIME_FAIL physical timeout'); core.quit(); return end
        if started then
            if not activated and elapsed > 3 then
                activated=true
                target:sendEvent('NGCTestTarget',{})
                world.players[1]:sendEvent('NGCTestPlayer',{})
            elseif activated and elapsed > 9 and next(counts)==nil then
                print('NGC_RUNTIME_FAIL player setup did not complete');core.quit()
            end
            return
        end
        if elapsed < 1 or not world.players[1] then return end
        started = true
        local section=storage.globalSection('SettingsNGC')
        section:set('showMessages',false);section:set('showActiveBlockMessages',false)
        for tier=1,4 do
            local s=storage.globalSection('SettingsNGCTier'..tier)
            for _, key in ipairs({'handToHandKnockdownChance','hamstringChance','riposteChance'}) do
                if s:get(key) ~= nil then s:set(key,0) end
            end
        end
        assert((core.COMBAT_API_VERSION or 0) >= 1, 'Native combat hooks required')
        local player = world.players[1]
        world.setGameTimeScale(0)
        world.mwscript.getGlobalVariables().gamehour=10
        core.weather.changeWeather('Shadow Regression Region',core.weather.records.clear)
        local cell = world.getExteriorCell(50, 50)
        player:teleport(cell, util.vector3(409850, 410000, 1100), {rotation=util.transform.rotateZ(0), onGround=true})
        target = world.createObject('fargoth')
        target:teleport(cell, position, {rotation=util.transform.rotateZ(math.pi), onGround=true})
        for _, id in ipairs({'iron longsword','chitin short bow'}) do world.createObject(id):moveInto(player) end
        world.createObject('chitin arrow',100):moveInto(player)
        world.createObject('iron dagger'):moveInto(target)
        world.createObject('iron_shield'):moveInto(target)

    end},
    eventHandlers = {
        NGCTestPhase = function(data)
            counts[data.name] = {success=0, failed=0, unarmed=0, helper=0, zero=0, positive=0, damage=0}
            target:teleport(target.cell, position, {rotation=util.transform.rotateZ(data.rear and 0 or math.pi), onGround=true})
            target:sendEvent('NGCTestPhase', data)
        end,
        NGCTestCount = function(data)
            local c = counts[data.phase]
            if not c then return end
            if data.kind == 'unarmed' then c.unarmed=c.unarmed+1
            elseif data.kind == 'helper' then c.helper=c.helper+1
            else
                if data.success then c.success=c.success+1 else c.failed=c.failed+1 end
                if data.damage > 0 then c.positive=c.positive+1; c.damage=c.damage+data.damage
                else c.zero=c.zero+1 end
            end
        end,
        NGCTestBowBonus = function(data)
            counts[data.phase].bonus = data.bonus or 0
            print('NGC_PHYSICAL_BOW_BONUS',data.phase,data.bonus or 0)
        end,
        NGCTestDone = function()
            local ok, err = pcall(finish)
            if not ok then
                for name,c in pairs(counts) do print('NGC_PHYSICAL_DIAGNOSTIC', name,c.success,c.failed,c.unarmed,c.helper,c.zero,c.positive,c.damage) end
                print('NGC_RUNTIME_FAIL', err); core.quit()
            end
        end,
    },
}
