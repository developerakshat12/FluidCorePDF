<#
.SYNOPSIS
    Compiles patched Poppler and Poppler-GLib with native Win32 CRITICAL_SECTION mutexes.

.DESCRIPTION
    Automates fetching Poppler matching the MSYS2 UCRT64 toolchain, applies
    poppler_win32_critical_section.patch to eliminate libwinpthread mutex handle leaks,
    builds both libpoppler-163.dll and libpoppler-glib-8.dll in a single pass,
    atomically deploys the DLL pair across all project and toolchain destinations,
    and runs a strict post-build SHA-256 ABI parity audit across all deployment paths.
#>
param(
    [string]$MsysRoot = "",
    [string]$ProjectRoot = "",
    [string]$PopplerVersion = "26.08.0",
    [string[]]$DestDirs = @(),
    [switch]$DeployToMsys64 = $true,
    [switch]$ForceRebuild
)

$ErrorActionPreference = "Stop"

# Auto-detect Script and Project directory
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $ProjectRoot) {
    $ProjectRoot = Split-Path -Parent (Split-Path -Parent $ScriptDir)
}

# Auto-detect MSYS2 root
if (-not $MsysRoot) {
    $Candidates = @($env:MSYS2_ROOT, "C:\msys64", "D:\msys64", "C:\tools\msys64")
    foreach ($cand in $Candidates) {
        if ($cand -and (Test-Path "$cand\ucrt64\bin\gcc.exe")) {
            $MsysRoot = $cand
            break
        }
    }
}
if (-not $MsysRoot -or -not (Test-Path "$MsysRoot\ucrt64\bin\gcc.exe")) {
    throw "MSYS2 UCRT64 toolchain not found. Please specify -MsysRoot (e.g. C:\msys64)."
}

# Setup MSYS2 UCRT64 environment
$env:PATH = "$MsysRoot\ucrt64\bin;$MsysRoot\usr\bin;$env:PATH"
$env:MSYSTEM = "UCRT64"
$env:PKG_CONFIG_PATH = "$MsysRoot\ucrt64\lib\pkgconfig;$MsysRoot\ucrt64\share\pkgconfig"

# Validate required toolchain utilities
$RequiredTools = @("gcc.exe", "cmake.exe", "ninja.exe")
foreach ($tool in $RequiredTools) {
    $toolPath = Join-Path "$MsysRoot\ucrt64\bin" $tool
    if (-not (Test-Path $toolPath)) {
        throw "Required build tool missing: $toolPath. Please install mingw-w64-ucrt-x86_64-toolchain via pacman."
    }
}

# Resolve patch executable
$PatchExe = $null
$PatchCandidates = @(
    "$MsysRoot\usr\bin\patch.exe",
    "C:\Program Files\Git\usr\bin\patch.exe",
    "patch.exe"
)
foreach ($pc in $PatchCandidates) {
    if (Test-Path $pc) {
        $PatchExe = $pc
        break
    }
}
if (-not $PatchExe) {
    $cmd = Get-Command patch.exe -ErrorAction SilentlyContinue
    if ($cmd) { $PatchExe = $cmd.Source }
}
if (-not $PatchExe) {
    throw "patch.exe utility not found. Please install patch via MSYS2 (pacman -S patch) or Git for Windows."
}

# Resolve tar executable
$TarExe = "tar.exe"
if (Test-Path "C:\Windows\System32\tar.exe") {
    $TarExe = "C:\Windows\System32\tar.exe"
}

# Patch paths
$GlibPatchFile = Join-Path $ProjectRoot "ops\patches\0001-msys2-glib-mkenums-python-fix.patch"
$MutexPatchFile = Join-Path $ProjectRoot "ops\patches\poppler_win32_critical_section.patch"

if (-not (Test-Path $MutexPatchFile)) {
    throw "Mutex patch file not found: $MutexPatchFile"
}

# Build workspaces
$WorkDir = "$MsysRoot\tmp\poppler_build"
$Tarball = Join-Path $WorkDir "poppler-$PopplerVersion.tar.xz"
$SrcDir = Join-Path $WorkDir "poppler-$PopplerVersion"
$BuildDir = Join-Path $WorkDir "build"

if ($ForceRebuild -and (Test-Path $WorkDir)) {
    Write-Host "[Poppler Build] Cleaning workdir $WorkDir..." -ForegroundColor Yellow
    Remove-Item -Recurse -Force $WorkDir
}

if (-not (Test-Path $WorkDir)) {
    New-Item -ItemType Directory -Path $WorkDir -Force | Out-Null
}

if (-not (Test-Path $Tarball)) {
    Write-Host "[Poppler Build] Downloading Poppler $PopplerVersion tarball..." -ForegroundColor Cyan
    $Url = "https://poppler.freedesktop.org/poppler-$PopplerVersion.tar.xz"
    Invoke-WebRequest -Uri $Url -OutFile $Tarball
}

