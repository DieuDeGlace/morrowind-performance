local core = require('openmw.core')
local I = require('openmw.interfaces')
local types = require('openmw.types')
local world = require('openmw.world')
local settings = require('scripts.ngc.openmw.settings')

for _, group in ipairs(settings.groups()) do
    I.Settings.registerGroup(group)
end

local function recoverProjectile(data)
    local target = data.target
    if not target or not target:isValid() or not types.Actor.objectIsInstance(target)
        or types.Player.objectIsInstance(target) then
        return
    end
    local record = types.Weapon.records[data.recordId]
    if not record or (record.enchant and record.enchant ~= '') then
        return
    end
    local weaponType = record.type
    if weaponType == types.Weapon.TYPE.Arrow or weaponType == types.Weapon.TYPE.Bolt
        or weaponType == types.Weapon.TYPE.MarksmanThrown then
        world.createObject(record.id, 1):moveInto(target)
    end
end

-- OpenMW has no MWSE-style player combatStopped event. Observe only actors
-- participating in this player's fights; no global nearby-actor scans are needed.
local combat = { participants = {}, enemies = {}, observed = false }
local elapsed = 0

local function trackCombat(data)
    for _, actor in ipairs({ data.attacker, data.target }) do
        if actor and actor:isValid() then combat.participants[actor.id] = actor end
    end
end

local function updateCombat(dt)
    elapsed = elapsed + dt
    if elapsed < 0.25 then return end
    elapsed = 0
    for id, entry in pairs(combat.enemies) do
        if not entry.actor:isValid() or not entry.player:isValid()
            or types.Actor.isDead(entry.actor) or types.Actor.isDead(entry.player) then
            combat.enemies[id] = nil
        end
    end
    if next(combat.enemies) then
        combat.observed = true
        combat.deadline = nil
    elseif combat.observed then
        local now = core.getSimulationTime()
        combat.deadline = combat.deadline or now + 0.5
        if now >= combat.deadline then
            for _, actor in pairs(combat.participants) do
                if actor:isValid() then actor:sendEvent('NGCCombatEnded', {}) end
            end
            combat = { participants = {}, enemies = {}, observed = false }
        end
    end
end

return {
    engineHandlers = {
        onUpdate = updateCombat,
        onSave = function() return combat end,
        onLoad = function(saved) if saved then combat = saved end end,
    },
    eventHandlers = {
        NGCRecoverProjectile = recoverProjectile,
        NGCTrackCombat = trackCombat,
        NGCCombatState = function(data)
            if data.player then
                combat.enemies[data.actor.id] = { actor = data.actor, player = data.player }
                combat.observed = true
            else
                combat.enemies[data.actor.id] = nil
            end
        end,
        NGCParticipantInactive = function(data)
            combat.enemies[data.actor.id] = nil
            combat.participants[data.actor.id] = nil
        end,
    },
}
