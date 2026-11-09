local animation = require('openmw.animation')
local core = require('openmw.core')
local I = require('openmw.interfaces')
local self = require('openmw.self')
local types = require('openmw.types')
local config = require('scripts.ngc.openmw.config')
local rules = require('scripts.ngc.openmw.rules')
local settings = require('scripts.ngc.openmw.settings')
local baseCombat = I.Combat

local Actor, NPC, Creature, Weapon = types.Actor, types.NPC, types.Creature, types.Weapon
local weaponTypes = Weapon.TYPE
local isPlayer = types.Player.objectIsInstance(self)
local state = { riposteUntil = 0, hamstringDelta = 0, hamstringUntil = 0, knockdownUntil = 0,
    stunUntil = 0, rushExpiries = {}, armorCache = {} }
local updateElapsed = 0
local accuracyEnabled
local trackingCombat, reportedInCombat = false, nil

local skillByType = {
    [weaponTypes.ShortBladeOneHand] = 'shortblade',
    [weaponTypes.LongBladeOneHand] = 'longblade', [weaponTypes.LongBladeTwoHand] = 'longblade',
    [weaponTypes.BluntOneHand] = 'bluntweapon', [weaponTypes.BluntTwoClose] = 'bluntweapon',
    [weaponTypes.BluntTwoWide] = 'bluntweapon', [weaponTypes.SpearTwoWide] = 'spear',
    [weaponTypes.AxeOneHand] = 'axe', [weaponTypes.AxeTwoHand] = 'axe',
    [weaponTypes.MarksmanBow] = 'marksman', [weaponTypes.MarksmanCrossbow] = 'marksman',
    [weaponTypes.MarksmanThrown] = 'marksman',
}

local function attribute(actor, name)
    return Actor.stats.attributes[name](actor).modified
end

local function skill(actor, name)
    if Creature.objectIsInstance(actor) then
        local specialization = core.stats.Skill.record(name).specialization
        return Creature.record(actor)[specialization .. 'Skill']
    end
    return NPC.stats.skills[name](actor).modified
end

local function fatigueRatio(actor)
    local fatigue = Actor.stats.dynamic.fatigue(actor)
    return rules.ratio(fatigue.current, fatigue.base + fatigue.modifier)
end

local function effect(actor, id)
    return Actor.activeEffects(actor):getEffect(id).magnitude
end

local function weaponRecord(attack)
    if attack.weapon and attack.weapon:isValid() then
        return Weapon.record(attack.weapon)
    end
    local ammo = attack.ammo and Weapon.records[attack.ammo]
    if ammo and ammo.type == weaponTypes.MarksmanThrown then
        return ammo
    end
end

local function notify(actor, message, bonus)
    if actor and actor:isValid() and types.Player.objectIsInstance(actor) then
        actor:sendEvent('NGCNotify', { message = message, bonus = bonus })
    end
end

local function totalBaseArmor()
    local armor = 0
    for _, item in pairs(Actor.getEquipment(self)) do
        if types.Armor.objectIsInstance(item) then
            armor = armor + types.Armor.record(item).baseArmor
        end
    end
    return armor
end

local function scriptedHealthDamage(damage, attacker)
    local hit = {
        successful = true, damage = { health = damage }, attacker = attacker,
        sourceType = I.Combat.ATTACK_SOURCE_TYPES.Unspecified, strength = 1,
        _ngcArmorApplied = true, _ngcScriptHealth = true,
    }
    baseCombat.onHit(hit)
    return hit.damage.health
end

local function removeHamstring()
    if state.hamstringDelta ~= 0 then
        local speed = Actor.stats.attributes.speed(self)
        speed.modifier = speed.modifier - state.hamstringDelta
        state.hamstringDelta = 0
    end
    state.hamstringUntil = 0
end

local function updateHamstring(now, cfg)
    if state.hamstringUntil <= now or not cfg.toggleWeaponPerks or Actor.isDead(self) then
        removeHamstring()
        return
    end
    local speed = Actor.stats.attributes.speed(self)
    local unaffected = math.max(0, speed.modified - state.hamstringDelta)
    local delta = unaffected * (rules.clamp(cfg.hamstringModifier, 0, 1) - 1)
    speed.modifier = speed.modifier + delta - state.hamstringDelta
    state.hamstringDelta = delta
end

