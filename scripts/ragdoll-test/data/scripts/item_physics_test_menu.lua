local core = require('openmw.core')
local menu = require('openmw.menu')
local requested, loaded, savedAt = false, false, 0
return {
    eventHandlers = {
        NativeItemPhysics_Save = function()
            menu.saveGame('Native item physics regression', 'native-item-test')
            requested, savedAt = true, core.getRealTime()
        end,
    },
    engineHandlers = {
        onFrame = function()
            if not requested or loaded or core.getRealTime() - savedAt < 1 then return end
            local directory = menu.getCurrentSaveDir()
            if not directory then return end
            for slot, info in pairs(menu.getSaves(directory)) do
                if info.description == 'Native item physics regression' then
                    loaded = true
                    menu.loadGame(directory, slot)
                    return
                end
            end
        end,
    },
}
