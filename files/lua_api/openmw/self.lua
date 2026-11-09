---
-- Provides full access to the object the script is attached to.
-- All fields and function of `GameObject` are also available for `openmw.self`.
-- @context local
-- @module Self
-- @extends openmw.core#GameObject
-- @usage local self = require('openmw.self')
-- local types = require('openmw.types')
-- if self.type == types.Player then  -- All fields and functions of `GameObject` are available.
--     self:sendEvent("something", self.position)
-- end



---
-- Returns true if the script isActive (the object it is attached to is in an active cell).
-- If it is not active, then `openmw.nearby` can not be used.
-- @function [parent=#Self] isActive
-- @param Self
-- @return #boolean

---
-- The object the script is attached to (readonly)
-- @field [parent=#Self] openmw.core#GameObject object


---
-- Movement controls (only for actors)
-- @field [parent=#Self] #ActorControls controls

---
-- @type ATTACK_TYPE
-- @field #number NoAttack
-- @field #number Any
-- @field #number Chop
-- @field #number Slash
-- @field #number Thrust

---
-- Allows to view and/or modify controls of an actor. All fields are mutable.
-- @type ActorControls
-- @field [parent=#ActorControls] #number movement +1 - move forward, -1 - move backward
-- @field [parent=#ActorControls] #number sideMovement +1 - move right, -1 - move left
-- @field [parent=#ActorControls] #number yawChange Turn right (radians); if negative - turn left
-- @field [parent=#ActorControls] #number pitchChange Look down (radians); if negative - look up
-- @field [parent=#ActorControls] #boolean run true - run, false - walk
-- @field [parent=#ActorControls] #boolean sneak If true - sneak
-- @field [parent=#ActorControls] #boolean jump If true - initiate a jump
-- @field [parent=#ActorControls] #ATTACK_TYPE use Activates the readied weapon/spell according to a provided value. For weapons, keeping this value modified will charge the attack until set to @{#ATTACK_TYPE.NoAttack}. If an @{#ATTACK_TYPE} not appropriate for a currently equipped weapon provided - an appropriate @{#ATTACK_TYPE} will be used instead.

---
-- Enables or disables standard AI (enabled by default).
-- @function [parent=#Self] enableAI
-- @param Self
-- @param #boolean v

return nil

--- Native hit chance override for attacks made by this actor. Nil restores the
-- native formula. Finite values from 0 to 100; native range and other eligibility
-- checks still apply. Requires core.COMBAT_API_VERSION >= 1.
-- @field [parent=#ActorControls] #number hitChanceOverride

--- Native melee shield block chance override. Nil restores the native formula.
-- Requires a usable shield, a ready actor, and an attacker inside the normal
-- blocking arc. Finite values from 0 to 100. Does not change global GMSTs.
-- @field [parent=#ActorControls] #number blockChanceOverride

--- Multiplier applied once to the final native NPC/creature movement speed,
-- including swimming and levitation. Default 1; finite values from 0 to 1000.
-- Does not modify Speed, skills, movement input or AI ownership.
-- @field [parent=#ActorControls] #number movementSpeedMultiplier

--- Subscribe this actor to UnarmedAttack local events. Default false. An event
-- contains attacker, strength and type for a non-werewolf NPC's unarmed attack
-- with an identified living target, before the ordinary Hit event, independently
-- of the native hit roll or shield block. Delivery remains asynchronous.
-- Values reset with script-container creation; set/clear on activation/deactivation.
-- @field [parent=#ActorControls] #boolean unarmedAttackEvents
