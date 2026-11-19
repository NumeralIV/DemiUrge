# DEMIURGE: builds the DemiurgeEditor target (Win64 Development) with Unreal Engine 5.7's Build.bat.
#
# Usage (from any working directory):
#   powershell -NoProfile -File "C:\Users\Relly\Documents\Fable5.2 Test\demiurge\Unreal\build.ps1"
#
# Prints the result, elapsed seconds, the log path, and every console line that contains "error"
# or "warning C". The full build output is written to a log file in the temp folder.
# Exit code: the exit code of Build.bat (0 = succeeded).

$ErrorActionPreference = 'Continue'

$engineRoot = [System.IO.Path]::GetFullPath('C:/Program Files/Epic Games/UE_5.7')
$buildBat   = [System.IO.Path]::GetFullPath((Join-Path $engineRoot 'Engine/Build/BatchFiles/Build.bat'))
$uproject   = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'Demiurge.uproject'))

if (-not (Test-Path -LiteralPath $buildBat)) {
    Write-Host "Build.bat not found: $buildBat"
    exit 2
}
if (-not (Test-Path -LiteralPath $uproject)) {
    Write-Host "Project not found: $uproject"
    exit 2
}

$stamp   = Get-Date -Format 'yyyyMMdd-HHmmss'
$logPath = Join-Path ([System.IO.Path]::GetTempPath()) "demiurge-ue-build-$stamp.log"

$buildArgs = @('DemiurgeEditor', 'Win64', 'Development', "-Project=$uproject", '-WaitMutex', '-NoHotReload')

Write-Host "Building DemiurgeEditor Win64 Development"
Write-Host "Project: $uproject"

$watch = [System.Diagnostics.Stopwatch]::StartNew()
$lines = @(& $buildBat @buildArgs 2>&1 | ForEach-Object { "$_" })
$exitCode = $LASTEXITCODE
$watch.Stop()
$seconds = [Math]::Round($watch.Elapsed.TotalSeconds, 1)

$lines | Set-Content -LiteralPath $logPath -Encoding UTF8

$result = if ($exitCode -eq 0) { 'Succeeded' } else { 'Failed' }
$hits = @($lines | Where-Object { $_ -match 'error|warning C' })

Write-Host ""
Write-Host "Result:  $result (exit code $exitCode)"
Write-Host "Elapsed: $seconds s"
Write-Host "Log:     $logPath"
Write-Host "Lines containing 'error' or 'warning C': $($hits.Count)"
foreach ($hit in $hits) {
    Write-Host $hit
}

exit $exitCode
