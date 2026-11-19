# Rebuild the SurfaceLab materials headless (Scripts/make_materials.py).
$proj = Join-Path $PSScriptRoot 'PlanetLab.uproject'
$script = Join-Path $PSScriptRoot 'Scripts\make_materials.py'
$t0 = Get-Date
& 'C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' $proj -run=pythonscript "-script=$script" -unattended -nosplash -nullrhi | Out-Null
"mats in $([int]((Get-Date) - $t0).TotalSeconds)s"
Get-Content "$PSScriptRoot\Saved\Logs\PlanetLab.log" | Select-String -Pattern 'Error|saved /Game|link failed|SLW' | Select-Object -Last 30
