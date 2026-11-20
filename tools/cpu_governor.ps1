# DEMIURGE night-time CPU governor (owner, 2026-11-19: "cap it down to 50-65% thru the night").
# Every 20 s: heavy build and render processes are pinned to the first $Cores logical CPUs and lowered
# to BelowNormal priority, so together they can use at most $Cores / total of the CPU.
# Run:  powershell -NoProfile -ExecutionPolicy Bypass -File tools\cpu_governor.ps1 [-Cores 14] [-Log <file>]
param([int]$Cores = 14, [string]$Log = "$env:TEMP\demiurge-cpu-governor.log")
$names = @('UnrealEditor', 'UnrealEditor-Cmd', 'ShaderCompileWorker', 'UnrealBuildTool', 'dotnet', 'cl', 'link',
           'blender', 'Demiurge', 'simrun', 'python', 'clang', 'lld-link', 'UbaHost', 'ispc')
$total = [Environment]::ProcessorCount
$mask = [IntPtr]([int64]([math]::Pow(2, [math]::Min($Cores, $total)) - 1))
"$(Get-Date -Format s) governor start: $Cores of $total logical CPUs (max ~$([int](100 * $Cores / $total)) %)" | Out-File -Append $Log
while ($true) {
    foreach ($p in Get-Process -Name $names -ErrorAction SilentlyContinue) {
        try {
            if ($p.ProcessorAffinity -ne $mask) { $p.ProcessorAffinity = $mask }
            if ($p.PriorityClass -ne 'BelowNormal' -and $p.PriorityClass -ne 'Idle') { $p.PriorityClass = 'BelowNormal' }
        } catch { }
    }
    $load = (Get-CimInstance Win32_Processor | Measure-Object -Property LoadPercentage -Average).Average
    "$(Get-Date -Format s) cpu $load" | Out-File -Append $Log
    Start-Sleep -Seconds 20
}
