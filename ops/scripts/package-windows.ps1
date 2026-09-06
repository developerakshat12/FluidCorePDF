<#
.SYNOPSIS
    Packages FluidCore into a standalone, portable Windows zip distribution and native installer.

.DESCRIPTION
    Collects fluidcore_app.exe, recursively identifies and bundles all required UCRT64
    runtime DLLs, GLib schemas, GDK-Pixbuf loaders, and Adwaita icon themes.
    Creates a zip archive ready for execution on a clean Windows machine without MSYS2,
    and compiles a native Inno Setup installer (FluidCore-Setup-x64.exe) with Start Menu
    integration, desktop icon, and .ltproj file associations.
#>
param (
    [string]$OutputDir = "build-win\dist\fluidcore-windows-x64",
    [string]$ZipFile = "build-win\dist\fluidcore-windows-x64.zip",
    [switch]$BuildInstaller = $true,
    [string]$AppVersion = "1.0.1"
)

$ErrorActionPreference = "Stop"

$MsysCandidates = @(
    $env:MSYS2_ROOT,
    "C:\msys64",
    "D:\msys64",
    "C:\tools\msys64"
)
$MsysRoot = $null
foreach ($Cand in $MsysCandidates) {
    if ($Cand -and (Test-Path "$Cand\ucrt64\bin")) {
        $MsysRoot = $Cand
        break
    }
}
if (-not $MsysRoot) {
    $MsysRoot = "C:\msys64"
}
$UcrtBin = "$MsysRoot\ucrt64\bin"

$ObjDump = $null
if (Test-Path "$UcrtBin\objdump.exe") {
    $ObjDump = "$UcrtBin\objdump.exe"
} else {
    $Cmd = Get-Command objdump.exe -ErrorAction SilentlyContinue
    if ($Cmd) {
        $ObjDump = $Cmd.Source
    }
}

if (-not $ObjDump) {
    $Pacman = "$MsysRoot\usr\bin\pacman.exe"
    if (Test-Path $Pacman) {
        Write-Host "[FluidCore Packager] objdump.exe not found. Installing mingw-w64-ucrt-x86_64-binutils via pacman..." -ForegroundColor Yellow
        & $Pacman -S --noconfirm mingw-w64-ucrt-x86_64-binutils
        if (Test-Path "$UcrtBin\objdump.exe") {
            $ObjDump = "$UcrtBin\objdump.exe"
        }
    }
}

