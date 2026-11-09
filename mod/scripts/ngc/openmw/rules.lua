-- Combat calculations have no engine dependencies, so they can be tested separately.
local M = {}

function M.clamp(value, minimum, maximum)
    return math.max(minimum, math.min(value, maximum))
end

function M.ratio(current, maximum)
    return maximum > 0 and current / maximum or 0
end

function M.tier(config, skill)
    for index = 4, 1, -1 do
        local tier = config['weaponTier' .. index]
        if skill >= tier.weaponSkillMin then
            return tier
        end
    end
    return {}
end

function M.roll(chance)
    return chance ~= nil and chance > 0 and math.random(100) <= chance
end

function M.reduceDamage(config, damage, attackerFatigue, sanctuary, agility, luck)
    local fatigueReduction = (1 - attackerFatigue) * config.fatigueReductionModifier
    local sanctuaryScale = math.max(0.1, (agility + luck - 30) * config.sanctuaryModifier / 100)
    local reduction = fatigueReduction + sanctuary * sanctuaryScale / 100
    return reduction > 0 and math.max(0, damage * (1 - reduction)) or damage
end

function M.coreBonus(config, damage, skill, attackBonus)
    local attackModifier = config.toggleAlwaysHit and attackBonus * config.attackBonusModifier or 0
    return damage * (skill * config.weaponSkillModifier + attackModifier) / 100
end

function M.knockdownChance(config, tier, agility)
    -- Preserve the original boundary behavior: agility >= 100 uses modifier 1.
    local modifier = 1
    if agility >= 30 and agility < 100 then modifier = (100 - agility) / 100 end
    modifier = math.max(config.agilityKnockdownChanceMinMod, modifier)
    return (tier.handToHandKnockdownChance or 0) * modifier
end

function M.unarmedDamage(config, tier, strength)
    local minimum = tier.handToHandBaseDamageMin or config.handToHandBaseDamageMin
    local maximum = tier.handToHandBaseDamageMax or config.handToHandBaseDamageMax
    -- Preserve NGC's base damage plus its strength-adjusted bonus.
    return math.random(minimum, maximum) * (1.5 + strength / 100)
end

function M.handArmorDamage(damage, armor, minimumMultiplier)
    if damage <= 0 then return 0 end
    return damage * math.max(minimumMultiplier, damage / (damage + armor))
end

function M.multistrike(config, tier, counter, damage)
    -- NGC initializes a new counter to zero and tests for exactly three in main.lua.
    -- Keep that behavior, including when the configurable threshold is changed.
    counter = counter == nil and 0 or (counter < config.multistrikeStrikesNeeded and counter + 1 or 0)
    if counter ~= 3 then
        return counter, 0, false
    end
    local doubleStrike = M.roll(tier.multistrikeBonusChance)
    local multiplier = doubleStrike and config.multistrikeBonuseDamageMultiplier
        or (tier.multistrikeDamageMultiplier or 1)
    return 0, damage * multiplier, doubleStrike
end

return M
