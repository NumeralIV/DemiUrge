# DEMIURGE planet (C1): rebuild the planet materials headless (Scripts/planet.py) in the project this script
# belongs to (main project or a lane):  powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/planet/mats.ps1
$proj = (Resolve-Path (Join-Path $PSScriptRoot '..\..\Demiurge.uproject')).Path
$script = (Resolve-Path (Join-Path $PSScriptRoot '..\planet.py')).Path
$log = 'planet-mats.log'
$t0 = Get-Date
& 'C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' $proj -run=pythonscript "-script=$script" -unattended -nosplash -nullrhi "-abslog=$(Join-Path (Split-Path $proj) "Saved\Logs\$log")" | Out-Null
$code = $LASTEXITCODE
"planet mats: exit $code in $([int]((Get-Date) - $t0).TotalSeconds) s"
Get-Content (Join-Path (Split-Path $proj) "Saved\Logs\$log") | Select-String -Pattern 'planet.py|Error|error X|failed' | Select-Object -Last 40
