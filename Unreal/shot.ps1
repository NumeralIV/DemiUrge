# DEMIURGE: render one scenario offscreen and capture it (SPEC-UNREAL 0.5). Owner: C0 bridge.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File "<project or lane>\shot.ps1" <name> [game args] [script options]
#
#   <name>        output shots/ue-<name>.png (a name starting with "ue-" is used as is) and the
#                 scenario to run unless -scenario= is given (unknown names fall back to the longest
#                 registered prefix: "orbit-night" runs "orbit")
#   game args     passed to the game: -cam=lat,lon,distKm,heading,pitch,fov -sun=lat,lon -year=
#                 -warm= (default 240) -seq=a,b,c -fixture= -seed= -skip -live -speed= -ev= -parts=
#                 -noparts= -overlay= -exitafter= ... (see Source/Demiurge/Game/Core/DmScenario.h)
#   script options (not passed on):
#     -timeout=<s>    kill the game after this many seconds (default 600)
#     -stall=<s>      kill it when its log has not grown for this long (default 300)
#     -slotwait=<s>   give up waiting for a render slot after this long (default 3600)
#     -noshot         run without -shot (scenarios that end themselves with Finish); success = exit 0
#
# Runs the project this script sits in (main project or a lane). A machine-wide lock allows at most
# 2 game instances to render at once (12 GB GPU): two named mutexes Global\DemiurgeShotSlot0/1, so a
# crashed or killed script releases its slot at once. The slot also records the game's process id
# (Unreal/Saved/ShotSlots of the main project); a game left running by a killed script is ended by
# the next script that takes that slot. Prints the PNG path, seconds, exit code and the log's key
# lines (DmShot, DmStats, DmScenario, DmFixture, errors). Exit code 0 = PNG written (or -noshot and
# the game exited 0); 1 = no shot; 2 = timeout / stall (game killed); 3 = no render slot.

param([Parameter(Position = 0)][string]$Name = 'orbit', [Parameter(ValueFromRemainingArguments = $true)][string[]]$Rest)

$ErrorActionPreference = 'Stop'
$editor = 'C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor.exe'
$proj = Join-Path $PSScriptRoot 'Demiurge.uproject'
if (-not (Test-Path -LiteralPath $proj)) { Write-Host "shot: no Demiurge.uproject next to $PSCommandPath"; exit 1 }

# ---- the root (folder with docs/ and shots/) and the main project
$root = $PSScriptRoot
for ($i = 0; $i -lt 6; $i++) {
	if ((Test-Path -LiteralPath (Join-Path $root 'docs')) -and (Test-Path -LiteralPath (Join-Path $root 'shots'))) { break }
	$root = Split-Path -Parent $root
}
if (-not (Test-Path -LiteralPath (Join-Path $root 'shots'))) { Write-Host "shot: cannot find the shots folder above $PSScriptRoot"; exit 1 }
$mainProj = Join-Path $root 'Unreal'
$slotDir = Join-Path $mainProj 'Saved\ShotSlots'
if (-not (Test-Path -LiteralPath $slotDir)) { [void](New-Item -ItemType Directory -Force -Path $slotDir) }

# ---- arguments
$timeout = 600; $stall = 300; $slotWait = 3600; $noShot = $false
$gameArgs = New-Object System.Collections.Generic.List[string]
$hasScenario = $false; $hasWarm = $false
foreach ($a in @($Rest)) {
	if ($null -eq $a -or $a -eq '') { continue }
	if ($a -match '^-timeout=(\d+)$') { $timeout = [int]$Matches[1]; continue }
	if ($a -match '^-stall=(\d+)$') { $stall = [int]$Matches[1]; continue }
	if ($a -match '^-slotwait=(\d+)$') { $slotWait = [int]$Matches[1]; continue }
	if ($a -eq '-noshot') { $noShot = $true; continue }
	if ($a -like '-scenario=*') { $hasScenario = $true }
	if ($a -like '-warm=*') { $hasWarm = $true }
	if ($a -like '-shot=*' -or $a -like '-log=*') { Write-Host "shot: ignoring $a (set by shot.ps1)"; continue }
	$gameArgs.Add($a)
}
$base = if ($Name.StartsWith('ue-')) { $Name } else { "ue-$Name" }
$png = Join-Path (Join-Path $root 'shots') "$base.png"
$logName = "shot-$Name.log"
$logPath = Join-Path $PSScriptRoot "Saved\Logs\$logName"
if (-not $hasScenario) { $gameArgs.Add("-scenario=$Name") }
if (-not $hasWarm) { $gameArgs.Add('-warm=240') }

function Quote([string]$s) { if ($s -match '[\s"]') { return '"' + $s.Replace('"', '\"') + '"' } return $s }
$argList = @((Quote $proj), '-game', '-RenderOffscreen', '-windowed', '-ResX=1920', '-ResY=1080', '-nosplash', '-unattended', '-nosound', '-NoVerifyGC', (Quote "-log=$logName"))
if (-not $noShot) { $argList += (Quote "-shot=$png") }
foreach ($a in $gameArgs) { $argList += (Quote $a) }
$argString = $argList -join ' '

function Stop-Tree([int]$procId) {
	try { & taskkill.exe /T /F /PID $procId 2>&1 | Out-Null } catch {}
}

