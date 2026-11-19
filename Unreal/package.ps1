# DEMIURGE: package a Windows Development build with RunUAT BuildCookRun (C10 integration).
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File "<project>\Unreal\package.ps1" [-Out <dir>] [-Config Development|Shipping]
#
# Builds the Game target, cooks /Game (DefaultGame.ini lists the directories; every asset is loaded by path),
# stages the OFL fonts (Content/Fonts) and paks everything, then archives to <root>/dist/DEMIURGE-Win64-<Config>
# (outside Unreal/, so lanes never copy it). Prints the command, the exit code, the elapsed time, the archive size
# and the log path. Exit code: RunUAT's (0 = succeeded).

param([string]$Out = '', [string]$Config = 'Development')

$ErrorActionPreference = 'Continue'
$uat = 'C:\Program Files\Epic Games\UE_5.7\Engine\Build\BatchFiles\RunUAT.bat'
$proj = (Resolve-Path (Join-Path $PSScriptRoot 'Demiurge.uproject')).Path
$root = $PSScriptRoot
for ($i = 0; $i -lt 6; $i++) { if (Test-Path -LiteralPath (Join-Path $root 'docs')) { break }; $root = Split-Path -Parent $root }
if ($Out -eq '') { $Out = Join-Path $root "dist\DEMIURGE-Win64-$Config" }
[void](New-Item -ItemType Directory -Force -Path $Out)
$logDir = Join-Path $PSScriptRoot 'Saved\Logs'
[void](New-Item -ItemType Directory -Force -Path $logDir)
$log = Join-Path $logDir ("package-" + (Get-Date -Format 'yyyyMMdd-HHmmss') + ".log")

$uatArgs = @('BuildCookRun', "-project=$proj", '-noP4', '-platform=Win64', "-clientconfig=$Config", '-build', '-cook', '-stage', '-pak', '-iostore', '-prereqs', '-archive', "-archivedirectory=$Out", '-unattended', '-utf8output', '-nocompileeditor', '-skipbuildeditor')
Write-Host ("RunUAT " + ($uatArgs -join ' '))
$t0 = Get-Date
& $uat @uatArgs *>&1 | ForEach-Object { "$_" } | Set-Content -LiteralPath $log -Encoding UTF8
$code = $LASTEXITCODE
$secs = [int]((Get-Date) - $t0).TotalSeconds
$size = 0
if (Test-Path -LiteralPath $Out) { $size = (Get-ChildItem -LiteralPath $Out -Recurse -File | Measure-Object -Property Length -Sum).Sum }
Write-Host ("package: exit $code in $secs s; archive $Out = {0:N0} bytes ({1:N2} GB)" -f $size, ($size / 1GB))
Write-Host "log: $log"
Get-Content -LiteralPath $log | Select-String -Pattern 'BUILD SUCCESSFUL|BUILD FAILED|AutomationTool exiting|Error:|error C|Cook: Error' | Select-Object -Last 25
exit $code
