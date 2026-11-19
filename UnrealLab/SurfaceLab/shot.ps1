# Render one SurfaceLab view offscreen.  shot.ps1 <name> [extra args, e.g. -look=lat,lon,distKm,heading,pitch,fov -sunl=elev,az]
param([string]$Name = 'orbit', [Parameter(ValueFromRemainingArguments = $true)][string[]]$Rest)
$proj = Join-Path $PSScriptRoot 'PlanetLab.uproject'
$shot = Join-Path (Resolve-Path "$PSScriptRoot\..\..\shots") "ue-surfacelab-$Name.png"
if (Test-Path $shot) { Remove-Item $shot }
$t0 = Get-Date
& 'C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor.exe' $proj -game -RenderOffscreen -windowed -ResX=1920 -ResY=1080 -nosplash -unattended -nosound "-shot=$shot" -warm=240 "-log=surf-$Name.log" @Rest | Out-Null
$ok = Test-Path $shot
"$Name : $(if ($ok) { $shot } else { 'NO SHOT' }) in $([int]((Get-Date) - $t0).TotalSeconds)s"
Get-Content "$PSScriptRoot\Saved\Logs\surf-$Name.log" | Select-String -Pattern 'Error|Fatal|STATS|LOD settle|SurfaceTerrain|SurfacePlanet|Lab:|FLY DONE' | Select-Object -Last 14
