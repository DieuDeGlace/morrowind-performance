local content = require('openmw.content')
local core = require('openmw.core')
local defaults = require('scripts.ngc.openmw.defaults')
local settings = require('scripts.ngc.openmw.settings')

local function effect(id, magnitude, duration)
    return {
        id = id, magnitudeMin = magnitude, magnitudeMax = magnitude,
        duration = duration or 0, range = content.RANGE.Self,
    }
end

local function spell(id, name, effects, ability)
    content.spells.records[id] = {
        name = name, type = ability and content.spells.TYPE.Ability or content.spells.TYPE.Spell,
        cost = 0, alwaysSucceedFlag = true, effects = effects,
    }
end

local function onContentFilesLoaded()
    assert(core.API_REVISION >= 129, 'Next Generation Combat requires OpenMW Lua API 129 or newer')
    for _, filename in ipairs({ 'Next Generation Combat.esp', 'Next Generation Combat - Hand To Hand Fixes.esp' }) do
        assert(core.contentFiles.has(filename), 'Next Generation Combat requires the original ' .. filename .. ' enabled')
    end
    assert(content.spells.records.mic_adrenaline_rush, 'The original NGC Adrenaline Rush ability is missing')
    spell(settings.accuracySpell, 'NGC accuracy', {
        effect('fortifyattack', settings.accuracyBonus),
    }, true)

    local gmst = content.gameSettings.records
    if defaults.toggleHandToHandPerks then
        gmst.fMinHandToHandMult = 0.05
        gmst.fMaxHandToHandMult = 0.25
    end
    if defaults.toggleBalanceGMSTs then
        local names = {
            knockdownMult = 'fKnockDownMult', knockdownOddsMult = 'iKnockDownOddsMult',
            fatigueAttackMult = 'fFatigueAttackMult', fatigueAttackBase = 'fFatigueAttackBase',
            weaponFatigueMult = 'fWeaponFatigueMult', projectileMaxSpeed = 'fProjectileMaxSpeed',
            projectileMinSpeed = 'fProjectileMinSpeed', thrownWeaponMaxSpeed = 'fThrownWeaponMaxSpeed',
            thrownWeaponMinSpeed = 'fThrownWeaponMinSpeed',
        }
        for key, name in pairs(names) do
            gmst[name] = defaults.gmst[key]
        end
    end
    -- Recovery is resolved once by our hit handler, allowing per-actor thrown perks.
    gmst.fNGCProjectileStoreChance = gmst.fProjectileThrownStoreChance
    gmst.fProjectileThrownStoreChance = 0
end

return { engineHandlers = { onContentFilesLoaded = onContentFilesLoaded } }
