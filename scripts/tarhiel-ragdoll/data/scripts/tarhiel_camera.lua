local camera = require('openmw.camera')
local debug = require('openmw.debug')
local capture
local nextImage = 1
local imageTimes = { 0.25, 3, 8, 15 }
return {
    eventHandlers = {
        TarhielTest_Camera = function()
            camera.setYaw(math.pi / 2)
            camera.setPitch(0.35)
        end,
        TarhielTest_Capture = function()
            camera.setPitch(0.60)
            capture = 0
        end,
    },
    engineHandlers = {
        onFrame = function(dt)
            if capture == nil or nextImage > #imageTimes then return end
            capture = capture + dt
            if capture >= imageTimes[nextImage] then
                debug.takeScreenshot()
                nextImage = nextImage + 1
            end
        end,
    },
}
