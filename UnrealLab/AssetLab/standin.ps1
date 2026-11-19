# Refresh the AssetLab copy of the towns mesh factory (the generated stand-in that real assets replace) from the main
# project, renaming the module API macro. Read-only reference: AssetLab never edits the towns module.
$src = Join-Path $PSScriptRoot '..\..\Unreal\Source\Demiurge\Game\Towns'
$dst = Join-Path $PSScriptRoot 'Source\PlanetLab\StandIn'
New-Item -ItemType Directory -Force $dst | Out-Null
foreach ($f in 'DmTownsMesh.h', 'DmTownsMesh.cpp', 'DmTownsKit.h', 'DmTownsKit.cpp') {
  $t = Get-Content (Join-Path $src $f) -Raw
  $t = $t -replace 'DEMIURGE_API', 'PLANETLAB_API' -replace '"Game/Towns/', '"StandIn/'
  $hdr = "// AssetLab copy (read-only reference) of Unreal/Source/Demiurge/Game/Towns: the generated stand-in the real asset replaces.`n// Refreshed by UnrealLab/AssetLab/standin.ps1; never edit here.`n"
  Set-Content -Path (Join-Path $dst $f) -Value ($hdr + $t) -Encoding UTF8 -NoNewline
}
"stand-in sources refreshed"
