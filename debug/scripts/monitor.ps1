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

.PARAMETER AppPath
    Optional explicit path to fluidcore_app.exe. Defaults to the build-win binary.
#>
param(
    [double]$IntervalSeconds = 3.0,
    [switch]$Launch,
    [string]$Document = "",
    [string]$AppPath = ""
)

$ErrorActionPreference = "Continue"

# debug/scripts/monitor.ps1 -> debug/scripts -> debug -> repo root
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Split-Path -Parent (Split-Path -Parent $ScriptDir)

# Launch app if requested
if ($Launch) {
    $MsysRoot = "C:\msys64"

    if (-not $AppPath) {
        $AppPath = Join-Path $ProjectRoot "build-win\src\app\fluidcore_app.exe"
    }
    if (-not (Test-Path $AppPath)) {
        $AppPath = Join-Path $ProjectRoot "fluidcore-windows-x64\fluidcore_app.exe"
    }
    if (-not (Test-Path $AppPath)) {
        Write-Error "fluidcore_app.exe not found. Build first: powershell -File ops\scripts\build-win.ps1"
        exit 1
    }
    $AppPath = (Resolve-Path $AppPath).Path

    $AppDir = Split-Path -Parent $AppPath
    # A packaged build carries its runtime DLLs beside the exe, so that directory has to
    # precede the MSYS2 toolchain on PATH. A source build wants the opposite: the MSYS2
    # DLLs must win, or the app silently loads a stale packaged runtime.
    $IsPackaged = (Split-Path -Leaf $AppDir) -eq "fluidcore-windows-x64"
    $PathPrefix = if ($IsPackaged) { "$AppDir;" } else { "" }
    $env:PATH = "$PathPrefix$MsysRoot\ucrt64\bin;$MsysRoot\usr\bin;$env:PATH"
    $env:MSYSTEM = "UCRT64"
    $env:FLUIDCORE_HEARTBEAT = "1"
    $env:FLUIDCORE_LOG_TELEMETRY = "1"
    $env:FLUIDCORE_EPHEMERAL_SEARCH = "1"

    Write-Host "[Monitor] Launching $AppPath..." -ForegroundColor Cyan
    if ($IsPackaged) {
        Write-Host "[Monitor] WARNING: monitoring a packaged build, not your source tree." -ForegroundColor Yellow
        Write-Host "[Monitor]          Changes to src/ will NOT be reflected." -ForegroundColor Yellow
    }

    $AppStdoutLog = Join-Path $ProjectRoot "fluidcore_stdout.log"
    $AppStderrLog = Join-Path $ProjectRoot "fluidcore_stderr.log"

    # Launch detached with redirected stdout/stderr so app logging doesn't overwrite the
    # terminal dashboard.
    #
    # ArgumentList is added only when there is a document: Start-Process rejects an empty
    # or null ArgumentList outright, and passing "" produced a parameter binding error
    # that aborted the launch while leaving the caller stuck on "Connecting".
    $launchArgs = @{
        FilePath               = $AppPath
        RedirectStandardOutput = $AppStdoutLog
        RedirectStandardError  = $AppStderrLog
    }
    if ($Document) {
        $launchArgs.ArgumentList = "`"$Document`""
    }
    Start-Process @launchArgs
    Start-Sleep -Seconds 2
}

Write-Host "Connecting to fluidcore_app.exe..." -ForegroundColor Cyan

$proc = $null
$waitDeadline = [DateTime]::UtcNow.AddSeconds(15)
while (-not $proc) {
    $procs = Get-Process -Name fluidcore_app -ErrorAction SilentlyContinue
    if ($procs) {
        $proc = $procs[0]
        break
    }
    if ([DateTime]::UtcNow -gt $waitDeadline) {
        # Previously this looped forever, so a failed launch looked like a hang.
        Write-Host ""
        Write-Host "[Monitor] No fluidcore_app.exe process appeared within 15s." -ForegroundColor Red
        Write-Host "[Monitor] Check fluidcore_stderr.log next to the repo root for the launch error." -ForegroundColor Red
        exit 1
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

$recentEvents = @()

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
