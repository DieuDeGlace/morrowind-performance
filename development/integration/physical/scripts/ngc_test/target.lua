local core = require('openmw.core')
local I = require('openmw.interfaces')
local self = require('openmw.self')
local types = require('openmw.types')
local Actor,NPC=types.Actor,types.NPC
local active,phase,elapsed=false,nil,0
I.Combat.addOnHitHandler(function(data)
    if not active or not phase or phase.shield then return end
    core.sendGlobalEvent('NGCTestCount',{phase=phase.name,kind=data._ngcScriptHealth and 'helper' or 'hit',
        success=data.successful,damage=data.damage and data.damage.health or 0})
end)
return {
    engineHandlers={onUpdate=function(dt)
        if not active then return end
        local fatigue=Actor.stats.dynamic.fatigue(self);fatigue.current=fatigue.base+fatigue.modifier
        elapsed=elapsed+dt
        self.controls.movement=0;self.controls.sideMovement=0
        local t=elapsed-2
        self.controls.use=phase and phase.shield and t>=0 and t%1.1<0.3 and self.ATTACK_TYPE.Any or self.ATTACK_TYPE.NoAttack
    end},
    eventHandlers={
        NGCTestTarget=function()
            active=true;self:enableAI(false)
            Actor.stats.dynamic.health(self).base=100000;Actor.stats.dynamic.health(self).current=100000
            NPC.stats.skills.unarmored(self).base=0;NPC.stats.skills.shortblade(self).base=100
            Actor.stats.attributes.agility(self).base=100
        end,
        NGCTestPhase=function(data)
            phase=data;elapsed=0
            Actor.setEquipment(self,data.shield and {[Actor.EQUIPMENT_SLOT.CarriedRight]='iron dagger'} or {})
            Actor.setStance(self,Actor.STANCE.Weapon)
        end,
    },
}
