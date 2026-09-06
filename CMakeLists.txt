cmake_minimum_required(VERSION 3.20)

project(fluidcore-platform
    VERSION 1.0.1
    DESCRIPTION "Offline-first fluid document synthesis platform (libfluidcore + GTK frontend)"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(FLUIDCORE_SANITIZERS "Enable ASan/UBSan on supported compilers" OFF)
option(FLUIDCORE_BUILD_APP "Build the GTK3 frontend (src/app). Requires gtk+-3.0 via pkg-config." OFF)

include(CTest)

add_subdirectory(src/libfluidcore)

if(FLUIDCORE_BUILD_APP)
    add_subdirectory(src/app)
endif()
