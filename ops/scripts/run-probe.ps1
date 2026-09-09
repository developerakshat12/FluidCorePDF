param(
    [string]$Mode = "--persistent"
)

$ErrorActionPreference = "Stop"

$MsysRoot = "C:\msys64"
$env:PATH = "D:\fluidcore-windows-x64;$MsysRoot\ucrt64\bin;$MsysRoot\usr\bin;$env:PATH"
$env:MSYSTEM = "UCRT64"
$env:PKG_CONFIG_PATH = "$MsysRoot\ucrt64\lib\pkgconfig;$MsysRoot\ucrt64\share\pkgconfig"

$ExePath = "D:\FluidCorePDF\fluidcore-platform\build-win\src\app\search_isolation_probe.exe"
$PdfPath = "D:\study material\FIN F414 - FRAM\FRAMTextbook.ltproj\documents\Hull J.C.-Options, Futures and Other Derivatives_9th edition.pdf"

Write-Host "=================================================================" -ForegroundColor Cyan
Write-Host " RUNNING PROBE: Mode = $Mode" -ForegroundColor Cyan
Write-Host "=================================================================" -ForegroundColor Cyan
& $ExePath $Mode $PdfPath

