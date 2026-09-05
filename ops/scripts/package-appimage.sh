#!/usr/bin/env bash
# ==============================================================================
# package-appimage.sh - FluidCore Standalone Portable AppImage Generator
# ==============================================================================
# Generates a self-contained, portable AppImage package using a pinned
# appimagetool release and an intermediate testable AppDir staging directory.
#
# Usage:
#   ./ops/scripts/package-appimage.sh [--build-dir <dir>] [--output-dir <dir>] [--version <ver>]
# ==============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
cd "${PROJECT_ROOT}"

BUILD_DIR="${PROJECT_ROOT}/build-linux"
OUTPUT_DIR=""
VERSION="0.9.0"
APPDIR_ONLY=false

while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-dir)
            BUILD_DIR="$2"
            shift 2
            ;;
        --output-dir)
            OUTPUT_DIR="$2"
            shift 2
            ;;
        --version)
            VERSION="$2"
            shift 2
            ;;
        --appdir-only)
            APPDIR_ONLY=true
            shift
            ;;
        -h|--help)
            echo "Usage: $0 [--build-dir <dir>] [--output-dir <dir>] [--version <ver>] [--appdir-only]"
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            exit 1
            ;;
    esac
done

if [[ -z "${OUTPUT_DIR}" ]]; then
    OUTPUT_DIR="${BUILD_DIR}/dist"
fi

APP_BIN="${BUILD_DIR}/src/app/fluidcore_app"
if [[ ! -f "${APP_BIN}" ]]; then
    if [[ -f "${PROJECT_ROOT}/build/src/app/fluidcore_app" ]]; then
        APP_BIN="${PROJECT_ROOT}/build/src/app/fluidcore_app"
    else
        echo "Error: fluidcore_app not found at ${APP_BIN}. Build the application first." >&2
        exit 1
    fi
fi

mkdir -p "${OUTPUT_DIR}"
APPDIR="${OUTPUT_DIR}/AppDir"

echo -e "\033[1;36m[FluidCore AppImage] Assembling intermediate AppDir at ${APPDIR}...\033[0m"
rm -rf "${APPDIR}"
mkdir -p "${APPDIR}/usr/bin"
mkdir -p "${APPDIR}/usr/lib"
mkdir -p "${APPDIR}/usr/share/applications"
mkdir -p "${APPDIR}/usr/share/mime/packages"
mkdir -p "${APPDIR}/usr/share/metainfo"
mkdir -p "${APPDIR}/usr/share/glib-2.0/schemas"

# 1. Copy application binary
cp -p "${APP_BIN}" "${APPDIR}/usr/bin/fluidcore_app"
chmod 755 "${APPDIR}/usr/bin/fluidcore_app"
ln -sf fluidcore_app "${APPDIR}/usr/bin/fluidcore"

# 2. Desktop file and icons
cp -p "${PROJECT_ROOT}/resources/linux/org.fluidcore.platform.desktop" "${APPDIR}/org.fluidcore.platform.desktop"
cp -p "${PROJECT_ROOT}/resources/linux/org.fluidcore.platform.desktop" "${APPDIR}/usr/share/applications/org.fluidcore.platform.desktop"

ICON_SRC="${PROJECT_ROOT}/resources/icons/fluidcore.png"
if [[ -f "${PROJECT_ROOT}/resources/icons/hicolor/256x256/apps/org.fluidcore.platform.png" ]]; then
    ICON_SRC="${PROJECT_ROOT}/resources/icons/hicolor/256x256/apps/org.fluidcore.platform.png"
fi

cp -p "${ICON_SRC}" "${APPDIR}/org.fluidcore.platform.png"
cp -p "${ICON_SRC}" "${APPDIR}/.DirIcon"

if [[ -d "${PROJECT_ROOT}/resources/icons/hicolor" ]]; then
    mkdir -p "${APPDIR}/usr/share/icons"
    cp -rp "${PROJECT_ROOT}/resources/icons/hicolor" "${APPDIR}/usr/share/icons/"
fi

# 3. MIME info & Metainfo
cp -p "${PROJECT_ROOT}/resources/linux/org.fluidcore.platform.xml" "${APPDIR}/usr/share/mime/packages/org.fluidcore.platform.xml"
cp -p "${PROJECT_ROOT}/resources/linux/org.fluidcore.platform.metainfo.xml" "${APPDIR}/usr/share/metainfo/org.fluidcore.platform.metainfo.xml"
cp -p "${PROJECT_ROOT}/resources/linux/org.fluidcore.platform.metainfo.xml" "${APPDIR}/usr/share/metainfo/org.fluidcore.platform.appdata.xml"

