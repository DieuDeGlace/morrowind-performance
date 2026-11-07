local cases = {
    { "almalexia", 'Creature' }, -- r\Almelexia.NIF
    { "Almalexia_warrior", 'Creature' }, -- r\Almelexia_warrior.NIF
    { "gateway_haunt", 'Creature' }, -- r\AncestorGhost.NIF
    { "dagoth ulen", 'Creature' }, -- r\AscendedSleeper.NIF
    { "dagoth elam", 'Creature' }, -- r\AshGhoul.NIF
    { "ash_slave", 'Creature' }, -- r\AshSlave.NIF
    { "dagoth endus", 'Creature' }, -- r\AshVampire.NIF
    { "ash_zombie", 'Creature' }, -- r\AshZombie.NIF
    { "atronach_flame", 'Creature' }, -- r\Atronach_Fire.NIF
    { "atronach_frost", 'Creature' }, -- r\Atronach_Frost.NIF
    { "atronach_storm", 'Creature' }, -- r\Atronach_Storm.NIF
    { "Slaughterfish_Small", 'Creature' }, -- r\BabelFish.NIF
    { "BM_bear_black", 'Creature' }, -- r\bear_black_larger.NIF
    { "BM_bear_snow_unique", 'Creature' }, -- r\bear_blond_larger.NIF
    { "BM_bear_brown", 'Creature' }, -- r\bear_brown_larger.NIF
    { "bonelord", 'Creature' }, -- r\BoneLord.NIF
    { "bonewalker_dead", 'Creature' }, -- r\Bonewalker.NIF
    { "yagrum bagarn", 'Creature' }, -- r\BYagram.NIF
    { "mudcrab", 'Creature' }, -- r\CaveMudcrab.NIF
    { "clannfear", 'Creature' }, -- r\Clannfear.NIF
    { "ogrim", 'Creature' }, -- r\Clannfear_Daddy.NIF
    { "cliff racer", 'Creature' }, -- r\CliffRacer.NIF
    { "corprus_stalker", 'Creature' }, -- r\Corprus_Stalker.NIF
    { "draugr", 'Creature' }, -- r\cr_draugr.NIF
    { "daedroth", 'Creature' }, -- r\Daedroth.NIF
    { "dagoth_ur_1", 'Creature' }, -- r\Dagothr.NIF
    { "draugr_aesliip", 'Creature' }, -- r\draugrLord.NIF
    { "dremora", 'Creature' }, -- r\Dremora.NIF
    { "dreugh", 'Creature' }, -- r\Dreugh.NIF
    { "durzog_wild", 'Creature' }, -- r\Durzog.NIF
    { "durzog_war", 'Creature' }, -- r\Durzog_collar.NIF
    { "alit", 'Creature' }, -- r\DuskyAlit.NIF
    { "Dahrk Mezalf", 'Creature' }, -- r\DwarvenSpecter.NIF
    { "fabricant_summon", 'Creature' }, -- r\Fabricant.NIF
    { "fabricant_hulking", 'Creature' }, -- r\Fabricant_Hulking.NIF
    { "Imperfect", 'Creature' }, -- r\Fabricant_Imperfect.NIF
    { "bm_frost_giant", 'Creature' }, -- r\FrostGiant.NIF
    { "centurion_spider", 'Creature' }, -- r\G_CenturionSpider.NIF
    { "goblin_grunt", 'Creature' }, -- r\Goblin01.NIF
    { "goblin_bruiser", 'Creature' }, -- r\Goblin02.NIF
    { "goblin_officerUNI", 'Creature' }, -- r\Goblin03.NIF
    { "golden saint", 'Creature' }, -- r\Golden Saint.NIF
    { "Bonewalker_Greater_dead", 'Creature' }, -- r\GreatBonewalker.NIF
    { "guar", 'Creature' }, -- r\Guar.NIF
    { "guar_white_unique", 'Creature' }, -- r\Guar_white.NIF
    { "guar_pack", 'Creature' }, -- r\Guar_withpack.NIF
    { "heart_akulakhan", 'Creature' }, -- r\heart_Akulakhan.NIF
    { "BM_hircine", 'Creature' }, -- r\hircine.NIF
    { "BM_hircine_straspect", 'Creature' }, -- r\Hircine_Bear_larger.NIF
    { "BM_hircine_spdaspect", 'Creature' }, -- r\HircineWolf.NIF
    { "BM_horker", 'Creature' }, -- r\Horker.NIF
    { "BM_horker_large", 'Creature' }, -- r\Horker_larger.NIF
    { "hunger", 'Creature' }, -- r\Hunger.NIF
    { "BM_ice_troll", 'Creature' }, -- r\ice troll.NIF
    { "BM_riekling", 'Creature' }, -- r\IceMinion.NIF
    { "BM_riekling_mounted", 'Creature' }, -- r\IceMRaider.NIF
    { "kwama forager", 'Creature' }, -- r\Kwama Forager.NIF
    { "Kwama Queen", 'Creature' }, -- r\Kwama Queen.NIF
    { "kwama warrior", 'Creature' }, -- r\Kwama Warior.NIF
    { "kwama worker", 'Creature' }, -- r\Kwama Worker.NIF
    { "corprus_lame_fyr01", 'Creature' }, -- r\Lame_Corprus.NIF
    { "kagouti", 'Creature' }, -- r\LeastKagouti.NIF
    { "lich", 'Creature' }, -- r\Liche.NIF
    { "lich_barilzar", 'Creature' }, -- r\Liche_King.NIF
    { "vivec_god", 'Creature' }, -- r\LordVivec.NIF
    { "scrib", 'Creature' }, -- r\MineScrib.NIF
    { "BM_frost_boar", 'Creature' }, -- r\Mount.NIF
    { "netch_betty", 'Creature' }, -- r\Netch_Betty.NIF
    { "netch_bull", 'Creature' }, -- r\Netch_Bull.NIF
    { "nix-hound", 'Creature' }, -- r\NixHound.NIF
    { "Rat_pack_rerlas", 'Creature' }, -- r\PackRat.NIF
    { "glenmoril_raven", 'Creature' }, -- r\raven.NIF
    { "Rat", 'Creature' }, -- r\Rust rat.NIF
    { "scamp", 'Creature' }, -- r\Scamp_Fetch.NIF
    { "shalk", 'Creature' }, -- r\Shalk.NIF
    { "skeleton", 'Creature' }, -- r\Skeleton.NIF
    { "slaughterfish", 'Creature' }, -- r\SlaughterFish.NIF
    { "centurion_sphere", 'Creature' }, -- r\Sphere_Centurions.NIF
    { "centurion_projectile", 'Creature' }, -- r\SphereArcher.NIF
    { "BM_spriggan", 'Creature' }, -- r\Spriggan.NIF
    { "centurion_steam", 'Creature' }, -- r\Steam_Centurions.NIF
    { "BM_horker_swim_UNIQUE", 'Creature' }, -- r\swimmer.NIF
    { "BM_udyrfrykte", 'Creature' }, -- r\udyrfrykte.NIF
    { "BM_wolf_skeleton", 'Creature' }, -- r\UnDeadWolf_2.nif
    { "winged twilight", 'Creature' }, -- r\WingedTwilight.NIF
    { "BM_wolf_grey", 'Creature' }, -- r\Wolf_Black.NIF
    { "BM_wolf_red", 'Creature' }, -- r\Wolf_red.NIF
    { "BM_wolf_snow_unique", 'Creature' }, -- r\Wolf_white.NIF
    { "BM_werewolf_default", 'Creature' }, -- wolf\skinNPC.NIF
    { "itan", 'NPC' }, -- base_animKnA.nif
    { "hul", 'NPC' }, -- base_animKnA.nif
    { "ken", 'NPC' }, -- Breton male
    { "Joslin", 'NPC' }, -- Breton female
    { "todd", 'NPC' }, -- Dark Elf male
    { "nina", 'NPC' }, -- Dark Elf female
    { "andil", 'NPC' }, -- High Elf male
    { "faire", 'NPC' }, -- High Elf female
    { "Luven", 'NPC' }, -- Imperial male
    { "Cinda", 'NPC' }, -- Imperial female
    { "Joshur", 'NPC' }, -- base_animKnA.nif
    { "adanja", 'NPC' }, -- base_animKnA.nif
    { "sor", 'NPC' }, -- Nord male
    { "val", 'NPC' }, -- Nord female
    { "dreggs", 'NPC' }, -- Orc male
    { "sloomalah", 'NPC' }, -- Orc female
    { "Ian", 'NPC' }, -- Redguard male
    { "Ena", 'NPC' }, -- Redguard female
    { "cun", 'NPC' }, -- Wood Elf male
    { "nael", 'NPC' }, -- Wood Elf female
}

