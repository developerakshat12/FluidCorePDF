<#
.SYNOPSIS
    Real-time Terminal System & Memory Monitor for FluidCore.

.DESCRIPTION
    Monitors running fluidcore_app.exe processes and displays live CPU, Private Bytes (RAM),
    Working Set (Physical RAM), Peak RAM, Thread count, Handle count, and recent
    lifecycle telemetry events directly in the terminal dashboard.

.PARAMETER IntervalSeconds
    Refresh interval in seconds (default: 1.0).

.PARAMETER Launch
    If set, launches fluidcore_app.exe before monitoring.

.PARAMETER Document
    Optional PDF or project path to pass if launching.
#>
param(
    [double]$IntervalSeconds = 3.0,
    [switch]$Launch,
    [string]$Document = ""
)

$ErrorActionPreference = "Continue"
    
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Split-Path -Parent $ScriptDir

# Launch app if requested
if ($Launch) {
    $MsysRoot = "C:\msys64"
    $env:PATH = "D:\fluidcore-windows-x64;$MsysRoot\ucrt64\bin;$MsysRoot\usr\bin;$env:PATH"
    $env:MSYSTEM = "UCRT64"
    $env:FLUIDCORE_HEARTBEAT = "1"
    $env:FLUIDCORE_LOG_TELEMETRY = "1"
    $env:FLUIDCORE_EPHEMERAL_SEARCH = "1"

    $AppPath = "D:\fluidcore-windows-x64\fluidcore_app.exe"
    if (-not (Test-Path $AppPath)) {
        $AppPath = Join-Path $ProjectRoot "build-win\src\app\fluidcore_app.exe"
    }

    Write-Host "[Monitor] Launching $AppPath..." -ForegroundColor Cyan
    $DocArg = if ($Document) { "`"$Document`"" } else { "" }
    $AppStdoutLog = "D:\FluidCorePDF\fluidcore_stdout.log"
    $AppStderrLog = "D:\FluidCorePDF\fluidcore_stderr.log"

    # Launch detached with redirected stdout/stderr so app logging doesn't overwrite the terminal dashboard
    Start-Process -FilePath $AppPath -ArgumentList $DocArg -RedirectStandardOutput $AppStdoutLog -RedirectStandardError $AppStderrLog
    Start-Sleep -Seconds 2
}

Write-Host "Connecting to fluidcore_app.exe..." -ForegroundColor Cyan

$proc = $null
while (-not $proc) {
    $procs = Get-Process -Name fluidcore_app -ErrorAction SilentlyContinue
    if ($procs) {
        $proc = $procs[0]
        break
    }
    Start-Sleep -Milliseconds 500
}

$numCores = [Environment]::ProcessorCount
$prevTime = [DateTime]::UtcNow
$prevCpu = $proc.TotalProcessorTime.TotalSeconds
$startPriv = $proc.PrivateMemorySize64 / 1MB
$startWS = $proc.WorkingSet64 / 1MB
$peakPriv = $startPriv
$peakWS = $startWS

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Split-Path -Parent (Split-Path -Parent $ScriptDir)

$TelemetryPath = if (Test-Path "$ProjectRoot\debug\logs\fluidcore_telemetry.log") {
    "$ProjectRoot\debug\logs\fluidcore_telemetry.log"
} elseif (Test-Path "$ProjectRoot\debug\fluidcore_telemetry.log") {
    "$ProjectRoot\debug\fluidcore_telemetry.log"
} elseif (Test-Path "D:\FluidCorePDF\fluidcore_telemetry.log") {
    "D:\FluidCorePDF\fluidcore_telemetry.log"
} else {
    "$ProjectRoot\debug\logs\fluidcore_telemetry.log"
}
$recentEvents = @()

function Format-Size([double]$mb) {
    if ($mb -ge 1024) {
        return ("{0,7:F2} GB" -f ($mb / 1024))
    }
    return ("{0,7:F2} MB" -f $mb)
}

function Format-Delta([double]$delta) {
    $sign = if ($delta -ge 0) { "+" } else { "" }
    return ("{0}{1:F2} MB" -f $sign, $delta)
}

try {
    while (-not $proc.HasExited) {
        $proc.Refresh()
        $now = [DateTime]::UtcNow
        $elapsedSec = ($now - $prevTime).TotalSeconds
        if ($elapsedSec -lt 0.001) { $elapsedSec = 0.001 }

        $curCpu = $proc.TotalProcessorTime.TotalSeconds
        $cpuPercent = (($curCpu - $prevCpu) / ($elapsedSec * $numCores)) * 100
        if ($cpuPercent -lt 0) { $cpuPercent = 0 }

        $prevTime = $now
        $prevCpu = $curCpu

        $privMB = $proc.PrivateMemorySize64 / 1MB
        $wsMB = $proc.WorkingSet64 / 1MB
        $vmMB = $proc.VirtualMemorySize64 / 1MB

        if ($privMB -gt $peakPriv) { $peakPriv = $privMB }
        if ($wsMB -gt $peakWS) { $peakWS = $wsMB }

        $deltaPriv = $privMB - $startPriv
        $deltaWS = $wsMB - $startWS
        $threads = $proc.Threads.Count
        $handles = $proc.HandleCount

        # Check telemetry for recent search / memory events
        if (Test-Path $TelemetryPath) {
            $latest = Get-Content $TelemetryPath -Tail 20 -ErrorAction SilentlyContinue
            if ($latest) {
                $filtered = $latest | Where-Object { $_ -match "\[Search|\[Repeated|\[Heartbeat|Destroyed throwaway|COMPLETED SEARCH" }
                if ($filtered) {
                    $recentEvents = $filtered | Select-Object -Last 5
                }
            }
        }

        Clear-Host

        Write-Host "================================================================================" -ForegroundColor DarkCyan
        Write-Host "           FLUIDCORE REAL-TIME RESOURCE MONITOR (TERMINAL DASHBOARD)           " -ForegroundColor Cyan
        Write-Host "================================================================================" -ForegroundColor DarkCyan
        Write-Host (" PID: {0,-8} | Process: {1,-16} | Cores: {2} | Status: RUNNING" -f $proc.Id, $proc.ProcessName, $numCores) -ForegroundColor Green
        Write-Host "--------------------------------------------------------------------------------" -ForegroundColor DarkGray

        # CPU Progress Bar
        $cpuBarLen = [Math]::Min(30, [int]($cpuPercent / 3.33))
        $cpuBar = "[" + ("=" * $cpuBarLen) + (" " * (30 - $cpuBarLen)) + "]"
        $cpuColor = if ($cpuPercent -gt 50) { "Yellow" } else { "White" }
        Write-Host (" CPU Utilization:   {0,6:F1} %   {1}" -f $cpuPercent, $cpuBar) -ForegroundColor $cpuColor

        Write-Host ""
        Write-Host " [MEMORY & HEAP ALLOCATION]" -ForegroundColor Yellow
        Write-Host ("   Private Bytes (Committed RAM):  {0}   (Delta: {1,-9} | Peak: {2})" -f (Format-Size $privMB), (Format-Delta $deltaPriv), (Format-Size $peakPriv)) -ForegroundColor Cyan
        Write-Host ("   Working Set (Physical RAM):    {0}   (Delta: {1,-9} | Peak: {2})" -f (Format-Size $wsMB), (Format-Delta $deltaWS), (Format-Size $peakWS)) -ForegroundColor White
        Write-Host ("   Virtual Memory Address Space:  {0}" -f (Format-Size $vmMB)) -ForegroundColor DarkGray

        Write-Host ""
        Write-Host " [SUBSYSTEM CONCURRENCY & HANDLES]" -ForegroundColor Yellow
        Write-Host ("   Active Thread Count:            {0,-6} (UI + TileCache + Search Worker)" -f $threads) -ForegroundColor White
        Write-Host ("   OS Kernel / GDI Handle Count:   {0,-6} (Win32 Handles)" -f $handles) -ForegroundColor White

        Write-Host ""
        Write-Host " [LIVE SEARCH & LIFECYCLE STREAM]" -ForegroundColor Yellow
        if ($recentEvents.Count -gt 0) {
            foreach ($ev in $recentEvents) {
                $line = if ($ev.Length -gt 76) { $ev.Substring(0, 73) + "..." } else { $ev }
                $color = if ($line -match "Destroyed throwaway|COMPLETED|PASS") { "Green" } elseif ($line -match "START SEARCH") { "Cyan" } else { "DarkGray" }
                Write-Host ("   " + $line) -ForegroundColor $color
            }
        } else {
            Write-Host "   Waiting for search or navigation in GUI..." -ForegroundColor DarkGray
        }

        Write-Host "--------------------------------------------------------------------------------" -ForegroundColor DarkGray
        Write-Host " Press [Ctrl+C] to exit monitor. GUI app will continue running." -ForegroundColor DarkGray
        Write-Host "================================================================================" -ForegroundColor DarkCyan

        Start-Sleep -Seconds $IntervalSeconds
    }
    Write-Host "`n[Monitor] fluidcore_app.exe (PID $($proc.Id)) has terminated." -ForegroundColor Yellow
}
catch {
    Write-Host ""
}
