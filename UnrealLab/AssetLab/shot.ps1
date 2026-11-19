# Render one AssetLab view offscreen -> art/renders/ue/<name>.png
#   shot.ps1 <name> [args, e.g. -assets=SM_Shrine_Intact,standin -focus=asset -view=40,230,18 -sun=150,7]
param([string]$Name = 'test', [Parameter(ValueFromRemainingArguments = $true)][string[]]$Rest)
$proj = Join-Path $PSScriptRoot 'PlanetLab.uproject'
$dir = Join-Path $PSScriptRoot '..\..\art\renders\ue'
New-Item -ItemType Directory -Force $dir | Out-Null
$shot = Join-Path (Resolve-Path $dir) "$Name.png"
if (Test-Path $shot) { Remove-Item $shot }
$t0 = Get-Date
& 'C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor.exe' $proj -game -RenderOffscreen -windowed -ResX=1920 -ResY=1080 -nosplash -unattended -nosound "-shot=$shot" "-warm=$(if ($env:LIFEWARM) { $env:LIFEWARM } else { 300 })" "-log=assetlab-$Name.log" @Rest | Out-Null
$ok = Test-Path $shot
"$Name : $(if ($ok) { $shot } else { 'NO SHOT' }) in $([int]((Get-Date) - $t0).TotalSeconds)s"
Get-Content "$PSScriptRoot\Saved\Logs\assetlab-$Name.log" | Select-String -Pattern 'ASSETSHOW|LIFESTATS timing|LifeCam|Error|Fatal|Ensure' | Select-Object -Last 16
