local camera = require('openmw.camera')
local controls = require('openmw.interfaces').Controls
local nearby = require('openmw.nearby')
local self = require('openmw.self')
local types = require('openmw.types')
local util = require('openmw.util')

local target
local dragAimTime
local stableAimTime
local swingTime
local nextStrike = 0
local chargeTime = .58

local function aimAt(point)
    local direction = (point - camera.getPosition()):normalize()
    self.controls.yawChange = util.normalizeAngle(math.atan2(direction.x, direction.y) - self.rotation:getYaw())
    self.controls.pitchChange = util.normalizeAngle(math.asin(-direction.z) - self.rotation:getPitch())
end

local function findBody()
    local position = target.position
    for z = 8, 36, 7 do
        for y = -35, 45, 10 do
            for x = -35, 35, 10 do
                local point = position + util.vector3(x, y, z)
                local direction = (point - camera.getPosition()):normalize()
                local hit = nearby.castRenderingRay(camera.getPosition(), point + direction * 8, { ignore = self })
                if hit.hitObject == target then return point end
            end
        end
    end
    return position + util.vector3(0, 0, 14)
end

return {
    eventHandlers = {
        RagdollTest_Look = function(npc)
            aimAt(npc.position + util.vector3(0, 0, 22))
        end,
        RagdollTest_Drag = function(npc)
            target = npc
            dragAimTime = 0
            stableAimTime = 0
            camera.setMode(camera.MODE.FirstPerson)
        end,
        RagdollTest_Strike = function(attack)
            target = attack.target
            swingTime = 0
            nextStrike = 0
            chargeTime = attack.chargeTime or .58
            camera.setMode(camera.MODE.FirstPerson)
            controls.overrideCombatControls(true)
            if attack.weapon then
                local weapon = assert(types.Actor.inventory(self):find(attack.weapon), 'Test weapon missing')
                types.Actor.setEquipment(self, { [types.Actor.EQUIPMENT_SLOT.CarriedRight] = weapon })
            end
            types.Actor.setStance(self, types.Actor.STANCE.Weapon)
        end,
    },
    engineHandlers = {
        onFrame = function(dt)
            if not target or not target:isValid() or not target.cell then return end
            if dragAimTime then
                local focus = camera.getFocusRay().hitObject
                dragAimTime = dragAimTime + dt
                if focus == target then
                    stableAimTime = stableAimTime + dt
                else
                    stableAimTime = 0
                    local direction = (findBody() - camera.getPosition()):normalize()
                    camera.setYaw(math.atan2(direction.x, direction.y))
                    camera.setPitch(math.asin(-direction.z))
                end
                if stableAimTime >= .55 then
                    print('RAGDOLL_TEST_DRAG_READY')
                    dragAimTime = nil
                    target = nil
                elseif dragAimTime >= 5 then
                    print('RAGDOLL_TEST_FAIL could not aim at corpse', focus, target)
                    dragAimTime = nil
                    target = nil
                end
                return
            end
            swingTime = swingTime + dt
            if swingTime >= 6 then
                self.controls.use = self.ATTACK_TYPE.NoAttack
                controls.overrideCombatControls(false)
                target = nil
                return
            end
            if swingTime >= nextStrike then
                aimAt(findBody())
                nextStrike = swingTime + chargeTime + .72
            end
            self.controls.use = nextStrike - swingTime > .72 and self.ATTACK_TYPE.Any or self.ATTACK_TYPE.NoAttack
        end,
    },
}
