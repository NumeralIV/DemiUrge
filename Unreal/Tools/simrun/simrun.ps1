# DEMIURGE: builds (incrementally) and runs the headless simulation runner (Tools/simrun).
#
# Usage (the project path contains a space, so quote it):
#   powershell -NoProfile -ExecutionPolicy Bypass -File "C:\Users\Relly\Documents\Fable5.2 Test\demiurge\Unreal\Tools\simrun\simrun.ps1" [simrun args]
#   e.g.  ... simrun.ps1 --seed 7 --years 4000 --policy passive
#         ... simrun.ps1 --seeds 1-6 --years 4000 --policy passive --summary --jobs 6
#         ... simrun.ps1 --build-only
#         ... simrun.ps1 --tests [filters]   builds SimTests/test_sim*.cpp and test_integration*.cpp against EVERY real module (no
#             stubs; like `run.ps1 all` but private to this folder: build-tests/) and runs it
#         ... simrun.ps1 --stub Chronicle,Save --seed 3     (link those modules' SimTests stubs instead of
#             the real files, e.g. while one is mid-edit; separate build dir build-stub-<names>; simrun
#             prints a warning line naming them)
#   simrun --help lists every flag.
#
# Compiles EVERY real Sim/<Module>.cpp (no stubs), Sim/Core/*.cpp, Tools/MapImg.cpp and
# Tools/simrun/*.cpp with the same flags as SimTests/run.ps1 (UeMacroGuard.h force-included,
# shadowing / C4668 as errors, /fp:precise, /O2, /GR-) into Tools/simrun/build/simrun.exe.
# Only changed sources (or all, when any header changed) are recompiled. Exit code: simrun's
# (0 = every run finished without NaN and, with --summary, every target passed), 1 = build failed.

$ErrorActionPreference = 'Continue'

$here     = $PSScriptRoot
$tools    = Split-Path -Parent $here
$unreal   = Split-Path -Parent $tools
$project  = Split-Path -Parent $unreal
$sim      = Join-Path $unreal 'Source\Demiurge\Sim'
$simTests = Join-Path $unreal 'SimTests'
$vcvars   = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat'
$buildOnly = $false
$testMode = $false
$passArgs = @()
$stubbed = @()
for ($i = 0; $i -lt $args.Count; $i++) {
    $a = $args[$i]
    if ($a -eq '--build-only') { $buildOnly = $true }
    elseif ($a -eq '--tests') { $testMode = $true }
    elseif ($a -eq '--stub' -and $i + 1 -lt $args.Count) { $i++; $stubbed += @($args[$i] -split ',' | Where-Object { $_ -ne '' }) }
    else { $passArgs += $a }
}
$stubbed = @($stubbed | Sort-Object -Unique)
$buildDir = Join-Path $here 'build'
if ($stubbed.Count -gt 0) { $buildDir = Join-Path $here ('build-stub-' + (($stubbed | ForEach-Object { $_.ToLowerInvariant() }) -join '-')) }
if ($testMode) { $buildDir = $buildDir + '-tests' }
New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

function Fail-Build([string]$msg) {
    Write-Host $msg
    Write-Host "simrun build failed"
    exit 1
}

