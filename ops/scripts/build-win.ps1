<#
.SYNOPSIS
    Builds, tests, or launches FluidCore natively on Windows using MSYS2 UCRT64.

.DESCRIPTION
    Configures environment variables for MSYS2 UCRT64 (GCC, CMake, Ninja, GTK3, Cairo, Poppler)
    and executes CMake configure, Ninja build, CTest, or launches the GUI application.

.PARAMETER Config
    Build configuration: RelWithDebInfo (default), Debug, or Release.

.PARAMETER Test
    If set, executes ctest across all unit and integration test targets.

.PARAMETER Run
    If set, launches fluidcore_app.exe.

.PARAMETER Document
    Optional path to a PDF document to open when running.

.PARAMETER Clean
    If set, removes the build-win directory before building.
#>
param (
    [string]$Config = "RelWithDebInfo",
    [switch]$Test,
    [switch]$Run,
    [string]$Document = "",
    [switch]$Clean
)

$ErrorActionPreference = "Stop"

$MsysRoot = "C:\msys64"
if (-not (Test-Path "$MsysRoot\ucrt64\bin\gcc.exe")) {
    Write-Error "MSYS2 UCRT64 toolchain not found at $MsysRoot\ucrt64. Please run MSYS2 package setup first."
}

$env:PATH = "$MsysRoot\ucrt64\bin;$MsysRoot\usr\bin;$env:PATH"
$env:MSYSTEM = "UCRT64"
$env:PKG_CONFIG_PATH = "$MsysRoot\ucrt64\lib\pkgconfig;$MsysRoot\ucrt64\share\pkgconfig"
if (Test-Path "$MsysRoot\ucrt64\etc\fonts") {
    $env:FONTCONFIG_PATH = "$MsysRoot\ucrt64\etc\fonts"
}
if (Test-Path "$MsysRoot\ucrt64\share") {
    $env:XDG_DATA_DIRS = "$MsysRoot\ucrt64\share"
}

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Split-Path -Parent (Split-Path -Parent $ScriptDir)
Set-Location $ProjectRoot

$BuildDir = Join-Path $ProjectRoot "build-win"

if ($Clean -and (Test-Path $BuildDir)) {
    Write-Host "[FluidCore] Cleaning $BuildDir..." -ForegroundColor Yellow
    Remove-Item -Recurse -Force $BuildDir
}

if (-not (Test-Path "$BuildDir\build.ninja")) {
    Write-Host "[FluidCore] Configuring CMake in $BuildDir..." -ForegroundColor Cyan
    & cmake -S . -B $BuildDir -G Ninja -DCMAKE_BUILD_TYPE=$Config -DFLUIDCORE_BUILD_APP=ON
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

Write-Host "[FluidCore] Building with Ninja..." -ForegroundColor Cyan
& cmake --build $BuildDir
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if ($Test) {
    Write-Host "[FluidCore] Running CTest test suites..." -ForegroundColor Green
    & ctest --test-dir $BuildDir --output-on-failure
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

if ($Run) {
    $AppPath = Join-Path $BuildDir "src\app\fluidcore_app.exe"
    if (-not (Test-Path $AppPath)) {
        Write-Error "Binary not found at $AppPath"
    }
    Write-Host "[FluidCore] Launching $AppPath..." -ForegroundColor Green
    if ($Document) {
        $DocPath = $Document
        if (Test-Path $Document) {
            $DocPath = (Resolve-Path $Document).Path
        }
        & $AppPath $DocPath
    } else {
        & $AppPath
    }
}
