#!/bin/bash
set -e
cd /mnt/d/FluidCorePDF/fluidcore-platform/build
cmake .. -DFLUIDCORE_BUILD_APP=ON
cmake --build . -j4
ctest --output-on-failure