# ---- take a render slot (2 machine-wide)
$t0 = Get-Date
$mutex = $null; $slot = -1; $waited = $false
while ($slot -lt 0) {
	for ($k = 0; $k -lt 2 -and $slot -lt 0; $k++) {
		$m = New-Object System.Threading.Mutex($false, "Global\DemiurgeShotSlot$k")
		$got = $false
		try { $got = $m.WaitOne(0) } catch [System.Threading.AbandonedMutexException] { $got = $true }
		if ($got) { $mutex = $m; $slot = $k } else { $m.Dispose() }
	}
	if ($slot -ge 0) { break }
	if (-not $waited) { Write-Host "shot ${Name}: both render slots busy, waiting..."; $waited = $true }
	if (((Get-Date) - $t0).TotalSeconds -gt $slotWait) { Write-Host "shot ${Name}: NO SLOT after $slotWait s"; exit 3 }
	Start-Sleep -Milliseconds 1500
}
$waitSecs = [int]((Get-Date) - $t0).TotalSeconds
$slotFile = Join-Path $slotDir "slot$slot.json"

$exitCode = 1
$reason = ''
$proc = $null
try {
	# a game left behind by a killed shot.ps1 that held this slot
	if (Test-Path -LiteralPath $slotFile) {
		try {
			$old = Get-Content -LiteralPath $slotFile -Raw | ConvertFrom-Json
			$op = Get-Process -Id ([int]$old.pid) -ErrorAction SilentlyContinue
			if ($op -and $op.ProcessName -like 'UnrealEditor*' -and $op.StartTime.ToString('o') -eq $old.start) {
				Write-Host "shot ${Name}: ending orphaned game pid $($old.pid) left in slot $slot ($($old.name))"
				Stop-Tree ([int]$old.pid)
			}
		} catch {}
		Remove-Item -LiteralPath $slotFile -Force -ErrorAction SilentlyContinue
	}
	if (-not $noShot -and (Test-Path -LiteralPath $png)) { Remove-Item -LiteralPath $png -Force }
	if (Test-Path -LiteralPath $logPath) { Remove-Item -LiteralPath $logPath -Force -ErrorAction SilentlyContinue }

	$tRun = Get-Date
	$proc = Start-Process -FilePath $editor -ArgumentList $argString -PassThru -WindowStyle Hidden
	$null = $proc.Handle   # keep the handle so ExitCode is available after exit
	@{ pid = $proc.Id; start = $proc.StartTime.ToString('o'); name = $Name; project = $proj } | ConvertTo-Json | Set-Content -LiteralPath $slotFile -Encoding UTF8

	$lastSize = -1; $lastGrow = Get-Date; $pngSeen = $null
	while (-not $proc.HasExited) {
		Start-Sleep -Milliseconds 500
		$now = Get-Date
		$size = 0
		if (Test-Path -LiteralPath $logPath) { $size = (Get-Item -LiteralPath $logPath).Length }
		if ($size -ne $lastSize) { $lastSize = $size; $lastGrow = $now }
		if (-not $noShot -and -not $pngSeen -and (Test-Path -LiteralPath $png)) { $pngSeen = $now }
		if ($pngSeen -and ($now - $pngSeen).TotalSeconds -gt 60) { $reason = 'did not exit 60 s after the shot (killed)'; Stop-Tree $proc.Id; break }
		if (($now - $tRun).TotalSeconds -gt $timeout) { $reason = "timeout after $timeout s (killed)"; Stop-Tree $proc.Id; break }
		if (($now - $lastGrow).TotalSeconds -gt $stall) { $reason = "log silent for $stall s (killed)"; Stop-Tree $proc.Id; break }
	}
	try { $proc.WaitForExit(15000) | Out-Null } catch {}
	$code = if ($proc.HasExited) { $proc.ExitCode } else { -1 }
	$secs = [int]((Get-Date) - $tRun).TotalSeconds
	$ok = if ($noShot) { $code -eq 0 -and $reason -eq '' } else { Test-Path -LiteralPath $png }
	if ($ok) { $exitCode = 0 } elseif ($reason -ne '') { $exitCode = 2 } else { $exitCode = 1 }
	$what = if ($noShot) { '(no shot)' } elseif (Test-Path -LiteralPath $png) { $png } else { 'NO SHOT' }
	$extra = if ($reason) { " - $reason" } else { '' }
	Write-Host "$Name : $what in $secs s (slot $slot, waited $waitSecs s, exit code $code)$extra"
	if (Test-Path -LiteralPath $logPath) {
		Get-Content -LiteralPath $logPath | Select-String -Pattern 'Error|Fatal|Ensure|DmShot|DmStats|DmScenario|DmFixture|DmSim:|DmSimTest|DmPickTest|DmPlaceholder|DmScene' |
			Where-Object { $_.Line -notmatch 'LogStreaming|LogConfig|LogPython|SourceControl|LogEOS' } | Select-Object -Last 40 | ForEach-Object { $_.Line }
	} else { Write-Host "(no log at $logPath)" }
}
finally {
	if ($proc -and -not $proc.HasExited) { Stop-Tree $proc.Id }
	Remove-Item -LiteralPath $slotFile -Force -ErrorAction SilentlyContinue
	if ($mutex) { try { $mutex.ReleaseMutex() } catch {}; $mutex.Dispose() }
}
exit $exitCode
