# Native ragdoll experiment

Set OPENMW_RAGDOLLS=1 for this OpenMW process. Actor animations hand off
supported skeletons at death. The Bip01 and BBip01 maps cover humanoids and
creatures with matching bones. Separate maps cover cliff racer wings and
tail, shalk legs, and mudcrab claws and legs. The normal death animation
handles other skeletons.

The rig parts use the original skeleton and a shared Bullet solver. They
collide with terrain, doors and world geometry. Player melee can wake a
settled corpse and push the part it reaches. Bodies retain their inventory
and ordinary activation. Joint poses are session-only.

The solver takes fixed 120 Hz steps while bodies are awake and stops when
the last rig settles. It caps simultaneous awake rigs at 12 and retained
rigs at 64. Rig bodies stay outside the normal actor movement and ray query
filters. Rig profiles are in apps/openmw/mwphysics/ragdoll.cpp.

## Replay

Use a disposable OpenMW profile. Add the data directory here and load
RagdollTest.omwscripts after the Morrowind masters. Start with --skip-menu
--start "Seyda Neen". The scene prepares several corpses in the Census
Office so you can aim a normal attack at them.

To run the automated replay, put an empty ragdoll-smoke.txt in the same
data directory. The replay checks indoor death and inventory, delivers
player melee swings, then stages five exterior creatures and swings a
sword at a guar. The log records rig creation, articulation, settling
and hit location. The same replay with OPENMW_RAGDOLLS=0 exercises the
original death path.
