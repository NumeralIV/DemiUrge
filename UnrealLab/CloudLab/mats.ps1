# Recreate CloudLab materials headless (Scripts/make_clouds.py; -All also runs PlanetLab's make_materials.py).
param([switch]$All)
$proj = Join-Path $PSScriptRoot 'PlanetLab.uproject'
$ed = 'C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$log = Join-Path $PSScriptRoot 'Saved\Logs\PlanetLab.log'
$scripts = @('make_clouds.py'); if ($All) { $scripts = @('make_materials.py') + $scripts }
foreach ($s in $scripts) {
  & $ed $proj -run=pythonscript "-script=$(Join-Path $PSScriptRoot "Scripts\$s")" -unattended -nosplash -nullrhi | Out-Null
  Get-Content $log | Select-String -Pattern 'Error|saved /Game|link failed|Traceback|Exception' | Select-Object -First 30
}
