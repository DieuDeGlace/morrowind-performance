-- Engine doubles enforce separate script environments and asynchronous actor events.
-- Optional upstream files exercise the actual 0.51 Combat chain, not a reimplementation.
local passed = 0
local function equal(actual, expected)
    assert(actual == expected, string.format('Expected %s, got %s', tostring(expected), tostring(actual)))
end
local function near(actual, expected)
    assert(math.abs(actual - expected) < 0.00001, string.format('Expected %.6f, got %.6f', expected, actual))
end
local function test(name, fn)
    local ok, err = pcall(fn)
    if not ok then error(name .. ': ' .. tostring(err), 0) end
    passed = passed + 1
    print('PASS ' .. name)
end
local function readModule(name, env)
    return assert(setfenv(assert(loadfile(projectRoot .. '/' .. name:gsub('%.', '/') .. '.lua')), env))()
end

local vectorMeta = {}
local function vector(x, y, z) return setmetatable({ x = x, y = y, z = z }, vectorMeta) end
function vectorMeta.__sub(a, b) return vector(a.x - b.x, a.y - b.y, a.z - b.z) end
function vectorMeta.__index(v, key)
    if key == 'length' then return function() return math.sqrt(v.x * v.x + v.y * v.y + v.z * v.z) end end
    if key == 'dot' then return function(_, b) return v.x * b.x + v.y * b.y + v.z * b.z end end
end
local function stat(base)
    return setmetatable({ base = base, current = base, modifier = 0, damage = 0 }, {
        __index = function(s, key) if key == 'modified' then return s.base + s.modifier - s.damage end end,
    })
end
local function enum(names)
    local result = {}
    for name in names:gmatch('%S+') do result[name] = name end
    return result
end

