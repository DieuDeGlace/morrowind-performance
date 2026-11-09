local core = require('openmw.core')
local elapsed = 0
return {
    engineHandlers = {onUpdate = function(dt)
        elapsed = elapsed + dt
        if elapsed > 30 then print('NGC_RUNTIME_FAIL timeout'); core.quit() end
    end},
    eventHandlers = {NGCTestReady = function(data)
        print('NGC_RUNTIME_PASS', core.API_REVISION, core.COMBAT_API_VERSION or 0, data.native)
        core.quit()
    end},
}
