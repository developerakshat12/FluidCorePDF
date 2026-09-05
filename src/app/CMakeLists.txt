find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)
pkg_check_modules(POPPLER_GLIB REQUIRED IMPORTED_TARGET poppler-glib)
pkg_check_modules(CAIRO REQUIRED IMPORTED_TARGET cairo)

if(WIN32)
    add_compile_definitions(_USE_MATH_DEFINES)
endif()

set(FLUIDCORE_APP_SOURCES
    main.cpp
    document/DocumentPane.cpp
    document/InkOverlay.cpp
    document/ReturnAnchorPill.cpp
    document/SearchBarWidget.cpp
    document/SqueezeRenderHelper.cpp
    export/ExportDialog.cpp
    export/ExportProgressDialog.cpp
    services/DocumentSearchService.cpp
    services/ExcerptTileCache.cpp
    services/PageTileCache.cpp
    services/PdfDocumentService.cpp
    services/PdfExportService.cpp
    services/StrokeStabilizer.cpp
    services/TextSelectionService.cpp
    services/ToolManager.cpp
    workspace/TopToolbarWidget.cpp
    workspace/WorkspaceInteraction.cpp
    workspace/WorkspaceRenderer.cpp
    workspace/WorkspaceView.cpp
    window/AppHeaderBar.cpp
    ui/AppIcons.cpp
)

if(WIN32)
    list(APPEND FLUIDCORE_APP_SOURCES fluidcore.rc)
endif()

add_executable(fluidcore_app ${FLUIDCORE_APP_SOURCES})

