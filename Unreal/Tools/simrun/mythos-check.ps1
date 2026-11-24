# DEMIURGE: the integration check of the v4 mythos tools (SPEC-MYTHOS 9, track M9 "mythos-tools"; 5.1 rule 6).
#
#   1. 8 seeds x every v1 mythos policy (simrun --policy myth) x 4,500 years, three times with --hash: no run may
#      report a non-finite value, and every (seed, policy) must give the same hash all three times.
#   2. --saveload card (save / load right after the first budgeted card opens) on 8 seeds x responder-myth, lawgiver,
#      sign-only, twice: both runs of each (seed, policy) must end with the same hash, and every re-save of a loaded
#      world must be byte-identical.
#
# Usage (the project path contains a space, so quote it):
#   powershell -NoProfile -ExecutionPolicy Bypass -File "C:\Users\Relly\Documents\Fable5.2 Test\demiurge\Unreal\Tools\simrun\mythos-check.ps1" [-Jobs 8] [-BuildTag m9] [-Years 4500] [-Repeats 3]
# The last line is "mythos-check PASS" or "mythos-check FAIL (...)"; exit code 0 only on PASS.

param([int]$Jobs = 8, [string]$BuildTag = '', [int]$Years = 4500, [int]$Repeats = 3, [string]$Seeds = '1-8')

$ErrorActionPreference = 'Continue'
$here = $PSScriptRoot
$simrun = Join-Path $here 'simrun.ps1'
$common = @('--seeds', $Seeds, '--years', "$Years", '--hash', '--quiet', '--jobs', "$Jobs")
# build once, then run that one binary for every repeat: other workflows may edit Sim/ while this runs, and a rebuild
# between two repeats would compare two different games
$buildArgs = @('--build-only')
if ($BuildTag -ne '') { $buildArgs += @('--build-tag', $BuildTag) }
$built = @(& powershell -NoProfile -ExecutionPolicy Bypass -File $simrun @buildArgs 2>&1 | ForEach-Object { "$_" })
$exeLine = @($built | Where-Object { $_ -match '^simrun built: (.+)$' })
if ($exeLine.Count -eq 0) { $built | ForEach-Object { Write-Host $_ }; Write-Host 'mythos-check FAIL (build)'; exit 1 }
$exe = ($exeLine[0] -replace '^simrun built: ', '').Trim()
$snap = Join-Path ([System.IO.Path]::GetTempPath()) ('mythos-check-' + [System.Guid]::NewGuid().ToString('N') + '.exe')
Copy-Item -LiteralPath $exe -Destination $snap
$env:DM_ROOT = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $here))
Write-Host "mythos-check: one binary for every run ($exe)"

function Get-Hashes([string[]]$extra) {
    $out = @(& $snap @common @extra 2>&1 | ForEach-Object { "$_" })
    $h = @{}
    $nan = 0
    $resaveBad = 0
    $cur = $null
    foreach ($l in $out) {
        if ($l -match '^== seed (\d+)\s+policy (\S+)') { $cur = "$($Matches[1]) $($Matches[2])" }
        elseif ($l -match '^\s+hash ([0-9a-f]{8})' -and $cur) { $h[$cur] = $Matches[1]; $cur = $null }
        elseif ($l -cmatch 'NON-FINITE') { $nan++; Write-Host $l }
        elseif ($l -match 're-save of the loaded world DIFFERENT') { $resaveBad++; Write-Host $l }
        elseif ($l -match 'simrun build failed|LOAD FAILED|SAVE FAILED') { Write-Host $l }
    }
    return @{ hashes = $h; nan = $nan; resaveBad = $resaveBad; lines = $out }
}

$fail = @()
# ---- 1. determinism and NaN, every v1 policy
$runs = @()
for ($r = 1; $r -le $Repeats; $r++) {
    Write-Host "mythos-check: --policy myth, run $r of $Repeats"
    $runs += , (Get-Hashes @('--policy', 'myth'))
}
$keys = @($runs[0].hashes.Keys | Sort-Object)
Write-Host "mythos-check: $($keys.Count) runs per repeat"
if ($keys.Count -eq 0) { $fail += 'no run finished' }
$unstable = 0
foreach ($k in $keys) {
    $vals = @($runs | ForEach-Object { $_.hashes[$k] })
    if (@($vals | Sort-Object -Unique).Count -ne 1) { $unstable++; Write-Host ("UNSTABLE {0}: {1}" -f $k, ($vals -join ' ')) }
}
$nanAll = ($runs | ForEach-Object { $_.nan } | Measure-Object -Sum).Sum
Write-Host "mythos-check: hashes stable $Repeats x for $($keys.Count - $unstable) of $($keys.Count); non-finite reports $nanAll"
if ($unstable -gt 0) { $fail += "$unstable unstable hashes" }
if ($nanAll -gt 0) { $fail += "$nanAll non-finite" }

# ---- 2. save / load at a card opening replays identically
$a = Get-Hashes @('--policy', 'responder-myth,lawgiver,sign-only', '--saveload', 'card')
$b = Get-Hashes @('--policy', 'responder-myth,lawgiver,sign-only', '--saveload', 'card')
$saves = @($a.lines | Where-Object { $_ -match 'save/load at year .* \(a card opened\)' }).Count
$diff = 0
foreach ($k in ($a.hashes.Keys | Sort-Object)) { if ($a.hashes[$k] -ne $b.hashes[$k]) { $diff++; Write-Host ("SAVELOAD DIFFERS {0}: {1} {2}" -f $k, $a.hashes[$k], $b.hashes[$k]) } }
Write-Host "mythos-check: --saveload card: $saves saves at a card opening; replays identical for $($a.hashes.Count - $diff) of $($a.hashes.Count); bad re-saves $($a.resaveBad + $b.resaveBad)"
if ($a.hashes.Count -eq 0) { $fail += 'no saveload run finished' }
if ($diff -gt 0) { $fail += "$diff saveload replays differ" }
if ($a.resaveBad + $b.resaveBad -gt 0) { $fail += 're-save differs' }
if ($a.nan + $b.nan -gt 0) { $fail += 'non-finite after a load' }

Remove-Item -LiteralPath $snap -ErrorAction SilentlyContinue
if ($fail.Count -eq 0) { Write-Host 'mythos-check PASS'; exit 0 }
Write-Host ("mythos-check FAIL ({0})" -f ($fail -join '; '))
exit 1
