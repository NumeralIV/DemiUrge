# DEMIURGE: builds and runs the standalone simulation tests with MSVC (no Unreal).
#
# Usage (from any working directory; the project path contains a space, so quote it):
#   powershell -NoProfile -ExecutionPolicy Bypass -File "C:\Users\Relly\Documents\Fable5.2 Test\demiurge\Unreal\SimTests\run.ps1" <key> [test-name filter ...]
#   (-ExecutionPolicy Bypass is required where the machine policy blocks scripts, e.g. from Git Bash.)
#   Environment: DM_VERBOSE=1 prints every test with its time.
#
#   <key>   core | state | link | all | a Stage A key (names terrain climate hydro life path-units civ
#           economy society diplomacy powers disasters chronicle sim save) | a mythos key (mythos cards
#           doctrine characters legends faiths omens keepers renewal) | any other word
#   filter  optional substrings: only tests whose DM_TEST name contains one of them run
#
# What gets compiled for <key> (into SimTests/build/<key>/, so parallel runs never collide):
#   Sim/Core/*.cpp, Sim/State.cpp, Sim/Schema.cpp, Tools/MapImg.cpp, SimTests/TestMain.cpp, TestWorld.cpp
#   + Sim/<Module>.cpp for the module(s) of <key>
#   + SimTests/stubs/<Module>_stub.cpp for every OTHER module (a module without a stub: its real .cpp)
#   + SimTests/test_<key>*.cpp and test_<module>*.cpp (lower case) for each module of <key>
#   all: every real Sim/<Module>.cpp, no stubs, every SimTests/test_*.cpp
# Flags: cl /std:c++20 /permissive- /W4 /we4456 /we4457 /we4458 /we4459 /we4005 /we4668 /fp:precise
#        /Zc:preprocessor /O2 /GR- /EHsc /FI UeMacroGuard.h ; only sources that changed (or whose headers changed) are recompiled.
# Before compiling, Sim/ is scanned for constructs Unreal or the determinism rules forbid
# (exceptions, RTTI, Unreal headers, <random>, std::rand); a hit fails the run.
# The last line printed is the summary: "<key> N passed, M failed" (or "<key> build failed").
# Exit code 0 only if everything built and every test passed.

param(
    [Parameter(Position = 0)][string]$Key = 'all',
    [Parameter(Position = 1, ValueFromRemainingArguments = $true)][string[]]$Filters
)

$ErrorActionPreference = 'Continue'

$simTests = $PSScriptRoot
$unreal   = Split-Path -Parent $simTests
$project  = Split-Path -Parent $unreal
$sim      = Join-Path $unreal 'Source\Demiurge\Sim'
$tools    = Join-Path $unreal 'Tools'
$stubsDir = Join-Path $simTests 'stubs'
$vcvars   = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat'

