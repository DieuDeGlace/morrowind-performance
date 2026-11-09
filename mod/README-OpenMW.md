# Next Generation Combat — OpenMW native parity preview 1

An OpenMW Lua translation of **Next Generation Combat 1.2.2 by Aracelliy**.
The original, unmodified ESPs and icon are included. Original mod and author:
https://www.nexusmods.com/morrowind/mods/46993

## Install or update

Use the Windows/Android **Physics Preview 11** engine for the native combat hooks.
Windows is a complete OpenMW build; start it through `openmw-launcher.exe`.
On Android, update the existing Physics Preview app using the signed APK.

Extract this mod into its own data directory, or replace the files in the existing
NGC OpenMW mod directory. Keep the same content entries, in this order after the
Morrowind/Tribunal/Bloodmoon masters:

1. `Next Generation Combat.esp`
2. `Next Generation Combat - Hand To Hand Fixes.esp`
3. `Next Generation Combat.omwscripts`

Example:

```ini
data="C:\Games\Morrowind Mods\Next Generation Combat OpenMW"
content=Next Generation Combat.esp
content=Next Generation Combat - Hand To Hand Fixes.esp
content=Next Generation Combat.omwscripts
```

Load the manifest after other combat scripts. Use **Options → Scripts → Next
Generation Combat** for configuration. Existing port settings keep their section
names and remain stored in each save. The settings page identifies native or
compatibility mode. MWSE JSON files are not read by OpenMW.

## Native combat behavior

The engine's optional hooks replace three approximations used by the old port:

- **Always Hit:** sets the actor's actual native hit chance to 100. No Fortify
  Attack buff is needed; real Fortify Attack still contributes to NGC damage.
- **Held shield guard:** uses the native shield block path at 100% chance while
  guarding. Normal readiness, shield condition, facing arc, fatigue loss, shield
  wear, Block experience and block reactions remain part of the engine path.
- **Hamstring and backward full draw:** multiply final movement speed, including
  swimming/levitation. They do not change Speed or distort diagonal movement.

An unarmed commitment event fires at attack release with an identified living
target. The independent fist health hit is processed before the ordinary native
hit/miss. It also occurs when the native fatigue hit misses or is blocked, as in
the original mod. Werewolves and ordinary unarmed creatures do not receive the
NPC hand-to-hand supplement. Creature damage retains the original strength bonus.

| Weapon | Preserved mechanics |
| --- | --- |
| Short blade | Critical strike, execute, original skill tiers |
| Long blade | Original counter initialization/fourth-hit trigger, double strike, timed riposte |
| Axe | Three bleed ticks and original stack limits |
| Blunt | Summed base armor bonus and one-second stun |
| Spear | Fatigue advantage and original three-second `mic_adrenaline_rush` ability |
| Fists | Separate physical health hit, strength/armor/knockdown scaling, separate fatigue hit |
| Bow | Three-second full draw, stored one-use bonus, hamstring, fatigue drain, sneak zoom |
| Crossbow | Original close-range bonus and accelerated Repeater reload |
| Thrown | Agility scaling, critical chance and recovery tiers |

Hold **Z** with a usable readied shield to guard. The key and optional right mouse
binding are configurable. A controller or Android button must be mapped to the
chosen key. The original guard icon is included.

Temporary effects expire on the first eligible simulation frame. Idle control/AI
polling remains at four updates per second; no global nearby-actor scans are used.
Older saved hamstring Speed penalties are removed when native mode takes over.
The old accuracy ability is also removed, with its record retained for save
compatibility. Bow zoom restores correctly across native float rounding.

## Parity coverage and boundaries

The regression suite executes the **unmodified original MWSE scripts** as its
comparison oracle. Each engine/mode run checks 540 fist combinations, 10,240
weapon-hit combinations, eight creature/werewolf/Blind combinations, and 70
fortified, drained, exhausted or negative-fatigue cases: **10,858 direct numerical
comparisons per run**. Native runs pass 47 test groups; compatibility runs pass 45.
Additional checks cover perk timers, skills, cleanup, guarding and saved state.
The real OpenMW Lua Combat handlers are exercised for both engine API 129
(Android/0.51) and API 166 (Windows/0.53).

Native physical-attack and binding checks use a separate packaged-engine gameplay
scenario. See the accompanying validation report for completed checks and scope.
Actual Pixel 10 Pro gameplay/performance still requires device confirmation.

OpenMW's settings UI and asynchronous script scheduling differ from MWSE. Ordinary
weapon bonuses still resume the defender's Combat chain after an attacker event
round trip. Repeater changes the actor's animation speed instead of mutating a
shared weapon record. Exhausted bows are sheathed to cancel the draw. Projectile
perks use the weapon that produced the hit; projectile recovery is per hit rather
than a shared global roll. These are intentional engine adaptations. This preview
does not certify identical behavior for every MWSE interaction or other combat mod.

Hand-to-hand fatigue multipliers and balance GMSTs are applied before loading a
save. Edit their load-time values in `scripts/ngc/openmw/defaults.lua` and restart
OpenMW. Other settings can be changed from the Scripts menu.

On stock OpenMW API 129 or newer, compatibility mode retains the older spell-based
accuracy, scripted guard and Speed-modifier approximations. Native parity requires
an engine exposing `core.COMBAT_API_VERSION >= 1`, such as Physics Preview 11.

Before removing this mod from a save, disable Always Hit and Weapon perks, release
guard/draw, allow four unpaused seconds for effect cleanup, then save and exit.

## Credits and references

Original mod, ESPs, icon and MWSE implementation: **Aracelliy**. The original
author permits adaptations with credit. OpenMW port and native integration were
prepared for DieuDeGlace's performance/physics fork.

- Original: https://www.nexusmods.com/morrowind/mods/46993
- MWSE attack event: https://mwse.github.io/MWSE/events/attack/
- MWSE damage event: https://mwse.github.io/MWSE/events/damage/
- OpenMW Combat API: https://openmw.readthedocs.io/en/latest/reference/lua-scripting/interface_combat.html
- Native hook fields are documented in the fork's `files/lua_api/openmw/self.lua`.
