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
#   refresh <key> [-DryRun]  pull main's current files into the lane (everything except the lane's owned
#                            files and the excluded folders), so a lane picks up work others synced.
#   status <key>             owned files and how they differ between the lane and main
#   build <key>              build the lane (same as Unreal/Lanes/<key>/build.ps1)
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
$NeverSync = @('build.ps1', '.lane', 'Demiurge.uproject')   # lane scaffolding (build.ps1 is generated per lane)

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
	return ((Get-FileHash -LiteralPath $a -Algorithm MD5).Hash -eq (Get-FileHash -LiteralPath $b -Algorithm MD5).Hash)
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
# Builds this lane's DemiurgeEditor (Win64 Development) with Build.bat -NoMutex: UBT's mutex is global
# (keyed by the engine's UnrealBuildTool.dll path), so without -NoMutex every lane would wait for every
# other lane. Each lane is its own project (own Intermediate / Binaries), so concurrent builds are safe.
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
$buildArgs = @('DemiurgeEditor', 'Win64', 'Development', "-Project=$uproject", '-NoMutex', '-NoHotReload', "-Log=$ubtLog")
if ($Clean) { $buildArgs += '-Clean' }
Write-Host "Building lane __KEY__ (DemiurgeEditor Win64 Development, -NoMutex)"
$watch = [System.Diagnostics.Stopwatch]::StartNew()
$lines = @(& $buildBat @buildArgs 2>&1 | ForEach-Object { "$_" })
$exitCode = $LASTEXITCODE
$watch.Stop()
$lines | Set-Content -LiteralPath $outLog -Encoding UTF8
$result = if ($exitCode -eq 0) { 'Succeeded' } else { 'Failed' }
$hits = @($lines | Where-Object { $_ -match 'error|warning C' })
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
	$rc = @($Main, $lane, '/E', '/XD') + $xd + @('/XF', '.lane', '/MT:16', '/R:1', '/W:1', '/NFL', '/NDL', '/NJH', '/NJS', '/NP')
	& robocopy.exe @rc | Out-Null
	$code = $LASTEXITCODE
	if ($code -ge 8) { Fail "robocopy failed with exit code $code" }
	$info = [ordered]@{ key = $k; main = $Main; created = (Get-Date).ToString('s'); by = 'lane.ps1' }
	($info | ConvertTo-Json) | Set-Content -LiteralPath (Join-Path $lane '.lane') -Encoding UTF8
	Write-LaneBuild $lane $k
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

	$laneFiles = @(Get-TreeFiles $lane | Where-Object { (Test-Match $mine $_) -and ($NeverSync -notcontains $_) })
	$copied = 0; $same = 0; $refused = 0; $deleted = 0; $failed = 0
	foreach ($rel in $laneFiles) {
		$owner = $null
		foreach ($o in $others) { if (Test-Match $o.rx $rel) { $owner = $o.key; break } }
		if ($owner -and -not $Force) { Write-Host "  REFUSED $rel (claimed by '$owner'; -Force to override)"; $refused++; continue }
		$src = Join-Path $lane $rel
		$dst = Join-Path $Main $rel
		if (Test-SameFile $src $dst) { $same++; continue }
		$tag = if (Test-Path -LiteralPath $dst) { 'update' } else { 'new' }
		Write-Host "  $tag $rel"
		if (-not $DryRun) { if (-not (Copy-One $src $dst)) { $failed++; continue } }
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
			Write-Host "  delete $rel"
			if (-not $DryRun) { Remove-Item -LiteralPath (Join-Path $Main $rel) -Force }
			$deleted++
		}
	}
	$mode = if ($DryRun) { ' (dry run: nothing written)' } else { '' }
	Write-Host "sync '$k': $copied copied, $same identical, $refused refused, $deleted deleted, $failed failed$mode"
	if ($failed -gt 0) { exit 4 }
	if ($refused -gt 0) { exit 3 }
}

function Invoke-Refresh([string]$k) {
	Test-Key $k
	$lane = Get-LaneRoot $k
	if (-not (Test-Path -LiteralPath $lane)) { Fail "no lane '$k'" }
	$mine = @(Get-OwnedPatterns $k | ForEach-Object { ConvertTo-GlobRegex $_ })
	$n = 0; $kept = 0
	foreach ($rel in Get-TreeFiles $Main) {
		if ($NeverSync -contains $rel) { continue }
		if ($rel.StartsWith('lanes/', 'OrdinalIgnoreCase')) { continue }
		if ($mine.Count -gt 0 -and (Test-Match $mine $rel)) { $kept++; continue }
		$src = Join-Path $Main $rel
		$dst = Join-Path $lane $rel
		if (Test-SameFile $src $dst) { continue }
		Write-Host "  pull $rel"
		if (-not $DryRun) { [void](Copy-One $src $dst) }
		$n++
	}
	$mode = if ($DryRun) { ' (dry run)' } else { '' }
	Write-Host "refresh '$k': $n files pulled from main, $kept owned files left alone$mode"
}

function Invoke-Status([string]$k) {
	Test-Key $k
	$lane = Get-LaneRoot $k
	if (-not (Test-Path -LiteralPath $lane)) { Fail "no lane '$k'" }
	$pats = @(Get-OwnedPatterns $k)
	Write-Host "lane '$k': $lane"
	Write-Host "owned patterns: $($pats -join ', ')"
	$mine = @($pats | ForEach-Object { ConvertTo-GlobRegex $_ })
	$all = @(@(Get-TreeFiles $lane) + @(Get-TreeFiles $Main) | Where-Object { (Test-Match $mine $_) -and ($NeverSync -notcontains $_) } | Sort-Object -Unique)
	foreach ($rel in $all) {
		$a = Join-Path $lane $rel; $b = Join-Path $Main $rel
		$ea = Test-Path -LiteralPath $a; $eb = Test-Path -LiteralPath $b
		$st = if ($ea -and -not $eb) { 'lane only' } elseif ($eb -and -not $ea) { 'main only' } elseif (Test-SameFile $a $b) { 'same' } else { 'differs' }
		Write-Host ("  {0,-10} {1}" -f $st, $rel)
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
	'list'    { Invoke-List }
	'remove'  { Invoke-Remove $Key }
	default   {
		Get-Content -LiteralPath $PSCommandPath | Select-Object -Skip 1 -First 28 | ForEach-Object { $_ -replace '^# ?', '' }
	}
}
