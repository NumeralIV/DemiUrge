# Render one CloudLab view offscreen.  shot.ps1 <name> [extra args, e.g. -cam=5,20,15,30 -sun=110,10 -ev=0]
# Writes shots/ue-cloudlab-<name>.png and prints the perf summary the game mode logs (CloudLabPerf lines).
param([string]$Name = 'orbit', [Parameter(ValueFromRemainingArguments = $true)][string[]]$Rest)
$proj = Join-Path $PSScriptRoot 'PlanetLab.uproject'
$shot = Join-Path (Resolve-Path "$PSScriptRoot\..\..\shots") "ue-cloudlab-$Name.png"
if (Test-Path $shot) { Remove-Item $shot }
$t0 = Get-Date
& 'C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor.exe' $proj -game -RenderOffscreen -windowed -ResX=1920 -ResY=1080 -nosplash -unattended -nosound "-shot=$shot" -warm=240 "-log=cloudlab-$Name.log" @Rest | Out-Null
$ok = Test-Path $shot
"$Name : $(if ($ok) { $shot } else { 'NO SHOT' }) in $([int]((Get-Date) - $t0).TotalSeconds)s"
Get-Content "$PSScriptRoot\Saved\Logs\cloudlab-$Name.log" | Select-String -Pattern 'Error|Fatal|CloudLab' | Select-Object -Last 12
