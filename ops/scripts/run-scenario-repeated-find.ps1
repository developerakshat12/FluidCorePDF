param(
    [int]$Iterations = 20,
    [string]$Document = "D:\study material\FIN F414 - FRAM\FRAMTextbook.ltproj\documents\Hull J.C.-Options, Futures and Other Derivatives_9th edition.pdf"
)

$ErrorActionPreference = "Stop"

$MsysRoot = "C:\msys64"
$env:PATH = "D:\FluidCorePDF\fluidcore-platform\build-win\src\app;D:\fluidcore-windows-x64;$MsysRoot\ucrt64\bin;$MsysRoot\usr\bin;$env:PATH"
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

$AppPath = "D:\fluidcore-windows-x64\fluidcore_app.exe"

Write-Host "=================================================================" -ForegroundColor Cyan
Write-Host " RUNNING PRODUCTION APP REPEATED FIND BENCHMARK: $Iterations Passes" -ForegroundColor Cyan
Write-Host " Binary:   $AppPath" -ForegroundColor Cyan
Write-Host " Document: $Document" -ForegroundColor Cyan
Write-Host "=================================================================" -ForegroundColor Cyan

cmd.exe /c "$AppPath --find-iters=$Iterations --run-scenario-repeated-find ""$Document"""
Write-Host "Process exited with code $LASTEXITCODE" -ForegroundColor Green
