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

## Physics preview 3: general actor model support

With OPENMW_RAGDOLLS=1, deaths attempt physics for all NPC and creature model
types, including custom skeletons and incomplete Bip01 variants. Existing tuned
rigs remain preferred. Automatic discovery uses actual skin influences or
geometry-bearing transforms and limits each generated rig to 24 bodies.
Vertex-morphed/unrigged models such as the kwama forager get whole-body collision
and dragging; independent limbs require usable joints. The existing awake and
retained-rig limits still apply. Loose-item physics is outside this change.

The complete Windows package includes openmw-launcher.exe and all other desktop
applications. Start-Physics-Launcher.cmd enables physics for launcher-started
games. See RAGDOLL-TEST.md for the opt-in comprehensive actor replay.
