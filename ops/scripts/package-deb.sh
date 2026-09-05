#!/usr/bin/env bash
# ==============================================================================
# package-deb.sh - FluidCore Native Debian (.deb) Package Generator
# ==============================================================================
# Generates a standard Debian package with granular dependency declarations,
# FreeDesktop desktop entry, shared-mime-info XML registration, and hicolor icons.
#
# Usage:
#   ./ops/scripts/package-deb.sh [--build-dir <dir>] [--output-dir <dir>] [--version <ver>]
# ==============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
cd "${PROJECT_ROOT}"

BUILD_DIR="${PROJECT_ROOT}/build-linux"
OUTPUT_DIR=""
VERSION="0.9.0"
ARCH="amd64"

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
        --arch)
            ARCH="$2"
            shift 2
            ;;
        -h|--help)
            echo "Usage: $0 [--build-dir <dir>] [--output-dir <dir>] [--version <ver>] [--arch <amd64>]"
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
    # Check fallback in build/
    if [[ -f "${PROJECT_ROOT}/build/src/app/fluidcore_app" ]]; then
        APP_BIN="${PROJECT_ROOT}/build/src/app/fluidcore_app"
    else
        echo "Error: fluidcore_app not found at ${APP_BIN}. Build the application first." >&2
        exit 1
    fi
fi

if ! command -v dpkg-deb >/dev/null 2>&1; then
    echo "Error: dpkg-deb command not found. Please install dpkg." >&2
    exit 1
fi

mkdir -p "${OUTPUT_DIR}"

DEB_NAME="fluidcore_${VERSION}_${ARCH}.deb"
DEB_PATH="${OUTPUT_DIR}/${DEB_NAME}"
STAGE_DIR="$(mktemp -d -t fluidcore-deb-XXXXXX)"
trap 'rm -rf "${STAGE_DIR}"' EXIT

echo -e "\033[1;36m[FluidCore .deb] Staging package filesystem in ${STAGE_DIR}...\033[0m"

# 1. Binary directory
mkdir -p "${STAGE_DIR}/usr/bin"
cp -p "${APP_BIN}" "${STAGE_DIR}/usr/bin/fluidcore_app"
chmod 755 "${STAGE_DIR}/usr/bin/fluidcore_app"

# Wrapper symlink for convenience: /usr/bin/fluidcore -> fluidcore_app
cat > "${STAGE_DIR}/usr/bin/fluidcore" << 'EOF'
#!/bin/sh
exec /usr/bin/fluidcore_app "$@"
EOF
chmod 755 "${STAGE_DIR}/usr/bin/fluidcore"

# 2. Desktop entry
mkdir -p "${STAGE_DIR}/usr/share/applications"
cp -p "${PROJECT_ROOT}/resources/linux/org.fluidcore.platform.desktop" \
      "${STAGE_DIR}/usr/share/applications/org.fluidcore.platform.desktop"
chmod 644 "${STAGE_DIR}/usr/share/applications/org.fluidcore.platform.desktop"

# 3. MIME info
mkdir -p "${STAGE_DIR}/usr/share/mime/packages"
cp -p "${PROJECT_ROOT}/resources/linux/org.fluidcore.platform.xml" \
      "${STAGE_DIR}/usr/share/mime/packages/org.fluidcore.platform.xml"
chmod 644 "${STAGE_DIR}/usr/share/mime/packages/org.fluidcore.platform.xml"

# 4. AppStream Metainfo
mkdir -p "${STAGE_DIR}/usr/share/metainfo"
cp -p "${PROJECT_ROOT}/resources/linux/org.fluidcore.platform.metainfo.xml" \
      "${STAGE_DIR}/usr/share/metainfo/org.fluidcore.platform.metainfo.xml"
chmod 644 "${STAGE_DIR}/usr/share/metainfo/org.fluidcore.platform.metainfo.xml"

# 5. Icons hierarchy
if [[ -d "${PROJECT_ROOT}/resources/icons/hicolor" ]]; then
    mkdir -p "${STAGE_DIR}/usr/share/icons"
    cp -rp "${PROJECT_ROOT}/resources/icons/hicolor" "${STAGE_DIR}/usr/share/icons/"
fi

# 6. Compute installed size (in KB)
INSTALLED_SIZE=$(du -sk "${STAGE_DIR}" | awk '{print $1}')

# 7. Control file
mkdir -p "${STAGE_DIR}/DEBIAN"
cat > "${STAGE_DIR}/DEBIAN/control" << EOF
Package: fluidcore
Version: ${VERSION}
Section: utils
Priority: optional
Architecture: ${ARCH}
Essential: no
Installed-Size: ${INSTALLED_SIZE}
Maintainer: FluidCore Platform Contributors <maintainers@fluidcore.org>
Depends: libc6 (>= 2.34), libgtk-3-0 (>= 3.22), libpoppler-glib8 (>= 20.0), libcairo2 (>= 1.16), libsqlite3-0 (>= 3.35), zlib1g (>= 1.2.11)
Recommends: shared-mime-info, hicolor-icon-theme
Homepage: https://github.com/fluidcore/fluidcore-platform
Description: Offline-first fluid document synthesis platform
 FluidCore pairs fluid PDF reading with accordion squeeze folding and an
 infinite 2D synthesis canvas. Designed for active reading and research with
 100% offline, crash-safe SQLite WAL durability.
EOF

# 8. Post-install and Post-remove maintainer scripts
cat > "${STAGE_DIR}/DEBIAN/postinst" << 'EOF'
#!/bin/sh
set -e
if [ "$1" = "configure" ]; then
    if command -v update-desktop-database >/dev/null 2>&1; then
        update-desktop-database -q || true
    fi
    if command -v update-mime-database >/dev/null 2>&1; then
        update-mime-database /usr/share/mime || true
    fi
    if command -v gtk-update-icon-cache >/dev/null 2>&1; then
        gtk-update-icon-cache -q -t -f /usr/share/icons/hicolor || true
    fi
fi
exit 0
EOF
chmod 755 "${STAGE_DIR}/DEBIAN/postinst"

cat > "${STAGE_DIR}/DEBIAN/postrm" << 'EOF'
#!/bin/sh
set -e
if [ "$1" = "remove" ] || [ "$1" = "purge" ]; then
    if command -v update-desktop-database >/dev/null 2>&1; then
        update-desktop-database -q || true
    fi
    if command -v update-mime-database >/dev/null 2>&1; then
        update-mime-database /usr/share/mime || true
    fi
    if command -v gtk-update-icon-cache >/dev/null 2>&1; then
        gtk-update-icon-cache -q -t -f /usr/share/icons/hicolor || true
    fi
fi
exit 0
EOF
chmod 755 "${STAGE_DIR}/DEBIAN/postrm"

# 9. Build the .deb archive
echo -e "\033[1;36m[FluidCore .deb] Compiling ${DEB_NAME}...\033[0m"
dpkg-deb --build --root-owner-group "${STAGE_DIR}" "${DEB_PATH}"

# 10. Smoke test package
echo -e "\033[1;32m[FluidCore .deb] Verifying package metadata and contents...\033[0m"
dpkg-deb -I "${DEB_PATH}"
echo -e "\033[1;32m[FluidCore .deb] Successfully generated ${DEB_PATH} ($(du -h "${DEB_PATH}" | awk '{print $1}'))\033[0m"
