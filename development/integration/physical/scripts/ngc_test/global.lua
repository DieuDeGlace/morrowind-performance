local storage = require('openmw.storage')
local core = require('openmw.core')
local world = require('openmw.world')
local util = require('openmw.util')
local elapsed, started, activated, target = 0, false, false, nil
local counts = {}
local center=util.vector3(409850,410000,1100)
local function finish()
    local accuracy,fists,miss,front,rear,normal,full=counts.accuracy,counts.fists,counts.miss,counts.front,counts.rear,counts.normal,counts.full
    assert(accuracy and accuracy.success>=2 and accuracy.failed==0 and accuracy.positive>=2,
        'ESP ability did not guarantee the low-skill player hits')
    assert(fists and fists.success>=2 and fists.helper==fists.success, 'Fist health supplements did not match the hits')
    assert(miss and miss.failed>=2 and miss.success==0 and miss.helper==miss.failed, 'Missed fists lost the independent health supplement')
    assert(front and front.blocks>=2 and front.positive==0, 'Lua frontal shield guard failed')
    assert(front.shieldAfter and front.shieldAfter<front.shieldBefore, 'Lua guard did not wear the shield')
    assert(rear and rear.positive>=2 and rear.blocks==0, 'Lua guard incorrectly stopped rear attacks')
    assert(normal and full and normal.positive>=1 and full.positive>=1, 'Bow projectile did not hit')
    assert(normal.bonus==0 and full.bonus==1 and full.damage>normal.damage*1.5, 'Full draw bonus did not reach the projectile')
    for name,c in pairs(counts) do
        print('NGC_PHYSICAL_RESULT',name,c.success,c.failed,c.helper,c.blocks,c.zero,c.positive,c.damage)
    end
    print('NGC_RUNTIME_PASS ESP accuracy, fists, Lua shield guard and full draw on Preview 10')
    core.quit()
end
return {
    engineHandlers={onUpdate=function(dt)
        elapsed=elapsed+dt
        if elapsed>90 then print('NGC_RUNTIME_FAIL physical timeout');core.quit();return end
        if started then
            if not activated and elapsed>3 then
                activated=true
                target:sendEvent('NGCTestTarget',{})
                world.players[1]:sendEvent('NGCTestPlayer',{})
            elseif activated and elapsed>10 and next(counts)==nil then
                print('NGC_RUNTIME_FAIL player setup did not complete');core.quit()
            end
            return
        end
        if elapsed<1 or not world.players[1] then return end
        started=true
        local section=storage.globalSection('SettingsNGC')
        section:set('showMessages',false);section:set('showActiveBlockMessages',false)
        for tier=1,4 do
            local s=storage.globalSection('SettingsNGCTier'..tier)
            for _,key in ipairs({'handToHandKnockdownChance','hamstringChance','riposteChance'}) do
                if s:get(key)~=nil then s:set(key,0) end
            end
        end
        world.setGameTimeScale(0);world.mwscript.getGlobalVariables().gamehour=10
        core.weather.changeWeather('Shadow Regression Region',core.weather.records.clear)
        local cell=world.getExteriorCell(50,50)
        local player=world.players[1]
        player:teleport(cell,center,{rotation=util.transform.rotateZ(0),onGround=true})
        target=world.createObject('fargoth')
        target:teleport(cell,center+util.vector3(0,60,0),{rotation=util.transform.rotateZ(math.pi),onGround=true})
        for _,id in ipairs({'iron longsword','chitin short bow','iron_shield'}) do world.createObject(id):moveInto(player) end
        world.createObject('chitin arrow',100):moveInto(player)
        world.createObject('iron dagger'):moveInto(target)
    end},
    eventHandlers={
        NGCTestPhase=function(data)
            counts[data.name]={success=0,failed=0,helper=0,blocks=0,zero=0,positive=0,damage=0,shieldBefore=data.shieldBefore}
            storage.globalSection('SettingsNGC'):set('toggleAlwaysHit',data.name~='miss')
            world.players[1]:teleport(target.cell,center,{rotation=util.transform.rotateZ(data.rear and math.pi or 0),onGround=true})
            target:teleport(target.cell,center+util.vector3(0,60,0),
                {rotation=util.transform.rotateZ(math.pi),onGround=true})
            target:sendEvent('NGCTestPhase',data)
        end,
        NGCTestCount=function(data)
            local c=counts[data.phase];if not c then return end
            if data.kind=='helper' then c.helper=c.helper+1
            elseif data.kind=='block' then c.blocks=c.blocks+1
            else
                if data.success then c.success=c.success+1 else c.failed=c.failed+1 end
                if data.damage>0 then c.positive=c.positive+1;c.damage=c.damage+data.damage else c.zero=c.zero+1 end
            end
        end,
        NGCTestShieldBefore=function(data)
            counts[data.phase].shieldBefore=data.condition
        end,
        NGCTestShieldCondition=function(data)
            counts[data.phase].shieldAfter=data.condition
            print('NGC_LUA_SHIELD_WEAR',data.phase,counts[data.phase].shieldBefore,data.condition)
        end,
        NGCTestBowBonus=function(data)
            counts[data.phase].bonus=data.bonus or 0
            print('NGC_PHYSICAL_BOW_BONUS',data.phase,data.bonus or 0)
        end,
        NGCTestDone=function()
            local ok,err=pcall(finish)
            if not ok then
                for name,c in pairs(counts) do print('NGC_PHYSICAL_DIAGNOSTIC',name,c.success,c.failed,c.helper,c.blocks,c.zero,c.positive,c.damage) end
                print('NGC_RUNTIME_FAIL',err);core.quit()
            end
        end,
    },
}
