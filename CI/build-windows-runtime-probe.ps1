param([ValidateRange(1, 32)][int]$Jobs = 4)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Assert-NativeExit([string]$Step) {
    if ($LASTEXITCODE -ne 0) { throw "$Step failed (exit $LASTEXITCODE)." }
}
function Download-File([string]$Uri, [string]$Destination) {
    & curl.exe --fail --location --retry 3 --silent --show-error --output $Destination $Uri
    Assert-NativeExit "Download $Uri"
}

$repoRoot = Split-Path -Parent $PSScriptRoot
Push-Location $repoRoot
try {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vsRoot) { throw 'Install Visual Studio 2022 Build Tools with the C++ workload.' }
    $devCmd = Join-Path $vsRoot 'Common7/Tools/VsDevCmd.bat'
    $devEnv = & cmd.exe /d /c "`"$devCmd`" -no_logo -arch=x64 -host_arch=x64 >nul && set"
    Assert-NativeExit 'Initialize MSVC'
    foreach ($line in $devEnv) {
        if ($line -match '^([^=]+)=(.*)$') {
            [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process')
        }
    }
    $cmake = Join-Path $vsRoot 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
    $ninja = Join-Path $vsRoot 'Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe'
    if (-not (Test-Path -LiteralPath $cmake)) { $cmake = (Get-Command cmake.exe).Source }
    if (-not (Test-Path -LiteralPath $ninja)) { $ninja = (Get-Command ninja.exe).Source }
    $sevenZip = (Get-Command 7z.exe -ErrorAction SilentlyContinue)
    if ($sevenZip) { $sevenZip = $sevenZip.Source }
    else { $sevenZip = Join-Path $env:ProgramFiles '7-Zip/7z.exe' }
    if (-not (Test-Path -LiteralPath $sevenZip)) { throw '7-Zip is required.' }

    $versions = Get-Content CI/deps_versions.msvc.sh -Raw
    $vcpkgTag = [regex]::Match($versions, '(?m)^VCPKG_TAG=[''"'']?([^''"''\r\n]+)').Groups[1].Value
    $qtVersion = [regex]::Match($versions, '(?m)^QT_VER=[''"'']?([^''"''\r\n]+)').Groups[1].Value
    if (-not $vcpkgTag -or -not $qtVersion) { throw 'Dependency versions are missing.' }
    $depsRoot = Join-Path $repoRoot 'deps'
    $vcpkgRoot = Join-Path $depsRoot "vcpkg-x64-windows-2022-$vcpkgTag"
    $qtParent = Join-Path $depsRoot 'Qt'
    $qtRoot = Join-Path $qtParent "$qtVersion/msvc2019_64"
    New-Item -ItemType Directory -Path $depsRoot, $qtParent -Force | Out-Null
    if (-not (Test-Path -LiteralPath "$vcpkgRoot/scripts/buildsystems/vcpkg.cmake")) {
        $manifest = Join-Path $depsRoot "vcpkg-x64-windows-2022-$vcpkgTag-manifest.txt"
        Download-File "https://gitlab.com/OpenMW/openmw-deps/-/raw/main/windows/$(Split-Path -Leaf $manifest)" $manifest
        $lines = Get-Content -LiteralPath $manifest
        $hashAndFile = $lines[1] -split '\s+'
        $archive = Join-Path $depsRoot $hashAndFile[1]
        if (-not (Test-Path -LiteralPath $archive)) { Download-File $lines[0] $archive }
        if ((Get-FileHash -LiteralPath $archive -Algorithm SHA512).Hash -ne $hashAndFile[0]) {
            throw "Dependency checksum mismatch: $archive"
        }
        & $sevenZip x -y -bsp0 -bso0 "-o$vcpkgRoot" $archive
        Assert-NativeExit 'Extract dependencies'
    }
    if (-not (Test-Path -LiteralPath "$qtRoot/bin/qmake.exe")) {
        $aqt = Join-Path $qtParent 'aqt_x64.exe'
        if (-not (Test-Path -LiteralPath $aqt)) {
            Download-File 'https://github.com/miurahr/aqtinstall/releases/download/v3.1.15/aqt_x64.exe' $aqt
        }
        Push-Location $qtParent
        try { & $aqt install-qt windows desktop $qtVersion win64_msvc2019_64; Assert-NativeExit 'Install Qt' }
        finally { Pop-Location }
    }

    $buildDir = Join-Path $repoRoot 'build-runtime-probe'
    $installedDeps = Join-Path $vcpkgRoot 'installed/x64-windows'
    $cmakeArgs = @('-S', $repoRoot, '-B', $buildDir, '-G', 'Ninja',
        "-DCMAKE_MAKE_PROGRAM=$ninja", '-DCMAKE_BUILD_TYPE=Release',
        '-DCMAKE_JOB_POOLS=release_link=2', '-DCMAKE_JOB_POOL_LINK=release_link',
        "-DCMAKE_TOOLCHAIN_FILE=$vcpkgRoot/scripts/buildsystems/vcpkg.cmake",
        "-DCMAKE_PREFIX_PATH=$qtRoot", "-DLuaJit_INCLUDE_DIR=$installedDeps/include/luajit",
        "-DLuaJit_LIBRARY=$installedDeps/lib/lua51.lib", '-DOPENMW_USE_SYSTEM_SQLITE3=OFF',
        '-DOPENMW_USE_SYSTEM_YAML_CPP=OFF', '-DOPENMW_LTO_BUILD=OFF',
        '-DBUILD_OPENMW=ON', '-DBUILD_LAUNCHER=ON', '-DBUILD_WIZARD=ON',
        '-DBUILD_OPENCS=ON', '-DBUILD_MWINIIMPORTER=ON', '-DBUILD_ESSIMPORTER=ON',
        '-DBUILD_BSATOOL=ON', '-DBUILD_ESMTOOL=ON', '-DBUILD_NIFTEST=ON',
        '-DBUILD_NAVMESHTOOL=ON', '-DBUILD_BULLETOBJECTTOOL=ON',
        '-DOPENMW_DEPLOY_RUNTIME_DEPENDENCIES=ON', '-DDEPLOY_QT_TRANSLATIONS=ON',
        '-DBUILD_COMPONENTS_TESTS=ON', '-DBUILD_OPENMW_TESTS=ON', '-DBUILD_OPENCS_TESTS=ON')
    & $cmake @cmakeArgs
    Assert-NativeExit 'Configure Release'
    & $cmake --build $buildDir --target openmw --parallel $Jobs
    Assert-NativeExit 'Build Release'


    $releaseRoot = Join-Path $repoRoot 'release'
    $baseline = Join-Path $releaseRoot 'baseline.zip'
    New-Item -ItemType Directory -Path $releaseRoot -Force | Out-Null
    Download-File 'https://github.com/DieuDeGlace/morrowind-performance/releases/download/v0.53.0-physics.preview.2/OpenMW-0.53.0-Ragdoll-Preview-2-Windows-x64.zip' $baseline
    $stage = Join-Path $releaseRoot 'probe-baseline'
    Expand-Archive -LiteralPath $baseline -DestinationPath $stage -Force
    $engine = @(Get-ChildItem -LiteralPath $stage -Filter openmw.exe -Recurse -File)
    if ($engine.Count -ne 1) { throw 'Expected one baseline runtime.' }
    $package = $engine[0].Directory.FullName
    Copy-Item -LiteralPath "$buildDir/openmw.exe" -Destination "$package/openmw.exe" -Force
    & "$repoRoot/CI/add-physics-preview-files.ps1" -PackageDir $package
    $commit = & git rev-parse HEAD
    [IO.File]::WriteAllText((Join-Path $package 'BUILD-INFO.txt'),
        "INTERNAL GAMEPLAY PROBE ONLY. Release engine without LTO, commit $commit. Other desktop applications and runtime files are from public physics preview 2. This is not the complete public release build.")
    $archive = Join-Path $releaseRoot 'OpenMW-physics-runtime-probe.zip'
    & $sevenZip a -tzip -mx=3 $archive $package
    Assert-NativeExit 'Archive runtime probe'
}
finally { Pop-Location }
