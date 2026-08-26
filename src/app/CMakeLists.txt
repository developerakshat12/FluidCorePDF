find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)
pkg_check_modules(POPPLER_GLIB REQUIRED IMPORTED_TARGET poppler-glib)
pkg_check_modules(CAIRO REQUIRED IMPORTED_TARGET cairo)

# Target name avoids the global CMake namespace of the `fluidcore` library.
add_executable(fluidcore_app
    main.cpp
    DocumentPane.cpp
    InkOverlay.cpp
    PageTileCache.cpp
    ThumbnailCache.cpp
    ThumbnailSidebar.cpp
    WorkspaceView.cpp
)

target_include_directories(fluidcore_app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(fluidcore_app PRIVATE fluidcore PkgConfig::GTK3 PkgConfig::POPPLER_GLIB
                                            PkgConfig::CAIRO)

if(MSVC)
    target_compile_options(fluidcore_app PRIVATE /W4 /permissive-)
else()
    target_compile_options(fluidcore_app PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING)
    add_executable(thumbnail_layout_test ThumbnailLayoutTest.cpp)
    target_include_directories(thumbnail_layout_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    if(MSVC)
        target_compile_options(thumbnail_layout_test PRIVATE /W4 /permissive-)
    else()
        target_compile_options(thumbnail_layout_test PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME ThumbnailLayoutTest COMMAND thumbnail_layout_test)

    add_executable(damage_rect_test DamageRectTest.cpp)
    target_include_directories(damage_rect_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    if(MSVC)
        target_compile_options(damage_rect_test PRIVATE /W4 /permissive-)
    else()
        target_compile_options(damage_rect_test PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME DamageRectTest COMMAND damage_rect_test)

    add_executable(page_tile_cache_test PageTileCacheTest.cpp PageTileCache.cpp)
    target_include_directories(page_tile_cache_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(page_tile_cache_test PRIVATE PkgConfig::CAIRO PkgConfig::POPPLER_GLIB)
    if(MSVC)
        target_compile_options(page_tile_cache_test PRIVATE /W4 /permissive-)
    else()
        target_compile_options(page_tile_cache_test PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME PageTileCacheTest COMMAND page_tile_cache_test)
endif()
