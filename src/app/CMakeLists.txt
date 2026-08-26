find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)

# Target name avoids the global CMake namespace of the `fluidcore` library.
add_executable(fluidcore_app
    main.cpp
    WorkspaceView.cpp
)

target_include_directories(fluidcore_app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(fluidcore_app PRIVATE fluidcore PkgConfig::GTK3)

if(MSVC)
    target_compile_options(fluidcore_app PRIVATE /W4 /permissive-)
else()
    target_compile_options(fluidcore_app PRIVATE -Wall -Wextra -Wpedantic)
endif()
