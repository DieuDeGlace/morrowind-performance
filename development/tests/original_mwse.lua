-- Runs the bundled, unmodified MWSE main/perk scripts with a small engine double.
-- The original callbacks supply expected values; this file contains no NGC formulas.
return function(config, source, target, engine)
    local function copy(value)
        if type(value) ~= 'table' then return value end
        local result = {}
        for key, child in pairs(value) do result[key] = copy(child) end
        return result
    end
    local cfg = copy(config)
    cfg.activeBlockKey = { keyCode = 44 }
    cfg.nonStandardAttackKey = {}
    cfg.toggleActiveBlocking = false
    local callbacks, modules, timers = {}, {}, {}
    local clock, spells, experience = 0, {}, 0
    local env = setmetatable({}, { __index = _G })
    local function load(name)
        local path = developmentRoot .. '/MWSE/mods/' .. name:gsub('%.', '/') .. '.lua'
        return setfenv(assert(loadfile(path)), env)()
    end
    env.require = function(name)
        if modules[name] == nil then modules[name] = load(name) end
        return modules[name]
    end
    env.event = { register = function(name, fn)
        callbacks[name] = callbacks[name] or {}
        callbacks[name][#callbacks[name] + 1] = fn
    end }
    local function emit(name, data)
        for _, callback in ipairs(callbacks[name] or {}) do callback(data) end
    end
    env.timer = { expired = 'expired', active = 'active', start = function(options)
        local timer = { state = 'active', due = clock + options.duration, options = options, left = options.iterations or 1 }
        function timer:cancel() self.state = 'expired' end
        function timer:reset() self.due = clock + self.options.duration end
        timers[#timers + 1] = timer
        return timer
    end }
    env.mge = { enableZoom = function() end, setZoom = function() end }
    env.mwse = { log = function() end }
    env.mwscript = { getSpellEffects = function() return false end,
        addSpell = function(data) spells[data.spell] = true end,
        removeSpell = function(data) spells[data.spell] = nil end,
    }
    env.math = setmetatable({ round = function(value, places)
        local scale = 10 ^ places
        return math.floor(value * scale + 0.5) / scale
    end }, { __index = math })
    local types = { ShortBladeOneHand = 0, LongBladeOneHand = 1, LongBladeTwoHand = 2,
        BluntOneHand = 3, BluntTwoClose = 4, BluntTwoWide = 5, SpearTwoWide = 6,
        AxeOneHand = 7, AxeTwoHand = 8, MarksmanBow = 9, MarksmanCrossbow = 10, MarksmanThrown = 11 }
    local skillIndices = { block = 0, bluntweapon = 4, longblade = 5, axe = 6, spear = 7,
        lightarmor = 21, shortblade = 22, marksman = 23, handtohand = 26 }
    local function mobile(actor)
        local ref = { id = actor.id, position = { distance = function(_, other)
            return (actor.position - other.actor.position):length()
        end } }
        ref.position.actor = actor
        local result = { reference = ref, actorType = actor.creature and 0 or 1, werewolf = actor.werewolf or false,
            blind = actor.effects.blind or 0, sanctuary = actor.effects.sanctuary or 0,
            attackBonus = actor.effects.fortifyattack or 0, paralyze = actor.effects.paralyze or 0, skills = {},
            health = { current = actor.dynamic.health.current, base = actor.dynamic.health.base },
            fatigue = { current = math.max(0, actor.dynamic.fatigue.current), base = actor.dynamic.fatigue.base },
        }
        ref.mobile = result
        for id, stat in pairs(actor.attributes) do result[id] = { current = stat.modified, base = stat.base } end
        for id, stat in pairs(actor.skills) do
            result[id] = { current = stat.modified, base = stat.base }
            if skillIndices[id] then result.skills[skillIndices[id] + 1] = result[id] end
        end
        result.handToHand, result.longBlade = result.handtohand, result.longblade
        result.shortBlade, result.bluntWeapon = result.shortblade, result.bluntweapon
        if actor.equipment.CarriedLeft then result.readiedShield = {} end
        result.actionData = { attackSwing = 1 }
        local weapon = actor.equipment.CarriedRight
        if weapon then result.readiedWeapon = { object = { type = types[weapon.record.type] } } end
        function result:exerciseSkill(skillId, progress)
            local event = { skill = skillId, progress = progress }
            emit('exerciseSkill', event)
            experience = experience + event.progress
        end
        function result:applyHealthDamage(damage)
            local event = { damage = damage, reference = ref, mobile = self }
            emit('damage', event)
            self.health.current = self.health.current - event.damage
        end
        return result, ref
    end
    local attacker, sourceRef = mobile(source)
    local defender, targetRef = mobile(target)
    env.tes3 = {
        player = source.player and sourceRef or (target.player and targetRef or { id = 'other-player' }),
        mobilePlayer = source.player and attacker or defender,
        skill = { handToHand = 26 },
        isModActive = function() return true end, messageBox = function() end,
        playAnimation = function() end, menuMode = function() return false end,
        findGMST = function(name) return { value = engine.gmst[name] or 1 } end,
        setStatistic = function(data) data.reference.mobile[data.name].current = data.current end,
    }
    local common = load('ngc.common')
    common.config = cfg
    common.loadConfig = function() end
    common.updateEnemyHealthBar = function() end
    common.getARforTarget = function()
        local armor = 0
        for _, item in pairs(target.equipment) do
            if item.kind == 'armor' then armor = armor + item.record.baseArmor end
        end
        return armor
    end
    modules['ngc.common'] = common
    modules['ngc.mcm'] = { registerModConfig = function() end }
    load('ngc.main')
    emit('initialized')
    return {
        source = attacker, target = defender, spells = spells,
        experience = function() return experience end,
        guard = function() modules['ngc.block'].keyPressed({keyCode=44}) end,
        bowStart = function() modules['ngc.perks.bow'].attackPressed({}) end,
        attack = function()
            emit('attack', { reference = sourceRef, mobile = attacker, targetReference = targetRef, targetMobile = defender })
        end,
        damage = function(damage)
            local event = { reference = targetRef, mobile = defender, attackerReference = sourceRef,
                attacker = attacker, damage = damage, source = 'attack' }
            emit('damage', event)
            defender.health.current = defender.health.current - event.damage
            return event.damage
        end,
        advance = function(dt)
            clock = clock + dt
            for _, timer in ipairs(timers) do
                while timer.state == 'active' and timer.due <= clock do
                    timer.options.callback()
                    timer.left = timer.left - 1
                    timer.due = timer.due + timer.options.duration
                    if timer.left <= 0 then timer.state = 'expired' end
                end
            end
        end,
    }
end
