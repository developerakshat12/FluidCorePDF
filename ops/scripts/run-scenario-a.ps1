$ErrorActionPreference = "Stop"

$MsysRoot = "C:\msys64"
$env:PATH = "D:\fluidcore-windows-x64;$MsysRoot\ucrt64\bin;$MsysRoot\usr\bin;$env:PATH"
$env:MSYSTEM = "UCRT64"
$env:PKG_CONFIG_PATH = "$MsysRoot\ucrt64\lib\pkgconfig;$MsysRoot\ucrt64\share\pkgconfig"

$AppPath = "D:\fluidcore-windows-x64\fluidcore_app.exe"
$DocPath = "D:\study material\FIN F414 - FRAM\FRAMTextbook.ltproj"

Write-Host "[FluidCore] Running Scenario A on $DocPath..." -ForegroundColor Cyan
& $AppPath --run-scenario-a $DocPath