# ---------------------------------------------------------------- sources
$sources = New-Object System.Collections.Generic.List[string]
foreach ($f in (Get-ChildItem -LiteralPath (Join-Path $sim 'Core') -Filter '*.cpp' -File)) { $sources.Add($f.FullName) }
foreach ($f in (Get-ChildItem -LiteralPath $sim -Filter '*.cpp' -File)) {
    if ($stubbed -contains $f.BaseName) {
        $stub = Join-Path $simTests ("stubs\" + $f.BaseName + "_stub.cpp")
        if (-not (Test-Path -LiteralPath $stub)) { Fail-Build "no stub for $($f.BaseName)" }
        $sources.Add($stub)
    } else { $sources.Add($f.FullName) }
}
$sources.Add((Join-Path $tools 'MapImg.cpp'))
if ($testMode) {
    $sources.Add((Join-Path $simTests 'TestMain.cpp'))
    $sources.Add((Join-Path $simTests 'TestWorld.cpp'))
    foreach ($f in (Get-ChildItem -LiteralPath $simTests -Filter 'test_sim*.cpp' -File)) { $sources.Add($f.FullName) }   # test_sim.cpp + test_sim_adv.cpp
    foreach ($f in (Get-ChildItem -LiteralPath $simTests -Filter 'test_integration*.cpp' -File)) { $sources.Add($f.FullName) }   # the integrator's cross-module tests
} else {
    foreach ($f in (Get-ChildItem -LiteralPath $here -Filter '*.cpp' -File)) { $sources.Add($f.FullName) }
}

$seen = @{}
foreach ($s in $sources) {
    $base = [System.IO.Path]::GetFileNameWithoutExtension($s).ToLowerInvariant()
    if ($seen.ContainsKey($base) -and $seen[$base] -ne $s) { Fail-Build "two sources share the object name '$base': $($seen[$base]) and $s" }
    $seen[$base] = $s
}

# ---------------------------------------------------------------- MSVC environment (cached)
$envCache = Join-Path $buildDir 'vcvars.env'
$useCache = (Test-Path -LiteralPath $envCache) -and ((Get-Item -LiteralPath $envCache).LastWriteTime -gt (Get-Date).AddDays(-7))
$envLines = $null
if ($useCache) { $envLines = [System.IO.File]::ReadAllLines($envCache) }
if (-not $envLines -or -not ($envLines | Where-Object { $_ -like 'INCLUDE=*' })) {
    if (-not (Test-Path -LiteralPath $vcvars)) { Fail-Build "vcvars64.bat not found: $vcvars" }
    $envLines = @(& cmd.exe /d /c "`"$vcvars`" >nul 2>&1 && set")
    [System.IO.File]::WriteAllLines($envCache, $envLines)
}
foreach ($line in $envLines) {
    $eq = $line.IndexOf('=')
    if ($eq -gt 0) { [System.Environment]::SetEnvironmentVariable($line.Substring(0, $eq), $line.Substring($eq + 1), 'Process') }
}
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) { Fail-Build "cl.exe not found after vcvars64" }

# ---------------------------------------------------------------- compile (incremental)
$guard = Join-Path $simTests 'UeMacroGuard.h'
$cflags = @('/nologo', '/c', '/std:c++20', '/permissive-', '/W4', '/we4456', '/we4457', '/we4458', '/we4459', '/we4005',
    '/we4668', '/fp:precise', '/Zc:preprocessor',
    '/O2', '/GR-', '/EHsc', '/MP', '/utf-8', '/Zc:__cplusplus', '/D_CRT_SECURE_NO_WARNINGS', "/FI$guard",
    "/I$sim", "/I$tools", "/I$here", "/I$simTests")
$flagStamp = Join-Path $buildDir 'flags.txt'
$flagText = ($cflags -join ' ')
$flagsChanged = -not (Test-Path -LiteralPath $flagStamp) -or ([System.IO.File]::ReadAllText($flagStamp) -ne $flagText)

$newestHeader = [datetime]::MinValue
foreach ($d in @($sim, $tools, $simTests)) {
    foreach ($h in (Get-ChildItem -LiteralPath $d -Recurse -File -Filter '*.h' -ErrorAction SilentlyContinue)) {
        if ($h.FullName -like "*\build\*") { continue }
        if ($h.LastWriteTime -gt $newestHeader) { $newestHeader = $h.LastWriteTime }
    }
}
if ((Get-Item -LiteralPath $guard).LastWriteTime -gt $newestHeader) { $newestHeader = (Get-Item -LiteralPath $guard).LastWriteTime }

$toCompile = @()
$objs = @()
foreach ($s in $sources) {
    $obj = Join-Path $buildDir ([System.IO.Path]::GetFileNameWithoutExtension($s) + '.obj')
    $objs += $obj
    $stale = $flagsChanged -or -not (Test-Path -LiteralPath $obj)
    if (-not $stale) {
        $ot = (Get-Item -LiteralPath $obj).LastWriteTime
        if ((Get-Item -LiteralPath $s).LastWriteTime -gt $ot -or $newestHeader -gt $ot) { $stale = $true }
    }
    if ($stale) { $toCompile += $s }
}

if ($toCompile.Count -gt 0) {
    Write-Host "simrun: compiling $($toCompile.Count) of $($sources.Count) sources"
    $rsp = Join-Path $buildDir 'cl.rsp'
    $rspLines = @($cflags | ForEach-Object { '"' + $_ + '"' }) + @($toCompile | ForEach-Object { '"' + $_ + '"' })
    [System.IO.File]::WriteAllLines($rsp, $rspLines)
    Push-Location -LiteralPath $buildDir
    $out = @(& cl.exe "@cl.rsp" 2>&1 | ForEach-Object { "$_" })
    $code = $LASTEXITCODE
    Pop-Location
    foreach ($l in ($out | Where-Object { $_ -match 'error|warning' })) { Write-Host $l }
    if ($code -ne 0) { Fail-Build "compile failed (exit $code)" }
    [System.IO.File]::WriteAllText($flagStamp, $flagText)
}

$exe = Join-Path $buildDir $(if ($testMode) { 'simtests.exe' } else { 'simrun.exe' })
$lrsp = Join-Path $buildDir 'link.rsp'
$linkLines = @('/nologo', ('"/OUT:' + $exe + '"')) + @($objs | ForEach-Object { '"' + $_ + '"' })
# Relink only when needed (an object is newer than the exe, or the object list changed): a running
# simrun.exe is locked by Windows, and relinking it for nothing made a second concurrent run fail.
$needLink = $toCompile.Count -gt 0 -or -not (Test-Path -LiteralPath $exe) -or -not (Test-Path -LiteralPath $lrsp) -or
    (([System.IO.File]::ReadAllLines($lrsp) -join "`n") -ne ($linkLines -join "`n"))
if (-not $needLink) {
    $exeTime = (Get-Item -LiteralPath $exe).LastWriteTime
    foreach ($o in $objs) { if ((Get-Item -LiteralPath $o).LastWriteTime -gt $exeTime) { $needLink = $true; break } }
}
if ($needLink) {
    [System.IO.File]::WriteAllLines($lrsp, $linkLines)
    Push-Location -LiteralPath $buildDir
    $lout = @(& link.exe "@link.rsp" 2>&1 | ForEach-Object { "$_" })
    $lcode = $LASTEXITCODE
    Pop-Location
    if ($lcode -ne 0) {
        foreach ($l in $lout) { Write-Host $l }
        Fail-Build "link failed"
    }
}
if ($buildOnly) { Write-Host "simrun built: $exe"; exit 0 }

# ---------------------------------------------------------------- run
$env:DM_ROOT = $project
$env:DM_SIMRUN_STUBS = ($stubbed -join ',')
& $exe @passArgs
exit $LASTEXITCODE
