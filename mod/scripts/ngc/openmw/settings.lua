local defaults = require('scripts.ngc.openmw.defaults')

local M = {
    page = 'NGC',
    section = 'SettingsNGC',
    accuracySpell = 'ngc_omw_accuracy',
    accuracyBonus = 10000,
    requiredContent = {
        'Next Generation Combat.esp',
        'Next Generation Combat - Hand To Hand Fixes.esp',
        'Next Generation Combat - OpenMW.esp',
    },
}

local labels = {
    toggleAlwaysHit = 'Always hit',
    toggleWeaponPerks = 'Weapon perks',
    toggleActiveBlocking = 'Active shield blocking',
    toggleHandToHandPerks = 'Hand-to-hand perks',
    toggleSkillGain = 'Adjust combat skill gains',
    showMessages = 'Show perk messages',
    showDamageNumbers = 'Include bonus damage in messages',
    showActiveBlockMessages = 'Show guard messages',
    showDebugMessages = 'Log combat damage',
    showSkillGainDebugMessages = 'Log combat skill gains',
    toggleActiveBlockingMouse2 = 'Use right mouse button for blocking',
    activeBlockKey = 'Block key (OpenMW key name)',
}

local function label(key)
    return labels[key] or (key:gsub('(%l)(%u)', '%1 %2'):gsub('^%l', string.upper))
end

local function makeGroup(key, name, values, order)
    local settings = {}
    local keys = {}
    for setting, value in pairs(values) do
        if type(value) == 'number' or type(value) == 'boolean' or type(value) == 'string' then
            keys[#keys + 1] = setting
        end
    end
    table.sort(keys)
    for _, setting in ipairs(keys) do
        local value = values[setting]
        settings[#settings + 1] = {
            key = setting,
            name = label(setting),
            default = value,
            renderer = type(value) == 'boolean' and 'checkbox' or (type(value) == 'string' and 'textLine' or 'number'),
            argument = type(value) == 'number' and { min = 0 } or nil,
        }
    end
    return {
        key = key, page = M.page, l10n = 'NGC', name = name,
        order = order, permanentStorage = false, settings = settings,
    }
end

function M.groups()
    local values = {}
    for key, value in pairs(defaults) do
        -- These are applied by the LOAD script before any save is opened.
        if key ~= 'toggleBalanceGMSTs' then
            values[key] = value
        end
    end
    local groups = { makeGroup(M.section, 'Combat settings', values, 0) }
    for index = 1, 4 do
        groups[#groups + 1] = makeGroup(M.section .. 'Tier' .. index,
            'Weapon tier ' .. index, defaults['weaponTier' .. index], index)
    end
    return groups
end

return M