local function synchronizeAccuracy(enabled)
    if accuracyEnabled == enabled then return end
    local spells = Actor.spells(self)
    if isPlayer and spells.ngc_ready_to_strike then spells:remove('ngc_ready_to_strike') end
    if enabled and not spells[settings.accuracySpell] then
        spells:add(settings.accuracySpell)
    elseif not enabled and spells[settings.accuracySpell] then
        spells:remove(settings.accuracySpell)
    end
    accuracyEnabled = enabled
end

local function recoverAttack(attack, target)
    if attack.sourceType ~= I.Combat.ATTACK_SOURCE_TYPES.Ranged or not attack.ammo
        or not target or not target:isValid() or types.Player.objectIsInstance(target) then
        return
    end
    local cfg = config.get()
    local record = weaponRecord(attack)
    local chance = core.getGMST('fNGCProjectileStoreChance')
    if cfg.toggleWeaponPerks and record and record.type == weaponTypes.MarksmanThrown then
        chance = rules.tier(cfg, skill(self, 'marksman')).thrownChanceToRecover or chance
    end
    if rules.roll(chance) then
        core.sendGlobalEvent('NGCRecoverProjectile', { target = target, recordId = attack.ammo })
    end
end

local function trackCombat(attacker, target)
    if not trackingCombat and (types.Player.objectIsInstance(attacker) or types.Player.objectIsInstance(target)) then
        trackingCombat = true
        core.sendGlobalEvent('NGCTrackCombat', { attacker = attacker, target = target })
    end
end

local function onHit(attack)
    local physical = attack.sourceType == I.Combat.ATTACK_SOURCE_TYPES.Melee
        or attack.sourceType == I.Combat.ATTACK_SOURCE_TYPES.Ranged
    local attacker = attack.attacker
    if not physical or not attacker or not attacker:isValid() or not Actor.objectIsInstance(attacker)
        or Actor.isDead(self) then
        return
    end
    trackCombat(attacker, self.object)
    local cfg = config.get()
    local handToHand = cfg.toggleWeaponPerks and cfg.toggleHandToHandPerks
        and attack.sourceType == I.Combat.ATTACK_SOURCE_TYPES.Melee and not attack.weapon
        and NPC.objectIsInstance(attacker) and not NPC.isWerewolf(attacker)
    if not attack.successful and not handToHand then return end
    local damage = attack.damage or {}
    attack.damage = damage
    damage.health = damage.health or 0
    damage.fatigue = damage.fatigue or 0
    -- Zero damage includes native shield blocks, immunities, and god mode.
    if damage.health <= 0 and damage.fatigue <= 0 and not handToHand then
        if attack.sourceType == I.Combat.ATTACK_SOURCE_TYPES.Ranged and attack.ammo then
            attacker:sendEvent('NGCRecoverAttack', { attack = attack, target = self.object })
        end
        return
    end
    if isPlayer and require('openmw.debug').isGodMode() then
        return
    end
    if isPlayer and I.NGCPlayer and I.NGCPlayer.tryBlock(attack) then
        attack.damage = { health = 0, fatigue = 0 }
        if not handToHand then return end
    end
    -- MWSE's damage hook is after armor, before applyHealthDamage's difficulty
    -- scaling. Apply native armor now, then suppress only the duplicate pass.
    if attack.successful then
        baseCombat.applyArmor(attack)
        attack._ngcArmorApplied = true
    end
    local health = Actor.stats.dynamic.health(self)
    attacker:sendEvent('NGCResolveAttack', {
        target = self.object, attack = attack,
        armor = totalBaseArmor(),
        handToHand = handToHand,
        healthRatio = rules.ratio(health.current, health.base + health.modifier),
        knockedDown = state.knockdownUntil > core.getSimulationTime(),
    })
    -- The attacker owns combo/riposte state. Resume the normal Combat chain after
    -- its local script returns the result; no direct writes to another actor's stats.
    return false
end