if (-not $ObjDump) {
    Write-Error "objdump.exe not found at $UcrtBin\objdump.exe. MSYS2 UCRT64 binutils is required."
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
$DistDir = Split-Path -Parent $FullZipFile

Write-Host "[FluidCore Packager] Preparing distribution directory at $FullOutputDir..." -ForegroundColor Cyan
if (Test-Path $FullOutputDir) {
    Remove-Item -Recurse -Force $FullOutputDir
}
New-Item -ItemType Directory -Force -Path $FullOutputDir | Out-Null

Copy-Item $AppExe -Destination $FullOutputDir

# Copy icon assets into root and icons share
$IconSrc = Join-Path $ProjectRoot "resources\icons\fluidcore.ico"
$PngSrc = Join-Path $ProjectRoot "resources\icons\fluidcore.png"
if (Test-Path $IconSrc) {
    Copy-Item $IconSrc -Destination $FullOutputDir -Force
}
if (Test-Path $PngSrc) {
    Copy-Item $PngSrc -Destination $FullOutputDir -Force
}

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

# Bundle GDK-Pixbuf loaders (including SVG, PNG, JPEG, ICO, etc.)
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

# Recursive DLL resolution
$ProcessedDlls = New-Object 'System.Collections.Generic.HashSet[string]' ([System.StringComparer]::OrdinalIgnoreCase)
$Queue = New-Object 'System.Collections.Generic.Queue[string]'
$Queue.Enqueue((Join-Path $FullOutputDir "fluidcore_app.exe"))

# Enqueue all GDK-Pixbuf loader DLLs so their runtime dependencies (librsvg-2-2.dll, libxml2-16.dll, etc.) are resolved
if (Test-Path (Join-Path $FullOutputDir "lib\gdk-pixbuf-2.0")) {
    Get-ChildItem -Path (Join-Path $FullOutputDir "lib\gdk-pixbuf-2.0") -Recurse -Filter "*.dll" | ForEach-Object {
        $Queue.Enqueue($_.FullName)
    }
}

Write-Host "[FluidCore Packager] Resolving and copying runtime DLL dependencies..." -ForegroundColor Cyan

while ($Queue.Count -gt 0) {
    $CurrentBinary = $Queue.Dequeue()
    $DumpOutput = & $ObjDump -p $CurrentBinary 2>$null | Select-String "DLL Name:\s*(\S+)"

    foreach ($Line in $DumpOutput) {
        if ($Line.Matches.Count -gt 0) {
            $DllName = $Line.Matches[0].Groups[1].Value.Trim()

            # Ignore system Windows DLLs
            if ($DllName -match "^(kernel32|user32|gdi32|advapi32|shell32|ole32|oleaut32|uuid|ws2_32|msvcrt|ucrtbase|api-ms-|ext-ms-|comctl32|comdlg32|dwmapi|imm32|winmm|setupapi|cfgmgr32|hid|winspool|version|crypt32|shlwapi|dnsapi|iphlpapi|bcrypt|secur32|normaliz|rpcrt4|userenv|ntdll|dwrite|usp10|msimg32|gdiplus|bcryptprimitives)\.dll$") {
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

# Generate standalone launch script
$BatContent = @"
@echo off
setlocal
set "DIR=%~dp0"
set "PATH=%DIR%;%PATH%"
set "GSETTINGS_SCHEMA_DIR=%DIR%share\glib-2.0\schemas"
set "XDG_DATA_DIRS=%DIR%share"
set "FONTCONFIG_PATH=%DIR%etc\fonts"
set "GDK_PIXBUF_MODULE_FILE=%DIR%lib\gdk-pixbuf-2.0\2.10.0\loaders.cache"
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

# Inno Setup Native Installer Generation
if ($BuildInstaller) {
    Write-Host "[FluidCore Packager] Preparing Inno Setup Native Installer..." -ForegroundColor Cyan

    $IsccCandidates = @(
        "$ProjectRoot\.cache\innosetup\ISCC.exe",
        "C:\Program Files (x86)\Inno Setup 6\ISCC.exe",
        "C:\Program Files\Inno Setup 6\ISCC.exe",
        "C:\Program Files\Inno Setup 7\ISCC.exe"
    )
    $IsccExe = $null
    foreach ($Candidate in $IsccCandidates) {
        if (Test-Path $Candidate) {
            $IsccExe = $Candidate
            break
        }
    }
    if (-not $IsccExe) {
        $Cmd = Get-Command iscc.exe -ErrorAction SilentlyContinue
        if ($Cmd) {
            $IsccExe = $Cmd.Source
        }
    }

    if (-not $IsccExe) {
        Write-Host "[FluidCore Packager] Inno Setup compiler not found. Downloading portable Inno Setup..." -ForegroundColor Yellow
        $InnoCacheDir = Join-Path $ProjectRoot ".cache\innosetup"
        New-Item -ItemType Directory -Force -Path $InnoCacheDir | Out-Null
        $InstallerUrl = "https://github.com/jrsoftware/issrc/releases/download/is-7_1_0/innosetup-7.1.0-x64.exe"
        $TempInstaller = Join-Path $env:TEMP "innosetup-download.exe"
        Invoke-WebRequest -Uri $InstallerUrl -OutFile $TempInstaller -UseBasicParsing
        Start-Process -FilePath $TempInstaller -ArgumentList "/PORTABLE=1 /CURRENTUSER /VERYSILENT /SUPPRESSMSGBOXES /DIR=""$InnoCacheDir""" -Wait
        if (Test-Path "$InnoCacheDir\ISCC.exe") {
            $IsccExe = "$InnoCacheDir\ISCC.exe"
        } else {
            Write-Error "Failed to install portable Inno Setup."
        }
    }

    Write-Host "[FluidCore Packager] Using Inno Setup compiler: $IsccExe" -ForegroundColor Cyan
    $IssScript = Join-Path $ProjectRoot "ops\installer\fluidcore.iss"

    $IsccArgs = @(
        "/DMyAppVersion=$AppVersion",
        "/DSourceDistDir=$FullOutputDir",
        "/DOutputDir=$DistDir",
        $IssScript
    )

    Write-Host "[FluidCore Packager] Compiling Inno Setup installer..." -ForegroundColor Cyan
    $proc = Start-Process -FilePath $IsccExe -ArgumentList $IsccArgs -Wait -PassThru -NoNewWindow
    if ($proc.ExitCode -ne 0) {
        Write-Error "Inno Setup compilation failed with exit code $($proc.ExitCode)"
    }

    $InstallerExe = Join-Path $DistDir "FluidCore-Setup-x64.exe"
    if (Test-Path $InstallerExe) {
        $InstallerSizeMb = [math]::Round(((Get-Item $InstallerExe).Length / 1MB), 2)
        Write-Host "[FluidCore Packager] Success! Native installer created: $InstallerExe ($InstallerSizeMb MB)" -ForegroundColor Green
    } else {
        Write-Error "Expected installer binary not found at $InstallerExe"
    }
}