target_include_directories(fluidcore_app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(fluidcore_app PRIVATE fluidcore PkgConfig::GTK3 PkgConfig::POPPLER_GLIB
                                            PkgConfig::CAIRO)

if(MSVC)
    target_compile_options(fluidcore_app PRIVATE /W4 /permissive-)
else()
    target_compile_options(fluidcore_app PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING)
    add_executable(damage_rect_test tests/DamageRectTest.cpp)
    target_include_directories(damage_rect_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    if(MSVC)
        target_compile_options(damage_rect_test PRIVATE /W4 /permissive-)
    else()
        target_compile_options(damage_rect_test PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME DamageRectTest COMMAND damage_rect_test)

    add_executable(page_tile_cache_test tests/PageTileCacheTest.cpp services/PageTileCache.cpp)
    target_include_directories(page_tile_cache_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(page_tile_cache_test PRIVATE fluidcore PkgConfig::CAIRO PkgConfig::POPPLER_GLIB)
    if(MSVC)
        target_compile_options(page_tile_cache_test PRIVATE /W4 /permissive-)
    else()
        target_compile_options(page_tile_cache_test PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME PageTileCacheTest COMMAND page_tile_cache_test)

    add_executable(stroke_stabilizer_test tests/StrokeStabilizerTest.cpp services/StrokeStabilizer.cpp)
    target_include_directories(stroke_stabilizer_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    if(MSVC)
        target_compile_options(stroke_stabilizer_test PRIVATE /W4 /permissive-)
    else()
        target_compile_options(stroke_stabilizer_test PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME StrokeStabilizerTest COMMAND stroke_stabilizer_test)

    add_executable(squeeze_render_test tests/SqueezeRenderTest.cpp document/SqueezeRenderHelper.cpp)
    target_include_directories(squeeze_render_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(squeeze_render_test PRIVATE fluidcore PkgConfig::CAIRO)
    if(MSVC)
        target_compile_options(squeeze_render_test PRIVATE /W4 /permissive-)
    else()
        target_compile_options(squeeze_render_test PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME SqueezeRenderTest COMMAND squeeze_render_test)

    add_executable(search_squeeze_planner_test tests/SearchSqueezePlannerTest.cpp document/SqueezeRenderHelper.cpp)
    target_include_directories(search_squeeze_planner_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(search_squeeze_planner_test PRIVATE fluidcore PkgConfig::CAIRO)
    if(MSVC)
        target_compile_options(search_squeeze_planner_test PRIVATE /W4 /permissive-)
    else()
        target_compile_options(search_squeeze_planner_test PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME SearchSqueezePlannerTest COMMAND search_squeeze_planner_test)

    add_executable(return_anchor_pill_test tests/ReturnAnchorPillTest.cpp)
    target_include_directories(return_anchor_pill_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(return_anchor_pill_test PRIVATE fluidcore PkgConfig::CAIRO PkgConfig::GTK3)
    if(MSVC)
        target_compile_options(return_anchor_pill_test PRIVATE /W4 /permissive-)
    else()
        target_compile_options(return_anchor_pill_test PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME ReturnAnchorPillTest COMMAND return_anchor_pill_test)

    add_executable(bi_directional_anchor_test tests/BiDirectionalAnchorTest.cpp)
    target_include_directories(bi_directional_anchor_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(bi_directional_anchor_test PRIVATE fluidcore)
    if(MSVC)
        target_compile_options(bi_directional_anchor_test PRIVATE /W4 /permissive-)
    else()
        target_compile_options(bi_directional_anchor_test PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME BiDirectionalAnchorTest COMMAND bi_directional_anchor_test)

    add_executable(excerpt_tile_cache_test tests/ExcerptTileCacheTest.cpp services/ExcerptTileCache.cpp services/PdfDocumentService.cpp)
    target_include_directories(excerpt_tile_cache_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(excerpt_tile_cache_test PRIVATE fluidcore PkgConfig::CAIRO PkgConfig::POPPLER_GLIB PkgConfig::GTK3)
    if(MSVC)
        target_compile_options(excerpt_tile_cache_test PRIVATE /W4 /permissive-)
    else()
        target_compile_options(excerpt_tile_cache_test PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME ExcerptTileCacheTest COMMAND excerpt_tile_cache_test)

    add_executable(workspace_interaction_test tests/WorkspaceInteractionTest.cpp workspace/WorkspaceInteraction.cpp services/StrokeStabilizer.cpp)
    target_include_directories(workspace_interaction_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(workspace_interaction_test PRIVATE fluidcore PkgConfig::GTK3 PkgConfig::CAIRO PkgConfig::POPPLER_GLIB)
    if(MSVC)
        target_compile_options(workspace_interaction_test PRIVATE /W4 /permissive-)
    else()
        target_compile_options(workspace_interaction_test PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME WorkspaceInteractionTest COMMAND workspace_interaction_test)

    add_executable(crop_drag_crash_test tests/CropDragCrashTest.cpp workspace/WorkspaceRenderer.cpp workspace/WorkspaceInteraction.cpp services/ExcerptTileCache.cpp services/PdfDocumentService.cpp services/StrokeStabilizer.cpp)
    target_include_directories(crop_drag_crash_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(crop_drag_crash_test PRIVATE fluidcore PkgConfig::GTK3 PkgConfig::CAIRO PkgConfig::POPPLER_GLIB)
    if(MSVC)
        target_compile_options(crop_drag_crash_test PRIVATE /W4 /permissive-)
    else()
        target_compile_options(crop_drag_crash_test PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME CropDragCrashTest COMMAND crop_drag_crash_test)

    add_executable(pdf_export_service_test tests/PdfExportServiceTest.cpp services/PdfExportService.cpp services/StrokeStabilizer.cpp)
    target_include_directories(pdf_export_service_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(pdf_export_service_test PRIVATE fluidcore PkgConfig::CAIRO PkgConfig::POPPLER_GLIB)
    if(MSVC)
        target_compile_options(pdf_export_service_test PRIVATE /W4 /permissive-)
    else()
        target_compile_options(pdf_export_service_test PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME PdfExportServiceTest COMMAND pdf_export_service_test)

    add_executable(scalability_benchmark_test
        tests/ScalabilityBenchmarkTest.cpp
        services/ExcerptTileCache.cpp
        services/PdfDocumentService.cpp
        services/PageTileCache.cpp
    )
    target_include_directories(scalability_benchmark_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(scalability_benchmark_test PRIVATE fluidcore PkgConfig::CAIRO PkgConfig::POPPLER_GLIB PkgConfig::GTK3)
    if(WIN32)
        target_link_libraries(scalability_benchmark_test PRIVATE psapi)
    endif()
    if(MSVC)
        target_compile_options(scalability_benchmark_test PRIVATE /W4 /permissive-)
    else()
        target_compile_options(scalability_benchmark_test PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME ScalabilityBenchmarkTest COMMAND scalability_benchmark_test)
endif()
