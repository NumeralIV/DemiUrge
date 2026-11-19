# DEMIURGE nature (C4): rebuild the nature materials headless (Scripts/nature.py) in the project this script
# belongs to (main project or a lane):  powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/nature-mats.ps1
# Prints the exit code, the seconds and the script's NATUREMAT lines (DONE OK / errors).
# The project path has a space, which -script= cannot carry through PowerShell 5.1's argument quoting, so the
# script runs from a copy in %TEMP% (it only writes assets of the project given on the command line).
$proj = (Resolve-Path (Join-Path $PSScriptRoot '..\Demiurge.uproject')).Path
$src = (Resolve-Path (Join-Path $PSScriptRoot 'nature.py')).Path
$tmp = Join-Path ([System.IO.Path]::GetTempPath()) 'demiurge-nature-mats.py'
Copy-Item -LiteralPath $src -Destination $tmp -Force
$log = Join-Path (Split-Path $proj) 'Saved\Logs\nature-mats.log'
$t0 = Get-Date
& 'C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' $proj -run=pythonscript "-script=$tmp" -unattended -nosplash -nullrhi "-abslog=$log" | Out-Null
$code = $LASTEXITCODE
Remove-Item -LiteralPath $tmp -ErrorAction SilentlyContinue
"nature mats: exit $code in $([int]((Get-Date) - $t0).TotalSeconds) s"
Get-Content $log | Select-String -Pattern 'NATUREMAT|Error|error X|failed' | Select-Object -Last 40
