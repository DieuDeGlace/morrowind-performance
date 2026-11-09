local async = require('openmw.async')
local storage = require('openmw.storage')
local defaults = require('scripts.ngc.openmw.defaults')
local settings = require('scripts.ngc.openmw.settings')

local config = {}
local groups = settings.groups()

local function copy(source)
    local result = {}
    for key, value in pairs(source) do
        result[key] = type(value) == 'table' and copy(value) or value
    end
    return result
end

local function refresh()
    config = copy(defaults)
    for index, group in ipairs(groups) do
        local section = storage.globalSection(group.key)
        local target = index == 1 and config or config['weaponTier' .. (index - 1)]
        for _, setting in ipairs(group.settings) do
            local value = section:get(setting.key)
            if type(value) == type(setting.default) then
                if type(value) ~= 'number' or (value == value and value >= 0 and value < math.huge) then
                    target[setting.key] = value
                end
            end
        end
    end
    config.multistrikeStrikesNeeded = math.max(1, math.floor(config.multistrikeStrikesNeeded))
    for index = 0, 4 do
        local tier = index == 0 and config or config['weaponTier' .. index]
        tier.handToHandBaseDamageMin = math.floor(tier.handToHandBaseDamageMin)
        tier.handToHandBaseDamageMax = math.max(tier.handToHandBaseDamageMin, math.floor(tier.handToHandBaseDamageMax))
    end
end

for _, group in ipairs(groups) do
    storage.globalSection(group.key):subscribe(async:callback(refresh))
end
refresh()

return { get = function() return config end }
