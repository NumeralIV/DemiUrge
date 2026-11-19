# Render the CloudLab reference views (day, terminator, cyclone, ground, shadows, night): views.ps1 <suffix> [extra args for every view]
param([string]$Tag = 'x', [Parameter(ValueFromRemainingArguments = $true)][string[]]$Rest)
$shot = Join-Path $PSScriptRoot 'shot.ps1'
$views = [ordered]@{
  'day'        = @('-cam=5,15,15,30', '-sun=40,25')
  'terminator' = @('-cam=5,20,15,30', '-sun=110,10', '-moon=-13,6,25,27', '-stars=3')
  'cyclone'    = @('-pov=3,15,30,0,-50,50', '-sun=70,18')
  'ground'     = @('-pov=12,8,0.03,30,22,75', '-sun=60,30')
  'shadows'    = @('-pov=10,-52,8,0,-65,60', '-sun=-100,25')
  'night'      = @('-cam=5,215,10,30', '-sun=110,10', '-moonlux=0.5', '-moon=12,-5,25,27', '-stars=3')
}
foreach ($k in $views.Keys) {
  & powershell -NoProfile -ExecutionPolicy Bypass -File $shot "$k-$Tag" @($views[$k] + $Rest) | Select-String -Pattern 'Perf|NO SHOT'
}
