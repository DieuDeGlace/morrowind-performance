local animation = require('openmw.animation')
local camera = require('openmw.camera')
local core = require('openmw.core')
local input = require('openmw.input')
local I = require('openmw.interfaces')
local self = require('openmw.self')
local types = require('openmw.types')
local ui = require('openmw.ui')
local util = require('openmw.util')
local config = require('scripts.ngc.openmw.config')
local rules = require('scripts.ngc.openmw.rules')
local settings = require('scripts.ngc.openmw.settings')

local Actor, NPC, Armor, Weapon = types.Actor, types.NPC, types.Armor, types.Weapon
local guarding, guardExhausted = false, false
local fullDraw, bowExhausted = false, false
local drawGroup
local drawElapsed, wasDrawing, bowBonus = 0, false, nil
local guardIcon
local previousFov, appliedFov
local guardTick, drawTick = 0, 0
local keyCodes = {}
for name, code in pairs(input.KEY) do keyCodes[name:upper()] = code end

I.Settings.registerPage({ key = settings.page, l10n = 'NGC', name = 'Next Generation Combat', description = 'Page description' })

local oneHanded = {
    [Weapon.TYPE.ShortBladeOneHand] = true, [Weapon.TYPE.LongBladeOneHand] = true,
    [Weapon.TYPE.BluntOneHand] = true, [Weapon.TYPE.AxeOneHand] = true,
}

local function equippedWeapon()
    local item = Actor.getEquipment(self, Actor.EQUIPMENT_SLOT.CarriedRight)
    if item and Weapon.objectIsInstance(item) then return item, Weapon.record(item) end
end

local function shield()
    local item = Actor.getEquipment(self, Actor.EQUIPMENT_SLOT.CarriedLeft)
    if not item or not Armor.objectIsInstance(item) or Armor.record(item).type ~= Armor.TYPE.Shield then return end
    if types.Item.itemData(item).condition <= 0 then return end
    local _, weapon = equippedWeapon()
    if weapon and not oneHanded[weapon.type] then return end
    return item
end

local function fatigueAvailable(minimum)
    local fatigue = Actor.stats.dynamic.fatigue(self)
    local maximum = math.max(0, fatigue.base + fatigue.modifier)
    return maximum > 0 and fatigue.current >= maximum * minimum
end

local function drainFatigue(fraction)
    local fatigue = Actor.stats.dynamic.fatigue(self)
    fatigue.current = fatigue.current - math.max(0, fatigue.base + fatigue.modifier) * fraction
end

local function setGuard(enabled)
    if guarding == enabled then return end
    guarding = enabled
    guardTick = 0
    if enabled then
        guardIcon = ui.create({ type = ui.TYPE.Image, layer = 'HUD', props = {
            resource = ui.texture({ path = 'icons/ngc/active_block.tga' }),
            size = util.vector2(32, 32), relativePosition = util.vector2(0, 1),
            position = util.vector2(160, -12), anchor = util.vector2(0, 1),
        } })
    elseif guardIcon then
        guardIcon:destroy()
        guardIcon = nil
    end
    if config.get().showActiveBlockMessages then ui.showMessage(enabled and 'Guard up!' or 'Guard down!') end
end

local function clearZoom()
    if previousFov and camera.getFieldOfView() == appliedFov then camera.setFieldOfView(previousFov) end
    previousFov, appliedFov = nil, nil
end

local function stopDraw()
    fullDraw = false
    drawTick = 0
    clearZoom()
end

local function controlsAllowed()
    return not core.isWorldPaused() and not I.UI.getMode() and not Actor.isDead(self)
        and types.Player.getControlSwitch(self, types.Player.CONTROL_SWITCH.Controls)
        and types.Player.getControlSwitch(self, types.Player.CONTROL_SWITCH.Fighting)
        and Actor.canMove(self) and not NPC.isWerewolf(self)
end

local function blockPressed(cfg)
    if cfg.toggleActiveBlockingMouse2 then return input.isMouseButtonPressed(3) end
    local key = keyCodes[cfg.activeBlockKey:upper()]
    return key ~= nil and input.isKeyPressed(key)
end

