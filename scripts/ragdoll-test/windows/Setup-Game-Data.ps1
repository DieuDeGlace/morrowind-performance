param([string] $GameDirectory)

$ErrorActionPreference = 'Stop'
if (-not $GameDirectory) {
    $GameDirectory = Join-Path $([Environment]::GetFolderPath('ProgramFilesX86')) 'Steam\steamapps\common\Morrowind'
    if (-not (Test-Path -LiteralPath (Join-Path $GameDirectory 'Data Files\Morrowind.esm'))) {
        $GameDirectory = Read-Host 'Enter the Morrowind installation folder'
    }
}
if (-not $GameDirectory) { throw 'A Morrowind installation folder is required.' }

$game = (Resolve-Path -LiteralPath ($GameDirectory.Trim().Trim('"'))).ProviderPath
$data = Join-Path $game 'Data Files'
$ini = Join-Path $game 'Morrowind.ini'
foreach ($required in @($ini, (Join-Path $data 'Morrowind.esm'), (Join-Path $data 'Morrowind.bsa'))) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required file missing at $required"
    }
}

$profile = Join-Path $PSScriptRoot 'profile'
$demo = Join-Path $PSScriptRoot 'demo'
foreach ($folder in @($profile, $demo)) {
    New-Item -ItemType Directory -Path (Join-Path $folder 'data') -Force | Out-Null
}
$quotedData = $data.Replace('\', '/').Replace('&', '&&')
$lines = @('user-data="."', 'data-local="./data"', "data=""$quotedData""")
foreach ($master in @('Morrowind', 'Tribunal', 'Bloodmoon')) {
    if ((Test-Path -LiteralPath (Join-Path $data "$master.esm") -PathType Leaf) -and
        (Test-Path -LiteralPath (Join-Path $data "$master.bsa") -PathType Leaf)) {
        $lines += "fallback-archive=$master.bsa", "content=$master.esm"
    }
}

$inputCfg = Join-Path $profile ".game-data-$PID.input"
$importedCfg = Join-Path $profile ".game-data-$PID.output"
$demoCfg = Join-Path $demo ".game-data-$PID.output"
$encoding = New-Object System.Text.UTF8Encoding($false)
try {
    [IO.File]::WriteAllLines($inputCfg, [string[]] $lines, $encoding)
    & (Join-Path $PSScriptRoot 'openmw-iniimporter.exe') --ini $ini --cfg $inputCfg --output $importedCfg --no-archives
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $importedCfg -PathType Leaf)) {
        throw 'OpenMW could not import the game settings.'
    }
    $config = [IO.File]::ReadAllText($importedCfg, $encoding)
    if ($config -notmatch '(?m)^content=Morrowind\.esm\s*$') {
        throw 'The generated OpenMW profile is incomplete.'
    }
    $demoConfig = $config.TrimEnd() + "`r`ndata=""./scenario""`r`ncontent=RagdollTest.omwscripts`r`n"
    [IO.File]::WriteAllText($demoCfg, $demoConfig, $encoding)
    Move-Item -LiteralPath $importedCfg -Destination (Join-Path $profile 'openmw.cfg') -Force
    Move-Item -LiteralPath $demoCfg -Destination (Join-Path $demo 'openmw.cfg') -Force
    Write-Output 'The isolated OpenMW profiles are ready.'
} finally {
    foreach ($temporary in @($inputCfg, $importedCfg, $demoCfg)) {
        if (Test-Path -LiteralPath $temporary) {
            Remove-Item -LiteralPath $temporary
        }
    }
}
