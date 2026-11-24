# DEMIURGE night-time CPU governor (owner, 2026-11-19: "cap it down to 50-65% thru the night").
# Every 20 s: heavy build and render processes are pinned to the first $Cores logical CPUs and lowered
# to BelowNormal priority, so together they can use at most $Cores / total of the CPU.
# Run:  powershell -NoProfile -ExecutionPolicy Bypass -File tools\cpu_governor.ps1 [-Cores 14] [-Log <file>]
param([int]$Cores = 14, [string]$Log = "$env:TEMP\demiurge-cpu-governor.log")
# Wildcards on purpose: agents build renamed copies of tools (simrun-fam.exe, simrun-knob.exe, test_*.exe).
$names = @('UnrealEditor*', 'ShaderCompileWorker', 'UnrealBuildTool', 'dotnet', 'cl', 'link',
           'blender*', 'Demiurge*', 'simrun*', 'test_*', 'python*', 'clang*', 'lld-link', 'UbaHost', 'ispc')
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$total = [Environment]::ProcessorCount
$mask = [IntPtr]([int64]([math]::Pow(2, [math]::Min($Cores, $total)) - 1))
"$(Get-Date -Format s) governor start: $Cores of $total logical CPUs (max ~$([int](100 * $Cores / $total)) %)" | Out-File -Append $Log
# Short-lived compilers (cl, link) start and finish between passes, so the shells that launch them (test
# harness, build, lane, package and shot scripts) are pinned too: Windows children inherit affinity at creation.
$launchers = 'SimTests|build\.ps1|lane\.ps1|package\.ps1|shot\.ps1|simrun|render_slot|UnrealBuildTool|RunUAT'
while ($true) {
    $procs = @(Get-Process -Name $names -ErrorAction SilentlyContinue)
    # Anything compiled and run from inside the project or the temp folder (test harnesses, simrun variants).
    $procs += Get-Process -ErrorAction SilentlyContinue | Where-Object {
        $_.Path -and ($_.Path.StartsWith($projectRoot, 'OrdinalIgnoreCase') -or $_.Path.StartsWith($env:TEMP, 'OrdinalIgnoreCase')) -and
        $_.Path -notmatch '\\claude\\|powershell|bash' }
    foreach ($w in Get-CimInstance Win32_Process -Filter "Name='powershell.exe' OR Name='pwsh.exe' OR Name='bash.exe' OR Name='cmd.exe'" -ErrorAction SilentlyContinue) {
        if ($w.CommandLine -and $w.CommandLine -match $launchers -and $w.CommandLine -notmatch 'cpu_governor') {
            $procs += Get-Process -Id $w.ProcessId -ErrorAction SilentlyContinue
        }
    }
    foreach ($p in $procs) {
        try {
            if ($p.ProcessorAffinity -ne $mask) { $p.ProcessorAffinity = $mask }
            if ($p.PriorityClass -ne 'BelowNormal' -and $p.PriorityClass -ne 'Idle') { $p.PriorityClass = 'BelowNormal' }
        } catch { }
    }
    # % Processor Time (busy share of time). Win32_Processor.LoadPercentage tracks % Processor Utility, which
    # scales with turbo frequency and reads 100 % while real load is about 65 %.
    $load = try { [int](Get-Counter '\Processor(_Total)\% Processor Time' -SampleInterval 2 -MaxSamples 1).CounterSamples[0].CookedValue } catch { -1 }
    "$(Get-Date -Format s) cpu $load" | Out-File -Append $Log
    Start-Sleep -Seconds 20
}