if (-not (Test-Path $SrcDir)) {
    Write-Host "[Poppler Build] Extracting $Tarball..." -ForegroundColor Cyan
    & $TarExe -xf $Tarball -C $WorkDir

    if (Test-Path $GlibPatchFile) {
        Write-Host "[Poppler Build] Applying MSYS2 build fix (0001-msys2-glib-mkenums-python-fix.patch)..." -ForegroundColor Cyan
        & $PatchExe -p1 -d $SrcDir -i $GlibPatchFile
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to apply patch $GlibPatchFile"
        }
    }

    Write-Host "[Poppler Build] Applying Win32 CRITICAL_SECTION patch (poppler_win32_critical_section.patch)..." -ForegroundColor Cyan
    & $PatchExe -p1 -d $SrcDir -i $MutexPatchFile
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to apply patch $MutexPatchFile"
    }
}

if (-not (Test-Path "$BuildDir\build.ninja")) {
    Write-Host "[Poppler Build] Configuring CMake with Ninja..." -ForegroundColor Cyan
    & cmake -S $SrcDir -B $BuildDir -G Ninja `
      -DCMAKE_BUILD_TYPE=Release `
      -DCMAKE_INSTALL_PREFIX="$MsysRoot/ucrt64" `
      -DENABLE_UNSTABLE_API_ABI_HEADERS=ON `
      -DENABLE_GLIB=ON `
      -DENABLE_GOBJECT_INTROSPECTION=OFF `
      -DENABLE_CPP=OFF `
      -DENABLE_LIBJPEG=ON `
      -DENABLE_LIBOPENJPEG=openjpeg2 `
      -DENABLE_NSS3=ON `
      -DENABLE_GPGME=OFF `
      -DENABLE_UTILS=OFF `
      -DENABLE_LIBCURL=ON `
      -DENABLE_QT5=OFF `
      -DENABLE_QT6=OFF `
      -DENABLE_BOOST=OFF `
      -DENABLE_ZLIB_UNCOMPRESS=OFF `
      -DENABLE_GTK_DOC=OFF `
      -DENABLE_RELOCATABLE=ON `
      -DBUILD_CPP_TESTS=OFF `
      -DBUILD_GTK_TESTS=OFF `
      -DBUILD_MANUAL_TESTS=OFF `
      -DBUILD_QT5_TESTS=OFF `
      -DBUILD_QT6_TESTS=OFF
    if ($LASTEXITCODE -ne 0) {
        throw "CMake configuration failed"
    }
}

Write-Host "[Poppler Build] Compiling poppler and poppler-glib targets..." -ForegroundColor Cyan
& cmake --build $BuildDir --target poppler poppler-glib
if ($LASTEXITCODE -ne 0) {
    throw "Build failed"
}

# Identify built DLLs
$BuiltPopplerDll = Join-Path $BuildDir "libpoppler-163.dll"
$BuiltGlibDll = Join-Path $BuildDir "glib\libpoppler-glib-8.dll"

if (-not (Test-Path $BuiltPopplerDll)) {
    throw "Built DLL not found: $BuiltPopplerDll"
}
if (-not (Test-Path $BuiltGlibDll)) {
    throw "Built GLib DLL not found: $BuiltGlibDll"
}

$PopplerHash = (Get-FileHash -Path $BuiltPopplerDll -Algorithm SHA256).Hash
$GlibHash = (Get-FileHash -Path $BuiltGlibDll -Algorithm SHA256).Hash

Write-Host "[Poppler Build] Successfully compiled:" -ForegroundColor Green
Write-Host "  libpoppler-163.dll:      $PopplerHash ($( (Get-Item $BuiltPopplerDll).Length ) bytes)"
Write-Host "  libpoppler-glib-8.dll:   $GlibHash ($( (Get-Item $BuiltGlibDll).Length ) bytes)"

# Determine deployment destinations
if ($DestDirs.Count -eq 0) {
    $DestDirs = @(
        (Join-Path $ProjectRoot "build-win\src\app"),
        (Join-Path $ProjectRoot "build-win\dist\fluidcore-windows-x64"),
        "D:\fluidcore-windows-x64"
    )
}

foreach ($Dest in $DestDirs) {
    if (Test-Path $Dest) {
        Write-Host "[Poppler Build] Deploying dual DLL pair to $Dest..." -ForegroundColor Green
        Copy-Item -Force $BuiltPopplerDll (Join-Path $Dest "libpoppler-163.dll")
        Copy-Item -Force $BuiltGlibDll (Join-Path $Dest "libpoppler-glib-8.dll")
    }
}

if ($DeployToMsys64) {
    $MsysBin = "$MsysRoot\ucrt64\bin"
    Write-Host "[Poppler Build] Deploying dual DLL pair to toolchain: $MsysBin..." -ForegroundColor Green
    Copy-Item -Force $BuiltPopplerDll (Join-Path $MsysBin "libpoppler-163.dll")
    Copy-Item -Force $BuiltGlibDll (Join-Path $MsysBin "libpoppler-glib-8.dll")

    # Also install header for compiling tests against PopplerMutex
    $IncludeDest = "$MsysRoot\ucrt64\include\poppler"
    if (Test-Path $IncludeDest) {
        $MutexH = Join-Path $SrcDir "poppler\PopplerMutex.h"
        if (Test-Path $MutexH) {
            Copy-Item -Force $MutexH (Join-Path $IncludeDest "PopplerMutex.h")
        }
    }
}

# Strict Post-Build ABI SHA-256 Parity Audit across all targets
Write-Host "`n[ABI Parity Audit] Running SHA-256 cross-check across all deployment paths..." -ForegroundColor Cyan

$AuditDirs = [System.Collections.Generic.List[string]]::new()
foreach ($d in $DestDirs) {
    if (Test-Path $d) { $AuditDirs.Add($d) }
}
if ($DeployToMsys64 -and (Test-Path "$MsysRoot\ucrt64\bin")) {
    $AuditDirs.Add("$MsysRoot\ucrt64\bin")
}

$AuditFailed = $false
foreach ($targetDir in $AuditDirs) {
    $targetPoppler = Join-Path $targetDir "libpoppler-163.dll"
    $targetGlib = Join-Path $targetDir "libpoppler-glib-8.dll"

    if (-not (Test-Path $targetPoppler)) {
        Write-Host "  [FAIL] Missing $targetPoppler" -ForegroundColor Red
        $AuditFailed = $true
        continue
    }
    if (-not (Test-Path $targetGlib)) {
        Write-Host "  [FAIL] Missing $targetGlib" -ForegroundColor Red
        $AuditFailed = $true
        continue
    }

    $tPopHash = (Get-FileHash -Path $targetPoppler -Algorithm SHA256).Hash
    $tGlibHash = (Get-FileHash -Path $targetGlib -Algorithm SHA256).Hash

    if ($tPopHash -ne $PopplerHash) {
        Write-Host "  [FAIL] SHA-256 mismatch for $targetPoppler!" -ForegroundColor Red
        Write-Host "         Expected: $PopplerHash"
        Write-Host "         Found:    $tPopHash"
        $AuditFailed = $true
    }
    if ($tGlibHash -ne $GlibHash) {
        Write-Host "  [FAIL] SHA-256 mismatch for $targetGlib!" -ForegroundColor Red
        Write-Host "         Expected: $GlibHash"
        Write-Host "         Found:    $tGlibHash"
        $AuditFailed = $true
    }

    if ($tPopHash -eq $PopplerHash -and $tGlibHash -eq $GlibHash) {
        Write-Host "  [PASS] $targetDir -> 100% SHA-256 match" -ForegroundColor Green
    }
}

if ($AuditFailed) {
    throw "Post-build ABI parity audit failed! Deployed DLLs do not match the compiled binary."
}

Write-Host "[ABI Parity Audit] SUCCESS: 100% SHA-256 parity verified across all deployment paths." -ForegroundColor Green

# Update provenance record
$InfoFile = Join-Path $ProjectRoot "docs\POPPLER_BUILD_INFO.txt"
$DateStr = (Get-Date).ToString("yyyy-MM-dd HH:mm:ss zzz")
$GccVer = (& gcc.exe --version | Select-Object -First 1)

$InfoContent = @"
================================================================================
  POPPLER BUILD PROVENANCE INFORMATION (Win32 CRITICAL_SECTION Hardened Build)
================================================================================
Package Version:    poppler-$PopplerVersion
Source Tarball:     https://poppler.freedesktop.org/poppler-$PopplerVersion.tar.xz
Patch Applied:      ops/patches/poppler_win32_critical_section.patch
Patch Description:  Replaced std::recursive_mutex in Array/Dict/Annot/Page/Catalog/PDFDoc/XRef
                    with native Win32 CRITICAL_SECTION (PopplerRecursiveMutex) to
                    completely eliminate libwinpthread-1.dll 24-byte handle leaks on MinGW-w64.
Built Date:         $DateStr
Toolchain:          $GccVer
Targets Built:      poppler (libpoppler-163.dll), poppler-glib (libpoppler-glib-8.dll)
Built Hashes (SHA-256):
  - libpoppler-163.dll:    $PopplerHash ($( (Get-Item $BuiltPopplerDll).Length ) bytes)
  - libpoppler-glib-8.dll: $GlibHash ($( (Get-Item $BuiltGlibDll).Length ) bytes)
Audit Status:       100% SHA-256 parity verified across all deployment destinations.
================================================================================
"@

Set-Content -Path $InfoFile -Value $InfoContent -Encoding utf8
Write-Host "[Poppler Build] Provenance record updated: $InfoFile" -ForegroundColor Cyan