local function updateGuard(dt, cfg)
    local pressed = blockPressed(cfg)
    if not pressed then guardExhausted = false end
    local enabled = cfg.toggleActiveBlocking and pressed and not guardExhausted
        and Actor.getStance(self) == Actor.STANCE.Weapon and shield() ~= nil
        and fatigueAvailable(cfg.activeBlockingFatigueMin)
    setGuard(enabled)
    if not guarding then return end
    guardTick = guardTick + dt
    if guardTick >= 1 then
        guardTick = guardTick - 1
        local tier = rules.tier(cfg, NPC.stats.skills.block(self).modified)
        drainFatigue(tier.activeBlockingFatiguePercent or cfg.activeBlockingFatiguePercentBase)
        if not fatigueAvailable(cfg.activeBlockingFatigueMin) then
            guardExhausted = true
            setGuard(false)
        end
    end
end

local function updateBow(dt, cfg)
    local use = input.getBooleanActionValue('Use')
    if not use then bowExhausted = false end
    local _, weapon = equippedWeapon()
    local drawing = cfg.toggleWeaponPerks and weapon and weapon.type == Weapon.TYPE.MarksmanBow
        and Actor.getStance(self) == Actor.STANCE.Weapon and use and not bowExhausted and not guarding
    if not drawing then
        if wasDrawing then
            bowBonus = fullDraw and rules.tier(cfg, NPC.stats.skills.marksman(self).modified).bowFullDrawMultiplier or nil
        end
        wasDrawing = false
        drawElapsed = 0
        stopDraw()
        return
    end
    wasDrawing = true
    if not drawGroup or animation.getCurrentTime(self, drawGroup) < 0 then return end
    drawElapsed = drawElapsed + dt
    if drawElapsed < 3 then return end
    if not fullDraw then
        fullDraw = true
        drawTick = 0
        if cfg.showMessages then ui.showMessage('Full draw!') end
    else
        drawTick = drawTick + dt
    end
    if drawTick >= 1 then
        drawTick = drawTick - 1
        if not fatigueAvailable(cfg.fullDrawFatigueMin) then
            bowExhausted = true
            wasDrawing = false
            bowBonus = nil
            stopDraw()
            self.controls.use = self.ATTACK_TYPE.NoAttack
            Actor.setStance(self, Actor.STANCE.Nothing)
            return
        end
        drainFatigue(cfg.fullDrawFatigueDrainPercent)
    end
    if self.controls.movement < 0 then
        self.controls.movement = self.controls.movement * rules.clamp(cfg.fullDrawBackSpeedModifier, 0, 1)
    end
    if self.controls.sneak and cfg.bowZoomLevel > 0 then
        if not previousFov then previousFov = camera.getFieldOfView() end
        appliedFov = math.max(math.rad(5), previousFov / math.max(1, cfg.bowZoomLevel))
        camera.setFieldOfView(appliedFov)
    else
        clearZoom()
    end
end

