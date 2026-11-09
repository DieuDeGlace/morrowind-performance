-- Disable random native shield blocks so this fixture measures the Lua guard.
local content = require('openmw.content')
return {engineHandlers={onContentFilesLoaded=function()
    content.gameSettings.records.iBlockMinChance=0
    content.gameSettings.records.iBlockMaxChance=0
end}}
