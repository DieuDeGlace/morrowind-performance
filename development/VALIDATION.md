# Native parity preview 1 validation

Validated on 2026-11-09. These results establish the listed mechanics and tested
inputs; they do not certify identical behavior for every MWSE interaction.

## Source and build provenance

| Component | Commit | Successful GitHub build |
| --- | --- | --- |
| Windows engine | `2ec1f4284a3cdd8d4f11591d349b2d575b947e36` | [42605842132](https://github.com/DieuDeGlace/morrowind-performance/actions/runs/42605842132) |
| Android engine | `624b0bc81fbab02d046c1772c1d889f6b7f78547` | Compiled by the wrapper build below |
| Android wrapper | `25ea9b78d07a392553bf47ef2c289e57db99cfe5` | [42613626712](https://github.com/DieuDeGlace/morrowind-performance-android/actions/runs/42613626712) |

Windows passes **2,400 C++ tests** (1,666 + 580 + 154). The full Release/LTO
package contains all 11 executable tools, including `openmw-launcher.exe`,
`openmw.exe` and `openmw-cs.exe`. The packaged Qt launcher starts successfully
with an isolated profile; its temporary package config change is restored and
hash-checked. All 626 extracted files match the archive.

Android's signed ARM64 Release build passes the Gradle unit-test task, 11 engine
cache tests, five APK metadata tests and four base-config checks. Its engine pin
and archive digest agree across the wrapper's build files. APK version 5130,
application ID and signing certificate match the Physics Preview update path.
Both CI pipelines pass the GPU shadow-filter check. Native shadow changes from
Physics Preview 10 remain present; this change does not modify shadow rendering.

## Lua comparison matrix

The harness executes the actual Windows/API 166 and Android/API 129 Combat
scripts, each in portable and native-hook modes. Every run performs **10,858
direct numerical comparisons** against the preserved original MWSE scripts:

- 540 fist cases across skill, armor, strength, fatigue and Always Hit.
- 10,240 weapon-family, tier, toggle, fatigue and distance combinations.
- Eight creature, werewolf and Blind cases.
- 64 fortified/drained-stat and negative-fatigue weapon cases.
- Four fortified/exhausted fist cases and two guard/bow fatigue cases.

All four runs pass: **47 groups per native run; 45 per compatibility run**.
Further cases exercise timed effects, skill scaling, armor order, event chaining,
inactive/save cleanup, malformed/missing content, native flags, FOV restoration,
and both nil and negative inactive-animation timestamps.

The harness represents native control fields; it does not execute C++ hooks.
Real native coverage comes from the separate packaged-engine gameplay checks.
The original `development/MWSE` files and both runtime ESPs are unchanged.

## Packaged Windows gameplay

The isolated startup scenario verifies optional chance overrides (nil, zero,
100 and rejected invalid values), native Always Hit without its old spell,
and a 0.5 movement multiplier actually halving a moving player's native speed.

The isolated physical scenario uses ordinary native attacks and projectile
collisions, with a generated floor fixture and assets from the user's Morrowind
installation. Its final run has no Lua errors and confirms:

- Four successful fist attacks at chance 100; four misses at chance zero.
- One independent health supplement per committed fist attack in both phases.
- Four frontal shield blocks with zero weapon health damage.
- Four damaging rear attacks despite the same shield override.
- A normal bow hit consuming no bonus and a full draw consuming exactly 1.0
  once, with the expected greater projectile damage.

Reproducible scenarios are in `development/integration`; they never edit the
user's saves or active settings. Generated test geometry is included in source,
not in the runtime mod ZIP. No proprietary game assets are redistributed.

The initial live run found an inactive bow timestamp returning nil. The production
guard now accepts both API representations, with a regression and a clean live
retest. Test-only settings writes were also corrected to address registered tier
options only. Earlier failed fixture runs are retained locally and not counted
as passes.

## Reproduce

With Python 3.12 and Lupa 2.6 (`python -m pip install lupa==2.6`):

```text
python development/tests/run_tests.py --engine-source PATH_TO_ENGINE
python development/tests/run_tests.py --engine-source PATH_TO_ENGINE --native-hooks
python development/package.py
```

Repeat the first two commands with each engine checkout. `--quick` skips the
two large numerical loops; it is not a substitute for the full matrix.
For the real Windows executable:

```powershell
./development/integration/run-gameplay.ps1 -PackageDir PATH_TO_BUILD -GameData PATH_TO_DATA_FILES -Label native-startup
./development/integration/run-gameplay.ps1 -PackageDir PATH_TO_BUILD -GameData PATH_TO_DATA_FILES -Label native-attacks -Physical
```

Labels create new private profiles under `development/results/integration`.
A nonzero exit, timeout, missing pass marker or any Lua error fails the run.

## Limits

Actual Pixel 10 Pro gameplay, controller/Android guard mappings and device timing
remain unverified. Android compilation and its real API 129 Lua scripts pass;
the native Android executable has not been exercised on a phone in this session.
No combat or shadow frame-time claim is inferred from these correctness tests.

Ordinary weapon bonuses still use OpenMW's asynchronous event round trip.
Repeater adjusts actor animation speed, exhausted bows sheath, and recovery
rolls are per hit. The settings UI differs from MWSE. See the runtime README for
all adaptations, dependencies, installation and cleanup instructions.

## Package checks

`development/package.py` enforces a 15-file runtime allowlist, verifies both
original ESP hashes, checks every archived file against source and checks ZIP
CRCs. It emits a SHA-256 companion. Development scripts, original MWSE scripts,
test geometry and results are excluded from the runtime ZIP.
