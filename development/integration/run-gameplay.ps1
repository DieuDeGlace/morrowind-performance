param(
    [Parameter(Mandatory)][string]$PackageDir,
    [Parameter(Mandatory)][string]$GameData,
    [Parameter(Mandatory)][string]$Label,
    [switch]$Physical
)
$ErrorActionPreference = 'Stop'
if ($Label -notmatch '^[a-z0-9-]+$') { throw 'Invalid test label' }
$project = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$package = (Resolve-Path -LiteralPath $PackageDir).Path
$game = (Resolve-Path -LiteralPath $GameData).Path
$ini = Join-Path ([IO.Path]::GetDirectoryName($game)) 'Morrowind.ini'
foreach ($file in @((Join-Path $package 'openmw.exe'), (Join-Path $package 'openmw-iniimporter.exe'), (Join-Path $game 'Morrowind.esm'), $ini)) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Missing test dependency: $file" }
}
$profile = Join-Path $project "development\results\integration\$Label"
if (Test-Path -LiteralPath $profile) { throw 'Test profile already exists; choose a new label' }
New-Item -ItemType Directory -Path $profile -Force | Out-Null
$scenario = Join-Path $PSScriptRoot $(if ($Physical) { 'physical' } else { 'startup' })
function ConfigPath([string]$value) { return $value.Replace('\','/') }
$cfg = @(
    'content=Morrowind.esm', 'content=Tribunal.esm', 'content=Bloodmoon.esm',
    ('data="{0}"' -f (ConfigPath $game)),
    ('data="{0}"' -f (ConfigPath (Join-Path $project 'mod'))),
    ('data="{0}"' -f (ConfigPath $scenario)),
    ('user-data="{0}"' -f (ConfigPath $profile)),
    ('data-local="{0}/data"' -f (ConfigPath $profile)),
    'encoding=win1252', 'no-sound=1',
    'fallback-archive=Morrowind.bsa', 'fallback-archive=Tribunal.bsa', 'fallback-archive=Bloodmoon.bsa'
)
if ($Physical) { $cfg += 'content=ShadowRegression.esp' }
$cfg += @('content=Next Generation Combat.esp','content=Next Generation Combat - Hand To Hand Fixes.esp','content=Next Generation Combat.omwscripts','content=NGCParityTest.omwscripts')
$utf8 = New-Object System.Text.UTF8Encoding($false)
[IO.File]::WriteAllText("$profile/openmw.cfg", ($cfg -join "`n") + "`n", $utf8)
& (Join-Path $package 'openmw-iniimporter.exe') --ini $ini --cfg "$profile/openmw.cfg" --no-archives --encoding win1252 *> "$profile/ini-import.log"
if ($LASTEXITCODE -ne 0) { throw 'Test-profile INI import failed' }
[IO.File]::WriteAllText("$profile/settings.cfg", "[Video]`nresolution x = 640`nresolution y = 360`nfullscreen = false`nwindow border = false`nvsync = false`nframerate limit = 60`n[Shadows]`nenable shadows = false`n[Physics]`nasync num threads = 1`n", $utf8)
$exe = Join-Path $package 'openmw.exe'
$arguments = @('--replace=config', ('--config="{0}"' -f $profile), '--skip-menu', '--start="Seyda Neen"')
$process = Start-Process -FilePath $exe -ArgumentList $arguments -WorkingDirectory $package -WindowStyle Hidden -RedirectStandardOutput "$profile/stdout.log" -RedirectStandardError "$profile/stderr.log" -PassThru
$process.Id | Set-Content -LiteralPath "$profile/process.pid"
$timer = [Diagnostics.Stopwatch]::StartNew()
while (-not $process.HasExited) {
    Start-Sleep -Milliseconds 300
    $process.Refresh()
    if ($timer.Elapsed.TotalSeconds -gt 100) {
        if ($process.Path -eq $exe) { Stop-Process -Id $process.Id }
        throw 'Owned integration-test process timed out'
    }
}
$process.WaitForExit()
$stdout = [IO.File]::ReadAllText("$profile/stdout.log")
$stderr = [IO.File]::ReadAllText("$profile/stderr.log")
if ($process.ExitCode -ne 0 -or $stdout -notmatch 'NGC_RUNTIME_PASS' -or ($stdout+$stderr) -match 'NGC_RUNTIME_FAIL|onUpdate failed|onContentFilesLoaded failed|Lua error|Failed to load Lua') {
    Get-Content -LiteralPath "$profile/stdout.log" -Tail 26
    Get-Content -LiteralPath "$profile/stderr.log" -Tail 12
    throw 'Packaged combat gameplay test failed'
}
$report = @{result='passed';label=$Label;scope=$(if ($Physical) {'Native physical attacks, misses, fist commitment, shield arc and full draw'} else {'Content startup and native bindings'});package=$package;exit_code=$process.ExitCode;seconds=$timer.Elapsed.TotalSeconds;native_bindings=($stdout -match 'NGC_NATIVE_BINDINGS_PASS');phone_tested=$false}
[IO.File]::WriteAllText("$profile/RESULT.json", ($report | ConvertTo-Json) + "`n", $utf8)
Write-Output "Packaged combat gameplay test passed: $Label"
