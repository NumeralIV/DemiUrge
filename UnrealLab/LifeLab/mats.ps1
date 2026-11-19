# Rebuild the LifeLab materials headlessly.
$proj = Join-Path $PSScriptRoot 'PlanetLab.uproject'
$script = Join-Path $PSScriptRoot 'Scripts\make_life_materials.py'
$t0 = Get-Date
& 'C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' $proj -run=pythonscript "-script=$script" -unattended -nosplash -nosound "-log=lifemats.log" | Out-Null
"EXIT $LASTEXITCODE in $([int]((Get-Date) - $t0).TotalSeconds)s"
Get-Content "$PSScriptRoot\Saved\Logs\lifemats.log" | Select-String -Pattern 'LIFEMAT|Error|error |warning X|link failed' | Where-Object { $_ -notmatch 'LIFEMAT tex' } | Select-Object -First 60
