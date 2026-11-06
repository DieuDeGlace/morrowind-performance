# Tarhiel replay

This scene replays the original fall with the current native ragdoll
implementation. The Bip01 hierarchy was adapted from
[lihogloglo's experimental physics fork](https://github.com/lihogloglo/openmw-dev/tree/physics).

Set OPENMW_RAGDOLLS=1. Use a disposable profile, register data/ from this
directory and load TarhielTest.omwscripts after the Morrowind masters.
Start with --skip-menu --start "Seyda Neen". The scene moves the player
near the vanilla reference to trigger his scripted fall.

For automation, place an empty tarhiel-smoke.txt in the test data
directory. The scene checks that he dies with all three Scrolls of
Icarian Flight, captures the pose and exits. See the ragdoll-test directory
for the multi-actor replay and melee checks.
