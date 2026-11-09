# Next Generation Combat — OpenMW ESP/Lua preview 2

A standalone OpenMW adaptation of **Next Generation Combat 1.2.2 by Aracelliy**.
Original mod: https://www.nexusmods.com/morrowind/mods/46993

This revision uses ESP records and standard OpenMW Lua API 129 or newer.
It requires no custom combat engine hooks. Both original ESPs and the icon are
unchanged. A small companion ESP supplies the OpenMW accuracy ability.

## Install or update

Use OpenMW 0.51 or a newer compatible API version. DieuDeGlace's Physics
Preview 10 Windows and Android builds are supported. The Windows download is a
complete OpenMW build; launch normally through openmw-launcher.exe.

Extract this ZIP into a dedicated data directory, or replace the previous port's
files. Enable these entries after Morrowind/Tribunal/Bloodmoon and in this order:

1. Next Generation Combat.esp
2. Next Generation Combat - Hand To Hand Fixes.esp
3. Next Generation Combat - OpenMW.esp
4. Next Generation Combat.omwscripts

The third ESP is new and must be enabled when updating an earlier port.
Keep the Lua manifest after other combat scripts.

Example openmw.cfg entries:

    data="C:/Games/Morrowind Mods/Next Generation Combat OpenMW"
    content=Next Generation Combat.esp
    content=Next Generation Combat - Hand To Hand Fixes.esp
    content=Next Generation Combat - OpenMW.esp
    content=Next Generation Combat.omwscripts

Configure the mod in **Options → Scripts → Next Generation Combat**. Settings
keep their previous section names and are stored in each save. MWSE JSON
configuration files are not read by OpenMW.

## Combat behavior

- **Always Hit:** Lua adds the companion ESP's Fortify Attack ability to active
  actors. Its artificial accuracy bonus is subtracted when NGC calculates bonus
  damage, preserving real Fortify Attack bonuses. Disabling the option or
  deactivating an actor removes only this mod's ability.
- **Held shield guard:** Lua checks the shield, stance, fatigue and facing arc.
  Eligible frontal melee damage is stopped; shield wear, fatigue cost, Block
  experience, sound and animation are handled through standard Lua interfaces.
  Rear and ranged hits remain eligible to damage the defender.
- **Hamstring:** applies and later removes this mod's own Speed modifier while
  retaining modifiers from other sources.
- **Fists:** the defender's ordinary hit callback adds the separate health
  supplement, including failed hit callbacks, while retaining the normal
  fatigue hit. Werewolves and ordinary unarmed creatures are excluded from the
  NPC fist supplement; creature damage keeps its original strength bonus.
- **Bow movement:** full draw slows backward movement input. Guard, draw state
  and zoom reset when controls become unavailable.

| Weapon | Preserved mechanics |
| --- | --- |
| Short blade | Critical strike, execute and skill tiers |
| Long blade | Original fourth-hit counter, double strike and timed riposte |
| Axe | Bleed ticks and stack limits |
| Blunt | Base armor bonus and one-second stun |
| Spear | Fatigue advantage and original three-second Adrenaline Rush |
| Fists | Separate health/fatigue damage, strength/armor and knockdown scaling |
| Bow | Three-second full draw, one-use bonus, hamstring, fatigue drain and sneak zoom |
| Crossbow | Close-range bonus and accelerated Repeater reload |
| Thrown | Agility scaling, critical chance and recovery tiers |

Hold **Z** with a usable readied shield to guard. The key or right mouse binding
is configurable. Android/controller users must map the configured key. The
standard Boolean input action NGCGuard is also available to Lua input bindings.
The original guard icon is included.

Idle actor control and AI polling stays at four updates per second. Active
temporary effects expire on the first eligible simulation frame. No global
nearby-actor scans are used.

## Validation and remaining adaptations

The original MWSE scripts serve as a numerical oracle. Each supported engine
API run performs 10,858 direct formula comparisons plus targeted perk, timing,
armor, input, cleanup and saved-state tests. The controls double rejects fields
outside standard OpenMW's API.

Separate gameplay scenarios run in the actual unmodified Physics Preview 10
Windows executable. They validate ESP accuracy on a player, NPC and creature,
cleanup, physical hit/miss behavior, fist supplements, Lua shield guard and
wear, rear attacks, and the one-use bow bonus. See the accompanying validation
report for exact completed results.

This is not a blanket 1:1 certification. The accuracy ability raises the existing
hit formula rather than replacing it. Hamstring changes Speed, and backward bow
slowdown changes movement input rather than final engine speed. Fist health
damage starts from the available hit callback instead of MWSE's attack-release
event. Ordinary weapon bonuses use OpenMW's asynchronous actor-event round trip.
The settings UI differs from MWSE, Repeater changes actor animation speed,
exhausted bows sheath, and projectile recovery rolls are per hit.

Actual Android device gameplay and performance still need confirmation.
Correctness tests do not establish a frame-time or FPS improvement.

Balance GMSTs and hand-to-hand fatigue settings are applied through standard
OpenMW's LOAD API before opening a save. Edit load-time defaults in
scripts/ngc/openmw/defaults.lua and restart. Runtime options use the Scripts menu.

Before removing this mod from a save, disable Always Hit and Weapon perks,
release guard/draw, allow four unpaused seconds for cleanup, then save and exit.

## Credits and references

Original NGC mod, unchanged ESPs, icon and MWSE implementation: **Aracelliy**.
The original author permits adaptations with credit. OpenMW adaptation prepared
for DieuDeGlace's Windows/Android setup.

- Original: https://www.nexusmods.com/morrowind/mods/46993
- MWSE attack event: https://mwse.github.io/MWSE/events/attack/
- MWSE hand-to-hand event: https://mwse.github.io/MWSE/events/damageHandToHand/
- OpenMW Combat API: https://openmw.readthedocs.io/en/latest/reference/lua-scripting/interface_combat.html
