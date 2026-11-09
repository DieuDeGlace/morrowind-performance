local debug = require('openmw.debug')
local async = require('openmw.async')
local camera = require('openmw.camera')
local core = require('openmw.core')
local input = require('openmw.input')
local I = require('openmw.interfaces')
local self = require('openmw.self')
local types = require('openmw.types')
local Actor, NPC = types.Actor, types.NPC
local phases = {
    {name='hit', duration=6, chance=100}, {name='miss', duration=6, chance=0},
    {name='front', duration=6, chance=100, shield=true, weapon='iron longsword'},
    {name='rear', duration=6, chance=100, shield=true, rear=true, weapon='iron longsword'},
    {name='normal', duration=6, chance=100, weapon='chitin short bow', bow=1},
    {name='full', duration=9, chance=100, weapon='chitin short bow', bow=4.3},
}
local active, use, index, elapsed = false, false, 0, 0
local screenshot=false
local ngcPlayer
input.bindAction('Use', async:callback(function() return use end), {})
local function nextPhase()
    index=index+1; elapsed=0; use=false; screenshot=false
    local phase = phases[index]
    if not phase then active=false; core.sendGlobalEvent('NGCTestDone', {}); return end
    local slots = Actor.EQUIPMENT_SLOT
    Actor.setEquipment(self, phase.weapon and {[slots.CarriedRight]=phase.weapon, [slots.Ammunition]='chitin arrow'} or {})
    Actor.setStance(self, Actor.STANCE.Weapon)
    self.controls.hitChanceOverride=phase.chance
    core.sendGlobalEvent('NGCTestPhase', phase)
    print('NGC_PHYSICAL_PHASE', phase.name)
end
return {
    interfaceName = 'NGCPlayer',
    interface = {
        version = 2,
        tryBlock = function(...) return ngcPlayer.tryBlock(...) end,
        consumeBowBonus = function()
            local bonus = ngcPlayer.consumeBowBonus()
            if phases[index] and phases[index].bow then
                core.sendGlobalEvent('NGCTestBowBonus', {phase=phases[index].name, bonus=bonus})
            end
            return bonus
        end,
    },
    eventHandlers = {NGCTestPlayer = function()
        for _, name in ipairs({'handtohand','longblade','marksman'}) do NPC.stats.skills[name](self).base=100 end
        Actor.stats.attributes.strength(self).base=50
        print('NGC_PHYSICAL_CONTROLS',types.Player.getControlSwitch(self,types.Player.CONTROL_SWITCH.Controls),types.Player.getControlSwitch(self,types.Player.CONTROL_SWITCH.Fighting))
        I.Controls.overrideMovementControls(true)
        camera.setMode(camera.MODE.FirstPerson); camera.setPitch(0); camera.setYaw(0)
        active=true; nextPhase()
    end},
    engineHandlers = {
        onInterfaceOverride = function(previous) ngcPlayer = previous end,
        onUpdate=function()
            if active then
                local fatigue=Actor.stats.dynamic.fatigue(self)
                fatigue.current=fatigue.base+fatigue.modifier
            end
        end,
        onFrame=function(dt)
            if not active then return end
            elapsed=elapsed+dt
            self.controls.movement=0; self.controls.sideMovement=0
            self.controls.hitChanceOverride=phases[index].chance
            if not screenshot and elapsed>2 then debug.takeScreenshot();screenshot=true end
            local t=elapsed-2
            use=t>=0 and (phases[index].bow and t<phases[index].bow or not phases[index].bow and t%1.1<0.3)
            if elapsed>=phases[index].duration then nextPhase() end
        end,
    },
}
