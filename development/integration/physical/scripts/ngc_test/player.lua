local debug = require('openmw.debug')
local async = require('openmw.async')
local camera = require('openmw.camera')
local core = require('openmw.core')
local input = require('openmw.input')
local I = require('openmw.interfaces')
local self = require('openmw.self')
local types = require('openmw.types')
local Actor,NPC=types.Actor,types.NPC
local phases={
    {name='accuracy',duration=6,weapon='iron longsword'},
    {name='fists',duration=6},{name='miss',duration=6},
    {name='front',duration=6,shield=true,weapon='iron longsword'},
    {name='rear',duration=6,shield=true,rear=true,weapon='iron longsword'},
    {name='normal',duration=6,weapon='chitin short bow',bow=1},
    {name='full',duration=9,weapon='chitin short bow',bow=4.3},
}
local active,use,guard,index,elapsed=false,false,false,0,0
local screenshot,ngcPlayer,shieldMeasured=false,nil,false
input.bindAction('Use',async:callback(function() return use end),{})
input.bindAction('NGCGuard',async:callback(function() return guard end),{})
local function shieldCondition()
    local shield=Actor.getEquipment(self,Actor.EQUIPMENT_SLOT.CarriedLeft)
    return shield and types.Item.itemData(shield).condition
end
local function nextPhase()
    if phases[index] and phases[index].shield then
        core.sendGlobalEvent('NGCTestShieldCondition',{phase=phases[index].name,condition=shieldCondition()})
    end
    index=index+1;elapsed=0;use=false;guard=false;screenshot=false;shieldMeasured=false
    local phase=phases[index]
    if not phase then active=false;core.sendGlobalEvent('NGCTestDone',{});return end
    local slots=Actor.EQUIPMENT_SLOT
    local equipment={}
    if phase.weapon then equipment[slots.CarriedRight]=phase.weapon end
    if phase.bow then equipment[slots.Ammunition]='chitin arrow' end
    if phase.shield then equipment[slots.CarriedLeft]='iron_shield' end
    Actor.setEquipment(self,equipment)
    Actor.setStance(self,Actor.STANCE.Weapon)
    for _,name in ipairs({'handtohand','longblade','marksman'}) do NPC.stats.skills[name](self).base=phase.name=='accuracy' and 0 or 100 end
    NPC.stats.skills.block(self).base=0
    Actor.stats.attributes.strength(self).base=50
    Actor.stats.attributes.agility(self).base=0;Actor.stats.attributes.luck(self).base=0
    Actor.activeEffects(self):set(phase.name=='miss' and 10000 or 0,'blind')
    core.sendGlobalEvent('NGCTestPhase',phase)
    print('NGC_PHYSICAL_PHASE',phase.name)
end
I.Combat.addOnHitHandler(function(data)
    if not active or not phases[index].shield then return end
    core.sendGlobalEvent('NGCTestCount',{phase=phases[index].name,kind='hit',
        success=data.successful,damage=data.damage and data.damage.health or 0})
end)
return {
    interfaceName='NGCPlayer',
    interface={
        version=2,
        tryBlock=function(...)
            local blocked=ngcPlayer.tryBlock(...)
            if blocked and active then core.sendGlobalEvent('NGCTestCount',{phase=phases[index].name,kind='block'}) end
            return blocked
        end,
        consumeBowBonus=function()
            local bonus=ngcPlayer.consumeBowBonus()
            if phases[index] and phases[index].bow then
                core.sendGlobalEvent('NGCTestBowBonus',{phase=phases[index].name,bonus=bonus})
            end
            return bonus
        end,
    },
    eventHandlers={NGCTestPlayer=function()
        I.Controls.overrideMovementControls(true)
        Actor.stats.dynamic.health(self).base=100000;Actor.stats.dynamic.health(self).current=100000
        camera.setMode(camera.MODE.FirstPerson);camera.setPitch(0);camera.setYaw(0)
        active=true;nextPhase()
    end},
    engineHandlers={
        onInterfaceOverride=function(previous) ngcPlayer=previous end,
        onUpdate=function()
            if active then
                local fatigue=Actor.stats.dynamic.fatigue(self);fatigue.current=fatigue.base+fatigue.modifier
            end
        end,
        onFrame=function(dt)
            if not active then return end
            elapsed=elapsed+dt
            self.controls.movement=0;self.controls.sideMovement=0
            if not screenshot and elapsed>2 then debug.takeScreenshot();screenshot=true end
            local phase=phases[index]
            if phase.shield and not shieldMeasured and elapsed>1 then
                local condition=shieldCondition()
                if condition then
                    shieldMeasured=true
                    core.sendGlobalEvent('NGCTestShieldBefore',{phase=phase.name,condition=condition})
                end
            end
            local t=elapsed-2
            guard=phase.shield and t>=0 or false
            use=not phase.shield and t>=0 and (phase.bow and t<phase.bow or not phase.bow and t%1.1<0.3)
            if elapsed>=phase.duration then nextPhase() end
        end,
    },
}
