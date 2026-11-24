# DEMIURGE: the phase-1 guardrail of the v4 mythos build (SPEC-MYTHOS 5.1 rule 2; track M9 "mythos-tools").
#
# Runs simrun (building it first) on 8 seeds x passive, kind, cruel, chaotic twice with --hash: at --mythos-phase 0 and
# at --mythos-phase 1 --emergent off, and compares the "core" hashes of each (seed, policy) pair. The core hash is the
# v3.1 view of the world (year, tick, rng state, towns, people, sea, co2, cultures, units, relations: the version-1
# state without the mythos extensions and without the narration; simrun's coreHash). These gods ignore every card and
# never use a Word power, so with emergent off each pair must match bit for bit while the mythos runs beside them
# (faith records, cards opening and lapsing, rulers, the chronicle telling it). The phase-0 run is the pre-mythos game
# itself (phase0-guardrail.ps1 checks its full hashes against phase0-hashes.txt). Once the balance owner turns
# emergent families on, this guardrail gives way to before / after tables (SPEC-MYTHOS 5.1 rule 3).
#
# Usage (the project path contains a space, so quote it):
#   powershell -NoProfile -ExecutionPolicy Bypass -File "C:\Users\Relly\Documents\Fable5.2 Test\demiurge\Unreal\Tools\simrun\phase1-guardrail.ps1" [-Jobs 8] [-BuildTag m9] [-Years 4000]
# The last line is "phase1-guardrail N of 32 match"; exit code 0 only when all 32 match.

param([int]$Jobs = 8, [string]$BuildTag = '', [int]$Years = 4000, [string]$Stub = '')

$ErrorActionPreference = 'Continue'
$here = $PSScriptRoot

# build once and run that one binary for both phases (another workflow may edit Sim/ in between)
$buildArgs = @('--build-only')
if ($BuildTag -ne '') { $buildArgs += @('--build-tag', $BuildTag) }
if ($Stub -ne '') { $buildArgs += @('--stub', $Stub) }
$built = @(& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'simrun.ps1') @buildArgs 2>&1 | ForEach-Object { "$_" })
$exeLine = @($built | Where-Object { $_ -match '^simrun built: (.+)$' })
if ($exeLine.Count -eq 0) { $built | ForEach-Object { Write-Host $_ }; Write-Host 'phase1-guardrail 0 of 32 match'; exit 1 }
$snap = Join-Path ([System.IO.Path]::GetTempPath()) ('phase1-guardrail-' + [System.Guid]::NewGuid().ToString('N') + '.exe')
Copy-Item -LiteralPath (($exeLine[0] -replace '^simrun built: ', '').Trim()) -Destination $snap
$env:DM_ROOT = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $here))

function Get-Cores([string[]]$phaseArgs) {
    $simArgs = @('--seeds', '1-8', '--policy', 'passive,kind,cruel,chaotic', '--years', "$Years", '--hash', '--quiet', '--jobs', "$Jobs") + $phaseArgs
    $out = @(& $snap @simArgs 2>&1 | ForEach-Object { "$_" })
    $got = @{}
    $cur = $null
    foreach ($l in $out) {
        if ($l -match '^== seed (\d+)\s+policy (\S+)') { $cur = "$($Matches[1]) $($Matches[2])" }
        elseif ($l -match '^\s+hash [0-9a-f]{8} core ([0-9a-f]{8})' -and $cur) { $got[$cur] = $Matches[1]; $cur = $null }
        elseif ($l -cmatch 'simrun build failed|error|NON-FINITE') { Write-Host $l }
    }
    return $got
}

$p0 = Get-Cores @('--mythos-phase', '0')
$p1 = Get-Cores @('--mythos-phase', '1', '--emergent', 'off')
Remove-Item -LiteralPath $snap -ErrorAction SilentlyContinue
$ok = 0
foreach ($s in 1..8) {
    foreach ($pol in @('passive', 'kind', 'cruel', 'chaotic')) {
        $k = "$s $pol"
        if ($p0[$k] -and $p0[$k] -eq $p1[$k]) { $ok++ }
        else { Write-Host ("MISMATCH seed/policy {0}: phase 0 core {1}, phase 1 core {2}" -f $k, $(if ($p0[$k]) { $p0[$k] } else { 'nothing' }), $(if ($p1[$k]) { $p1[$k] } else { 'nothing' })) }
    }
}
Write-Host "phase1-guardrail $ok of 32 match"
if ($ok -eq 32) { exit 0 } else { exit 1 }
