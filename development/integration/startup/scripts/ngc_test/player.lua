local I = require('openmw.interfaces')
local core = require('openmw.core')
local self = require('openmw.self')
local types = require('openmw.types')
local mechanics = require('scripts.ngc.openmw.mechanics')
local Actor = types.Actor
local elapsed, done, prepared = 0, false, false
I.Controls.overrideMovementControls(true)
local function test()
    assert(core.magic.spells.records.ngc_omw_accuracy, 'Content helper was not registered')
    if mechanics.native then
        assert(self.controls.hitChanceOverride == 100, 'Native Always Hit was not enabled')
        assert(not Actor.spells(self).ngc_omw_accuracy, 'Native mode retained the accuracy spell')
        for _, name in ipairs({'hitChanceOverride', 'blockChanceOverride'}) do
            local saved = self.controls[name]
            self.controls[name] = 0
            assert(self.controls[name] == 0)
            self.controls[name] = nil
            assert(self.controls[name] == nil)
            for _, bad in ipairs({-1, 101, math.huge, 0/0}) do
                assert(not pcall(function() self.controls[name] = bad end), name .. ' accepted an invalid value')
            end
            self.controls[name] = saved
        end
        local base = Actor.getCurrentSpeed(self)
        assert(base > 0, 'Speed fixture needs a movable player')
        self.controls.movementSpeedMultiplier = 0.5
        local slowed = Actor.getCurrentSpeed(self)
        assert(math.abs(slowed / base - 0.5) < 0.0001, 'Final native movement speed was not scaled')
        self.controls.movementSpeedMultiplier = 1
        for _, bad in ipairs({-1, 1001, math.huge, 0/0}) do
            assert(not pcall(function() self.controls.movementSpeedMultiplier = bad end))
        end
        print('NGC_NATIVE_BINDINGS_PASS', base, slowed)
    else
        assert(Actor.spells(self).ngc_omw_accuracy, 'Portable Always Hit helper missing')
        print('NGC_PORTABLE_STARTUP_PASS')
    end
    core.sendGlobalEvent('NGCTestReady', {native = mechanics.native})
end
return {engineHandlers = {onUpdate = function(dt)
    if done then return end
    if not prepared then
        Actor.stats.attributes.strength(self).base=100
        Actor.stats.attributes.speed(self).base=50
        Actor.stats.dynamic.fatigue(self).base=100
        Actor.stats.dynamic.fatigue(self).current=100
        prepared=true
    end
    elapsed = elapsed + dt
    if elapsed < 5 then return end
    done = true
    local ok, err = pcall(test)
    if not ok then print('NGC_RUNTIME_FAIL', err); core.quit() end
end, onFrame=function()
    self.controls.movement=1
    self.controls.sideMovement=0
end}}
