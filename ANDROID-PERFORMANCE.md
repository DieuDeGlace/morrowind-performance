# Android performance engine

This branch retains the latest Andiweli/OpenMW-Android mobile compatibility and graphics patches on the upstream OpenMW 0.51.0 base (`f4bec41444214a7903bebd178389ca22ca13f646`). It adds the software scene occlusion culler, terrain/static/paged occluders, and temporal shadow reuse from kartoffels-ci/openmw `wip-merged` (`fefb35e4a51c98da305c2b6031af34666973d84f`).

The existing Android GLES2 PCF shadow filtering, CPU sun-occlusion rays for OMWFX, active-grid paging, Android Surface lifecycle bridge, touch/controller support, and current graphics fixes are preserved. Android uses its existing PCF quality controls instead of the desktop hardware-shadow-sampler filter. Texture-slot resizing is adapted to avoid colliding with other renderer reservations.

The source is prepatched so the app can download this exact revision as a checksummed archive and build without repeating sequential historical patch scripts. The app repository records the engine revision and archive digest. Android engine defaults remain unchanged; the new launcher controls enable scene culling and shadow reuse explicitly.
