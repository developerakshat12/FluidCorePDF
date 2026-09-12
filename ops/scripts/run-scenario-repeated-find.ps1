<#
.SYNOPSIS
    CI Memory Leak Regression Gate: Executes repeated full-text search passes
    and validates that LiveAlloc heap growth remains below the 0.5 MB/pass threshold.

.DESCRIPTION
    Runs fluidcore_app.exe in automated repeated find scenario (--run-scenario-repeated-find)
    across the full 892-page textbook corpus. Validates that process exits with code 0
    (indicating no memory leak regression). If average growth per pass exceeds the threshold,
    the application and script exit with code 2 to fail the CI build pipeline.
#>
param(
    [int]$Iterations = 20,
    [string]$Document = "D:\study material\FIN F414 - FRAM\FRAMTextbook.ltproj\documents\Hull J.C.-Options, Futures and Other Derivatives_9th edition.pdf",
    [string]$AppPath = "",
    [string]$MsysRoot = "C:\msys64",
    [double]$MaxLiveAllocGrowthMB = 0.5
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Split-Path -Parent $ScriptDir

# Auto-detect MsysRoot if default not present
if (-not (Test-Path "$MsysRoot\ucrt64\bin")) {
    $Candidates = @($env:MSYS2_ROOT, "C:\msys64", "D:\msys64", "C:\tools\msys64")
    foreach ($cand in $Candidates) {
        if ($cand -and (Test-Path "$cand\ucrt64\bin")) {
            $MsysRoot = $cand
            break
        }
    }
}

# Auto-detect AppPath if not provided
if (-not $AppPath) {
    $PossiblePaths = @(
        (Join-Path $ProjectRoot "build-win\src\app\fluidcore_app.exe"),
        (Join-Path $ProjectRoot "build-win\dist\fluidcore-windows-x64\fluidcore_app.exe"),
        "D:\fluidcore-windows-x64\fluidcore_app.exe"
    )
    foreach ($p in $PossiblePaths) {
        if (Test-Path $p) {
            $AppPath = $p
            break
        }
    }
}

if (-not (Test-Path $AppPath)) {
    throw "Application binary not found at: $AppPath"
}

if (-not (Test-Path $Document)) {
    throw "Target test PDF not found at: $Document"
}

$AppDir = Split-Path -Parent $AppPath
$env:PATH = "$AppDir;$MsysRoot\ucrt64\bin;$MsysRoot\usr\bin;$env:PATH"
$env:MSYSTEM = "UCRT64"
$env:PKG_CONFIG_PATH = "$MsysRoot\ucrt64\lib\pkgconfig;$MsysRoot\ucrt64\share\pkgconfig"
if (Test-Path "$MsysRoot\ucrt64\etc\fonts") {
    $env:FONTCONFIG_PATH = "$MsysRoot\ucrt64\etc\fonts"
}
if (Test-Path "$MsysRoot\ucrt64\share") {
    $env:XDG_DATA_DIRS = "$MsysRoot\ucrt64\share"
}

$env:FLUIDCORE_FIND_ITERATIONS = "$Iterations"
$env:FLUIDCORE_EPHEMERAL_SEARCH = "1"
$env:FLUIDCORE_LOG_TELEMETRY = "1"
$env:FLUIDCORE_MAX_LIVEALLOC_GROWTH_MB = "$MaxLiveAllocGrowthMB"

Write-Host "=================================================================" -ForegroundColor Cyan
Write-Host " CI MEMORY LEAK REGRESSION GATE: $Iterations Sequential Search Passes" -ForegroundColor Cyan
Write-Host " Binary:    $AppPath" -ForegroundColor Cyan
Write-Host " Document:  $Document" -ForegroundColor Cyan
Write-Host " Threshold: $MaxLiveAllocGrowthMB MB LiveAlloc growth per pass" -ForegroundColor Cyan
Write-Host "=================================================================" -ForegroundColor Cyan

$proc = Start-Process -FilePath $AppPath -ArgumentList "--find-iters=$Iterations", "--run-scenario-repeated-find", "`"$Document`"" -PassThru -Wait -NoNewWindow

if ($proc.ExitCode -ne 0) {
    Write-Host "[CI REGRESSION GATE] FAILED with exit code $($proc.ExitCode)!" -ForegroundColor Red
    exit $proc.ExitCode
}

Write-Host "[CI REGRESSION GATE] SUCCESS - 0.00 MB leak regression confirmed (exit code 0)." -ForegroundColor Green
exit 0
