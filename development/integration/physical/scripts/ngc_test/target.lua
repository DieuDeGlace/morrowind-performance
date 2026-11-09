local core = require('openmw.core')
local I = require('openmw.interfaces')
local self = require('openmw.self')
local types = require('openmw.types')
local Actor, NPC = types.Actor, types.NPC
local active, phase = false, ''
I.Combat.addOnHitHandler(function(data)
    if not active then return end
    core.sendGlobalEvent('NGCTestCount', {phase=phase, kind=data._ngcScriptHealth and 'helper' or 'hit',
        success=data.successful, damage=data.damage and data.damage.health or 0})
end)
return {
    engineHandlers = {onUpdate = function()
        if not active then return end
        local fatigue = Actor.stats.dynamic.fatigue(self)
        fatigue.current = fatigue.base + fatigue.modifier
        self.controls.use = self.ATTACK_TYPE.NoAttack
        self.controls.movement = 0; self.controls.sideMovement = 0
    end},
    eventHandlers = {
        NGCTestTarget = function()
            active = true; self:enableAI(false)
            Actor.stats.dynamic.health(self).base = 100000
            Actor.stats.dynamic.health(self).current = 100000
            NPC.stats.skills.unarmored(self).base = 0
            Actor.stats.attributes.agility(self).base = 100
        end,
        NGCTestPhase = function(data)
            phase = data.name
            local slots = Actor.EQUIPMENT_SLOT
            Actor.setEquipment(self, data.shield and {[slots.CarriedRight]='iron dagger', [slots.CarriedLeft]='iron_shield'} or {})
            Actor.setStance(self, Actor.STANCE.Weapon)
            self.controls.blockChanceOverride = data.shield and 100 or 0
        end,
        UnarmedAttack = function()
            if active then core.sendGlobalEvent('NGCTestCount', {phase=phase, kind='unarmed'}) end
        end,
    },
}