local function game(options)
    local g = { now = 0, queue = {}, sections = {}, actors = {}, records = {}, spells = {},
        messages = {}, hits = {}, keys = {}, use = false, fov = math.rad(70), skills = {},
        gmst = { fCombatArmorMinMult = 0.1, iBaseArmorSkill = 30, fUnarmoredBase1 = 0.1,
            fUnarmoredBase2 = 0.1, fDifficultyMult = 5, fProjectileThrownStoreChance = 25,
            fCombatBlockLeftAngle = -60, fCombatBlockRightAngle = 60 },
    }
    for id, record in pairs(espSpellRecords) do g.spells[id] = record end
    g.content = {}
    for name, enabled in pairs(espContentNames) do g.content[name] = enabled end
    if options and options.missingESP then g.content[options.missingESP] = false end
    local types = {}
    local Actor = { EQUIPMENT_SLOT = enum('Boots Cuirass Greaves Helmet LeftGauntlet LeftPauldron RightGauntlet RightPauldron CarriedLeft CarriedRight Ammunition'),
        STANCE = enum('Nothing Weapon Spell') }
    types.Actor = Actor
    types.Weapon = { TYPE = enum('ShortBladeOneHand LongBladeOneHand LongBladeTwoHand BluntOneHand BluntTwoClose BluntTwoWide SpearTwoWide AxeOneHand AxeTwoHand MarksmanBow MarksmanCrossbow MarksmanThrown Arrow Bolt'), records = g.records }
    types.Armor = { TYPE = enum('Boots Cuirass Greaves Helmet LBracer LGauntlet LPauldron RBracer RGauntlet RPauldron Shield') }
    types.NPC, types.Player, types.Creature = {}, {}, {}
    local function isActor(a) return a and a.kind == 'actor' end
    Actor.objectIsInstance = isActor
    types.Player.objectIsInstance = function(a) return isActor(a) and a.player end
    types.Creature.objectIsInstance = function(a) return isActor(a) and a.creature end
    types.NPC.objectIsInstance = function(a) return isActor(a) and not a.creature end
    types.NPC.isWerewolf = function(a) return a.werewolf or false end
    types.Player.CONTROL_SWITCH = enum('Controls Fighting')
    types.Player.getControlSwitch = function() return true end
    types.Weapon.objectIsInstance = function(a) return a.kind == 'weapon' end
    types.Armor.objectIsInstance = function(a) return a.kind == 'armor' end
    types.Weapon.record = function(a) return type(a) == 'string' and g.records[a] or a.record end
    types.Armor.record = function(a) return a.record end
    types.Creature.record = function(a) return { combatSkill = 50, magicSkill = 50, stealthSkill = 50, bloodType = 0 } end
    types.Item = { itemData = function(item) return item.data end }
    local function selectors(container)
        return setmetatable({}, { __index = function(_, name) return function(a) return a[container][name] end end })
    end
    Actor.stats = { attributes = selectors('attributes'), dynamic = selectors('dynamic') }
    types.NPC.stats = { skills = selectors('skills') }
    Actor.isDead = function(a) return a.dynamic.health.current <= 0 end
    Actor.getStance = function(a) return a.stance end
    Actor.getCapacity = function() return 250 end
    Actor.getEncumbrance = function() return 0 end
    Actor.setStance = function(a, stance) a.stance = stance end
    Actor.getEquipment = function(a, slot) return slot and a.equipment[slot] or a.equipment end
    Actor.spells = function(a) return a.spells end
    Actor.inventory = function(a) return a.inventory end
    Actor.activeEffects = function(a)
        return { set = function(_, value, id) a.effects[id] = value end,
            getEffect = function(_, id)
            local value = a.effects[id] or 0
            for spellId in pairs(a.spells) do
                local record = g.spells[spellId]
                if record then
                    for _, effect in ipairs(record.effects) do if effect.id == id then value = value + effect.magnitudeMin end end
                end
            end
            for _, active in pairs(a.activeSpells) do
                if active.expires > g.now then
                    for _, effect in ipairs(g.spells[active.id].effects) do if effect.id == id then value = value + effect.magnitudeMin end end
                end
            end
            return { magnitude = value }
        end }
    end
    Actor.activeSpells = function(a)
        return { add = function(_, options)
            local duration = g.spells[options.id].effects[1].duration
            a.activeSpells[options.id] = { id = options.id, expires = g.now + duration }
        end }
    end
    Actor.canMove = function(a) return not Actor.isDead(a) and a.dynamic.fatigue.current >= 0
        and Actor.activeEffects(a):getEffect('paralyze').magnitude == 0 end
    Actor._onHit = function(a, attack)
        g.hits[#g.hits + 1] = attack
        if attack.successful and not (a.player and g.godMode) then
            for name, damage in pairs(attack.damage or {}) do a.dynamic[name].current = a.dynamic[name].current - damage end
        end
    end
    g.types = types
    local core = {
        API_REVISION = 129, getSimulationTime = function() return g.now end,
        isWorldPaused = function() return g.paused or false end, getGameDifficulty = function() return g.difficulty or 0 end,
        contentFiles = { has = function(name) return g.content[name] or false end },
        getGMST = function(name) return g.gmst[name] or 1 end,
        stats = { Skill = { record = function() return { specialization = 'combat' } end } },
        magic = { EFFECT_TYPE = enum('Shield') },
        sound = { playSound3d = function() end },
        sendGlobalEvent = function(name, data) g.queue[#g.queue + 1] = { global = true, name = name, data = data } end,
    }
    local function section(name)
        if g.sections[name] then return g.sections[name] end
        local result = { values = {}, callbacks = {} }
        function result:get(key) return self.values[key] end
        function result:subscribe(callback) self.callbacks[#self.callbacks + 1] = callback end
        function result:set(key, value)
            self.values[key] = value
            for _, callback in ipairs(self.callbacks) do callback(name, key) end
        end
        g.sections[name] = result
        return result
    end
    function g:set(key, value, tier) section('SettingsNGC' .. (tier and 'Tier' .. tier or '')):set(key, value) end
    local baseModules = {
        ['openmw.types'] = types, ['openmw.core'] = core,
        ['openmw.storage'] = { globalSection = section },
        ['openmw.async'] = { callback = function(_, fn) return fn end },
        ['openmw.util'] = { vector3 = vector, vector2 = function(x, y) return {x = x, y = y} end,
            makeStrictReadOnly = function(t) return t end },
        ['openmw_aux.util'] = { shallowCopy = function(t) local c = {}; for k,v in pairs(t) do c[k]=v end; return c end,
            callEventHandlers = function(handlers, ...) for i = #handlers, 1, -1 do if handlers[i](...) == false then return true end end end },
        ['openmw.debug'] = { isGodMode = function() return g.godMode or false end },
        ['openmw.content'] = { RANGE = { Self = 0 }, spells = { records = g.spells, TYPE = enum('Ability Spell') }, gameSettings = { records = g.gmst } },
        ['openmw.world'] = { createObject = function(id, count)
            return { moveInto = function(_, a) a.inventory[id] = (a.inventory[id] or 0) + count end }
        end },
    }
    function g:context(actor)
        local env = setmetatable({}, { __index = _G })
        local modules = {}
        for name, value in pairs(baseModules) do modules[name] = value end
        local I = { Settings = { registerGroup = function(group)
            for _, setting in ipairs(group.settings) do section(group.key).values[setting.key] = setting.default end
        end, registerPage = function() end } }
        local ctx = { env = env, I = I, scripts = {}, animationHandlers = {}, usedSkills = {} }
        modules['openmw.interfaces'] = I
        modules['openmw.self'] = actor
        modules['openmw.animation'] = {
            PRIORITY = enum('Knockdown Block Default'), BLEND_MASK = enum('LeftArm'),
            hasGroup = function() return true end,
            getTextKeyTime = function() return 1 end,
            getCurrentTime = function() return g.animationTime or -1 end,
        }
        modules['openmw.camera'] = { getFieldOfView = function() return g.fov end, setFieldOfView = function(value) g.fov = value end }
        modules['openmw.input'] = { KEY = { Z = 44, Space = 32 },
            isKeyPressed = function(code) return g.keys[code] or false end,
            isMouseButtonPressed = function(code) return g.mouse == code end,
            getBooleanActionValue = function() return g.use end,
        }
        modules['openmw.ui'] = { showMessage = function(message) g.messages[#g.messages + 1] = message end,
            TYPE = { Image = 'Image' }, texture = function(options) return options end,
            create = function(layout)
                g.lastIcon = layout
                return { destroy = function() g.lastIcon = nil end }
            end,
        }
        I.UI = { getMode = function() return g.mode end }
        if actor and not actor.player then I.AI = { getTargets = function() return actor.combatTargets or {} end } end
        I.AnimationController = {
            addPlayBlendedAnimationHandler = function(fn) ctx.animationHandlers[#ctx.animationHandlers + 1] = fn end,
            playBlendedAnimation = function() end,
        }
        I.SkillProgression = {
            SKILL_USE_TYPES = enum('Armor_HitByOpponent Block_Success'),
            addSkillUsedHandler = function(fn) ctx.skillHandler = fn end,
            skillUsed = function(name, options)
                options.skillGain = options.skillGain or 1
                if ctx.skillHandler then ctx.skillHandler(name, options) end
                ctx.usedSkills[#ctx.usedSkills + 1] = { name = name, gain = options.skillGain }
            end,
        }
        function env.require(name)
            if modules[name] ~= nil then return modules[name] end
            local result = readModule(name, env)
            modules[name] = result
            return result
        end
        function ctx:load(name)
            local script = env.require(name)
            self.scripts[#self.scripts + 1] = script
            if script.interfaceName then I[script.interfaceName] = script.interface end
            return script
        end
        function ctx:engine(name, ...)
            for _, script in ipairs(self.scripts) do
                local handler = script.engineHandlers and script.engineHandlers[name]
                if handler then handler(...) end
            end
        end
        function ctx:event(name, data)
            for _, script in ipairs(self.scripts) do
                local handler = script.eventHandlers and script.eventHandlers[name]
                if handler then handler(data) end
            end
        end
        function ctx:animation(group, options)
            for _, handler in ipairs(self.animationHandlers) do handler(group, options) end
        end
        return ctx
    end
    local loadContext = g:context()
    loadContext:load('scripts.ngc.openmw.load')
    loadContext:engine('onContentFilesLoaded')
    g.global = g:context()
    g.global:load('scripts.ngc.openmw.global')
    function g:actor(id, player, creature)
        local a = { id = id, kind = 'actor', player = player or false, creature = creature,
            attributes = {}, dynamic = { health = stat(1000), fatigue = stat(100), magicka = stat(100) },
            skills = {}, effects = {}, activeSpells = {}, equipment = {}, inventory = {},
            stance = Actor.STANCE.Weapon, position = vector(0, 100, 0), controls = { movement = 0, sideMovement = 0 },
            ATTACK_TYPE = enum('NoAttack Any') }
        a.object = a
        a.rotation = setmetatable({}, { __mul = function(_, v) return v end })
        function a:isValid() return not self.invalid end
        function a:sendEvent(name, data) g.queue[#g.queue + 1] = { target = self, name = name, data = data } end
        a.spells = setmetatable({}, { __index = {
            add = function(spells, id) spells[id] = g.spells[id] end,
            remove = function(spells, id) spells[id] = nil end,
        } })
        for name in ('strength agility luck speed endurance willpower intelligence personality'):gmatch('%S+') do a.attributes[name] = stat(50) end
        for name in ('block shortblade longblade bluntweapon axe spear marksman handtohand lightarmor mediumarmor heavyarmor unarmored'):gmatch('%S+') do a.skills[name] = stat(0) end
        a.ctx = self:context(a)
        if referenceRoot then
            local function upstream(file) return setfenv(assert(loadfile(referenceRoot .. '/' .. file)), a.ctx.env)() end
            a.ctx.I.Combat = upstream('files_data_scripts_omw_combat_interface.lua').interface
            a.ctx.I.Combat = upstream('files_data-mw_scripts_omw_combat_local.lua').interface
        else
            local handlers = { function(attack) Actor._onHit(a, attack) end }
            a.ctx.I.Combat = {
                ATTACK_SOURCE_TYPES = { Melee = 'melee', Ranged = 'ranged', Unspecified = 'unspecified' },
                getArmorRating = function() return 0 end,
                getArmorSkill = function() return 'lightarmor' end,
                applyArmor = function() end,
                adjustDamageForDifficulty = function() end,
                addOnHitHandler = function(fn) handlers[#handlers + 1] = fn end,
                onHit = function(attack) for i = #handlers, 1, -1 do if handlers[i](attack) == false then break end end end,
            }
        end
        a.ctx.actorScript = a.ctx:load('scripts.ngc.openmw.actor')
        if player then a.ctx:load('scripts.ngc.openmw.player') end
        self.actors[#self.actors + 1] = a
        a.ctx:engine('onActive')
        return a
    end
    function g:weapon(a, kind, skill, level)
        local record = { id = a.id .. kind, type = kind, health = 100, weight = 5 }
        self.records[record.id] = record
        local item = { kind = 'weapon', record = record, data = { condition = 100 } }
        function item:isValid() return true end
        a.equipment.CarriedRight = item
        if skill then a.skills[skill].base = level or 100 end
        return item
    end
    function g:flush()
        local count = 0
        while #self.queue > 0 do
            local event = table.remove(self.queue, 1)
            count = count + 1
            assert(count < 500, 'Event loop did not terminate')
            if event.global then self.global:event(event.name, event.data)
            elseif event.target:isValid() then event.target.ctx:event(event.name, event.data) end
        end
    end
    function g:hit(a, b, health, fatigue, source, ammo, strength)
        local attack = { attacker = a, weapon = a.equipment.CarriedRight, damage = { health = health or 0, fatigue = fatigue or 0 },
            successful = true, sourceType = source or 'melee', strength = strength or 1, ammo = ammo }
        b.ctx.I.Combat.onHit(attack)
        self:flush()
        return attack
    end
    function g:advance(dt)
        self.now = self.now + dt
        for _, actor in ipairs(self.actors) do actor.ctx:engine('onUpdate', dt) end
        self.global:engine('onUpdate', dt)
        self:flush()
    end
    g:set('toggleAlwaysHit', false)
    return g
end

test('manifest paths and Lua syntax', function()
    for line in io.lines(projectRoot .. '/Next Generation Combat.omwscripts') do
        local path = line:match(': ([^ ]+%.lua)$')
        if path then assert(loadfile(projectRoot .. '/' .. path)) end
    end
end)
test('tier boundaries and zero fatigue denominator', function()
    local env = setmetatable({}, { __index = _G })
    local rules = readModule('scripts.ngc.openmw.rules', env)
    local defaults = readModule('scripts.ngc.openmw.defaults', env)
    equal(rules.ratio(100, 0), 0)
    equal(rules.tier(defaults, 24).criticalStrikeChance, nil)
    equal(rules.tier(defaults, 25).criticalStrikeChance, 10)
    equal(rules.tier(defaults, 100).criticalStrikeChance, 50)
    near(rules.knockdownChance(defaults, defaults.weaponTier4, 150), 20)
end)
test('load creates helper spells and applies GMSTs', function()
    local g = game()
    equal(g.gmst.fProjectileThrownStoreChance, 0)
    equal(g.gmst.fNGCProjectileStoreChance, 25)
    near(g.gmst.fMinHandToHandMult, 0.05)
    equal(g.gmst.fProjectileMaxSpeed, 4000)
    equal(g.spells.ngc_omw_accuracy.effects[1].magnitudeMin, 10000)
    equal(g.spells.mic_adrenaline_rush.effects[1].magnitudeMin, 50)
    equal(g.spells.ngc_omw_rush, nil)
end)
test('both unchanged original ESP files are required', function()
    for name in pairs(espContentNames) do
        local ok, message = pcall(game, { missingESP = name })
        equal(ok, false)
        assert(message:find(name, 1, true))
    end
end)
test('accuracy helper never stacks and never inflates damage', function()
    local g = game(); g:set('toggleAlwaysHit', true)
    local a, b = g:actor('a'), g:actor('b')
    g:weapon(a, 'LongBladeOneHand', 'longblade')
    g:advance(0.25); g:advance(0.25)
    g:hit(a, b, 10)
    near(b.dynamic.health.current, 988)
    g:set('toggleAlwaysHit', false); g:advance(0.25)
    equal(a.spells.ngc_omw_accuracy, nil)
end)
test('short blades combine critical and execute', function()
    local g = game(); g:set('criticalStrikeChance', 100, 4)
    local a, b = g:actor('a'), g:actor('b')
    g:weapon(a, 'ShortBladeOneHand', 'shortblade')
    b.dynamic.health.current = 200
    g:hit(a, b, 10)
    near(b.dynamic.health.current, 163)
end)
test('multistrike retains original fourth-hit timing across targets', function()
    local g = game(); g:set('multistrikeBonusChance', 0, 4)
    local a, b, c = g:actor('a'), g:actor('b'), g:actor('c')
    g:weapon(a, 'LongBladeOneHand', 'longblade')
    g:hit(a, b, 10); g:hit(a, c, 10); g:hit(a, b, 10)
    near(b.dynamic.health.current, 976)
    near(c.dynamic.health.current, 988)
    g:hit(a, c, 10)
    near(c.dynamic.health.current, 971)
    equal(a.ctx.actorScript.engineHandlers.onSave().strikes, 0)
end)
test('multistrike retains original configured-threshold quirks', function()
    local g = game(); g:set('multistrikeStrikesNeeded', 2); g:set('multistrikeBonusChance', 0, 4)
    local a, b = g:actor('a'), g:actor('b')
    g:weapon(a, 'LongBladeOneHand', 'longblade')
    g:hit(a, b, 10); g:advance(11); g:hit(a, b, 10)
    near(b.dynamic.health.current, 976)
    for _ = 1, 4 do g:hit(a, b, 10) end
    near(b.dynamic.health.current, 928)
end)
test('riposte belongs to defender and affects outgoing attacks', function()
    local g = game(); g:set('riposteChance', 100, 4); g:set('multistrikeStrikesNeeded', 99)
    local a, b = g:actor('a'), g:actor('b')
    g:weapon(a, 'LongBladeOneHand', 'longblade'); g:weapon(b, 'LongBladeOneHand', 'longblade')
    g:hit(a, b, 10); g:hit(b, a, 10)
    near(a.dynamic.health.current, 983)
end)
test('bleed stacks, expires, and stays nonlethal', function()
    local g = game(); g:set('bleedChance', 100, 4)
    local a, b = g:actor('a'), g:actor('b')
    g:weapon(a, 'AxeOneHand', 'axe')
    g:hit(a, b, 10); g:hit(a, b, 10)
    equal(b.ctx.actorScript.engineHandlers.onSave().bleed.stacks, 2)
    g:advance(1); near(b.dynamic.health.current, 966)
    b.dynamic.health.current = 5
    g:advance(2); near(b.dynamic.health.current, 5)
    equal(b.ctx.actorScript.engineHandlers.onSave().bleed, nil)
end)
test('stun resets the original paralyze attribute after one second', function()
    local g = game(); g:set('stunChance', 100, 4)
    local a, b = g:actor('a'), g:actor('b')
    g:weapon(a, 'BluntOneHand', 'bluntweapon')
    g:hit(a, b, 10); equal(g.types.Actor.canMove(b), false)
    b.effects.paralyze = 1
    g:advance(2); equal(g.types.Actor.canMove(b), true)
end)
test('hamstring preserves other speed modifiers and survives save/load', function()
    local g = game(); g:set('hamstringChance', 100, 4)
    local a, b = g:actor('a'), g:actor('b')
    g:weapon(a, 'MarksmanBow', 'marksman'); b.attributes.speed.base = 100
    g:hit(a, b, 10, 0, 'ranged'); near(b.attributes.speed.modified, 50)
    local saved = b.ctx.actorScript.engineHandlers.onSave()
    b.ctx.actorScript.engineHandlers.onLoad(saved); g:advance(0.25)
    near(b.attributes.speed.modified, 50)
    b.attributes.speed.modifier = b.attributes.speed.modifier + 20
    g:advance(0.25); near(b.attributes.speed.modified, 60)
    g:advance(3); near(b.attributes.speed.modified, 120)
end)
test('unarmed damage adds health damage while preserving fatigue damage', function()
    local g = game(); g:set('handToHandKnockdownChance', 100, 4)
    local a, b = g:actor('a'), g:actor('b'); a.skills.handtohand.base = 100
    b.attributes.agility.base = 10
    g:hit(a, b, 0, 5)
    assert(b.dynamic.health.current < 1000)
    near(b.dynamic.fatigue.current, 95)
    near(b.ctx.actorScript.engineHandlers.onSave().knockdownUntil, 3)
    equal(g.types.Actor.activeEffects(b):getEffect('paralyze').magnitude, 0)
end)
test('spear momentum triggers temporary adrenaline rush', function()
    local g = game(); g:set('adrenalineRushChance', 100, 4)
    local a, b = g:actor('a'), g:actor('b')
    g:weapon(a, 'SpearTwoWide', 'spear'); b.dynamic.fatigue.current = 50
    g:hit(a, b, 10); near(b.dynamic.health.current, 982)
    assert(a.spells.mic_adrenaline_rush)
    g:advance(3); equal(a.spells.mic_adrenaline_rush, nil)
end)
test('full draw uses the original three-second timer and one-shot cache', function()
    local g = game(); g:set('hamstringChance', 0, 4)
    local a, b = g:actor('a', true), g:actor('b')
    g:weapon(a, 'MarksmanBow', 'marksman')
    g.animationTime = 1; g.use = true
    a.ctx:animation('bowandarrow', { stopKey = 'shoot max attack' })
    a.ctx:engine('onFrame', 0.5); g.use = false; a.ctx:engine('onFrame', 0.1)
    g:hit(a, b, 10, 0, 'ranged', nil, 1); near(b.dynamic.health.current, 988)
    g.use = true; a.ctx:engine('onFrame', 3)
    g.use = false; a.ctx:engine('onFrame', 0.1)
    g:hit(a, b, 10, 0, 'ranged', nil, 1); near(b.dynamic.health.current, 966)
    g:hit(a, b, 10, 0, 'ranged', nil, 1); near(b.dynamic.health.current, 954)
end)
test('crossbow close-range bonus and thrown recovery', function()
    local g = game(); local a, b = g:actor('a'), g:actor('b')
    g:weapon(a, 'MarksmanCrossbow', 'marksman'); g:hit(a, b, 10, 0, 'ranged')
    near(b.dynamic.health.current, 985.5)
    local thrown = g:weapon(a, 'MarksmanThrown', 'marksman'); g:set('thrownCriticalStrikeChance', 0, 4)
    g:hit(a, b, 10, 0, 'ranged', thrown.record.id)
    near(b.dynamic.health.current, 971)
    equal(b.inventory[thrown.record.id], 1)
end)
test('native blocks, misses, immunities, and magic bypass perk processing', function()
    local g = game(); local a, b = g:actor('a'), g:actor('b')
    g:weapon(a, 'AxeOneHand', 'axe'); g:set('bleedChance', 100, 4)
    g:hit(a, b, 0); near(b.dynamic.health.current, 1000)
    equal(b.ctx.actorScript.engineHandlers.onSave().bleed, nil)
    g:hit(a, b, 10, 0, 'magic'); near(b.dynamic.health.current, 990)
    local attack = { attacker = a, weapon = a.equipment.CarriedRight, successful = false, damage = {}, sourceType = 'melee' }
    b.ctx.I.Combat.onHit(attack); g:flush(); near(b.dynamic.health.current, 990)
end)
test('immune targets can retain thrown weapons without taking damage', function()
    local g = game(); local a, b = g:actor('a'), g:actor('b')
    local thrown = g:weapon(a, 'MarksmanThrown', 'marksman')
    g:hit(a, b, 0, 0, 'ranged', thrown.record.id)
    near(b.dynamic.health.current, 1000); equal(b.inventory[thrown.record.id], 1)
end)
test('Blind retains the original early return and unmodified base damage', function()
    local g = game(); g:set('toggleAlwaysHit', true)
    local a, b = g:actor('a'), g:actor('b'); a.effects.blind = 100
    g:weapon(a, 'AxeOneHand', 'axe'); g:hit(a, b, 10)
    near(b.dynamic.health.current, 990)
end)
test('other combat handlers run exactly once per resolved hit', function()
    local g = game(); local a, b = g:actor('a'), g:actor('b')
    g:weapon(a, 'LongBladeOneHand', 'longblade')
    local calls = 0
    b.ctx.I.Combat.addOnHitHandler(function(attack)
        calls = calls + 1
        near(attack.damage.health, 12)
    end)
    g:hit(a, b, 10)
    equal(calls, 1); near(b.dynamic.health.current, 988)
end)
if referenceRoot then
    test('upstream armor mitigation is applied exactly once', function()
        local g = game(); local a, b = g:actor('a'), g:actor('b')
        g:weapon(a, 'LongBladeOneHand', 'longblade')
        b.skills.heavyarmor.base = 100
        b.equipment.Cuirass = { kind = 'armor', record = { type = 'Cuirass', baseArmor = 100, weight = 100, health = 100 }, data = { condition = 100 } }
        g:hit(a, b, 10)
        near(b.dynamic.health.current, 998.8)
    end)
end
test('guard blocks frontal melee, but allows rear and ranged damage', function()
    local g = game(); local a, b = g:actor('a'), g:actor('b', true)
    g:weapon(a, 'LongBladeOneHand', 'longblade'); g:weapon(b, 'LongBladeOneHand', 'longblade')
    b.equipment.CarriedLeft = { kind = 'armor', record = { type = 'Shield', baseArmor = 0, weight = 0, health = 100 }, data = { condition = 100 } }
    b.position = vector(0, 0, 0); g.keys[44] = true; b.ctx:engine('onFrame', 0.1)
    g:hit(a, b, 10); near(b.dynamic.health.current, 1000)
    a.position = vector(0, -100, 0); g:hit(a, b, 10); near(b.dynamic.health.current, 988)
    a.position = vector(0, 100, 0); g:hit(a, b, 10, 0, 'ranged'); near(b.dynamic.health.current, 976)
end)
test('guard fatigue and menu cleanup', function()
    local g = game(); local a, b = g:actor('a'), g:actor('b', true)
    g:weapon(a, 'LongBladeOneHand', 'longblade')
    b.equipment.CarriedLeft = { kind = 'armor', record = { type = 'Shield', baseArmor = 0, weight = 0, health = 100 }, data = { condition = 100 } }
    b.position = vector(0, 0, 0); g.keys[44] = true; b.ctx:engine('onFrame', 1)
    near(b.dynamic.fatigue.current, 75)
    g.mode = 'Inventory'; b.ctx:engine('onFrame', 0); g.mode = nil
    g:hit(a, b, 10); near(b.dynamic.health.current, 988)
    g:set('activeBlockKey', 'invalid key'); b.ctx:engine('onFrame', 0.1)
end)
test('bow zoom uses radians and restores the prior FOV', function()
    local g = game(); local a = g:actor('a', true)
    g:weapon(a, 'MarksmanBow', 'marksman'); g.use = true; g.animationTime = 1
    a.controls.sneak = true; a.controls.movement = -1
    a.ctx:animation('bowandarrow', { stopKey = 'shoot max attack' })
    a.ctx:engine('onFrame', 3)
    near(a.dynamic.fatigue.current, 100)
    a.controls.movement = -1; a.ctx:engine('onFrame', 1)
    near(g.fov, math.rad(35)); near(a.dynamic.fatigue.current, 95); near(a.controls.movement, -0.3)
    g.use = false; a.ctx:engine('onFrame', 0.1); near(g.fov, math.rad(70))
end)
test('exhausted bow draw sheathes the weapon', function()
    local g = game(); local a = g:actor('a', true)
    g:weapon(a, 'MarksmanBow', 'marksman'); g.use = true; g.animationTime = 1
    a.dynamic.fatigue.current = 10; a.ctx:animation('bowandarrow', { stopKey = 'shoot max attack' })
    a.ctx:engine('onFrame', 3); a.ctx:engine('onFrame', 1); equal(a.stance, 'Nothing')
end)
test('repeater changes animation speed without mutating weapon records', function()
    local g = game(); g:set('repeaterChance', 100, 4); local a = g:actor('a', true)
    local item = g:weapon(a, 'MarksmanCrossbow', 'marksman'); item.record.speed = 1.3
    local options = { startKey = 'shoot follow start', speed = 1.3 }
    a.ctx:animation('crossbow', options); near(options.speed, 10); near(item.record.speed, 1.3)
end)
test('skill scaling retains original melee and armor selection', function()
    local g = game(); local a = g:actor('a', true)
    local skill = a.ctx.I.SkillProgression
    skill.skillUsed('longblade', { skillGain = 10 }); near(a.ctx.usedSkills[1].gain, 6)
    skill.skillUsed('marksman', { skillGain = 10 }); near(a.ctx.usedSkills[2].gain, 10)
    skill.skillUsed('heavyarmor', { skillGain = 10 }); near(a.ctx.usedSkills[3].gain, 8)
end)
test('inactive actors release only their own temporary modifiers', function()
    local g = game(); g:set('toggleAlwaysHit', true); g:set('hamstringChance', 100, 4)
    local a, b = g:actor('a'), g:actor('b'); g:weapon(a, 'MarksmanBow', 'marksman')
    g:advance(0.25); g:hit(a, b, 10, 0, 'ranged')
    b.attributes.speed.modifier = b.attributes.speed.modifier + 20
    b.ctx:engine('onInactive'); near(b.attributes.speed.modified, 70); equal(b.spells.ngc_omw_accuracy, nil)
end)

test('combat cleanup follows AI combat packages rather than an idle timeout', function()
    local g = game(); local a, b = g:actor('a', true), g:actor('b')
    g:weapon(a, 'LongBladeOneHand', 'longblade')
    b.combatTargets = { a }
    g:hit(a, b, 10); g:advance(0.25)
    g:hit(a, b, 10); equal(a.ctx.actorScript.engineHandlers.onSave().strikes, 1)
    b.combatTargets = {}
    g:advance(0.25); g:advance(0.25); g:advance(0.5)
    equal(a.ctx.actorScript.engineHandlers.onSave().strikes, nil)
end)

local original = dofile(developmentRoot .. '/tests/original_mwse.lua')
test('separate fist health damage matches original Lua across 30 armor/skill cases', function()
    for _, level in ipairs({ 0, 25, 50, 75, 100 }) do
        for _, armor in ipairs({ 0, 50, 500 }) do
            for _, alwaysHit in ipairs({ false, true }) do
                local g = game(); g:set('toggleAlwaysHit', alwaysHit)
                local a, b = g:actor('a', true), g:actor('b')
                a.skills.handtohand.base = level
                a.attributes.strength.base = 80
                a.dynamic.fatigue.current = 50
                b.effects.sanctuary = 25
                g:set('handToHandBaseDamageMax', 2)
                for tier = 1, 4 do
                    local values = a.ctx.env.require('scripts.ngc.openmw.config').get()['weaponTier' .. tier]
                    g:set('handToHandBaseDamageMax', values.handToHandBaseDamageMin, tier)
                    g:set('handToHandKnockdownChance', 0, tier)
                end
                if armor > 0 then
                    b.equipment.Cuirass = { kind = 'armor', record = { type = 'Cuirass', baseArmor = armor, weight = 100, health = 100 }, data = { condition = 100 } }
                end
                local oracle = original(a.ctx.env.require('scripts.ngc.openmw.config').get(), a, b, g)
                oracle.attack()
                g:hit(a, b, 0, 5)
                near(b.dynamic.health.current, oracle.target.health.current)
                near(b.dynamic.fatigue.current, 95)
                near(a.ctx.usedSkills[1].gain, oracle.experience())
            end
        end
    end
end)
test('eight weapon families match original post-armor damage callbacks', function()
    local cases = {
        { 'ShortBladeOneHand', 'shortblade' }, { 'LongBladeOneHand', 'longblade' },
        { 'BluntOneHand', 'bluntweapon' }, { 'SpearTwoWide', 'spear' }, { 'AxeOneHand', 'axe' },
        { 'MarksmanBow', 'marksman' }, { 'MarksmanCrossbow', 'marksman' }, { 'MarksmanThrown', 'marksman' },
    }
    for _, case in ipairs(cases) do
        for _, level in ipairs({ 25, 100 }) do
            local g = game()
            for _, key in ipairs({ 'multistrikeBonusChance', 'stunChance', 'bleedChance', 'adrenalineRushChance', 'hamstringChance' }) do
                for tier = 1, 4 do g:set(key, 0, tier) end
            end
            for tier = 1, 4 do g:set('criticalStrikeChance', 100, tier); g:set('thrownCriticalStrikeChance', 100, tier) end
            local a, b = g:actor('a'), g:actor('b')
            g:weapon(a, case[1], case[2], level)
            b.dynamic.health.base, b.dynamic.health.current = 10000, 2000
            b.dynamic.fatigue.current = 50
            b.skills.heavyarmor.base = 100
            b.equipment.Cuirass = { kind = 'armor', record = { type = 'Cuirass', baseArmor = 100, weight = 100, health = 100 }, data = { condition = 100 } }
            local oracle = original(a.ctx.env.require('scripts.ngc.openmw.config').get(), a, b, g)
            for _ = 1, 4 do
                local postArmor = referenceRoot and math.max(1, b.ctx.I.Combat.adjustDamageForArmor(30)) or 30
                oracle.damage(postArmor)
                local ranged = case[2] == 'marksman'
                g:hit(a, b, 30, 0, ranged and 'ranged' or 'melee')
                near(b.dynamic.health.current, oracle.target.health.current)
            end
        end
    end
end)
test('a missed native fist attack still has the separate original health hit', function()
    local g = game(); local a, b = g:actor('a', true), g:actor('b')
    g:set('handToHandBaseDamageMax', 2)
    local oracle = original(a.ctx.env.require('scripts.ngc.openmw.config').get(), a, b, g)
    oracle.attack()
    b.ctx.I.Combat.onHit({ attacker = a, successful = false, damage = {}, sourceType = 'melee' })
    g:flush()
    near(b.dynamic.health.current, oracle.target.health.current)
    near(b.dynamic.fatigue.current, 100)
end)
test('player knockdown restores the original fatigue snapshot after two seconds', function()
    local g = game(); local a, b = g:actor('a'), g:actor('b', true)
    a.skills.handtohand.base = 100; b.attributes.agility.base = 10
    g:set('handToHandKnockdownChance', 100, 4)
    local oracle = original(a.ctx.env.require('scripts.ngc.openmw.config').get(), a, b, g)
    oracle.attack(); g:hit(a, b, 0, 5)
    assert(b.dynamic.fatigue.current < 0)
    oracle.advance(2); g:advance(2)
    near(b.dynamic.fatigue.current, oracle.target.fatigue.current)
end)
print(string.format('%d tests passed (%s combat handler).', passed, referenceRoot and 'OpenMW 0.51 upstream' or 'mock'))
