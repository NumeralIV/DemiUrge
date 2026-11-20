# DEMIURGE aurora: rebuild /Game/Sky/Aurora/M_Aurora headless (Scripts/aurora.py) in the project this script belongs
# to (main project or a lane):  powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/aurora/mats.ps1
# Route it through tools/render_slot.py like any other Unreal launch.
$proj = (Resolve-Path (Join-Path $PSScriptRoot '..\..\Demiurge.uproject')).Path
$script = (Resolve-Path (Join-Path $PSScriptRoot '..\aurora.py')).Path
$log = Join-Path (Split-Path $proj) 'Saved\Logs\aurora-mats.log'
$t0 = Get-Date
& 'C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' $proj -run=pythonscript "-script=$script" -unattended -nosplash -nullrhi "-abslog=$log" | Out-Null
$code = $LASTEXITCODE
"aurora mats: exit $code in $([int]((Get-Date) - $t0).TotalSeconds) s"
Get-Content $log | Select-String -Pattern 'aurora.py|Error|error X|failed' | Select-Object -Last 40
if ($code -ne 0) { exit $code }
if (-not (Get-Content $log | Select-String -SimpleMatch 'no failed links')) { exit 1 }
