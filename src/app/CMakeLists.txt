find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)
pkg_check_modules(POPPLER_GLIB REQUIRED IMPORTED_TARGET poppler-glib)
pkg_check_modules(CAIRO REQUIRED IMPORTED_TARGET cairo)

# Target name avoids the global CMake namespace of the `fluidcore` library.
add_executable(fluidcore_app
    main.cpp
    DocumentPane.cpp
    DocumentSearchService.cpp
    InkOverlay.cpp
    PageTileCache.cpp
    SearchBarWidget.cpp
    SqueezeRenderHelper.cpp
    StrokeStabilizer.cpp
    TextSelectionService.cpp
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

    add_executable(stroke_stabilizer_test StrokeStabilizerTest.cpp StrokeStabilizer.cpp)
    target_include_directories(stroke_stabilizer_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    if(MSVC)
        target_compile_options(stroke_stabilizer_test PRIVATE /W4 /permissive-)
    else()
        target_compile_options(stroke_stabilizer_test PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME StrokeStabilizerTest COMMAND stroke_stabilizer_test)

    add_executable(squeeze_render_test SqueezeRenderTest.cpp SqueezeRenderHelper.cpp)
    target_include_directories(squeeze_render_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(squeeze_render_test PRIVATE fluidcore PkgConfig::CAIRO)
    if(MSVC)
        target_compile_options(squeeze_render_test PRIVATE /W4 /permissive-)
    else()
        target_compile_options(squeeze_render_test PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME SqueezeRenderTest COMMAND squeeze_render_test)

    add_executable(search_squeeze_planner_test SearchSqueezePlannerTest.cpp SqueezeRenderHelper.cpp)
    target_include_directories(search_squeeze_planner_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(search_squeeze_planner_test PRIVATE fluidcore PkgConfig::CAIRO)
    if(MSVC)
        target_compile_options(search_squeeze_planner_test PRIVATE /W4 /permissive-)
    else()
        target_compile_options(search_squeeze_planner_test PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME SearchSqueezePlannerTest COMMAND search_squeeze_planner_test)
endif()
