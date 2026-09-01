add_library(fluidcore STATIC
    FluidCoreEngine.cpp
    squeeze/SqueezeEngine.cpp
    workspace/RTreeIndex.cpp
    workspace/WorkspaceModel.cpp
    workspace/ExcerptCardNode.cpp
    workspace/CardStackNode.cpp
    workspace/PhysicsSolver.cpp
    workspace/CanvasStrokeNode.cpp
    workspace/ExcerptPayload.cpp
    workspace/CardLayoutEngine.cpp
    graph/GraphTopology.cpp
    storage/ProjectStore.cpp
    storage/XoppDocument.cpp
    storage/AnnotationStore.cpp
    undo/UndoStack.cpp
    undo/AnnotationCommands.cpp
    undo/WorkspaceCommands.cpp
    undo/SqueezeCommands.cpp
    text/TextSelection.cpp
    search/AnchorSqueezePlanner.cpp
    search/SearchSqueezePlanner.cpp
    search/WorkspaceSearchEngine.cpp
    export/WorkspaceExportEngine.cpp
)

find_package(ZLIB REQUIRED)
find_package(SQLite3 REQUIRED)
target_link_libraries(fluidcore PRIVATE ZLIB::ZLIB SQLite::SQLite3)

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

    add_executable(excerpt_card_node_test tests/workspace/ExcerptCardNodeTest.cpp)
    target_link_libraries(excerpt_card_node_test PRIVATE fluidcore)
    add_test(NAME ExcerptCardNodeTest COMMAND excerpt_card_node_test)

    add_executable(physics_solver_test tests/workspace/PhysicsSolverTest.cpp)
    target_link_libraries(physics_solver_test PRIVATE fluidcore)
    add_test(NAME PhysicsSolverTest COMMAND physics_solver_test)

    add_executable(card_stack_node_test tests/workspace/CardStackNodeTest.cpp)
    target_link_libraries(card_stack_node_test PRIVATE fluidcore)
    add_test(NAME CardStackNodeTest COMMAND card_stack_node_test)

    add_executable(xopp_document_test tests/storage/XoppDocumentTest.cpp)
    target_link_libraries(xopp_document_test PRIVATE fluidcore)
    add_test(NAME XoppDocumentTest COMMAND xopp_document_test)

    add_executable(annotation_store_test tests/storage/AnnotationStoreTest.cpp)
    target_link_libraries(annotation_store_test PRIVATE fluidcore)
    add_test(NAME AnnotationStoreTest COMMAND annotation_store_test)

    add_executable(undo_stack_test tests/UndoStackTest.cpp)
    target_link_libraries(undo_stack_test PRIVATE fluidcore)
    add_test(NAME UndoStackTest COMMAND undo_stack_test)

    add_executable(text_selection_test tests/TextSelectionTest.cpp)
    target_link_libraries(text_selection_test PRIVATE fluidcore)
    add_test(NAME TextSelectionTest COMMAND text_selection_test)

    add_executable(squeeze_engine_test tests/SqueezeEngineTest.cpp)
    target_link_libraries(squeeze_engine_test PRIVATE fluidcore)
    add_test(NAME SqueezeEngineTest COMMAND squeeze_engine_test)

    add_executable(anchor_squeeze_planner_test tests/AnchorSqueezePlannerTest.cpp)
    target_link_libraries(anchor_squeeze_planner_test PRIVATE fluidcore)
    add_test(NAME AnchorSqueezePlannerTest COMMAND anchor_squeeze_planner_test)

    add_executable(graph_topology_test tests/GraphTopologyTest.cpp)
    target_link_libraries(graph_topology_test PRIVATE fluidcore)
    add_test(NAME GraphTopologyTest COMMAND graph_topology_test)

    add_executable(rtree_benchmark_test tests/workspace/RTreeBenchmarkTest.cpp)
    target_link_libraries(rtree_benchmark_test PRIVATE fluidcore)
    add_test(NAME RTreeBenchmarkTest COMMAND rtree_benchmark_test)

    add_executable(card_layout_engine_test tests/workspace/CardLayoutEngineTest.cpp)
    target_link_libraries(card_layout_engine_test PRIVATE fluidcore)
    add_test(NAME CardLayoutEngineTest COMMAND card_layout_engine_test)

    add_executable(workspace_search_engine_test tests/WorkspaceSearchEngineTest.cpp)
    target_link_libraries(workspace_search_engine_test PRIVATE fluidcore)
    add_test(NAME WorkspaceSearchEngineTest COMMAND workspace_search_engine_test)

    add_executable(workspace_export_engine_test tests/WorkspaceExportEngineTest.cpp)
    target_link_libraries(workspace_export_engine_test PRIVATE fluidcore)
    add_test(NAME WorkspaceExportEngineTest COMMAND workspace_export_engine_test)

    add_executable(project_store_test tests/storage/ProjectStoreTest.cpp)
    target_link_libraries(project_store_test PRIVATE fluidcore)
    add_test(NAME ProjectStoreTest COMMAND project_store_test)

    add_executable(crash_recovery_fuzz_test tests/storage/CrashRecoveryFuzzTest.cpp)
    target_link_libraries(crash_recovery_fuzz_test PRIVATE fluidcore)
    add_test(NAME CrashRecoveryFuzzTest COMMAND crash_recovery_fuzz_test)

    add_executable(round_trip_persistence_test tests/storage/RoundTripPersistenceTest.cpp)
    target_link_libraries(round_trip_persistence_test PRIVATE fluidcore)
    add_test(NAME RoundTripPersistenceTest COMMAND round_trip_persistence_test)
endif()
