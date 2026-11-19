# Import art/trees (FBX + PNG) into /Game/Trees and build the tree materials, headless. Env TREES_ONLY=broadleaf,palm limits meshes.
$proj = Join-Path $PSScriptRoot 'PlanetLab.uproject'
$script = Join-Path $PSScriptRoot 'Scripts\import_trees.py'
$t0 = Get-Date
& 'C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' $proj -run=pythonscript "-script=$script" -unattended -nosplash -nosound "-dpcvars=Interchange.FeatureFlags.Import.FBX=0,Interchange.FeatureFlags.Import.PNG=0,Interchange.FeatureFlags.Import.SyncToBrowser=0" "-log=treeimport.log" | Out-Null
"EXIT $LASTEXITCODE in $([int]((Get-Date) - $t0).TotalSeconds)s"
Get-Content "$PSScriptRoot\Saved\Logs\treeimport.log" | Select-String -Pattern 'TREEIMPORT|Error:|error |Traceback|Exception' | Select-Object -First 80
