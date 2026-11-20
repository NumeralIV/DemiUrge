# DEMIURGE: import the Blender asset families (art/*/manifest.json) into /Game/Assets/<Family>/ of THIS project.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File "<project>\Scripts\assets\import_assets.ps1" [options]
#
# The project is the one this script sits in (Unreal/ on main, Unreal/Lanes/<key>/ in a lane), so running a lane's
# copy imports into that lane only. The editor runs headless (UnrealEditor-Cmd, -run=pythonscript, no window) through
# tools/render_slot.py, which waits for a free heavy-job slot and for the machine to be under the owner's CPU / GPU caps.
#
# Options:
#   -Plan                 only the pure-Python plan (no Unreal): coverage, texture / instance resolution, issues
#   -Families age2,ships  only these families (keys: age0..age6, culture, wonders-a, wonders-b, props, ships,
#                         units-land, animals, trees, shrine)
#   -Match House_A,Tower  only meshes whose name contains one of these
#   -Limit 3              at most N meshes per family (quick checks)
#   -LodProfile game|art  LOD screen sizes: game (default, by projected size for the god camera) or the manifests'
#   -Nanite               enable Nanite on buildings, wonders, culture overlays and hard-surface props
#   -NoSkeletal           skip the animals' skeletal meshes and animations
#   -NoShrine             skip the shrine (art/tools/ue_import.py)
#   -OnlyMats             textures, masters and instances only
#   -Dry                  run the plan inside the editor, import nothing
#   -Timeout 7200         seconds before the editor is given up on
# Distance fields and Lumen mesh cards are derived data: the import session turns their generation off (each mesh
# edit rebuilt them, about 100 s per mesh); the game and the cook build them on first load and cache them in the DDC.
# Output: Saved/Logs/asset-import-<stamp>.log (full editor log), Saved/AssetImport/report.json and plan.json.
# Exit code: 0 when the log ends with "DMI RESULT PASS", 1 on FAIL, 2 when the editor never reported.
param(
	[switch]$Plan,
	[string]$Families = '',
	[string]$Match = '',
	[int]$Limit = 0,
	[ValidateSet('game', 'art')][string]$LodProfile = 'game',
	[switch]$Nanite,
	[switch]$NoSkeletal,
	[switch]$NoShrine,
	[switch]$OnlyMats,
	[switch]$Dry,
	[int]$Timeout = 7200
)
$ErrorActionPreference = 'Stop'

$here     = $PSScriptRoot
$project  = [System.IO.Path]::GetFullPath((Join-Path $here '..\..'))
$uproject = Join-Path $project 'Demiurge.uproject'
$script   = Join-Path $here 'import_assets.py'
# the demiurge root holds art/ and AGENTS.md: walk up from the project (Unreal/ or Unreal/Lanes/<key>/)
$root = $project
while ($root -and -not ((Test-Path -LiteralPath (Join-Path $root 'art')) -and (Test-Path -LiteralPath (Join-Path $root 'AGENTS.md')))) {
	$parent = Split-Path -Parent $root
	if ($parent -eq $root) { $root = $null; break }
	$root = $parent
}
if (-not $root) { Write-Host 'cannot find the demiurge root (art/ and AGENTS.md)'; exit 2 }
if (-not (Test-Path -LiteralPath $uproject)) { Write-Host "project not found: $uproject"; exit 2 }

$python = (Get-Command python -ErrorAction SilentlyContinue)
if (-not $python) { Write-Host 'python not on PATH'; exit 2 }

if ($Plan) {
	$args2 = @($script, '--plan')
	if ($Families) { $args2 += @('--families', $Families) }
	if ($Match) { $args2 += @('--match', $Match) }
	if ($Limit -gt 0) { $args2 += @('--limit', "$Limit") }
	& $python.Source @args2
	exit $LASTEXITCODE
}

$editor = 'C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
if (-not (Test-Path -LiteralPath $editor)) { Write-Host "editor not found: $editor"; exit 2 }

