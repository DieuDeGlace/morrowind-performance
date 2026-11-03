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

    $buildDir = Join-Path $repoRoot 'build-release'
    $installedDeps = Join-Path $vcpkgRoot 'installed/x64-windows'
    $cmakeArgs = @('-S', $repoRoot, '-B', $buildDir, '-G', 'Ninja',
        "-DCMAKE_MAKE_PROGRAM=$ninja", '-DCMAKE_BUILD_TYPE=Release',
        '-DCMAKE_JOB_POOLS=release_link=2', '-DCMAKE_JOB_POOL_LINK=release_link',
        "-DCMAKE_TOOLCHAIN_FILE=$vcpkgRoot/scripts/buildsystems/vcpkg.cmake",
        "-DCMAKE_PREFIX_PATH=$qtRoot", "-DLuaJit_INCLUDE_DIR=$installedDeps/include/luajit",
        "-DLuaJit_LIBRARY=$installedDeps/lib/lua51.lib", '-DOPENMW_USE_SYSTEM_SQLITE3=OFF',
        '-DOPENMW_USE_SYSTEM_YAML_CPP=OFF', '-DOPENMW_LTO_BUILD=ON',
        '-DBUILD_COMPONENTS_TESTS=ON', '-DBUILD_OPENMW_TESTS=ON', '-DBUILD_OPENCS_TESTS=ON')
    & $cmake @cmakeArgs
    Assert-NativeExit 'Configure Release'
    & $cmake --build $buildDir --parallel $Jobs
    Assert-NativeExit 'Build Release'

    Copy-Item "$installedDeps/bin/*.dll" -Destination $buildDir -Force
    Copy-Item "$installedDeps/bin/Release/MyGUIEngine.dll" -Destination $buildDir -Force
    Copy-Item "$installedDeps/bin/osgPlugins-3.6.5" -Destination $buildDir -Recurse -Force
    # Match upstream's flat OSG plugin layout as well as its plugin directory.
    Copy-Item "$installedDeps/bin/osgPlugins-3.6.5/*.dll" -Destination $buildDir -Force
    foreach ($name in 'Core', 'Gui', 'Network', 'OpenGL', 'OpenGLWidgets', 'Widgets', 'Svg') {
        Copy-Item "$qtRoot/bin/Qt6$name.dll" -Destination $buildDir -Force
    }
    foreach ($pluginDir in 'platforms', 'styles', 'imageformats', 'iconengines') {
        New-Item -ItemType Directory -Path "$buildDir/$pluginDir" -Force | Out-Null
    }
    Copy-Item "$qtRoot/plugins/platforms/qwindows.dll" "$buildDir/platforms/" -Force
    Copy-Item "$qtRoot/plugins/styles/qwindowsvistastyle.dll" "$buildDir/styles/" -Force
    Copy-Item "$qtRoot/plugins/imageformats/qsvg.dll" "$buildDir/imageformats/" -Force
    Copy-Item "$qtRoot/plugins/iconengines/qsvgicon.dll" "$buildDir/iconengines/" -Force
    foreach ($test in 'components-tests', 'openmw-tests', 'openmw-cs-tests') {
        & "$buildDir/$test.exe" "--gtest_output=xml:$buildDir/$test.xml"
        Assert-NativeExit $test
    }

    $cmakeSource = Get-Content CMakeLists.txt -Raw
    $versionParts = foreach ($part in 'MAJOR', 'MINOR', 'RELEASE') {
        [regex]::Match($cmakeSource, "set\(OPENMW_VERSION_$part (\d+)\)").Groups[1].Value
    }
    $version = $versionParts -join '.'
    $packageName = "OpenMW-$version-performance-Windows-x64"
    $releaseRoot = Join-Path $repoRoot 'release'
    $packageDir = Join-Path $releaseRoot $packageName
    & $cmake --install $buildDir --prefix $packageDir --config Release
    Assert-NativeExit 'Stage portable Release'
    Copy-Item "$repoRoot/PERFORMANCE-BUILD.md" $packageDir -Force
    Copy-Item "$repoRoot/files/performance-preset.cfg" $packageDir -Force
    $licenses = Join-Path $packageDir 'licenses'
    New-Item -ItemType Directory -Path $licenses -Force | Out-Null
    Copy-Item "$repoRoot/extern/maskedoc/license.txt" "$licenses/masked-occlusion-culling.txt" -Force
    foreach ($copyright in Get-ChildItem "$installedDeps/share" -Filter copyright -Recurse -File) {
        Copy-Item -LiteralPath $copyright.FullName -Destination "$licenses/$($copyright.Directory.Name).txt" -Force
    }
    Copy-Item "$repoRoot/CI/licenses/*" $licenses -Force
    $redist = Join-Path $env:VCToolsRedistDir 'vc_redist.x64.exe'
    if (Test-Path -LiteralPath $redist) {
        New-Item -ItemType Directory -Path "$packageDir/redist" -Force | Out-Null
        Copy-Item -LiteralPath $redist -Destination "$packageDir/redist/" -Force
    }
    $commit = & git rev-parse HEAD
    "Commit $commit`nConfiguration Release`nArchitecture x64`nBuilt $([DateTime]::UtcNow.ToString('yyyy-MM-dd'))" |
        Set-Content "$packageDir/BUILD-INFO.txt" -Encoding utf8
    & "$packageDir/openmw.exe" --version
    Assert-NativeExit 'Packaged version smoke test'
    $zipPath = Join-Path $releaseRoot "$packageName.zip"
    & $sevenZip a -tzip -mx=5 $zipPath $packageDir
    Assert-NativeExit 'Archive Release'
    $checksum = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()
    "$checksum  $packageName.zip" | Set-Content "$zipPath.sha256" -Encoding ascii
    Write-Host "Release ready: $zipPath"
}
finally { Pop-Location }