$keyLower = $Key.ToLowerInvariant()
if ($keyLower -notmatch '^[a-z0-9_-]+$') { Write-Host "bad key: $Key"; exit 2 }
$buildDir = Join-Path $simTests ("build\" + $keyLower)
New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

function Fail-Build([string]$msg) {
    Write-Host $msg
    Write-Host ""
    Write-Host "$keyLower build failed"
    exit 1
}

# ---------------------------------------------------------------- key -> modules
$moduleMap = @{
    'core' = @(); 'state' = @(); 'link' = @(); 'foundation' = @()
    'names' = @('Names'); 'terrain' = @('Terrain'); 'climate' = @('Climate'); 'hydro' = @('Hydro')
    'life' = @('Life'); 'path-units' = @('Path', 'Units'); 'path' = @('Path'); 'units' = @('Units')
    'civ' = @('Civ'); 'economy' = @('Economy'); 'society' = @('Society'); 'diplomacy' = @('Diplomacy')
    'powers' = @('Powers'); 'disasters' = @('Disasters'); 'chronicle' = @('Chronicle')
    'sim' = @('Sim', 'Balance'); 'balance' = @('Balance'); 'save' = @('Save'); 'integration' = @()
    # (v4 mythos, M0) SPEC-MYTHOS 1.1: one key per mythos module
    'mythos' = @('Mythos'); 'cards' = @('Cards'); 'doctrine' = @('Doctrine'); 'characters' = @('Characters')
    'legends' = @('Legends'); 'faiths' = @('Faiths'); 'omens' = @('Omens'); 'keepers' = @('Keepers'); 'renewal' = @('Renewal')
}

# every module: Sim/<M>.cpp (other than the foundation files) and stubs/<M>_stub.cpp
$foundationCpp = @('State', 'Schema')
$realModules = @()
if (Test-Path -LiteralPath $sim) {
    $realModules = @(Get-ChildItem -LiteralPath $sim -Filter '*.cpp' -File | ForEach-Object { $_.BaseName } | Where-Object { $foundationCpp -notcontains $_ })
}
$stubModules = @()
if (Test-Path -LiteralPath $stubsDir) {
    $stubModules = @(Get-ChildItem -LiteralPath $stubsDir -Filter '*_stub.cpp' -File | ForEach-Object { $_.BaseName -replace '_stub$', '' })
}
$allModules = @($realModules + $stubModules | Sort-Object -Unique)

$isAll = ($keyLower -eq 'all')
if ($isAll) {
    $selected = $realModules
} elseif ($moduleMap.ContainsKey($keyLower)) {
    $selected = $moduleMap[$keyLower]
} else {
    $guess = ($allModules | Where-Object { $_.ToLowerInvariant() -eq $keyLower })
    $selected = @($guess)
}

# ---------------------------------------------------------------- rule scan of Sim/
$violations = @()
if (Test-Path -LiteralPath $sim) {
    $simFiles = Get-ChildItem -LiteralPath $sim -Recurse -File -Include '*.h', '*.cpp'
    foreach ($f in $simFiles) {
        $lineNo = 0
        foreach ($line in [System.IO.File]::ReadAllLines($f.FullName)) {
            $lineNo++
            $code = ($line -replace '//.*$', '')
            $code = ($code -replace '"([^"\\]|\\.)*"', '""')
            if ($code -match '\b(throw|try|catch)\b') { $violations += "$($f.FullName):$lineNo exceptions are not allowed in Sim/: $($line.Trim())" }
            if ($code -match '\bdynamic_cast\b|\btypeid\b') { $violations += "$($f.FullName):$lineNo RTTI is not allowed in Sim/: $($line.Trim())" }
            if ($code -match '#\s*include\s*<random>') { $violations += "$($f.FullName):$lineNo <random> is not allowed in Sim/ (use dm::Rng)" }
            if ($code -match '\bstd::rand\b|\bsrand\s*\(|[^:\w]rand\s*\(') { $violations += "$($f.FullName):$lineNo rand() is not allowed in Sim/ (use dm::Rng)" }
            if ($line -match '#\s*include\s*"(CoreMinimal|Engine|UObject|GameFramework|Components|Kismet|Misc|HAL|Containers|Math)[/\.]|\.generated\.h') { $violations += "$($f.FullName):$lineNo Unreal headers are not allowed in Sim/" }
        }
    }
}
if ($violations.Count -gt 0) {
    foreach ($v in $violations) { Write-Host $v }
    Fail-Build "Sim/ rule check: $($violations.Count) violation(s)"
}

# ---------------------------------------------------------------- source list
$sources = New-Object System.Collections.Generic.List[string]
foreach ($f in (Get-ChildItem -LiteralPath (Join-Path $sim 'Core') -Filter '*.cpp' -File)) { $sources.Add($f.FullName) }
foreach ($b in $foundationCpp) { $p = Join-Path $sim "$b.cpp"; if (Test-Path -LiteralPath $p) { $sources.Add($p) } }
$sources.Add((Join-Path $tools 'MapImg.cpp'))
$sources.Add((Join-Path $simTests 'TestMain.cpp'))
$sources.Add((Join-Path $simTests 'TestWorld.cpp'))

$moduleNote = @()
foreach ($m in $allModules) {
    $real = Join-Path $sim "$m.cpp"
    $stub = Join-Path $stubsDir "${m}_stub.cpp"
    $hasReal = Test-Path -LiteralPath $real
    $hasStub = Test-Path -LiteralPath $stub
    if ($isAll -or $selected -contains $m) {
        if ($hasReal) { $sources.Add($real); $moduleNote += "$m(real)" }
        else { Fail-Build "module $m has no Sim/$m.cpp" }
    } elseif ($hasStub) {
        $sources.Add($stub)
    } elseif ($hasReal) {
        $sources.Add($real); $moduleNote += "$m(real, no stub)"
    }
}
foreach ($m in $selected) { if ($allModules -notcontains $m) { Fail-Build "module $m of key '$keyLower' does not exist yet (no Sim/$m.cpp)" } }

$testFiles = @()
if ($isAll) {
    $testFiles = @(Get-ChildItem -LiteralPath $simTests -Filter 'test_*.cpp' -File | ForEach-Object { $_.FullName })
} else {
    $patterns = @("test_$keyLower*.cpp")
    foreach ($m in $selected) { $patterns += ("test_" + $m.ToLowerInvariant() + "*.cpp") }
    foreach ($pat in ($patterns | Sort-Object -Unique)) {
        $testFiles += @(Get-ChildItem -LiteralPath $simTests -Filter $pat -File | ForEach-Object { $_.FullName })
    }
    $testFiles = @($testFiles | Sort-Object -Unique)
}
if ($testFiles.Count -eq 0) { Write-Host "warning: no test files for key '$keyLower' (expected SimTests/test_$keyLower*.cpp)" }
foreach ($t in $testFiles) { $sources.Add($t) }

# object file names must be unique within the build directory
$seen = @{}
foreach ($s in $sources) {
    $base = [System.IO.Path]::GetFileNameWithoutExtension($s).ToLowerInvariant()
    if ($seen.ContainsKey($base) -and $seen[$base] -ne $s) { Fail-Build "two sources share the object name '$base': $($seen[$base]) and $s" }
    $seen[$base] = $s
}

# ---------------------------------------------------------------- MSVC environment (cached per build dir)
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
# Matches the Unreal module build where it matters (Intermediate/.../Demiurge/*.rsp): /fp:precise
# (Demiurge.Build.cs sets FPSemantics = Precise), /Zc:preprocessor, /GR-, shadowing (C4456/8/9) and
# "undefined macro in #if" (C4668) as errors; plus C4457 and C4005 as errors here.
$cflags = @('/nologo', '/c', '/std:c++20', '/permissive-', '/W4', '/we4456', '/we4457', '/we4458', '/we4459', '/we4005',
    '/we4668', '/fp:precise', '/Zc:preprocessor',
    '/O2', '/GR-', '/EHsc', '/MP', '/utf-8', '/Zc:__cplusplus', '/D_CRT_SECURE_NO_WARNINGS', "/FI$guard",
    "/I$sim", "/I$simTests", "/I$tools")
$flagStamp = Join-Path $buildDir 'flags.txt'
$flagText = ($cflags -join ' ')
$flagsChanged = -not (Test-Path -LiteralPath $flagStamp) -or ([System.IO.File]::ReadAllText($flagStamp) -ne $flagText)

$headerDirs = @($sim, $simTests, $tools)
$newestHeader = [datetime]::MinValue
foreach ($d in $headerDirs) {
    foreach ($h in (Get-ChildItem -LiteralPath $d -Recurse -File -Filter '*.h' -ErrorAction SilentlyContinue)) {
        if ($h.FullName -like "*\build\*") { continue }
        if ($h.LastWriteTime -gt $newestHeader) { $newestHeader = $h.LastWriteTime }
    }
}

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

$watch = [System.Diagnostics.Stopwatch]::StartNew()
if ($toCompile.Count -gt 0) {
    Write-Host "compiling $($toCompile.Count) of $($sources.Count) sources for '$keyLower'$(if ($moduleNote.Count) { ' [' + ($moduleNote -join ', ') + ']' })"
    # a response file avoids PowerShell 5.1's quoting of arguments that contain spaces
    $rsp = Join-Path $buildDir 'cl.rsp'
    $rspLines = @($cflags | ForEach-Object { '"' + $_ + '"' }) + @($toCompile | ForEach-Object { '"' + $_ + '"' })
    [System.IO.File]::WriteAllLines($rsp, $rspLines)
    Push-Location -LiteralPath $buildDir
    $out = @(& cl.exe "@cl.rsp" 2>&1 | ForEach-Object { "$_" })
    $code = $LASTEXITCODE
    Pop-Location
    $shown = $out | Where-Object { $_ -match 'error|warning' }
    foreach ($l in $shown) { Write-Host $l }
    if ($code -ne 0) {
        Fail-Build "compile failed (exit $code)"
    }
    [System.IO.File]::WriteAllText($flagStamp, $flagText)
} else {
    Write-Host "all $($sources.Count) objects up to date for '$keyLower'"
}

$exe = Join-Path $buildDir 'simtests.exe'
$lrsp = Join-Path $buildDir 'link.rsp'
[System.IO.File]::WriteAllLines($lrsp, @('/nologo', ('"/OUT:' + $exe + '"')) + @($objs | ForEach-Object { '"' + $_ + '"' }))
Push-Location -LiteralPath $buildDir
$lout = @(& link.exe "@link.rsp" 2>&1 | ForEach-Object { "$_" })
Pop-Location
if ($LASTEXITCODE -ne 0) {
    foreach ($l in $lout) { Write-Host $l }
    Fail-Build "link failed"
}
$buildSec = [Math]::Round($watch.Elapsed.TotalSeconds, 1)
Write-Host "built in $buildSec s"

# ---------------------------------------------------------------- run
# (wave-3 gameplay) inside a private lane (Unreal/Lanes/<key>/, marked by .lane) the project root is the
# main project's, so tests that read <root>/Unreal/Source and shots/ find them
$laneMarker = Join-Path $unreal '.lane'
if (Test-Path -LiteralPath $laneMarker) {
    try { $project = Split-Path -Parent ([System.IO.Path]::GetFullPath((Get-Content -LiteralPath $laneMarker -Raw | ConvertFrom-Json).main)) } catch { }
}
$env:DM_ROOT = $project
$runArgs = @()
if ($Filters) { $runArgs = $Filters }
$passed = 0; $failed = 0; $gotResult = $false
& $exe @runArgs 2>&1 | ForEach-Object {
    $l = "$_"
    if ($l -match '^RESULT (\d+) (\d+)$') { $passed = [int]$Matches[1]; $failed = [int]$Matches[2]; $gotResult = $true }
    else { Write-Host $l }
}
$exitCode = $LASTEXITCODE
Write-Host ""
if (-not $gotResult) {
    Write-Host "$keyLower test run crashed (exit code $exitCode)"
    exit 1
}
Write-Host "$keyLower $passed passed, $failed failed"
if ($failed -gt 0 -or $exitCode -ne 0) { exit 1 }
exit 0
