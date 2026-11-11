# Updated OpenMW performance build

This fork combines OpenMW development revision `0ca40dbaf5b530d15265b126d446240ea6c7851f` (0.53.0 development) with kartoffels-ci/openmw `wip-merged` revision `fefb35e4a51c98da305c2b6031af34666973d84f`.

Preserved features include Intel Masked Occlusion Culling, terrain and simplified static occluders, active-grid object paging culling, the occlusion debug overlay, shadow temporal reuse with its staleness limit and frustum expansion, soft shadows, and in-game shadow controls. The bundled MOC code retains its x86 SIMD dispatch and ARM sse2neon support. Original branches and commit history are retained.

## Enable the performance features

The original defaults are preserved: occlusion culling is off and shadow update interval is 1. Merge the settings from `performance-preset.cfg` into your OpenMW `settings.cfg`, under the matching section names, then restart OpenMW. On Windows the usual location is `Documents\My Games\OpenMW\settings.cfg`. Keep your other settings.

The supplied preset enables software occlusion culling and sets shadow update interval to 2. Shadow reuse matters when shadows are enabled. These are experimental optimizations: measure frame times in your own dense scenes. Occluded shadow casters can lose shadows; temporal reuse can produce shadow lag or brief artifacts. Set `occlusion culling = false` or `shadow update interval = 1` to disable the corresponding feature.

## Build on Windows

Install Visual Studio 2022 Build Tools with the C++ desktop workload, Git, and 7-Zip. In PowerShell at the repository root run:

```powershell
powershell -ExecutionPolicy Bypass -File CI\build-windows-release.ps1 -Jobs 4
```

The script installs the upstream dependency versions into `deps`, verifies the vcpkg archive against the official SHA-512 manifest, configures x64 `Release` with link-time optimization, builds all desktop programs and the three upstream test suites, runs those suites, stages the portable program, runs its version command, and writes a ZIP and SHA-256 checksum into `release`.

The Windows Performance Release workflow provides the same build through GitHub Actions. Game data is not included. A Release configuration is an optimized build of this development fork, not an official stable OpenMW release. Testing with real game data and target GPUs is required to assess visual correctness and performance.


## Adjustable soft shadows (performance release 2)

In the PC game open Options → Video → Shadows, enable Soft Shadows, then set
Soft Shadow Width. The 0.5–3.0 multiplier is independent of map resolution;
1.0 preserves the existing soft filter. It updates through the normal Shadows
settings handler. The eight-sample count and all culling/shadow-reuse features
are retained. Higher widths may reduce small contact shadows.

For settings.cfg, merge into the existing [Shadows] section:

    soft shadows = true
    soft shadow radius = 1.5

This setting belongs to the performance build; stock OpenMW 0.51 requires this
build or its own compatible shader modification. Existing defaults remain.

## Physics preview 3: general actor and item physics

Actor and object physics are enabled by default. Deaths attempt physics for all
NPC and creature model
types, including custom skeletons and incomplete Bip01 variants. Existing tuned
rigs remain preferred. Automatic discovery uses actual skin influences or
geometry-bearing transforms and limits each generated rig to 24 bodies.
Vertex-morphed/unrigged models such as the kwama forager get whole-body collision
and dragging; independent limbs require usable joints. The existing awake and
retained-rig limits still apply. Living actors keep their normal animation and movement.

The complete Windows package includes openmw-launcher.exe and all other desktop
applications. Launch openmw-launcher.exe or openmw.exe directly; no command
script or environment flag is required. OPENMW_RAGDOLLS=0 disables the native
physics backend, while OPENMW_ITEMS=0 disables only native items. The existing
Start-Physics-Launcher.cmd remains an optional helper that explicitly enables
physics. See RAGDOLL-TEST.md for the opt-in comprehensive actor replay.

## Native items

The preview follows the default object rules of MaxYari's Lua Physics Engine:
https://github.com/MaxYari/OpenMWLuaPhysics (reference 1.4).
Clothing, armor, books, ingredients, miscellaneous items, potions, weapons,
apparatus, lockpicks, probes and repair tools can be pushed, grabbed and thrown.
Doors, containers, static scenery, activators and lights stay with their
ordinary behavior. Untouched items remain still; interaction wakes gravity.
Tap Activate to pick up; hold Activate to grab; Use while holding throws.

The default Potato collision policy uses world geometry and living actors,
with item/item and item/corpse collisions disabled. Items sleep after one second
below the linear speed threshold. Native limits are 32 awake and 128 retained
items on desktop, 16 awake and 64 retained on Android. Item transforms update
ordinary world records and survive saves; actor joint poses remain session-only.
Owned-item disturbance/destruction uses the engine's crime detection.

Optional installed meshes/debris assets allow matching unscripted miscellaneous
items to break into bounded debris. No third-party meshes, sounds, scripts or
textures are distributed. Without matching debris assets, items stay intact.
This is an independent native implementation of the defaults, not the mod's
full Lua interfaces, optional Normal collision mode or Impact Effects package.

Native item simulation automatically yields when LuaPhysicsEngine.omwscripts
or its engine scripts are configured. To use native items, disable that manifest;
its asset data folder may remain enabled for optional debris. Actor ragdolls
remain available. OPENMW_ITEMS=0 disables native items independently.


## Animation-guided body morph physics

Death-only skinning now derives bend regions and smooth weights from a model's
original vertex-morph animation. The kwama forager can bend in sections; other
large deforming body meshes use the same bounded fitter. Existing limb bones
remain connected to their original owners in mixed skeleton/morph models.
Living models keep their original animation exactly. Death conversion freezes
the current pose before binding the generated skin, and resurrection restores
the original model. No modified game assets are distributed.

This remains jointed rigid-body physics; elastic squash/stretch from impacts
is not implemented. Unsupported/oversized geometry retains existing automatic
or whole-model physics. See scripts/ragdoll-test/README.md for resource limits.

## Physics preview 7: perspective soft-shadow receiver

Soft-shadow directions now preserve homogeneous W when projecting the receiver
plane through LiSPSM. The previous per-pixel lighting path dropped W, making the
comparison plane disagree with the surface as the camera moved. The filter keeps
its eight texture reads and adjustable width. This changes shader resources only;
actor/item physics, shadow reuse and occlusion settings retain their behavior.

`CI/test-shadow-filter-gpu.py` runs the production GLSL against an independent
inverse-transpose plane oracle over 57 camera rotations, under orthographic and
perspective projections. The release workflow runs it with headless Mesa before
building the complete desktop package. To run locally, install `moderngl==5.12.0`
and `numpy==2.2.6`, then run `python CI/test-shadow-filter-gpu.py`.
GPU fixtures do not establish that every game scene or driver is flicker-free.


## Chronological shadow reuse and unnamed morph rigs

The alternating render buffers of one view now share a shadow-map history,
using OpenSceneGraph's view identifier. Cached frames use the latest map instead
of switching between independently fitted older maps. Projection uniforms remain
double buffered, and different view identifiers retain independent histories.
Interval 2 still skips every other shadow update while walking at 60 FPS;
projection changes and the existing 40 ms age limit force a refresh.

Morph-generated rigs can attach an unnamed mesh directly to an unnamed skeleton.
The skin transform fallback now stays within that node path, preventing the
out-of-bounds traversal seen immediately after kwama forager ragdoll activation.
Four skinning tests and eight real shadow-cull tests cover these changes.
The Windows release continues to include the launcher and all desktop tools.