# 4. Compile GLib GSettings schemas
if [[ -d "/usr/share/glib-2.0/schemas" ]]; then
    cp -p /usr/share/glib-2.0/schemas/*.xml "${APPDIR}/usr/share/glib-2.0/schemas/" 2>/dev/null || true
    if command -v glib-compile-schemas >/dev/null 2>&1; then
        glib-compile-schemas "${APPDIR}/usr/share/glib-2.0/schemas" || true
    fi
fi

# 5. Resolve and copy non-system runtime shared libraries
echo -e "\033[1;36m[FluidCore AppImage] Bundling required runtime shared libraries...\033[0m"

# Excludelist regex: system base libraries that must be provided by host OS
EXCLUDE_REGEX="^(libc|libm|libpthread|libdl|librt|ld-linux|libresolv|libnss|libutil|libX11|libX11-xcb|libxcb|libGL|libEGL|libGLX|libOpenGL|libdrm|libasound)\.so"

LIBS_TO_COPY=()
while IFS= read -r line; do
    # Parse ldd output: "libfoo.so.1 => /path/to/libfoo.so.1 (0x...)"
    if [[ "$line" =~ \=\>[[:space:]]*([^[:space:]]+) ]]; then
        LIB_PATH="${BASH_REMATCH[1]}"
        LIB_NAME="$(basename "${LIB_PATH}")"

        if [[ ! "${LIB_NAME}" =~ ${EXCLUDE_REGEX} && -f "${LIB_PATH}" ]]; then
            LIBS_TO_COPY+=("${LIB_PATH}")
        fi
    fi
done < <(ldd "${APP_BIN}" 2>/dev/null || true)

if [[ ${#LIBS_TO_COPY[@]} -gt 0 ]]; then
    cp -p -L "${LIBS_TO_COPY[@]}" "${APPDIR}/usr/lib/" 2>/dev/null || true
fi

# 6. Create AppRun launcher trampoline
cat > "${APPDIR}/AppRun" << 'EOF'
#!/bin/sh
set -e

SELF=$(readlink -f "$0")
HERE=${SELF%/*}

export PATH="${HERE}/usr/bin:${HERE}/bin:${PATH}"
export LD_LIBRARY_PATH="${HERE}/usr/lib:${HERE}/lib:${LD_LIBRARY_PATH:-}"
export XDG_DATA_DIRS="${HERE}/usr/share:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"

if [ -d "${HERE}/usr/share/glib-2.0/schemas" ]; then
    export GSETTINGS_SCHEMA_DIR="${HERE}/usr/share/glib-2.0/schemas:${GSETTINGS_SCHEMA_DIR:-}"
fi

exec "${HERE}/usr/bin/fluidcore_app" "$@"
EOF
chmod 755 "${APPDIR}/AppRun"

echo -e "\033[1;32m[FluidCore AppImage] Intermediate AppDir staged successfully at ${APPDIR}\033[0m"

if [[ "${APPDIR_ONLY}" == true ]]; then
    echo -e "\033[1;33m[FluidCore AppImage] --appdir-only specified. Skipping AppImage compression.\033[0m"
    exit 0
fi

# 7. Locate or download pinned appimagetool
APPIMAGETOOL=""
TOOL_DIR="${BUILD_DIR}/tools"
mkdir -p "${TOOL_DIR}"

if command -v appimagetool >/dev/null 2>&1; then
    APPIMAGETOOL="$(command -v appimagetool)"
elif [[ -x "${TOOL_DIR}/appimagetool" ]]; then
    APPIMAGETOOL="${TOOL_DIR}/appimagetool"
elif command -v curl >/dev/null 2>&1; then
    echo -e "\033[1;36m[FluidCore AppImage] Downloading pinned appimagetool to ${TOOL_DIR}...\033[0m"
    TOOL_URL="https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-x86_64.AppImage"
    if curl -fsSL -o "${TOOL_DIR}/appimagetool" "${TOOL_URL}"; then
        chmod +x "${TOOL_DIR}/appimagetool"
        APPIMAGETOOL="${TOOL_DIR}/appimagetool"
    fi
fi

if [[ -z "${APPIMAGETOOL}" || ! -x "${APPIMAGETOOL}" ]]; then
    echo -e "\033[1;33m[FluidCore AppImage] Warning: appimagetool not found or failed to download. Intermediate AppDir is ready at ${APPDIR} and can be run via ./build-linux/dist/AppDir/AppRun\033[0m"
    exit 0
fi

# 8. Compile standalone AppImage
APPIMAGE_NAME="FluidCore-${VERSION}-x86_64.AppImage"
APPIMAGE_PATH="${OUTPUT_DIR}/${APPIMAGE_NAME}"

echo -e "\033[1;36m[FluidCore AppImage] Packaging ${APPIMAGE_NAME} via appimagetool...\033[0m"
export ARCH=x86_64
export NO_APPSTREAM=1

# Use --appimage-extract-and-run for compatibility in container/WSL/CI environments without FUSE
if "${APPIMAGETOOL}" --version >/dev/null 2>&1; then
    "${APPIMAGETOOL}" --no-appstream "${APPDIR}" "${APPIMAGE_PATH}"
else
    "${APPIMAGETOOL}" --appimage-extract-and-run --no-appstream "${APPDIR}" "${APPIMAGE_PATH}"
fi

chmod +x "${APPIMAGE_PATH}"
echo -e "\033[1;32m[FluidCore AppImage] Successfully generated ${APPIMAGE_PATH} ($(du -h "${APPIMAGE_PATH}" | awk '{print $1}'))\033[0m"
