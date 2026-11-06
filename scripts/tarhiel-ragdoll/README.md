# Tarhiel ragdoll experiment

This branch is based on the Windows performance revision e9a484dcf5. The
prototype uses the Bip01 hierarchy from
[lihogloglo's physics fork](https://github.com/lihogloglo/openmw-dev/tree/physics)
as its rig reference. The game target links Bullet Dynamics for the test.

Set OPENMW_TARHIEL_RAGDOLL=1 for the test process. The NPC animation hands the
pose to the rig at death for the vanilla Tarhiel record, agronian guy, in
exteriors. The 11 bodies and 10 joints use a local terrain snapshot. The
simulation steps at 120 Hz, applies a pose only after a physics step, and stops
when the rig stays still or Bullet puts the bodies to sleep. The rig releases its resources when the model unloads.

To replay the scene, use a separate config and user data directory, register
data/ from this directory, and enable TarhielTest.omwscripts after the
Morrowind masters. Start with --skip-menu --start "Seyda Neen". The scene
moves the player near the vanilla reference to trigger the original fall.

For automated checking, place an empty tarhiel-smoke.txt in the test data
directory. The scene checks that the actor dies with all three Scrolls of
Icarian Flight, captures poses and exits. The engine log reports activation,
independent elbow and knee motion, and settling. A run with the flag set to
0 exercises the normal death animation in the same scene.

This test covers the terrain at Tarhiel's landing site. It does not yet
include static objects, other actors, grab/throw controls or saved joint
poses in the ragdoll simulation.
