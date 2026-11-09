-- Validate content once per actor context, avoiding repeated errors after LOAD fails.
local core = require('openmw.core')
local settings = require('scripts.ngc.openmw.settings')
local ready = core.API_REVISION >= 129
    and core.magic.spells.records[settings.accuracySpell] ~= nil
for _, filename in ipairs(settings.requiredContent) do
    ready = ready and core.contentFiles.has(filename)
end
return { ready = ready }
