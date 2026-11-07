param([Parameter(Mandatory)][string]$PackageDir)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# A physics preview is still a complete portable OpenMW distribution.
$required = @(
    'openmw.exe', 'openmw-launcher.exe', 'openmw-wizard.exe', 'openmw-cs.exe',
    'openmw-essimporter.exe', 'openmw-iniimporter.exe', 'openmw-navmeshtool.exe',
    'openmw-bulletobjecttool.exe', 'bsatool.exe', 'esmtool.exe', 'niftest.exe',
    'defaults.bin', 'defaults-cs.bin', 'openmw.cfg', 'gamecontrollerdb.txt',
    'Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll', 'Qt6Network.dll',
    'Qt6OpenGL.dll', 'Qt6OpenGLWidgets.dll', 'Qt6Svg.dll',
    'platforms/qwindows.dll', 'imageformats/qsvg.dll', 'iconengines/qsvgicon.dll'
)
$missing = @($required | Where-Object {
    $path = Join-Path $PackageDir $_
    # files/openmw-cs.cfg is intentionally empty upstream; its packed file
    # must exist, but has no bytes until editor defaults are added.
    -not (Test-Path -LiteralPath $path -PathType Leaf) -or
        ($_ -ne 'defaults-cs.bin' -and (Get-Item -LiteralPath $path).Length -eq 0)
})
foreach ($directory in 'resources/shaders', 'resources/vfs') {
    $path = Join-Path $PackageDir $directory
    if (-not (Test-Path -LiteralPath $path -PathType Container) -or
        -not (Get-ChildItem -LiteralPath $path -File -Recurse | Select-Object -First 1)) {
        $missing += $directory
    }
}
if ($missing.Count) {
    throw "Incomplete OpenMW package. Missing or empty: $($missing -join ', ')"
}
Write-Host 'Complete OpenMW package verified (engine, launcher, wizard, editor, tools, Qt, and resources).'