local core = require('openmw.core')
local types = require('openmw.types')
local util = require('openmw.util')
local world = require('openmw.world')
local elapsed, deadline, phase, nextCase = 0, 1, 'enter', 1
local targets = {}
local checked = 0

local function update(dt)
    elapsed = elapsed + dt
    if elapsed < deadline then return end
    local player = world.players[1]
    if not player or not player.cell then return end

    if phase == 'enter' then
        player:teleport(world.getExteriorCell(-3, -8), util.vector3(-16890, -58575, 1200), { onGround = true })
        phase, deadline = 'spawn', elapsed + 1
    elseif phase == 'spawn' then
        for _, actor in ipairs(world.activeActors) do
            if actor ~= player then actor.enabled = false end
        end
        for i = 1, 3 do
            local case = cases[nextCase]
            if not case then break end
            local actorType = types[case[2]]
            local record = world.createRecord(actorType.createRecordDraft {
                template = actorType.record(case[1]), isEssential = false, isRespawning = false, mwscript = '',
            })
            local target = world.createObject(record.id)
            target:addScript('scripts/ragdoll_target.lua')
            world.createObject('gold_001', 7):moveInto(target)
            target:teleport(player.cell, player.position + util.vector3((i - 2) * 230, 240 + i * 90, 200),
                { onGround = false })
            targets[#targets + 1] = { object = target, source = case[1], record = record.id }
            print('RAGDOLL_ALL_ACTOR_SPAWN', case[1], record.id)
            nextCase = nextCase + 1
        end
        if #targets == 0 then
            print('RAGDOLL_ALL_ACTORS_COMPLETE', checked)
            core.quit()
            phase = 'done'
        else
            phase, deadline = 'kill', elapsed + 1
        end
    elseif phase == 'kill' then
        for _, target in ipairs(targets) do target.object:sendEvent('RagdollTest_Die') end
        phase, deadline = 'observe', elapsed + 5
    elseif phase == 'observe' then
        for _, target in ipairs(targets) do
            assert(types.Actor.isDeathFinished(target.object), 'Death did not finish: ' .. target.source)
            assert(types.Actor.inventory(target.object):countOf('gold_001') >= 7, 'Inventory lost: ' .. target.source)
            print('RAGDOLL_ALL_ACTOR_CHECKED', target.source, target.record)
            target.object:remove()
            checked = checked + 1
        end
        targets = {}
        phase, deadline = 'spawn', elapsed + .25
    end
    assert(elapsed < 500, 'Actor coverage replay timed out at ' .. phase)
end

return {
    engineHandlers = {
        onUpdate = function(dt)
            local ok, err = pcall(update, dt)
            if not ok then
                print('RAGDOLL_ALL_ACTORS_FAIL', err)
                phase = 'done'
                core.quit()
            end
        end,
    },
}
