local content = require('openmw.content')
local core = require('openmw.core')
local defaults = require('scripts.ngc.openmw.defaults')
local settings = require('scripts.ngc.openmw.settings')

local function onContentFilesLoaded()
    assert(core.API_REVISION >= 129, 'Next Generation Combat requires OpenMW Lua API 129 or newer')
    for _, filename in ipairs(settings.requiredContent) do
        assert(core.contentFiles.has(filename), 'Next Generation Combat requires ' .. filename .. ' enabled')
    end
    assert(content.spells.records.mic_adrenaline_rush, 'The original NGC Adrenaline Rush ability is missing')
    assert(content.spells.records[settings.accuracySpell], 'The OpenMW companion ESP accuracy ability is missing')

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