local function resolveAttack(data)
    local target, attack = data.target, data.attack
    if not target or not target:isValid() or Actor.isDead(target) then
        return
    end
    trackCombat(self.object, target)
    local cfg = config.get()
    local now = core.getSimulationTime()
    local equipped = Actor.getEquipment(self, Actor.EQUIPMENT_SLOT.CarriedRight)
    local record = equipped and Weapon.objectIsInstance(equipped) and Weapon.record(equipped) or nil
    local weaponType = record and record.type
    local skillId = record and skillByType[weaponType] or 'handtohand'
    local level = skill(self, skillId or 'handtohand')
    local tier = rules.tier(cfg, level)
    local damage = attack.damage.health or 0
    local extra = 0
    local perks = {}
    local attackBonus = effect(self, 'fortifyattack')
    if Actor.spells(self)[settings.accuracySpell] then
        attackBonus = attackBonus - settings.accuracyBonus
    end
    attackBonus = math.max(0, attackBonus)

    local handHealth
    if data.handToHand then
        local handLevel = skill(self, 'handtohand')
        local handTier = rules.tier(cfg, handLevel)
        perks.knockdown = rules.roll(rules.knockdownChance(cfg, handTier, attribute(target, 'agility')))
        handHealth = rules.unarmedDamage(cfg, handTier, attribute(self, 'strength'))
        if data.knockedDown or perks.knockdown then
            handHealth = handHealth * (1 + (handTier.handToHandKnockdownDamageMultiplier or 0))
        end
        handHealth = rules.handArmorDamage(handHealth, data.armor, core.getGMST('fCombatArmorMinMult'))
        -- In the original this health hit is independent of the normal fist hit.
        -- Blind returns from onDamage without changing its incoming damage.
        if not (cfg.toggleAlwaysHit and rules.roll(effect(self, 'blind'))) then
            if cfg.toggleAlwaysHit then
                handHealth = rules.reduceDamage(cfg, handHealth, fatigueRatio(self), effect(target, 'sanctuary'),
                    attribute(target, 'agility'), attribute(target, 'luck'))
            end
            handHealth = handHealth + rules.coreBonus(cfg, handHealth, handLevel, attackBonus)
        else
            notify(self.object, 'Missed!')
        end
        if isPlayer then
            I.SkillProgression.skillUsed('handtohand', { skillGain = 1 })
        end
    end
    local blind = cfg.toggleAlwaysHit and damage > 0 and rules.roll(effect(self, 'blind'))
    if blind then notify(self.object, 'Missed!') end
    if cfg.toggleAlwaysHit and not blind then
        damage = rules.reduceDamage(cfg, damage, fatigueRatio(self), effect(target, 'sanctuary'),
            attribute(target, 'agility'), attribute(target, 'luck'))
    end

    if blind or not attack.successful then
        -- Preserve the original early return, which leaves base damage intact.
    elseif Creature.objectIsInstance(self) then
        damage = damage * (1 + attribute(self, 'strength') * cfg.creatureBonusModifier / 100
            + (cfg.toggleAlwaysHit and attackBonus * cfg.attackBonusModifier / 100 or 0))
    else
        extra = record and rules.coreBonus(cfg, damage, level, attackBonus) or 0
        if record and (cfg.toggleWeaponPerks or skillId == 'marksman') then
            if skillId == 'shortblade' then
                if rules.roll(tier.criticalStrikeChance) then
                    local bonus = damage * cfg.criticalStrikeMultiplier
                    extra = extra + bonus
                    notify(self.object, 'Critical strike!', bonus)
                end
                if data.healthRatio < cfg.executeThreshold then
                    extra = extra + damage * (tier.executeDamageMultiplier or 0)
                end
            elseif skillId == 'longblade' then
                local bonus, doubleStrike
                state.strikes, bonus, doubleStrike = rules.multistrike(cfg, tier, state.strikes, damage)
                extra = extra + bonus
                if bonus > 0 then notify(self.object, doubleStrike and 'Double strike!' or 'Multistrike!', bonus) end
                if state.riposteUntil > now then extra = extra + damage * cfg.riposteDamageMultiplier end
            elseif skillId == 'axe' then
                if rules.roll(tier.bleedChance) then
                    perks.bleed = { damage = damage * cfg.bleedMultiplier, maxStacks = tier.maxBleedStack or 1 }
                end
            elseif skillId == 'bluntweapon' then
                local armor = state.armorCache[target.id]
                if armor == nil then armor = data.armor; state.armorCache[target.id] = armor end
                extra = extra + damage * armor * (tier.bonusArmorDamageMultiplier or 0) / 100
                perks.stun = rules.roll(tier.stunChance)
            elseif skillId == 'spear' then
                if fatigueRatio(self) > fatigueRatio(target) then
                    extra = extra + damage * (tier.bonusDamageForFatigueMultiplier or 0)
                end
                if rules.roll(tier.adrenalineRushChance) then
                    Actor.spells(self):add('mic_adrenaline_rush')
                    state.rushExpiries[#state.rushExpiries + 1] = now + 3
                    notify(self.object, 'Adrenaline Rush!')
                end
            elseif weaponType == weaponTypes.MarksmanBow then
                local multiplier
                if isPlayer then multiplier = I.NGCPlayer.consumeBowBonus()
                else multiplier = tier.bowNPCDrawMultiplier end
                extra = extra + damage * (multiplier or 0)
                perks.hamstring = rules.roll(tier.hamstringChance)
            elseif weaponType == weaponTypes.MarksmanCrossbow then
                if (self.position - target.position):length() < cfg.crossbowCriticalRange then
                    extra = extra + damage * (tier.crossbowCriticalDamageMultiplier or 0)
                end
            elseif weaponType == weaponTypes.MarksmanThrown then
                extra = extra + damage * attribute(self, 'agility') * cfg.thrownAgilityModifier / 100
                if rules.roll(tier.thrownCriticalStrikeChance) then
                    local bonus = damage * cfg.criticalStrikeMultiplier
                    extra = extra + bonus
                    notify(self.object, 'Critical strike!', bonus)
                end
            end
        end
        damage = damage + extra
    end
    attack.damage.health = math.max(0, damage)

    recoverAttack(attack, target)
    target:sendEvent('NGCApplyAttack', { attack = attack, perks = perks, handHealth = handHealth })
end

local function applyAttack(data)
    if Actor.isDead(self) then return end
    local attack, perks = data.attack, data.perks
    local attacker = attack.attacker
    if attacker and not attacker:isValid() then attack.attacker = nil end
    local cfg = config.get()
    local now = core.getSimulationTime()
    if cfg.toggleWeaponPerks then
        if perks.bleed then
            local previous = state.bleed
            local stacks = previous and previous.expires > now and previous.stacks or 0
            if stacks < perks.bleed.maxStacks then
                stacks = stacks + 1
                state.bleed = { stacks = stacks, damage = perks.bleed.damage * stacks / 3,
                    remaining = 3, nextTick = now + 1, expires = now + 3 }
                notify(attacker, 'Bleeding!', perks.bleed.damage * stacks)
            end
        end
        if perks.stun and effect(self, 'paralyze') == 0 then
            Actor.activeEffects(self):set(1, 'paralyze')
            state.stunUntil = now + 1
            notify(attacker, 'Stunned!')
        end
        if perks.hamstring then
            state.hamstringUntil = now + 3
            updateHamstring(now, cfg)
            notify(attacker, 'Hamstrung!')
        end
        if perks.knockdown and cfg.toggleHandToHandPerks then
            if isPlayer then
                if state.playerFatigueSnapshot == nil then
                    state.playerFatigueSnapshot = Actor.stats.dynamic.fatigue(self).current
                    state.knockdownUntil = now + 2
                end
                Actor.stats.dynamic.fatigue(self).current = -100
            else
                state.knockdownUntil = now + 3
            end
            if not isPlayer and animation.hasGroup(self, 'knockdown') then
                I.AnimationController.playBlendedAnimation('knockdown', { priority = animation.PRIORITY.Knockdown })
            end
            notify(attacker, 'Knockdown!')
        end
        local equipped = Actor.getEquipment(self, Actor.EQUIPMENT_SLOT.CarriedRight)
        if equipped and Weapon.objectIsInstance(equipped) and skillByType[Weapon.record(equipped).type] == 'longblade'
            and rules.roll(rules.tier(cfg, skill(self, 'longblade')).riposteChance) then
            state.riposteUntil = now + cfg.riposteDuration
            notify(self.object, 'Riposte!')
        end
    end
    if data.handHealth ~= nil then
        scriptedHealthDamage(data.handHealth, attack.attacker)
    end
    if cfg.showDebugMessages then
        print(string.format('[NGC] Hit %s: %.2f health damage after armor, before difficulty; %.2f separate fist health damage',
            self.id, attack.damage.health or 0, data.handHealth or 0))
    end
    baseCombat.onHit(attack)
end

local function onUpdate(dt)
    if dt <= 0 then return end
    updateElapsed = updateElapsed + dt
    if updateElapsed < 0.25 then return end
    updateElapsed = 0
    local cfg = config.get()
    local now = core.getSimulationTime()
    synchronizeAccuracy(cfg.toggleAlwaysHit and not Actor.isDead(self))
    if trackingCombat and not isPlayer and I.AI then
        local player
        if not Actor.isDead(self) then
            for _, target in ipairs(I.AI.getTargets('Combat')) do
                if target:isValid() and types.Player.objectIsInstance(target) then player = target; break end
            end
        end
        if reportedInCombat ~= (player ~= nil) then
            reportedInCombat = player ~= nil
            core.sendGlobalEvent('NGCCombatState', { actor = self.object, player = player })
        end
    end
    if state.stunUntil > 0 and now >= state.stunUntil then
        Actor.activeEffects(self):set(0, 'paralyze')
        state.stunUntil = 0
    end
    if state.knockdownUntil > 0 and now >= state.knockdownUntil then
        if state.playerFatigueSnapshot ~= nil then
            Actor.stats.dynamic.fatigue(self).current = state.playerFatigueSnapshot
            state.playerFatigueSnapshot = nil
        elseif not Actor.isDead(self) and animation.hasGroup(self, 'idle') then
            I.AnimationController.playBlendedAnimation('idle', { priority = animation.PRIORITY.Default })
        end
        state.knockdownUntil = 0
    end
    for index = #state.rushExpiries, 1, -1 do
        if now >= state.rushExpiries[index] then
            Actor.spells(self):remove('mic_adrenaline_rush')
            table.remove(state.rushExpiries, index)
        end
    end
    if state.hamstringDelta ~= 0 or state.hamstringUntil > now then updateHamstring(now, cfg) end
    local bleed = state.bleed
    if not bleed then return end
    if not cfg.toggleWeaponPerks or Actor.isDead(self) then state.bleed = nil; return end
    local health = Actor.stats.dynamic.health(self)
    local remainingHealth = health.current
    while bleed.remaining > 0 and now >= bleed.nextTick do
        -- Like the original mod, bleed cannot deliver a killing blow.
        if remainingHealth > bleed.damage and not (isPlayer and require('openmw.debug').isGodMode()) then
            remainingHealth = remainingHealth - scriptedHealthDamage(bleed.damage)
        end
        bleed.remaining = bleed.remaining - 1
        bleed.nextTick = bleed.nextTick + 1
    end
    if bleed.remaining == 0 then state.bleed = nil end
end

-- Intercept before the handler chain. Calling the saved chain on completion
-- ensures other mods' hit handlers run exactly once despite the event round trip.
local combat = {}
for key, value in pairs(baseCombat) do combat[key] = value end
combat.applyArmor = function(attack)
    if not attack._ngcArmorApplied then baseCombat.applyArmor(attack) end
end
combat.adjustDamageForDifficulty = function(attack, defendant)
    if attack._ngcScriptHealth then
        -- Original applyHealthDamage(..., false, true, false): the helper health
        -- hit scales as a non-player attack even when the player threw the punch.
        baseCombat.adjustDamageForDifficulty({ damage = attack.damage }, defendant)
    else
        baseCombat.adjustDamageForDifficulty(attack, defendant)
    end
end
combat.onHit = function(attack)
    if onHit(attack) ~= false then baseCombat.onHit(attack) end
end

return {
    interfaceName = 'Combat',
    interface = combat,
    engineHandlers = {
        onUpdate = onUpdate,
        onSave = function() return state end,
        onLoad = function(saved)
            if saved then
                for key, value in pairs(saved) do state[key] = value end
            end
            updateElapsed = 0.25
            accuracyEnabled = nil
        end,
        onInactive = function()
            if trackingCombat then core.sendGlobalEvent('NGCParticipantInactive', { actor = self.object }) end
            trackingCombat, reportedInCombat = false, nil
            if state.stunUntil > 0 then Actor.activeEffects(self):set(0, 'paralyze'); state.stunUntil = 0 end
            if #state.rushExpiries > 0 then Actor.spells(self):remove('mic_adrenaline_rush'); state.rushExpiries = {} end
            removeHamstring()
            synchronizeAccuracy(false)
            state.bleed = nil
            state.strikes = nil
            state.riposteUntil = 0
        end,
        onActive = function() updateElapsed = 0.25; accuracyEnabled = nil end,
    },
    eventHandlers = {
        NGCResolveAttack = resolveAttack,
        NGCApplyAttack = applyAttack,
        NGCRecoverAttack = function(data) recoverAttack(data.attack, data.target) end,
        NGCCombatEnded = function()
            state.strikes = nil
            state.armorCache = {}
            state.bleed = nil
            if state.playerFatigueSnapshot == nil then state.knockdownUntil = 0 end
            trackingCombat, reportedInCombat = false, nil
        end,
    },
}
