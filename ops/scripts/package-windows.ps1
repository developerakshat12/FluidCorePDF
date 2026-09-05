<#
.SYNOPSIS
    Packages FluidCore into a standalone, portable Windows zip distribution.

.DESCRIPTION
    Collects fluidcore_app.exe, recursively identifies and bundles all required UCRT64
    runtime DLLs, GLib schemas, GDK-Pixbuf loaders, and Adwaita icon themes.
    Creates a zip archive ready for execution on a clean Windows machine without MSYS2.
#>
param (
    [string]$OutputDir = "build-win\dist\fluidcore-windows-x64",
    [string]$ZipFile = "build-win\dist\fluidcore-windows-x64.zip"
)

$ErrorActionPreference = "Stop"

$MsysRoot = "C:\msys64"
$UcrtBin = "$MsysRoot\ucrt64\bin"
$ObjDump = "$UcrtBin\objdump.exe"

if (-not (Test-Path $ObjDump)) {
    Write-Error "objdump.exe not found at $ObjDump. MSYS2 UCRT64 is required."
}

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Split-Path -Parent (Split-Path -Parent $ScriptDir)
Set-Location $ProjectRoot

$AppExe = Join-Path $ProjectRoot "build-win\src\app\fluidcore_app.exe"
if (-not (Test-Path $AppExe)) {
    Write-Error "fluidcore_app.exe not found. Please build the project first."
}

$FullOutputDir = [System.IO.Path]::GetFullPath((Join-Path $ProjectRoot $OutputDir))
$FullZipFile = [System.IO.Path]::GetFullPath((Join-Path $ProjectRoot $ZipFile))

Write-Host "[FluidCore Packager] Preparing distribution directory at $FullOutputDir..." -ForegroundColor Cyan
if (Test-Path $FullOutputDir) {
    Remove-Item -Recurse -Force $FullOutputDir
}
New-Item -ItemType Directory -Force -Path $FullOutputDir | Out-Null

Copy-Item $AppExe -Destination $FullOutputDir

# Recursive DLL resolution
$ProcessedDlls = New-Object 'System.Collections.Generic.HashSet[string]' ([System.StringComparer]::OrdinalIgnoreCase)
$Queue = New-Object 'System.Collections.Generic.Queue[string]'
$Queue.Enqueue((Join-Path $FullOutputDir "fluidcore_app.exe"))

Write-Host "[FluidCore Packager] Resolving and copying runtime DLL dependencies..." -ForegroundColor Cyan

while ($Queue.Count -gt 0) {
    $CurrentBinary = $Queue.Dequeue()
    $DumpOutput = & $ObjDump -p $CurrentBinary 2>$null | Select-String "DLL Name:\s*(\S+)"

    foreach ($Line in $DumpOutput) {
        if ($Line.Matches.Count -gt 0) {
            $DllName = $Line.Matches[0].Groups[1].Value.Trim()

            # Ignore system Windows DLLs
            if ($DllName -match "^(kernel32|user32|gdi32|advapi32|shell32|ole32|oleaut32|uuid|ws2_32|msvcrt|ucrtbase|api-ms-|ext-ms-|comctl32|comdlg32|dwmapi|imm32|winmm|setupapi|cfgmgr32|hid|winspool|version|crypt32|shlwapi|dnsapi|iphlpapi|bcrypt|secur32|normaliz|rpcrt4)\.dll$") {
                continue
            }

            if (-not $ProcessedDlls.Contains($DllName)) {
                $ProcessedDlls.Add($DllName) | Out-Null
                $SourceDll = Join-Path $UcrtBin $DllName
                if (Test-Path $SourceDll) {
                    $TargetDll = Join-Path $FullOutputDir $DllName
                    Copy-Item $SourceDll -Destination $TargetDll -Force
                    $Queue.Enqueue($TargetDll)
                }
            }
        }
    }
}

Write-Host "[FluidCore Packager] Bundled $($ProcessedDlls.Count) DLL dependencies." -ForegroundColor Green

# Bundle GLib schemas
$SchemasSrc = "$MsysRoot\ucrt64\share\glib-2.0\schemas"
if (Test-Path $SchemasSrc) {
    $SchemasDest = Join-Path $FullOutputDir "share\glib-2.0\schemas"
    New-Item -ItemType Directory -Force -Path $SchemasDest | Out-Null
    Copy-Item "$SchemasSrc\*" -Destination $SchemasDest -Recurse -Force
}

# Bundle Adwaita & Hicolor icons
$IconsSrc = "$MsysRoot\ucrt64\share\icons"
if (Test-Path $IconsSrc) {
    $IconsDest = Join-Path $FullOutputDir "share\icons"
    New-Item -ItemType Directory -Force -Path $IconsDest | Out-Null
    if (Test-Path "$IconsSrc\Adwaita") {
        Copy-Item "$IconsSrc\Adwaita" -Destination $IconsDest -Recurse -Force
    }
    if (Test-Path "$IconsSrc\hicolor") {
        Copy-Item "$IconsSrc\hicolor" -Destination $IconsDest -Recurse -Force
    }
}

# Bundle GDK-Pixbuf loaders
$PixbufSrc = "$MsysRoot\ucrt64\lib\gdk-pixbuf-2.0"
if (Test-Path $PixbufSrc) {
    $PixbufDest = Join-Path $FullOutputDir "lib\gdk-pixbuf-2.0"
    New-Item -ItemType Directory -Force -Path $PixbufDest | Out-Null
    Copy-Item "$PixbufSrc\*" -Destination $PixbufDest -Recurse -Force
}

# Bundle Fontconfig configuration
$FontsSrc = "$MsysRoot\ucrt64\etc\fonts"
if (Test-Path $FontsSrc) {
    $FontsDest = Join-Path $FullOutputDir "etc\fonts"
    New-Item -ItemType Directory -Force -Path $FontsDest | Out-Null
    Copy-Item "$FontsSrc\*" -Destination $FontsDest -Recurse -Force
}

# Generate standalone launch script
$BatContent = @"
@echo off
setlocal
set "DIR=%~dp0"
set "PATH=%DIR%;%PATH%"
set "GSETTINGS_SCHEMA_DIR=%DIR%share\glib-2.0\schemas"
set "XDG_DATA_DIRS=%DIR%share"
set "FONTCONFIG_PATH=%DIR%etc\fonts"
start "" "%DIR%fluidcore_app.exe" %*
endlocal
"@
Set-Content -Path (Join-Path $FullOutputDir "run_fluidcore.bat") -Value $BatContent -Encoding ASCII

# Zip the bundle
if (Test-Path $FullZipFile) {
    Remove-Item -Force $FullZipFile
}
Write-Host "[FluidCore Packager] Compressing package into $FullZipFile..." -ForegroundColor Cyan
Compress-Archive -Path "$FullOutputDir\*" -DestinationPath $FullZipFile -Force

$ZipSizeMb = [math]::Round(((Get-Item $FullZipFile).Length / 1MB), 2)
Write-Host "[FluidCore Packager] Success! Standalone package created: $FullZipFile ($ZipSizeMb MB)" -ForegroundColor Green
