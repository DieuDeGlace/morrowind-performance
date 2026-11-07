# Native ragdoll experiment

Set OPENMW_RAGDOLLS=1 for this OpenMW process. Actor animations hand off
supported skeletons at death. The Bip01 and BBip01 maps cover humanoids and
creatures with matching bones. Separate maps cover cliff racer wings and
tail, shalk legs, and mudcrab claws and legs. The normal death animation
handles other skeletons.

The rig parts use the original skeleton and a shared Bullet solver. They
collide with terrain, doors and world geometry. Player melee can wake a
settled corpse and push the part it reaches. Bodies retain their inventory
and ordinary activation. Tap Activate to loot a supported corpse. Hold
Activate for about a third of a second to grab a limb, then walk or look
around to drag it. Releasing Activate drops the body. Joint poses are
session-only.

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

For an input-driven drag replay, use ragdoll-drag-smoke.txt instead.
Wait for RAGDOLL_TEST_DRAG_READY, tap Activate once, then hold Activate
and move. The log records the tap, the grabbed body part and the distance
it moved before release.

## Skeleton ownership

During ragdoll playback, embedded transform animation callbacks on simulated
bones and their ancestors are suspended. Otherwise a NIF controller can restore
its cached animation rotation after the physics pose is applied, making an
articulated rig appear rigid. Unrelated callbacks (such as effects) keep running.
Controllers are restored when normal animation resumes or the rig is removed.
The pose is reapplied each animation update, including frames without a physics
substep and root transform changes. This code is shared by Windows and Android.

The RagdollControllers tests in openmw-tests reproduce the embedded NIF rotation
overwrite and check callback preservation and restoration. For visual validation,
use the replay above and check an NPC, guar, cliff racer, shalk, and mudcrab during
their initial fall; limbs should rotate relative to the torso before settling.