cmake_minimum_required(VERSION 3.20)
project(rlvoxel C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=gnu11")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Threads REQUIRED)

include(FetchContent)
FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG master
    GIT_SHALLOW TRUE
)

set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG master
    GIT_SHALLOW TRUE
)

FetchContent_Declare(
    fastnoiselite
    GIT_REPOSITORY https://github.com/Auburn/FastNoiseLite.git
    GIT_TAG master
    GIT_SHALLOW TRUE
)

FetchContent_Declare(
    enet
    GIT_REPOSITORY https://github.com/lsalzman/enet.git
    GIT_TAG v1.3.18
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(raylib stb fastnoiselite enet)

if (TARGET enet)
    set(ENET_TARGET enet)
elseif(TARGET enet_static)
    set(ENET_TARGET enet_static)
else()
    message(FATAL_ERROR "ENet target not found after FetchContent_MakeAvailable(enet)")
endif()

add_subdirectory(libvoxel)
add_subdirectory(libui)
add_subdirectory(libnet)
add_subdirectory(raylib-cimgui)

set(RUNTIME_SOURCES
    src/gfx/clouds.c
    src/gfx/renderer.c
    src/gfx/shader_paths.c
    src/gfx/selection_highlight.c
    src/ui/hud.c
    src/game/game.c
    src/game/game_input.c
    src/game/player.c
    src/game/raycast.c
    src/physics/physics.c
    src/world/blocks.c
    src/world/chunk.c
    src/world/world.c
    src/world/worldgen.c
    src/world/lighting.c
    src/world/mesher.c
    src/math/random.c
    src/math/noise.c
    src/profiling/profiler.c
    src/profiling/profiler_renderer.c
    src/diagnostics/telemetry.c
    src/diagnostics/process_stats.c
    src/server/server_core.c
)

add_library(rlvoxel_runtime STATIC ${RUNTIME_SOURCES})
target_include_directories(rlvoxel_runtime PUBLIC
    src
    ${stb_SOURCE_DIR}
    ${fastnoiselite_SOURCE_DIR}/C
    ${CMAKE_SOURCE_DIR}/libvoxel/include
)

target_link_libraries(rlvoxel_runtime PUBLIC raylib voxel ui net raylib-cimgui
                                             Threads::Threads)
if (NOT WIN32)
    target_link_libraries(rlvoxel_runtime PUBLIC m)
endif()

add_executable(rlVoxel src/app.c)
target_link_libraries(rlVoxel PRIVATE rlvoxel_runtime)

add_executable(rlVoxelServer src/server/server_main.c)
target_link_libraries(rlVoxelServer PRIVATE rlvoxel_runtime)
