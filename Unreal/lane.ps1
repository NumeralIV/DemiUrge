# DEMIURGE: private build lanes for client engineers (SPEC-UNREAL 0.1). Owner: C0 bridge.
# (Build output folders named build / build-* anywhere are never copied either.)
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File "<root>\Unreal\lane.ps1" <command> <key> [options]
#
#   new <key> [-Force]       copy Unreal/ into Unreal/Lanes/<key>/ (no Intermediate, Binaries, Saved,
#                            DerivedDataCache, Lanes, SimTests/build; nothing is rebuilt or compiled)
#                            and write the lane's own build.ps1 (Build.bat -NoMutex: lanes never block
#                            each other or the main build). -Force replaces an existing lane's sources
#                            (keeps its Intermediate / Binaries so the next build stays incremental).
#   sync <key> [-DryRun] [-Force] [-Prune]
#                            copy ONLY the files listed in Unreal/lanes/<key>.owned (one path or glob per
#                            line, relative to Unreal/, e.g. Source/Demiurge/Game/Planet/**) from the lane
#                            to the main project. Identical files are skipped (no needless rebuilds).
#                            A file that another key's .owned list claims is refused unless -Force
#                            ("planet" and "planet-verify" are related keys and never conflict).
#                            -Prune also deletes main files that match the owned list but no longer exist
#                            in the lane. The .owned file itself is synced when the lane holds one.
#                            Stale-copy protection (the lane's .lane-base records what it took from main):
#                            a file you never changed in the lane is skipped when main has a newer version
#                            ("skip ... run refresh"); a file changed both in the lane and in main since is a
#                            CONFLICT (exit 3; merge, then -Force); -Prune only deletes files the lane had
#                            and removed, and only if main still holds that version.
#   refresh <key> [-DryRun]  pull main's current files into the lane (everything except the lane's owned
#                            files and the excluded folders), so a lane picks up work others synced; files
#                            main deleted or renamed are removed from the lane (unless you changed them).
#   status <key>             owned files and how they differ between the lane and main
#                            (same / lane only / main only / yours / main newer / both changed)
#   build <key>              build the lane (same as Unreal/Lanes/<key>/build.ps1)
#   scaffold <key>           rewrite the lane's generated build.ps1 (after lane.ps1 changed)
#   list                     every lane
#   remove <key>             delete the lane folder (Unreal/Lanes/<key> only)
#
# Owned lists live in Unreal/lanes/<key>.owned (on Windows Unreal/lanes and Unreal/Lanes are the same
# folder: lanes are subfolders, owned lists are files next to them). A list written inside the lane
# (Unreal/Lanes/<key>/lanes/<key>.owned) is also read, merged with main's, and synced back.
# Keys: letters, digits, '-' and '_'.

