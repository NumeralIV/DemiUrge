# DEMIURGE: the phase-0 guardrail of the v4 mythos build (SPEC-MYTHOS 5.1 rule 1; contracts track M0).
#
# Runs simrun (building it first) on 8 seeds x passive, kind, cruel, chaotic with --hash and compares every
# fingerprint with phase0-hashes.txt, the hashes of the pre-mythos build. With MythosTune::phase 0 (the default)
# every one must match bit for bit. Extra simrun arguments are passed through (e.g. --stub Chronicle).
#
# Usage (the project path contains a space, so quote it):
#   powershell -NoProfile -ExecutionPolicy Bypass -File "C:\Users\Relly\Documents\Fable5.2 Test\demiurge\Unreal\Tools\simrun\phase0-guardrail.ps1" [-Jobs 8] [-BuildTag m9]
# The last line is "phase0-guardrail N of 32 match" ; exit code 0 only when all 32 match.

param([int]$Jobs = 8, [string]$BuildTag = '')

$ErrorActionPreference = 'Continue'
$here = $PSScriptRoot
$want = @{}
foreach ($line in [System.IO.File]::ReadAllLines((Join-Path $here 'phase0-hashes.txt'))) {
    if ($line -match '^\s*#' -or $line.Trim() -eq '') { continue }
    $p = $line.Trim() -split '\s+'
    if ($p.Count -eq 3) { $want["$($p[0]) $($p[1])"] = $p[2] }
}
if ($want.Count -ne 32) { Write-Host "phase0-hashes.txt holds $($want.Count) rows, expected 32"; Write-Host "phase0-guardrail 0 of 32 match"; exit 1 }

# (B1) phase 0 explicitly: Balance.cpp now turns the mythos on (phase 1), so the build's default is no longer phase 0
$simArgs = @('--seeds', '1-8', '--policy', 'passive,kind,cruel,chaotic', '--hash', '--quiet', '--jobs', "$Jobs", '--mythos-phase', '0')
if ($BuildTag -ne '') { $simArgs += @('--build-tag', $BuildTag) }   # (M9) a private build directory (simrun.ps1 --build-tag)
$out = @(& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'simrun.ps1') @simArgs 2>&1 | ForEach-Object { "$_" })
$got = @{}
$cur = $null
foreach ($l in $out) {
    if ($l -match '^== seed (\d+)\s+policy (\S+)') { $cur = "$($Matches[1]) $($Matches[2])" }
    elseif ($l -match '^\s+hash ([0-9a-f]{8})' -and $cur) { $got[$cur] = $Matches[1]; $cur = $null }
    elseif ($l -match 'simrun build failed|error') { Write-Host $l }
}
$ok = 0
foreach ($k in ($want.Keys | Sort-Object)) {
    $g = $got[$k]
    if ($g -eq $want[$k]) { $ok++ }
    else { Write-Host ("MISMATCH seed/policy {0}: want {1}, got {2}" -f $k, $want[$k], $(if ($g) { $g } else { 'nothing' })) }
}
Write-Host "phase0-guardrail $ok of 32 match"
if ($ok -eq 32) { exit 0 } else { exit 1 }
