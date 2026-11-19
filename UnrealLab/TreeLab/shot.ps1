# Render one LifeLab view offscreen.  shot.ps1 <name> [extra args, e.g. -focus=village -view=600,210,25 -night]
param([string]$Name = 'test', [Parameter(ValueFromRemainingArguments = $true)][string[]]$Rest)
$proj = Join-Path $PSScriptRoot 'PlanetLab.uproject'
$shot = Join-Path (Resolve-Path "$PSScriptRoot\..\..\shots") "ue-treelab-$Name.png"
if (Test-Path $shot) { Remove-Item $shot }
$t0 = Get-Date
& 'C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor.exe' $proj -game -RenderOffscreen -windowed -ResX=1920 -ResY=1080 -nosplash -unattended -nosound "-shot=$shot" "-warm=$(if ($env:LIFEWARM) { $env:LIFEWARM } else { 300 })" "-log=treelab-$Name.log" @Rest | Out-Null
$ok = Test-Path $shot
"$Name : $(if ($ok) { $shot } else { 'NO SHOT' }) in $([int]((Get-Date) - $t0).TotalSeconds)s"
Get-Content "$PSScriptRoot\Saved\Logs\treelab-$Name.log" | Select-String -Pattern 'LIFESTATS|LifeWorld|LifeCam|LifeTex|Error|Fatal|Ensure' | Select-Object -Last 14