# options travel as environment variables; the commandlet's -script= value cannot hold a path with spaces, so a
# two-line bootstrap in the temp folder (no spaces) runs the real script from DMI_SCRIPT
$env:DMI_ROOT = $root
$env:DMI_SCRIPT = $script
$env:DMI_FAMILIES = $Families
$env:DMI_MATCH = $Match
$env:DMI_LIMIT = if ($Limit -gt 0) { "$Limit" } else { '' }
$env:DMI_LOD_PROFILE = $LodProfile
$env:DMI_NANITE = if ($Nanite) { '1' } else { '0' }
$env:DMI_SKELETAL = if ($NoSkeletal) { '0' } else { '1' }
$env:DMI_SHRINE = if ($NoShrine) { '0' } else { '1' }
$env:DMI_ONLY_MATS = if ($OnlyMats) { '1' } else { '' }
$env:DMI_DRY = if ($Dry) { '1' } else { '' }

$tmp = [System.IO.Path]::GetTempPath()
if ($tmp.Contains(' ')) { Write-Host "temp folder has a space: $tmp"; exit 2 }
$boot = Join-Path $tmp 'demiurge_import_assets_boot.py'
Set-Content -LiteralPath $boot -Encoding ascii -Value @(
	'import os',
	"p = os.environ['DMI_SCRIPT']",
	"exec(compile(open(p, encoding='utf-8').read(), p, 'exec'), {'__file__': p, '__name__': '__main__'})"
)

$logDir = Join-Path $project 'Saved\Logs'
if (-not (Test-Path -LiteralPath $logDir)) { [void](New-Item -ItemType Directory -Force -Path $logDir) }
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$log = Join-Path $logDir "asset-import-$stamp.log"
$slot = Join-Path $root 'tools\render_slot.py'

Write-Host "asset import: $uproject"
Write-Host "  families: $(if ($Families) { $Families } else { 'all' })  match: $Match  limit: $Limit  lods: $LodProfile  nanite: $([bool]$Nanite)"
Write-Host "  log: $log"
$t0 = Get-Date
$editorArgs = @($uproject, '-run=pythonscript', "-script=$boot", '-unattended', '-nosplash', '-ini:Engine:[/Script/Engine.RendererSettings]:r.GenerateMeshDistanceFields=False', '-stdout', '-FullStdOutLogOutput', "-abslog=$log")
$all = @($slot, '--timeout', "$Timeout", '--', $editor) + $editorArgs
$quoted = $all | ForEach-Object { if ($_ -match ' ') { '"' + $_ + '"' } else { $_ } }
$proc = Start-Process -FilePath $python.Source -ArgumentList $quoted -NoNewWindow -PassThru -RedirectStandardOutput (Join-Path $logDir "asset-import-$stamp.stdout.txt") -RedirectStandardError (Join-Path $logDir "asset-import-$stamp.stderr.txt")
if (-not $proc.WaitForExit($Timeout * 1000)) {
	Write-Host "timed out after $Timeout s; stopping the editor"
	Get-Process -Name 'UnrealEditor-Cmd' -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $editor -and $_.StartTime -ge $t0 } | Stop-Process -Force
	exit 2
}
$secs = [int]((Get-Date) - $t0).TotalSeconds
if (-not (Test-Path -LiteralPath $log)) { Write-Host "no editor log after $secs s (see $logDir\asset-import-$stamp.stderr.txt)"; exit 2 }
$lines = Get-Content -LiteralPath $log
$lines | Where-Object { $_ -match 'DMI (ERROR|WARN|family|facing|textures|material instances|meshes|report|RESULT|plan)' } | Select-Object -Last 80 | ForEach-Object { Write-Host $_ }
Write-Host "elapsed $secs s"
if ($lines -match 'DMI RESULT PASS') { exit 0 }
if ($lines -match 'DMI RESULT FAIL') { exit 1 }
Write-Host 'the editor never reported a result (crash or script error): see the log'
$lines | Where-Object { $_ -match 'Error|Traceback|LogPython' } | Select-Object -Last 30 | ForEach-Object { Write-Host $_ }
exit 2
