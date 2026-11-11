param([Parameter(Mandatory)][string]$PackageDir)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
Copy-Item "$repoRoot/scripts/ragdoll-test/windows/*" -Destination $PackageDir -Force
foreach ($profile in 'profile', 'demo') {
    New-Item -ItemType Directory -Path "$PackageDir/$profile" -Force | Out-Null
    Copy-Item "$repoRoot/files/performance-preset.cfg" "$PackageDir/$profile/settings.cfg" -Force
}
New-Item -ItemType Directory -Path "$PackageDir/demo/scenario" -Force | Out-Null
Copy-Item "$repoRoot/scripts/ragdoll-test/data/*" -Destination "$PackageDir/demo/scenario" -Recurse -Force
Copy-Item "$repoRoot/scripts/ragdoll-test/README.md" "$PackageDir/RAGDOLL-TEST.md" -Force
@"
Run openmw-launcher.exe directly to configure and launch OpenMW. Actor ragdolls
and native object physics are enabled by default; no .cmd helper is required.
OPENMW_RAGDOLLS=0 disables the backend; OPENMW_ITEMS=0 disables only native items.
The package also includes the installation wizard,
construction set, importers, and command-line tools.

For an isolated direct game launch, run Setup-Game-Data.cmd once, then use
Start-Physics-Test.cmd. Start-Ragdoll-Demo.cmd opens the disposable test scene.
Your own installed Morrowind game data is required.
"@ | Set-Content "$PackageDir/PHYSICS-PREVIEW.txt" -Encoding utf8