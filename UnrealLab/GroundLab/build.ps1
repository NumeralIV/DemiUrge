# Build the PlanetLab editor target; prints a short summary.
$proj = Join-Path $PSScriptRoot 'PlanetLab.uproject'
$t0 = Get-Date
$out = & 'C:\Program Files\Epic Games\UE_5.7\Engine\Build\BatchFiles\Build.bat' PlanetLabEditor Win64 Development "-Project=$proj" -WaitMutex -NoHotReload 2>&1
$code = $LASTEXITCODE
$out | Where-Object { $_ -match 'error|warning C|Result:' } | Select-Object -First 40
"EXIT $code in $([int]((Get-Date) - $t0).TotalSeconds)s"
exit $code
