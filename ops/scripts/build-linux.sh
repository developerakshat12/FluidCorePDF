#!/usr/bin/env bash
# ==============================================================================
# build-linux.sh - Native Linux Build, Test, Benchmark, and Packaging Harness
# ==============================================================================
# Mirrors ops/scripts/build-win.ps1 for Linux environments (Ubuntu 24.04, CI, etc.)
# Usage:
#   ./ops/scripts/build-linux.sh [OPTIONS]
# Options:
#   --config <type>       CMake build type: RelWithDebInfo (default), Release, Debug
#   --test                Run all CTest unit and integration test suites
#   --package             Generate .deb and .AppImage distribution packages
#   --benchmark           Run the 50-PDF scalability and memory benchmark
#   --run                 Launch fluidcore_app
#   --document <file>     Optional document (.pdf or .ltproj) to open when running
#   --clean               Remove build directory before compiling
#   --build-dir <dir>     Custom build directory (default: build-linux)
# ==============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
cd "${PROJECT_ROOT}"

CONFIG="RelWithDebInfo"
DO_TEST=false
DO_PACKAGE=false
DO_BENCHMARK=false
DO_RUN=false
DO_CLEAN=false
DOCUMENT=""
BUILD_DIR="${PROJECT_ROOT}/build-linux"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --config)
            CONFIG="$2"
            shift 2
            ;;
        --test)
            DO_TEST=true
            shift
            ;;
        --package)
            DO_PACKAGE=true
            shift
            ;;
        --benchmark)
            DO_BENCHMARK=true
            shift
            ;;
        --run)
            DO_RUN=true
            shift
            ;;
        --document)
            DOCUMENT="$2"
            shift 2
            ;;
        --clean)
            DO_CLEAN=true
            shift
            ;;
        --build-dir)
            BUILD_DIR="$2"
            shift 2
            ;;
        -h|--help)
            echo "Usage: $0 [--config <RelWithDebInfo|Release|Debug>] [--test] [--package] [--benchmark] [--run] [--document <path>] [--clean] [--build-dir <path>]"
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            exit 1
            ;;
    esac
done

if [[ "${DO_CLEAN}" == true && -d "${BUILD_DIR}" ]]; then
    echo -e "\033[1;33m[FluidCore] Cleaning ${BUILD_DIR}...\033[0m"
    rm -rf "${BUILD_DIR}"
fi

mkdir -p "${BUILD_DIR}"

if [[ ! -f "${BUILD_DIR}/build.ninja" && ! -f "${BUILD_DIR}/Makefile" ]]; then
    echo -e "\033[1;36m[FluidCore] Configuring CMake in ${BUILD_DIR} (${CONFIG})...\033[0m"
    cmake -S "${PROJECT_ROOT}" -B "${BUILD_DIR}" -G Ninja \
        -DCMAKE_BUILD_TYPE="${CONFIG}" \
        -DFLUIDCORE_BUILD_APP=ON
fi

echo -e "\033[1;36m[FluidCore] Building targets with Ninja...\033[0m"
cmake --build "${BUILD_DIR}"

if [[ "${DO_TEST}" == true ]]; then
    echo -e "\033[1;32m[FluidCore] Running CTest test suites (36 targets)...\033[0m"
    ctest --test-dir "${BUILD_DIR}" --output-on-failure
fi

if [[ "${DO_BENCHMARK}" == true ]]; then
    BENCH_BIN="${BUILD_DIR}/src/app/scalability_benchmark_test"
    if [[ ! -x "${BENCH_BIN}" ]]; then
        echo "Benchmark executable not found at ${BENCH_BIN}" >&2
        exit 1
    fi
    echo -e "\033[1;36m[FluidCore] Running 50-PDF Scalability & Memory Benchmark...\033[0m"
    "${BENCH_BIN}"
fi

if [[ "${DO_PACKAGE}" == true ]]; then
    echo -e "\033[1;35m[FluidCore] Packaging Linux distributions (.deb & AppImage)...\033[0m"
    bash "${SCRIPT_DIR}/package-deb.sh" --build-dir "${BUILD_DIR}"
    bash "${SCRIPT_DIR}/package-appimage.sh" --build-dir "${BUILD_DIR}"
fi

if [[ "${DO_RUN}" == true ]]; then
    APP_BIN="${BUILD_DIR}/src/app/fluidcore_app"
    if [[ ! -x "${APP_BIN}" ]]; then
        echo "Binary not found at ${APP_BIN}" >&2
        exit 1
    fi
    echo -e "\033[1;32m[FluidCore] Launching ${APP_BIN}...\033[0m"
    if [[ -n "${DOCUMENT}" ]]; then
        "${APP_BIN}" "${DOCUMENT}"
    else
        "${APP_BIN}"
    fi
fi
