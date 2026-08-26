add_library(fluidcore STATIC
    squeeze/SqueezeEngine.cpp
    workspace/WorkspaceModel.cpp
    graph/GraphTopology.cpp
    storage/ProjectStore.cpp
)

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
endif()
