# Next Generation Combat - OpenMW 0.51.0

A native Lua port of the supplied Next Generation Combat 1.2.2, revised to follow its original combat formulas and use its **two original, unmodified ESPs**. This is a close behavioral port, not a certified 1:1 replacement for MWSE. In-game validation is still required.

## Install

Extract the upload ZIP into its own mod folder and add that folder as an OpenMW data directory. Enable these content files in this order, after their Morrowind/Tribunal/Bloodmoon masters:

1. `Next Generation Combat.esp`
2. `Next Generation Combat - Hand To Hand Fixes.esp`
3. `Next Generation Combat.omwscripts`

Load the manifest after other combat scripts. Both original ESPs are checked at startup. The hand-to-hand fix ESP retains its original miss-sound and WerewolfClawMult records. No replacement ESP is generated.

When using the development workspace, add its **mod** subfolder as the data directory. Do not add the development folder. Example configuration for an extracted ZIP:

```ini
data="C:\Games\Morrowind Mods\Next Generation Combat OpenMW"
content=Next Generation Combat.esp
content=Next Generation Combat - Hand To Hand Fixes.esp
content=Next Generation Combat.omwscripts
```

Configure the port under **Options -> Scripts -> Next Generation Combat**. Settings are stored per save. Initial values match the supplied MWSE configuration; OpenMW does not read the MWSE JSON at runtime.

## Hand-to-hand health damage

Fists inflict a **separate physical health hit in addition to normal engine fatigue damage**. The supplemental hit is not a magical Damage Health effect. Its calculation follows the original Lua:

1. Pick the base health damage from the hand-to-hand skill tier.
2. Add the original strength bonus: `base * (1.5 + strength / 100)`.
3. Apply the tier's extra damage when NGC tracks the target as knocked down.
4. Reduce that health hit using the sum of the equipped armor pieces' base ratings and `fCombatArmorMinMult`.
5. Apply the original fatigue/Sanctuary reductions and skill/Fortify Attack bonus.
6. Apply difficulty using the original helper call's non-player-attack convention.

The supplemental health hit has its own armor calculation and is never sent through normal armor a second time. Normal unarmed fatigue damage remains separate. A native missed fist attempt can still receive this health hit, as the original script applies the supplement at attack commitment. The exact commitment timing cannot be reproduced by the 0.51 hit callback; see differences below.

## Other restored mechanics

| System | Behavior |
| --- | --- |
| Short blade | Original critical chances, critical multiplier, execute threshold, and tier bonuses |
| Long blade | Original counter initialization and three-count trigger, double strike, and timed riposte |
| Axe | Three bleed ticks and the original tier stack limits |
| Blunt | Bonus damage based on summed base armor ratings; a one-second paralyze-attribute stun |
| Spear | Fatigue advantage damage and the ESP's actual `mic_adrenaline_rush` ability, removed after three seconds |
| Bow | Three-second full-draw timer, one-use stored bonus, hamstring, fatigue drain, sneak zoom, and backward-movement reduction |
| Crossbow | Original close-range bonus; Repeater accelerates the reload toward the original speed of 10 |
| Thrown | Original agility scaling, critical chances, and tier recovery chances |
| Shield | Held guard, original icon, engine-facing angle limits, shield wear, fatigue costs, and Block experience |
| Experience | Original melee/Block and armor skill selection; Marksman is not additionally scaled |

Hold **Z** with a usable readied shield to guard. The block key can be changed using an OpenMW key name such as `Space` or `LeftAlt`. Right mouse is optional; rebind OpenMW's Inventory action if it also uses right mouse. The original guard icon is included and shown on the HUD.

Weapon bonuses now operate **after armor and before difficulty**, following the original damage callback's placement. Fist armor uses the original summed base ratings. NPC knockdown is animation-based again, without the first prototype's added three-second paralysis spell. Player knockdown uses negative fatigue and restores the saved fatigue value after two seconds. The original multistrike and high-agility boundary quirks are preserved rather than rebalanced. Blind also retains the original early-return behavior: its 'Missed!' message does not cancel the incoming base damage.

The initial hand-to-hand fatigue multipliers and balance GMSTs are set before saves load. Change their values in `scripts/ngc/openmw/defaults.lua` and restart OpenMW to change those load-time settings. Runtime perk switches do not rewrite already loaded GMSTs.

## Remaining engine differences

- **Accuracy:** OpenMW 0.51 has no MWSE `calcHitChance` hook. A private 10,000-point Fortify Attack ability keeps the native accuracy/enchantment/weapon-wear path. Its own magnitude is excluded from NGC damage bonuses. Extreme modded evasion can still exceed it, and it may appear in active-effect displays.
- **Hit timing:** A physical hit takes one extra local-event round trip so the attacker owns its combo and riposte state. The fist supplement is initiated from native hit/miss events, rather than the earlier MWSE commitment event. A commitment that produces no native hit event cannot receive an identical supplement. This can affect range changes, blocked attacks, reactions, and death attribution.
- **Animation and movement:** NPC knockdown uses OpenMW animation control. Repeater accelerates the reload animation because shared weapon records cannot be edited in place. Hamstring approximates the original final-speed multiplier through a temporary Speed modifier; interactions with Athletics, movement mods, and animation timing can differ. An exhausted bow is sheathed to cancel it instead of using the original dummy-arrow workaround.
- **Combat end:** Cleanup is inferred from participating NPCs' Combat AI packages, with a short grace period. Custom AI can differ from MWSE's exact player combatStopped event.
- **UI:** OpenMW's Scripts menu replaces MWSE MCM. The original guard texture is reused, but HUD placement and MGE zoom rendering can differ.
- **Projectiles:** The engine's shared recovery roll is replaced by one scripted roll per successful ranged hit. Ordinary arrow/bolt recovery retains the previous GMST chance; thrown recovery uses the attacker's tier. Enchanted ammunition is excluded. Another recovery overhaul needs a compatibility patch.

Timers are updated at quarter-second intervals. OpenMW's own armor, difficulty, stagger, and stat behavior still applies; another combat mod may require a patch. This build targets **Lua API 129 / OpenMW 0.51.0** and rejects other API revisions rather than claiming untested support.

## Validation

33 Lua 5.1 test groups pass with OpenMW 0.51's real Combat scripts in an engine-double harness. They include direct comparisons with the bundled original MWSE main/perk scripts: 30 fist armor/skill cases and 64 weapon-hit comparisons across eight weapon families. Tests also cover the original ESP hashes, separate fatigue damage, player fatigue restoration, full-draw timing, combat cleanup, guard direction, and avoiding duplicate armor application.

**No actual OpenMW gameplay session has been run.** Native record loading, animation timing, rendering, and engine scheduling still need in-game checks. Test fists against armored and unarmored targets, then check guard, full draw, temporary effects, save/load, Repeater, and thrown recovery. Keep the port's practical API differences in mind when comparing with MWSE.

Before disabling the port on an existing save, turn off Always hit and Weapon perks, release guard/draw, allow four unpaused seconds for cleanup, save, and exit. Then disable the manifest and the two ESPs as appropriate for that save.

References: [OpenMW 0.51 Combat](https://openmw.readthedocs.io/en/openmw-0.51.0/reference/lua-scripting/interface_combat.html), [OpenMW 0.51 source](https://github.com/OpenMW/openmw/tree/openmw-0.51.0), and [MWSE damage](https://mwse.github.io/MWSE/events/damage/).
