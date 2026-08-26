add_library(fluidcore STATIC
    FluidCoreEngine.cpp
    squeeze/SqueezeEngine.cpp
    workspace/RTreeIndex.cpp
    workspace/WorkspaceModel.cpp
    graph/GraphTopology.cpp
    storage/ProjectStore.cpp
    storage/XoppDocument.cpp
)

find_package(ZLIB REQUIRED)
target_link_libraries(fluidcore PRIVATE ZLIB::ZLIB)

target_include_directories(fluidcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(fluidcore PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(fluidcore PRIVATE /W4 /permissive-)
else()
    target_compile_options(fluidcore PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(FLUIDCORE_SANITIZERS AND NOT MSVC)
    target_compile_options(fluidcore PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(fluidcore PUBLIC -fsanitize=address,undefined)
endif()

if(BUILD_TESTING)
    add_executable(fluidcore_api_smoke_test tests/FluidCoreApiSmokeTest.cpp)
    target_link_libraries(fluidcore_api_smoke_test PRIVATE fluidcore)
    add_test(NAME FluidCoreApiSmokeTest COMMAND fluidcore_api_smoke_test)

    add_executable(fluidcore_engine_test tests/FluidCoreEngineTest.cpp)
    target_link_libraries(fluidcore_engine_test PRIVATE fluidcore)
    add_test(NAME FluidCoreEngineTest COMMAND fluidcore_engine_test)

    add_executable(rtree_index_test tests/workspace/RTreeIndexTest.cpp)
    target_link_libraries(rtree_index_test PRIVATE fluidcore)
    add_test(NAME RTreeIndexTest COMMAND rtree_index_test)

    add_executable(workspace_model_test tests/workspace/WorkspaceModelTest.cpp)
    target_link_libraries(workspace_model_test PRIVATE fluidcore)
    add_test(NAME WorkspaceModelTest COMMAND workspace_model_test)

    add_executable(xopp_document_test tests/storage/XoppDocumentTest.cpp)
    target_link_libraries(xopp_document_test PRIVATE fluidcore)
    add_test(NAME XoppDocumentTest COMMAND xopp_document_test)
endif()