local function tryBlock(attack)
    local cfg = config.get()
    if not guarding or not cfg.toggleActiveBlocking or not controlsAllowed()
        or attack.sourceType ~= I.Combat.ATTACK_SOURCE_TYPES.Melee
        or not shield() or not fatigueAvailable(cfg.activeBlockingFatigueMin) then
        return false
    end
    local difference = attack.attacker.position - self.position
    local horizontal = util.vector3(difference.x, difference.y, 0)
    local length = horizontal:length()
    local forward = self.rotation * util.vector3(0, 1, 0)
    local angle = math.deg(math.atan2(horizontal.x * forward.y - horizontal.y * forward.x, forward:dot(horizontal)))
    if length > 0 and (angle < core.getGMST('fCombatBlockLeftAngle') or angle > core.getGMST('fCombatBlockRightAngle')) then
        return false
    end
    local item = shield()
    local damage = attack.damage and ((attack.damage.health or 0) + (attack.damage.fatigue or 0)) or 0
    if damage > 0 then core.sendGlobalEvent('ModifyItemCondition', { actor = self.object, item = item, amount = -math.floor(damage) }) end
    local capacity = Actor.getCapacity(self)
    local encumbrance = rules.clamp(rules.ratio(Actor.getEncumbrance(self), capacity), 0, 1)
    local fatigueLoss = core.getGMST('fFatigueBlockBase') + encumbrance * core.getGMST('fFatigueBlockMult')
    if attack.weapon and attack.weapon:isValid() then
        fatigueLoss = fatigueLoss + Weapon.record(attack.weapon).weight * (attack.strength or 0)
            * core.getGMST('fWeaponFatigueBlockMult')
    end
    local fatigue = Actor.stats.dynamic.fatigue(self)
    fatigue.current = math.max(0, fatigue.current - fatigueLoss)
    I.SkillProgression.skillUsed('block', { useType = I.SkillProgression.SKILL_USE_TYPES.Block_Success })
    local armorSkill = I.Combat.getArmorSkill(item)
    local sounds = { lightarmor = 'Light Armor Hit', mediumarmor = 'Medium Armor Hit', heavyarmor = 'Heavy Armor Hit' }
    core.sound.playSound3d(sounds[armorSkill] or 'Light Armor Hit', self)
    if animation.hasGroup(self, 'shield') then
        I.AnimationController.playBlendedAnimation('shield', { priority = animation.PRIORITY.Block, blendMask = animation.BLEND_MASK.LeftArm })
    end
    return true
end

I.AnimationController.addPlayBlendedAnimationHandler(function(_, options)
    local _, weapon = equippedWeapon()
    if not weapon then return end
    if weapon.type == Weapon.TYPE.MarksmanCrossbow and options.startKey == 'shoot follow start'
        and config.get().toggleWeaponPerks then
        local cfg = config.get()
        if rules.roll(rules.tier(cfg, NPC.stats.skills.marksman(self).modified).repeaterChance) then
            options.speed = (options.speed or 1) * cfg.repeaterSpeedMultiplier / math.max(0.0001, weapon.speed or 1)
            if cfg.showMessages then ui.showMessage('Repeater!') end
        end
    end
end)

I.AnimationController.addPlayBlendedAnimationHandler(function(group, options)
    if options.stopKey == 'shoot max attack' then drawGroup = group end
end)

local weaponSkills = { block = true, bluntweapon = true, longblade = true, axe = true, spear = true,
    shortblade = true, handtohand = true }
local armorSkills = { lightarmor = true, mediumarmor = true, heavyarmor = true, unarmored = true }

I.SkillProgression.addSkillUsedHandler(function(skillId, params)
    local cfg = config.get()
    if not cfg.toggleSkillGain then return end
    local multiplier
    if weaponSkills[skillId] then
        local tier = rules.tier(cfg, NPC.stats.skills[skillId](self).base)
        multiplier = tier.weaponSkillGainModifier or cfg.weaponSkillGainBaseModifier
    elseif armorSkills[skillId] then
        multiplier = cfg.armorSkillGainBaseModifier
    end
    if multiplier then
        if cfg.showSkillGainDebugMessages then print(string.format('[NGC] %s skill gain: %.3f -> %.3f', skillId, params.skillGain, params.skillGain * multiplier)) end
        params.skillGain = params.skillGain * multiplier
    end
end)

local function reset()
    setGuard(false)
    stopDraw()
    guardExhausted, bowExhausted = false, false
    drawGroup = nil
    drawElapsed, wasDrawing, bowBonus = 0, false, nil
end

return {
    interfaceName = 'NGCPlayer',
    interface = { version = 2, tryBlock = tryBlock,
        consumeBowBonus = function()
            local bonus = bowBonus
            bowBonus = nil
            return bonus
        end,
    },
    engineHandlers = {
        onFrame = function(dt)
            if not controlsAllowed() then reset(); return end
            local cfg = config.get()
            updateGuard(dt, cfg)
            updateBow(dt, cfg)
        end,
        onLoad = reset,
        onTeleported = reset,
    },
    eventHandlers = {
        NGCNotify = function(data)
            local cfg = config.get()
            if not cfg.showMessages then return end
            local message = data.message
            if cfg.showDamageNumbers and data.bonus then message = string.format('%s Bonus damage: %.1f', message, data.bonus) end
            ui.showMessage(message)
        end,
    },
}