param(
	[Parameter(Position = 0)][string]$Command = 'help',
	[Parameter(Position = 1)][string]$Key = '',
	[switch]$Force,
	[switch]$DryRun,
	[switch]$Prune
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2

# ---------------------------------------------------------------- locate the main project
function Get-MainRoot {
	$here = $PSScriptRoot
	$marker = Join-Path $here '.lane'
	if (Test-Path -LiteralPath $marker) {
		$info = Get-Content -LiteralPath $marker -Raw | ConvertFrom-Json
		return [System.IO.Path]::GetFullPath($info.main)
	}
	return [System.IO.Path]::GetFullPath($here)
}

$Main = Get-MainRoot
$LanesDir = Join-Path $Main 'Lanes'
$Excluded = @('Intermediate', 'Binaries', 'Saved', 'DerivedDataCache', 'Lanes', 'SimTests/build')
$BaseName = '.lane-base'
$NeverSync = @('build.ps1', '.lane', $BaseName, 'Demiurge.uproject')   # lane scaffolding (build.ps1 is generated per lane)

function Fail([string]$msg) { Write-Host "lane: $msg"; exit 1 }

function Test-Key([string]$k) {
	if ([string]::IsNullOrWhiteSpace($k)) { Fail "missing <key>" }
	if ($k -notmatch '^[A-Za-z0-9_-]{1,48}$') { Fail "bad key '$k' (letters, digits, - and _ only)" }
}

function Get-LaneRoot([string]$k) { return (Join-Path $LanesDir $k) }

# relative path with forward slashes
function Get-Rel([string]$root, [string]$full) {
	$r = $full.Substring($root.Length).TrimStart('\', '/')
	return $r.Replace('\', '/')
}

function Test-ExcludedRel([string]$rel) {
	foreach ($e in $Excluded) {
		if ($rel.Equals($e, 'OrdinalIgnoreCase') -or $rel.StartsWith($e + '/', 'OrdinalIgnoreCase')) { return $true }
	}
	# build output folders anywhere (SimTests/build, Tools/simrun/build-*, ...)
	foreach ($seg in $rel.Split('/')) { if ($seg -match '^(build|build-.*|\.vs)$') { return $true } }
	return $false
}

# every file under root (relative paths), skipping the excluded folders without descending into them
function Get-TreeFiles([string]$root) {
	$out = New-Object System.Collections.Generic.List[string]
	$stack = New-Object System.Collections.Generic.Stack[string]
	$stack.Push($root)
	while ($stack.Count -gt 0) {
		$dir = $stack.Pop()
		foreach ($f in [System.IO.Directory]::GetFiles($dir)) { $out.Add((Get-Rel $root $f)) }
		foreach ($d in [System.IO.Directory]::GetDirectories($dir)) {
			$rel = Get-Rel $root $d
			if (-not (Test-ExcludedRel $rel)) { $stack.Push($d) }
		}
	}
	return $out
}

# glob (relative to Unreal/, '/' separators, ** = any depth, * = within one segment) -> regex
function ConvertTo-GlobRegex([string]$glob) {
	$g = $glob.Trim().Replace('\', '/')
	if ($g.StartsWith('./')) { $g = $g.Substring(2) }
	if ($g.EndsWith('/')) { $g = $g + '**' }
	$sb = New-Object System.Text.StringBuilder
	$i = 0
	while ($i -lt $g.Length) {
		$ch = $g[$i]
		if ($ch -eq '*') {
			if ($i + 1 -lt $g.Length -and $g[$i + 1] -eq '*') {
				if ($i + 2 -lt $g.Length -and $g[$i + 2] -eq '/') { [void]$sb.Append('(?:.*/)?'); $i += 3; continue }
				[void]$sb.Append('.*'); $i += 2; continue
			}
			[void]$sb.Append('[^/]*')
		}
		elseif ($ch -eq '?') { [void]$sb.Append('[^/]') }
		else { [void]$sb.Append([regex]::Escape([string]$ch)) }
		$i++
	}
	return New-Object System.Text.RegularExpressions.Regex ('^' + $sb.ToString() + '$'), 'IgnoreCase'
}

function Read-OwnedFile([string]$path) {
	$pats = @()
	if (-not (Test-Path -LiteralPath $path)) { return $pats }
	foreach ($line in Get-Content -LiteralPath $path) {
		$t = $line.Trim()
		if ($t -eq '' -or $t.StartsWith('#')) { continue }
		$t = $t.Replace('\', '/')
		if ($t.StartsWith('Unreal/', 'OrdinalIgnoreCase')) { $t = $t.Substring(7) }   # tolerate a leading Unreal/
		if ($t.Contains('..') -or [System.IO.Path]::IsPathRooted($t)) { Write-Host "lane: ignoring owned entry '$t' (must be relative to Unreal/, no '..')"; continue }
		$pats += $t
	}
	return $pats
}

function Get-OwnedPatterns([string]$k) {
	$mainList = Join-Path $LanesDir "$k.owned"
	$laneList = Join-Path (Join-Path (Get-LaneRoot $k) 'lanes') "$k.owned"
	$p = @(Read-OwnedFile $mainList) + @(Read-OwnedFile $laneList)
	return @($p | Select-Object -Unique)
}

function Test-Match($regexes, [string]$rel) {
	foreach ($r in $regexes) { if ($r.IsMatch($rel)) { return $true } }
	return $false
}

function Test-Related([string]$a, [string]$b) {
	if ($a -eq $b) { return $true }
	if ($a.StartsWith($b + '-') -or $b.StartsWith($a + '-')) { return $true }
	return $false
}

function Test-SameFile([string]$a, [string]$b) {
	$fa = New-Object System.IO.FileInfo $a
	$fb = New-Object System.IO.FileInfo $b
	if (-not $fa.Exists -or -not $fb.Exists) { return $false }
	if ($fa.Length -ne $fb.Length) { return $false }
	if ($fa.Length -eq 0) { return $true }
	return ((Get-Md5 $a) -eq (Get-Md5 $b))
}

# ---------------------------------------------------------------- the lane's base (what it last took from main)
# <lane>/.lane-base: "md5<TAB>relative path" of every file as it was copied from main (new / refresh) or
# last synced to main. It tells "you changed this" from "main changed this since", so sync never pushes a
# stale copy over a newer main file, refresh removes files main deleted, and -Prune never deletes files
# that were added to main after the lane last looked. Lanes made before it existed have none (old rules).
$Md5 = [System.Security.Cryptography.MD5]::Create()
function Get-Md5([string]$path) {
	$s = [System.IO.File]::OpenRead($path)
	try { return [System.BitConverter]::ToString($Md5.ComputeHash($s)).Replace('-', '') } finally { $s.Dispose() }
}

function Read-Base([string]$lane) {
	$p = Join-Path $lane $BaseName
	if (-not (Test-Path -LiteralPath $p)) { return $null }
	$h = @{}
	foreach ($line in [System.IO.File]::ReadAllLines($p)) {
		$i = $line.IndexOf("`t")
		if ($i -gt 0) { $h[$line.Substring($i + 1)] = $line.Substring(0, $i) }
	}
	return $h
}

function Write-Base([string]$lane, $h) {
	$lines = New-Object System.Collections.Generic.List[string]
	foreach ($k in @($h.Keys | Sort-Object)) { $lines.Add("$($h[$k])`t$k") }
	[System.IO.File]::WriteAllLines((Join-Path $lane $BaseName), $lines.ToArray())
}

function New-Base([string]$lane) {
	# only files the lane holds exactly as main has them (new -Force keeps lane-only files: those are yours)
	$h = @{}
	foreach ($rel in Get-TreeFiles $lane) {
		if ($NeverSync -contains $rel) { continue }
		$m = Join-Path $Main $rel
		if (Test-SameFile (Join-Path $lane $rel) $m) { $h[$rel] = Get-Md5 $m }
	}
	Write-Base $lane $h
	return $h.Count
}

function Copy-One([string]$src, [string]$dst) {
	$dir = Split-Path -Parent $dst
	if (-not (Test-Path -LiteralPath $dir)) { [void](New-Item -ItemType Directory -Force -Path $dir) }
	try { Copy-Item -LiteralPath $src -Destination $dst -Force -ErrorAction Stop; return $true }
	catch { Write-Host "  FAILED to copy $src -> $dst : $($_.Exception.Message)"; return $false }
}

# ---------------------------------------------------------------- the lane's own build script
function Write-LaneBuild([string]$laneRoot, [string]$k) {
	$text = @'
# DEMIURGE lane build (generated by Unreal/lane.ps1 for lane "__KEY__"; do not sync to main).
# Builds this lane's DemiurgeEditor (Win64 Development) with Build.bat, without blocking other builds:
#  - UBT's mutex is global (keyed by the engine's UnrealBuildTool.dll path): -NoMutex.
#  - Build.bat also serializes itself with a lock file %TMP%\<path of Build.bat>.lock: this script
#    points TMP / TEMP at the lane's Saved/Temp, so each lane has its own Build.bat lock (and UBT temp).
# Each lane is its own project (own Intermediate / Binaries), so concurrent builds are safe.
# Prints the result, elapsed seconds, the log paths and every line with "error" or "warning C".
param([switch]$Clean)
$ErrorActionPreference = 'Continue'
$engineRoot = [System.IO.Path]::GetFullPath('C:/Program Files/Epic Games/UE_5.7')
$buildBat   = [System.IO.Path]::GetFullPath((Join-Path $engineRoot 'Engine/Build/BatchFiles/Build.bat'))
$uproject   = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'Demiurge.uproject'))
$logDir     = Join-Path $PSScriptRoot 'Saved/Logs'
if (-not (Test-Path -LiteralPath $logDir)) { [void](New-Item -ItemType Directory -Force -Path $logDir) }
$stamp  = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$ubtLog = Join-Path $logDir "UBT-$stamp.txt"
$outLog = Join-Path $logDir "build-$stamp.log"
$tmpDir = Join-Path $PSScriptRoot 'Saved/Temp'
if (-not (Test-Path -LiteralPath $tmpDir)) { [void](New-Item -ItemType Directory -Force -Path $tmpDir) }
$env:TMP = [System.IO.Path]::GetFullPath($tmpDir)
$env:TEMP = $env:TMP
$buildArgs = @('DemiurgeEditor', 'Win64', 'Development', "-Project=$uproject", '-NoMutex', '-NoHotReload', "-Log=$ubtLog")
if ($Clean) { $buildArgs += '-Clean' }
Write-Host "Building lane __KEY__ (DemiurgeEditor Win64 Development, -NoMutex)"
$watch = [System.Diagnostics.Stopwatch]::StartNew()
$lines = @(& $buildBat @buildArgs 2>&1 | ForEach-Object { "$_" })
$exitCode = $LASTEXITCODE
$watch.Stop()
$lines | Set-Content -LiteralPath $outLog -Encoding UTF8
$result = if ($exitCode -eq 0) { 'Succeeded' } else { 'Failed' }
$hits = @($lines | Where-Object { $_ -match 'error|warning C' -and $_ -notmatch 'WarningsAsErrors' })
Write-Host ""
Write-Host "Result:  $result (exit code $exitCode)"
Write-Host "Elapsed: $([Math]::Round($watch.Elapsed.TotalSeconds, 1)) s"
Write-Host "Log:     $outLog"
Write-Host "UBT log: $ubtLog"
Write-Host "Lines containing 'error' or 'warning C': $($hits.Count)"
foreach ($hit in $hits) { Write-Host $hit }
exit $exitCode
'@
	$text = $text.Replace('__KEY__', $k)
	Set-Content -LiteralPath (Join-Path $laneRoot 'build.ps1') -Value $text -Encoding UTF8
}

# ---------------------------------------------------------------- commands
function Invoke-New([string]$k) {
	Test-Key $k
	$lane = Get-LaneRoot $k
	if ((Test-Path -LiteralPath $lane) -and -not $Force) { Fail "lane '$k' exists ($lane); use -Force to replace its sources, or 'refresh'" }
	$t0 = Get-Date
	if (-not (Test-Path -LiteralPath $LanesDir)) { [void](New-Item -ItemType Directory -Force -Path $LanesDir) }
	$xd = @()
	foreach ($e in $Excluded) { $xd += (Join-Path $Main $e.Replace('/', '\')) }
	# robocopy: /E subfolders, /XD excluded folders (by full path: only the top-level ones), multithreaded, quiet
	$xd += @('build', 'build-*', '.vs')   # build output folders anywhere (SimTests/build, Tools/simrun/build-*)
	$rc = @($Main, $lane, '/E', '/XD') + $xd + @('/XF', '.lane', $BaseName, '/MT:16', '/R:1', '/W:1', '/NFL', '/NDL', '/NJH', '/NJS', '/NP')
	& robocopy.exe @rc | Out-Null
	$code = $LASTEXITCODE
	if ($code -ge 8) { Fail "robocopy failed with exit code $code" }
	$info = [ordered]@{ key = $k; main = $Main; created = (Get-Date).ToString('s'); by = 'lane.ps1' }
	($info | ConvertTo-Json) | Set-Content -LiteralPath (Join-Path $lane '.lane') -Encoding UTF8
	Write-LaneBuild $lane $k
	[void](New-Base $lane)
	$owned = Join-Path $LanesDir "$k.owned"
	if (-not (Test-Path -LiteralPath $owned)) {
		@("# Files lane '$k' owns, relative to Unreal/ (one path or glob per line; ** = any depth).",
		  "# lane.ps1 sync $k copies exactly these from Unreal/Lanes/$k/ to the main project.",
		  "# Example:  Source/Demiurge/Game/<Area>/**   and   Scripts/$k.py") | Set-Content -LiteralPath $owned -Encoding UTF8
	}
	$n = @(Get-TreeFiles $lane).Count
	$secs = [Math]::Round(((Get-Date) - $t0).TotalSeconds, 1)
	Write-Host "lane '$k' created: $lane ($n files, $secs s)"
	Write-Host "  build:  powershell -NoProfile -ExecutionPolicy Bypass -File `"$lane\build.ps1`""
	Write-Host "  shot:   powershell -NoProfile -ExecutionPolicy Bypass -File `"$lane\shot.ps1`" <name> [-scenario=orbit ...]"
	Write-Host "  owned:  $owned"
}

function Invoke-Sync([string]$k) {
	Test-Key $k
	$lane = Get-LaneRoot $k
	if (-not (Test-Path -LiteralPath $lane)) { Fail "no lane '$k'" }
	$pats = @(Get-OwnedPatterns $k)
	if ($pats.Count -eq 0) { Fail "no owned files listed for '$k' (write $LanesDir\$k.owned first)" }
	$mine = @($pats | ForEach-Object { ConvertTo-GlobRegex $_ })

	# other keys' claims
	$others = @()
	foreach ($f in Get-ChildItem -LiteralPath $LanesDir -Filter '*.owned' -File) {
		$ok = [System.IO.Path]::GetFileNameWithoutExtension($f.Name)
		if (Test-Related $ok $k) { continue }
		$op = @(Read-OwnedFile $f.FullName)
		if ($op.Count -gt 0) { $others += [pscustomobject]@{ key = $ok; rx = @($op | ForEach-Object { ConvertTo-GlobRegex $_ }) } }
	}

	$base = Read-Base $lane
	if ($null -eq $base) { Write-Host "  (lane made before .lane-base existed: no stale-copy protection; 'lane.ps1 new $k -Force' adds it)" }
	$baseChanged = $false
	$laneFiles = @(Get-TreeFiles $lane | Where-Object { (Test-Match $mine $_) -and ($NeverSync -notcontains $_) })
	$copied = 0; $same = 0; $refused = 0; $deleted = 0; $failed = 0; $stale = 0; $conflicts = 0
	foreach ($rel in $laneFiles) {
		$src = Join-Path $lane $rel
		$dst = Join-Path $Main $rel
		$laneHash = $null
		if (Test-SameFile $src $dst) {
			$same++
			if ($null -ne $base) { $laneHash = Get-Md5 $src; if ($base[$rel] -ne $laneHash) { $base[$rel] = $laneHash; $baseChanged = $true } }
			continue
		}
		$mainExists = Test-Path -LiteralPath $dst
		if ($null -ne $base) {
			$laneHash = Get-Md5 $src
			$was = $base[$rel]
			if ($was -and $laneHash -eq $was) {
				# the lane never touched it: main moved on (or deleted it); pushing it would undo someone's work
				$why = if ($mainExists) { 'main has a newer version' } else { 'main removed it' }
				Write-Host "  skip $rel (unchanged in your lane; ${why}: run 'lane.ps1 refresh $k')"
				$stale++; continue
			}
			$mainChanged = $mainExists -and ((-not $was) -or ((Get-Md5 $dst) -ne $was))
			if ($mainChanged -and -not $Force) {
				Write-Host "  CONFLICT $rel (changed in your lane AND in main since your lane last took it: merge main's version into yours, then sync -Force)"
				$conflicts++; continue
			}
		}
		$owner = $null
		foreach ($o in $others) { if (Test-Match $o.rx $rel) { $owner = $o.key; break } }
		if ($owner -and -not $Force) { Write-Host "  REFUSED $rel (claimed by '$owner'; -Force to override)"; $refused++; continue }
		$tag = if ($mainExists) { 'update' } else { 'new' }
		Write-Host "  $tag $rel"
		if (-not $DryRun) {
			if (-not (Copy-One $src $dst)) { $failed++; continue }
			if ($null -ne $base) { $base[$rel] = $laneHash; $baseChanged = $true }
		}
		$copied++
	}
	# the lane's own owned list
	$laneList = Join-Path (Join-Path $lane 'lanes') "$k.owned"
	if (Test-Path -LiteralPath $laneList) {
		$mainList = Join-Path $LanesDir "$k.owned"
		if (-not (Test-SameFile $laneList $mainList)) {
			Write-Host "  owned list lanes/$k.owned (merged)"
			if (-not $DryRun) { ($pats | ForEach-Object { $_ }) | Set-Content -LiteralPath $mainList -Encoding UTF8 }
		}
	}
	if ($Prune) {
		$mainFiles = @(Get-TreeFiles $Main | Where-Object { (Test-Match $mine $_) -and ($NeverSync -notcontains $_) })
		foreach ($rel in $mainFiles) {
			if (Test-Path -LiteralPath (Join-Path $lane $rel)) { continue }
			$owner = $null
			foreach ($o in $others) { if (Test-Match $o.rx $rel) { $owner = $o.key; break } }
			if ($owner -and -not $Force) { Write-Host "  KEEP $rel (claimed by '$owner')"; continue }
			if ($null -ne $base -and -not $Force) {
				# delete only what this lane had and removed, and only if main still holds that same version
				$was = $base[$rel]
				if (-not $was) { Write-Host "  KEEP $rel (added to main after your lane last looked; -Force to delete)"; continue }
				if ((Get-Md5 (Join-Path $Main $rel)) -ne $was) { Write-Host "  KEEP $rel (changed in main since your lane last took it; -Force to delete)"; continue }
			}
			Write-Host "  delete $rel"
			if (-not $DryRun) {
				Remove-Item -LiteralPath (Join-Path $Main $rel) -Force
				if ($null -ne $base -and $base.ContainsKey($rel)) { $base.Remove($rel); $baseChanged = $true }
			}
			$deleted++
		}
	}
	if ($baseChanged -and -not $DryRun) { Write-Base $lane $base }
	$mode = if ($DryRun) { ' (dry run: nothing written)' } else { '' }
	Write-Host "sync '$k': $copied copied, $same identical, $stale skipped (unchanged in lane), $conflicts conflicts, $refused refused, $deleted deleted, $failed failed$mode"
	if ($failed -gt 0) { exit 4 }
	if ($refused -gt 0 -or $conflicts -gt 0) { exit 3 }
}

function Invoke-Refresh([string]$k) {
	Test-Key $k
	$lane = Get-LaneRoot $k
	if (-not (Test-Path -LiteralPath $lane)) { Fail "no lane '$k'" }
	$mine = @(Get-OwnedPatterns $k | ForEach-Object { ConvertTo-GlobRegex $_ })
	$base = Read-Base $lane
	if ($null -eq $base) { $base = @{} }   # older lane: start recording from now on
	$n = 0; $kept = 0; $removed = 0; $ownedNewer = 0
	$mainSet = @{}
	foreach ($rel in Get-TreeFiles $Main) {
		if ($NeverSync -contains $rel) { continue }
		if ($rel.StartsWith('lanes/', 'OrdinalIgnoreCase')) { continue }
		$mainSet[$rel] = $true
		$src = Join-Path $Main $rel
		$dst = Join-Path $lane $rel
		if ($mine.Count -gt 0 -and (Test-Match $mine $rel)) {
			$kept++
			if ((Test-Path -LiteralPath $dst) -and -not (Test-SameFile $src $dst)) {
				if ($base.ContainsKey($rel) -and (Get-Md5 $dst) -eq $base[$rel]) {
					# yours, but untouched in this lane: main's newer version (a verifier's fix) is safe to take
					Write-Host "  pull $rel (owned, unchanged in your lane)"
					if (-not $DryRun) { if (Copy-One $src $dst) { $base[$rel] = Get-Md5 $src } }
					$n++
				}
				else { Write-Host "  owned, changed in your lane and main differs (not pulled; sync reports a CONFLICT): $rel"; $ownedNewer++ }
			}
			continue
		}
		if (Test-SameFile $src $dst) {
			if (-not $base.ContainsKey($rel)) { $base[$rel] = Get-Md5 $src }
			continue
		}
		$note = ''
		if ((Test-Path -LiteralPath $dst) -and $base.ContainsKey($rel) -and (Get-Md5 $dst) -ne $base[$rel]) { $note = ' (overwrites your local edit of a file you do not own)' }
		Write-Host "  pull $rel$note"
		if (-not $DryRun) { if (Copy-One $src $dst) { $base[$rel] = Get-Md5 $src } }
		$n++
	}
	# files main deleted (or renamed) since the lane took them: remove the lane's untouched copies, so a
	# rename in main does not leave two definitions of the same class in the lane
	foreach ($rel in @($base.Keys)) {
		if ($mainSet.ContainsKey($rel) -or ($NeverSync -contains $rel)) { continue }
		$dst = Join-Path $lane $rel
		if (-not (Test-Path -LiteralPath $dst)) { if (-not $DryRun) { $base.Remove($rel) }; continue }
		if ((Get-Md5 $dst) -ne $base[$rel]) { Write-Host "  KEEP $rel (main deleted it but you changed it)"; continue }
		Write-Host "  remove $rel (deleted in main)"
		if (-not $DryRun) { Remove-Item -LiteralPath $dst -Force; $base.Remove($rel) }
		$removed++
	}
	if (-not $DryRun) { Write-Base $lane $base }
	$mode = if ($DryRun) { ' (dry run)' } else { '' }
	Write-Host "refresh '$k': $n files pulled from main, $removed removed (deleted in main), $kept owned files kept ($ownedNewer changed in both)$mode"
}

function Invoke-Status([string]$k) {
	Test-Key $k
	$lane = Get-LaneRoot $k
	if (-not (Test-Path -LiteralPath $lane)) { Fail "no lane '$k'" }
	$pats = @(Get-OwnedPatterns $k)
	Write-Host "lane '$k': $lane"
	Write-Host "owned patterns: $($pats -join ', ')"
	$mine = @($pats | ForEach-Object { ConvertTo-GlobRegex $_ })
	$base = Read-Base $lane
	$all = @(@(Get-TreeFiles $lane) + @(Get-TreeFiles $Main) | Where-Object { (Test-Match $mine $_) -and ($NeverSync -notcontains $_) } | Sort-Object -Unique)
	foreach ($rel in $all) {
		$a = Join-Path $lane $rel; $b = Join-Path $Main $rel
		$ea = Test-Path -LiteralPath $a; $eb = Test-Path -LiteralPath $b
		$st = if ($ea -and -not $eb) { 'lane only' } elseif ($eb -and -not $ea) { 'main only' } elseif (Test-SameFile $a $b) { 'same' } else { 'differs' }
		if ($st -eq 'differs' -and $null -ne $base -and $base[$rel]) {
			$laneSame = (Get-Md5 $a) -eq $base[$rel]; $mainSame = (Get-Md5 $b) -eq $base[$rel]
			$st = if ($laneSame) { 'main newer' } elseif ($mainSame) { 'yours' } else { 'both changed' }
		}
		Write-Host ("  {0,-12} {1}" -f $st, $rel)
	}
}

function Invoke-List {
	if (-not (Test-Path -LiteralPath $LanesDir)) { Write-Host "no lanes"; return }
	foreach ($d in Get-ChildItem -LiteralPath $LanesDir -Directory) {
		$m = Join-Path $d.FullName '.lane'
		$created = ''
		if (Test-Path -LiteralPath $m) { $created = (Get-Content -LiteralPath $m -Raw | ConvertFrom-Json).created }
		$built = Test-Path -LiteralPath (Join-Path $d.FullName 'Binaries/Win64/UnrealEditor-Demiurge.dll')
		$owned = Test-Path -LiteralPath (Join-Path $LanesDir ($d.Name + '.owned'))
		Write-Host ("  {0,-20} created {1}  built={2}  owned-list={3}" -f $d.Name, $created, $built, $owned)
	}
}

function Invoke-Remove([string]$k) {
	Test-Key $k
	$lane = Get-LaneRoot $k
	if (-not (Test-Path -LiteralPath $lane)) { Fail "no lane '$k'" }
	$full = [System.IO.Path]::GetFullPath($lane)
	if (-not $full.StartsWith([System.IO.Path]::GetFullPath($LanesDir) + '\', 'OrdinalIgnoreCase')) { Fail "refusing to remove $full" }
	Remove-Item -LiteralPath $full -Recurse -Force
	Write-Host "lane '$k' removed (its owned list $LanesDir\$k.owned is kept)"
}

switch ($Command.ToLowerInvariant()) {
	'new'     { Invoke-New $Key }
	'sync'    { Invoke-Sync $Key }
	'refresh' { Invoke-Refresh $Key }
	'status'  { Invoke-Status $Key }
	'build'   {
		Test-Key $Key
		$b = Join-Path (Get-LaneRoot $Key) 'build.ps1'
		if (-not (Test-Path -LiteralPath $b)) { Fail "no lane '$Key'" }
		& powershell -NoProfile -ExecutionPolicy Bypass -File $b
		exit $LASTEXITCODE
	}
	'scaffold' {
		Test-Key $Key
		$lane = Get-LaneRoot $Key
		if (-not (Test-Path -LiteralPath $lane)) { Fail "no lane '$Key'" }
		Write-LaneBuild $lane $Key
		Write-Host "lane '$Key': build.ps1 rewritten"
	}
	'list'    { Invoke-List }
	'remove'  { Invoke-Remove $Key }
	default   {
		Get-Content -LiteralPath $PSCommandPath | Select-Object -Skip 1 -First 30 | ForEach-Object { $_ -replace '^# ?', '' }
	}
}
